#include "document.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTransform>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
QByteArray hashFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) return {};
    return hash.result();
}
QByteArray pixels(const QImage &image, const QRect &rect) {
    QByteArray bytes;
    bytes.reserve(rect.width() * rect.height());
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        bytes.append(reinterpret_cast<const char *>(image.constScanLine(y) + rect.x()), rect.width());
    return qCompress(bytes, 1);
}
void patch(QImage &image, const QRect &rect, const QByteArray &compressed) {
    const auto bytes = qUncompress(compressed);
    for (int y = 0; y < rect.height(); ++y)
        std::memcpy(image.scanLine(y + rect.y()) + rect.x(), bytes.constData() + y * rect.width(), size_t(rect.width()));
}
}
class PixelCommand : public QUndoCommand {
public:
    PixelCommand(Document *d, QRect rect, const QImage &after, const QString &label)
        : QUndoCommand(label), doc(d), area(rect), before(pixels(d->image(), rect)), after(pixels(after, rect)) {}
    void undo() override { patch(doc->map_.image, area, before); emit doc->changed(); }
    void redo() override { patch(doc->map_.image, area, after); emit doc->changed(); }
    qsizetype byteSize() const { return before.size() + after.size(); }
private:
    Document *doc;
    QRect area;
    QByteArray before, after;
};
class MetadataCommand : public QUndoCommand {
public:
    MetadataCommand(Document *d, const MapIO::Metadata &m) : QUndoCommand("지도 속성 변경"), doc(d), before(d->meta()), after(m) {}
    void undo() override { doc->map_.meta = before; emit doc->changed(); }
    void redo() override { doc->map_.meta = after; emit doc->changed(); }
private:
    Document *doc;
    MapIO::Metadata before, after;
};
class RotateCommand : public QUndoCommand {
public:
    RotateCommand(Document *d, QImage after, MapIO::Metadata meta) : QUndoCommand("이미지 회전"), doc(d),
        beforeSize(d->image().size()), afterSize(after.size()),
        before(pixels(d->image(), d->image().rect())), after(pixels(after, after.rect())),
        oldMeta(d->meta()), newMeta(meta) {}
    void apply(QSize size, const QByteArray &data, const MapIO::Metadata &meta) {
        doc->map_.image = QImage(size, QImage::Format_Grayscale8);
        patch(doc->map_.image, doc->map_.image.rect(), data);
        doc->map_.meta = meta;
        emit doc->changed();
    }
    void undo() override { apply(beforeSize, before, oldMeta); }
    void redo() override { apply(afterSize, after, newMeta); }
    qsizetype byteSize() const { return before.size() + after.size(); }
private:
    Document *doc;
    QSize beforeSize, afterSize;
    QByteArray before, after;
    MapIO::Metadata oldMeta, newMeta;
};
Document::Document(QObject *parent) : QObject(parent), undo_(this) {
    undo_.setUndoLimit(40);
    connect(&undo_, &QUndoStack::cleanChanged, this, &Document::changed);
    connect(&undo_, &QUndoStack::indexChanged, this, &Document::changed);
}
void Document::open(const QString &path) {
    auto loadedMap = MapIO::load(path); // Keep current map on any parse failure.
    undo_.clear();
    map_ = std::move(loadedMap);
    historyBytes_ = 0; historyBaselineDirty_ = false;
    needsPair_ = !map_.hasYaml;
    refreshDiskHashes();
    emit loaded(); emit changed();
}
void Document::newMap(int w, int h, double resolution) {
    if (w <= 0 || h <= 0 || qint64(w) * h > MapIO::MaxPixels) throw std::runtime_error("지도 크기 제한을 초과했습니다.");
    MapIO::Map fresh;
    fresh.meta.resolution = resolution;
    const auto error = MapIO::validate(fresh.meta);
    if (!error.isEmpty()) throw std::runtime_error(error.toUtf8().constData());
    fresh.image = QImage(w, h, QImage::Format_Grayscale8);
    if (fresh.image.isNull()) throw std::runtime_error("메모리 할당 실패");
    fresh.image.fill(205);
    fresh.extra = YAML::Node(YAML::NodeType::Map);
    undo_.clear(); map_ = std::move(fresh); needsPair_ = true; historyBytes_ = 0; historyBaselineDirty_ = false;
    yamlHash_.clear(); imageHash_.clear();
    emit loaded(); emit changed();
}
void Document::editImage(const QImage &after, const QString &label) {
    if (after.size() != image().size() || after.format() != QImage::Format_Grayscale8) return;
    int left = image().width(), top = image().height(), right = -1, bottom = -1;
    for (int y = 0; y < image().height(); ++y) {
        const auto *a = image().constScanLine(y), *b = after.constScanLine(y);
        if (std::memcmp(a, b, size_t(image().width())) == 0) continue;
        top = qMin(top, y); bottom = y;
        for (int x = 0; x < image().width(); ++x) if (a[x] != b[x]) { left = qMin(left, x); right = qMax(right, x); }
    }
    if (right >= left) {
        auto *command = new PixelCommand(this, QRect(QPoint(left, top), QPoint(right, bottom)), after, label);
        push(command, command->byteSize());
    }
}
void Document::editMetadata(const MapIO::Metadata &meta) {
    auto error = MapIO::validate(meta);
    if (!error.isEmpty()) throw std::runtime_error(error.toUtf8().constData());
    if (meta != map_.meta) push(new MetadataCommand(this, meta), 512);
}
void Document::rotateImage(double degrees) {
    if (empty() || !std::isfinite(degrees)) return;
    degrees = std::remainder(degrees, 360.0);
    if (std::abs(degrees) < 1e-10) return;
    // QImage's bounding transform describes the same expanded pixel rectangle.
    const auto transform = QImage::trueMatrix(QTransform().rotate(degrees), image().width(), image().height());
    const QRect bounds = transform.mapRect(QRectF(0, 0, image().width(), image().height())).toAlignedRect();
    const QSize size = bounds.size();
    if (qint64(size.width()) * size.height() > MapIO::MaxPixels) throw std::runtime_error("회전 후 지도 크기가 32 Mi 픽셀을 초과합니다.");
    QImage result(size, QImage::Format_Grayscale8);
    if (result.isNull()) throw std::runtime_error("회전 이미지 메모리 할당 실패");
    result.fill(MapIO::unknownGray(meta()));
    const auto inverse = transform.inverted();
    for (int y = 0; y < result.height(); ++y) for (int x = 0; x < result.width(); ++x) {
        const auto src = inverse.map(QPointF(x + 0.5, y + 0.5));
        const int sx = int(std::floor(src.x())), sy = int(std::floor(src.y()));
        if (image().rect().contains(sx, sy)) result.scanLine(y)[x] = image().constScanLine(sy)[sx];
    }
    auto updated = meta();
    const auto oldBottomLeft = inverse.map(QPointF(0, result.height()));
    const auto origin = MapIO::pixelToWorld(oldBottomLeft, image().height(), meta());
    updated.x = origin.x(); updated.y = origin.y();
    updated.yaw += degrees * std::acos(-1.0) / 180;
    auto *command = new RotateCommand(this, result, updated);
    push(command, command->byteSize());
}
void Document::push(QUndoCommand *command, qsizetype bytes) {
    // Account conservatively, including discarded redo branches, to stay bounded.
    if (historyBytes_ + bytes > 96 * 1024 * 1024) {
        historyBaselineDirty_ = dirty();
        undo_.clear(); historyBytes_ = 0;
    }
    historyBytes_ += bytes; undo_.push(command);
}
void Document::refreshDiskHashes() {
    yamlHash_ = hashFile(map_.yamlPath); imageHash_ = hashFile(map_.imagePath);
}
bool Document::diskMatches() const {
    return yamlHash_ == hashFile(map_.yamlPath) && imageHash_ == hashFile(map_.imagePath);
}
MapIO::SaveResult Document::save(const QString &newPath) {
    if (empty()) throw std::runtime_error("열린 지도가 없습니다.");
    const auto path = newPath.isEmpty() ? map_.yamlPath : newPath;
    const bool same = !map_.yamlPath.isEmpty() && MapIO::sameFilePath(path, map_.yamlPath);
    if (same && !diskMatches()) throw std::runtime_error("열어 둔 지도 파일이 외부에서 변경되었거나 삭제되었습니다. 다시 열거나 다른 이름으로 저장하세요.");
    if (same && !dirty()) return {map_.yamlPath, map_.imagePath, {}};
    auto saved = MapIO::savePair(map_, path, same ? map_.imagePath : QString());
    map_.yamlPath = saved.yamlPath; map_.imagePath = saved.imagePath; map_.hasYaml = true;
    needsPair_ = false; historyBaselineDirty_ = false;
    refreshDiskHashes(); undo_.setClean(); emit changed();
    return saved;
}
