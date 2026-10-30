#include "i18n.h"
#include "document.h"
#include <QTransform>
#include <QSignalBlocker>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
template<class Function> auto work(WorkQueue *queue, const QString &label, Function function) -> std::invoke_result_t<Function> {
    return queue ? queue->run(label, std::move(function)) : function();
}
QByteArray rawPixels(const QImage &image, const QRect &rect) {
    QByteArray bytes;
    bytes.reserve(rect.width() * rect.height());
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        bytes.append(reinterpret_cast<const char *>(image.constScanLine(y) + rect.x()), rect.width());
    return bytes;
}
QByteArray compress(const QByteArray &bytes) {
    auto result = qCompress(bytes, 1);
    if (result.isEmpty()) throw std::runtime_error(I18n::utf8("메모리 할당 실패"));
    return result;
}
QByteArray unpack(const QByteArray &bytes, qsizetype expected) {
    auto result = qUncompress(bytes);
    if (result.size() != expected) throw std::runtime_error(I18n::utf8("편집 기록을 복원할 메모리가 부족하거나 기록이 손상되었습니다."));
    return result;
}
void patch(QImage &image, const QRect &rect, const QByteArray &bytes) {
    auto *target = image.bits(); const auto stride = image.bytesPerLine();
    if (!target) throw std::runtime_error(I18n::utf8("메모리 할당 실패"));
    for (int y = 0; y < rect.height(); ++y)
        std::memcpy(target + qsizetype(y + rect.y()) * stride + rect.x(), bytes.constData() + qsizetype(y) * rect.width(), size_t(rect.width()));
}
void xorPatch(QImage &image, const QRect &rect, const QByteArray &delta) {
    auto *target = image.bits(); const auto stride = image.bytesPerLine();
    if (!target) throw std::runtime_error(I18n::utf8("메모리 할당 실패"));
    for (int y = 0; y < rect.height(); ++y) {
        auto *row = target + qsizetype(y + rect.y()) * stride + rect.x();
        const auto *source = reinterpret_cast<const uchar *>(delta.constData() + qsizetype(y) * rect.width());
        for (int x = 0; x < rect.width(); ++x) row[x] ^= source[x];
    }
}
// XOR is reversible in either direction, so one sparse delta replaces two
// complete before/after patches without changing any pixel values.
struct PixelData { QRect area; QByteArray packed, initial; };
struct GeometryData {
    QImage initial;
    QSize beforeSize, afterSize;
    QByteArray before, after;
    MapIO::Metadata oldMeta, newMeta;
};
GeometryData geometryData(const MapIO::Map &original, QImage result, const MapIO::Metadata &updated) {
    const auto error = MapIO::validate(updated, updated.mode == "raw");
    if (!error.isEmpty()) throw std::runtime_error(error.toUtf8().constData());
    GeometryData data;
    data.beforeSize = original.image.size(); data.afterSize = result.size();
    data.oldMeta = original.meta; data.newMeta = updated;
    data.before = compress(rawPixels(original.image, original.image.rect()));
    data.after = compress(rawPixels(result, result.rect())); data.initial = std::move(result);
    return data;
}
}
class PixelCommand : public QUndoCommand {
public:
    PixelCommand(Document *d, PixelData data, const QString &label)
        : QUndoCommand(label), doc(d), data(std::move(data)) {}
    void undo() override { apply(); }
    void redo() override {
        if (!data.initial.isEmpty()) { xorPatch(doc->map_.image, data.area, data.initial); data.initial = {}; }
        else apply();
    }
    qsizetype byteSize() const { return data.packed.size(); }
private:
    void apply() {
        const auto expected = qsizetype(data.area.width()) * data.area.height();
        const auto bytes = work(doc->worker_, I18n::text("편집 기록 복원 중…"), [packed = data.packed, expected] { return unpack(packed, expected); });
        xorPatch(doc->map_.image, data.area, bytes);
    }
    Document *doc;
    PixelData data;
};
class MetadataCommand : public QUndoCommand {
public:
    MetadataCommand(Document *d, const MapIO::Metadata &m) : QUndoCommand(I18n::text("지도 속성 변경")), doc(d), before(d->meta()), after(m) {}
    void undo() override { doc->map_.meta = before; }
    void redo() override { doc->map_.meta = after; }
    bool geometry() const { return before.resolution != after.resolution || before.x != after.x || before.y != after.y || before.yaw != after.yaw; }
private:
    Document *doc;
    MapIO::Metadata before, after;
};
class RotateCommand : public QUndoCommand {
public:
    RotateCommand(Document *d, GeometryData data, const QString &label) : QUndoCommand(label), doc(d), data(std::move(data)) {}
    void undo() override { apply(data.beforeSize, data.before, data.oldMeta); }
    void redo() override {
        if (!data.initial.isNull()) { doc->map_.image = std::move(data.initial); data.initial = {}; doc->map_.meta = data.newMeta; }
        else apply(data.afterSize, data.after, data.newMeta);
    }
    qsizetype byteSize() const { return data.before.size() + data.after.size(); }
private:
    void apply(QSize size, QByteArray packed, const MapIO::Metadata &meta) {
        auto image = work(doc->worker_, I18n::text("편집 기록 복원 중…"), [size, packed] {
            const auto bytes = unpack(packed, qsizetype(size.width()) * size.height());
            QImage image(size, QImage::Format_Grayscale8);
            if (image.isNull()) throw std::runtime_error(I18n::utf8("메모리 할당 실패"));
            patch(image, image.rect(), bytes); return image;
        });
        doc->map_.image = std::move(image); doc->map_.meta = meta;
    }
    Document *doc;
    GeometryData data;
};
Document::Document(QObject *parent, bool allowRaw) : QObject(parent), undo_(this), allowRaw_(allowRaw) {
    undo_.setUndoLimit(40);
    connect(&undo_, &QUndoStack::cleanChanged, this, &Document::changed);
    connect(&undo_, &QUndoStack::indexChanged, this, &Document::changed);
}
void Document::open(const QString &path) {
    auto loadedMap = work(worker_, I18n::text("지도 읽는 중…"), [path, raw = allowRaw_] { return MapIO::load(path, raw); });
    const bool needsSave = !loadedMap.hasYaml;
    replace(std::move(loadedMap), needsSave);
}
void Document::replace(MapIO::Map loadedMap, bool needsSave) {
    const auto error = MapIO::validate(loadedMap.meta, allowRaw_);
    if (!error.isEmpty()) throw std::runtime_error(error.toUtf8().constData());
    if (loadedMap.image.isNull() || loadedMap.image.format() != QImage::Format_Grayscale8)
        throw std::runtime_error(I18n::utf8("유효한 이미지가 없습니다."));
    { const QSignalBlocker block(&undo_); undo_.clear(); }
    map_ = std::move(loadedMap);
    historyBytes_ = 0; historyBaselineDirty_ = false;
    needsPair_ = needsSave;
    refreshDiskHashes();
    emit loaded(); emit changed();
}
void Document::clear() {
    map_ = {};
    needsPair_ = historyBaselineDirty_ = false;
    historyBytes_ = 0;
    yamlHash_.clear(); imageHash_.clear();
    { const QSignalBlocker block(&undo_); undo_.clear(); } emit changed();
}
namespace {
bool geometryCommand(const QUndoCommand *command) {
    if (auto *meta = dynamic_cast<const MetadataCommand *>(command)) return meta->geometry();
    return dynamic_cast<const RotateCommand *>(command) != nullptr;
}
}
bool Document::undoChangesGeometry() const { return undo_.canUndo() && geometryCommand(undo_.command(undo_.index() - 1)); }
bool Document::redoChangesGeometry() const { return undo_.canRedo() && geometryCommand(undo_.command(undo_.index())); }
void Document::newMap(int w, int h, double resolution) {
    if (w <= 0 || h <= 0 || qint64(w) * h > MapIO::MaxPixels) throw std::runtime_error(I18n::utf8("지도 크기 제한을 초과했습니다."));
    MapIO::Map fresh;
    fresh.meta.resolution = resolution;
    const auto error = MapIO::validate(fresh.meta);
    if (!error.isEmpty()) throw std::runtime_error(error.toUtf8().constData());
    fresh.image = QImage(w, h, QImage::Format_Grayscale8);
    if (fresh.image.isNull()) throw std::runtime_error(I18n::utf8("메모리 할당 실패"));
    fresh.image.fill(205);
    fresh.extra = YAML::Node(YAML::NodeType::Map);
    undo_.clear(); map_ = std::move(fresh); needsPair_ = true; historyBytes_ = 0; historyBaselineDirty_ = false;
    yamlHash_.clear(); imageHash_.clear();
    emit loaded(); emit changed();
}
void Document::editImage(const QImage &after, const QString &label, const QRect &changedArea) {
    if (after.size() != image().size() || after.format() != QImage::Format_Grayscale8) return;
    const auto search = changedArea.isNull() ? image().rect() : changedArea.intersected(image().rect());
    if (search.isEmpty()) return;
    auto data = work(worker_, I18n::text("편집 기록 만드는 중…"), [before = image(), after, search] {
        int left = before.width(), top = before.height(), right = -1, bottom = -1;
        for (int y = search.top(); y <= search.bottom(); ++y) {
            const auto *a = before.constScanLine(y), *b = after.constScanLine(y);
            if (std::memcmp(a + search.left(), b + search.left(), size_t(search.width())) == 0) continue;
            top = qMin(top, y); bottom = y;
            for (int x = search.left(); x <= search.right(); ++x) if (a[x] != b[x]) { left = qMin(left, x); right = qMax(right, x); }
        }
        PixelData data;
        if (right >= left) {
            data.area = QRect(QPoint(left, top), QPoint(right, bottom));
            data.initial.resize(qsizetype(data.area.width()) * data.area.height());
            auto *delta = reinterpret_cast<uchar *>(data.initial.data());
            for (int y = 0; y < data.area.height(); ++y) {
                const auto *a = before.constScanLine(y + top) + left, *b = after.constScanLine(y + top) + left;
                auto *row = delta + qsizetype(y) * data.area.width();
                for (int x = 0; x < data.area.width(); ++x) row[x] = a[x] ^ b[x];
            }
            data.packed = compress(data.initial);
        }
        return data;
    });
    if (!data.area.isEmpty()) {
        auto *command = new PixelCommand(this, std::move(data), label); push(command, command->byteSize());
    }
}
void Document::editMetadata(const MapIO::Metadata &meta) {
    auto error = MapIO::validate(meta, allowRaw_);
    if (!error.isEmpty()) throw std::runtime_error(error.toUtf8().constData());
    if (meta != map_.meta) push(new MetadataCommand(this, meta), 512);
}
void Document::rotateImage(double degrees) {
    if (empty() || !std::isfinite(degrees)) return;
    degrees = std::remainder(degrees, 360.0);
    if (std::abs(degrees) < 1e-10) return;
    auto data = work(worker_, I18n::text("지도 회전 중…"), [original = map_, degrees] {
        const auto &image = original.image;
        const auto transform = QImage::trueMatrix(QTransform().rotate(degrees), image.width(), image.height());
        const auto size = transform.mapRect(QRectF(0, 0, image.width(), image.height())).toAlignedRect().size();
        if (qint64(size.width()) * size.height() > MapIO::MaxPixels) throw std::runtime_error(I18n::utf8("회전 후 지도 크기가 32 Mi 픽셀을 초과합니다."));
        QImage result(size, QImage::Format_Grayscale8);
        if (result.isNull()) throw std::runtime_error(I18n::utf8("회전 이미지 메모리 할당 실패"));
        result.fill(MapIO::unknownGray(original.meta));
        const auto inverse = transform.inverted(); const auto *source = image.constBits(); const auto stride = image.bytesPerLine();
        for (int y = 0; y < result.height(); ++y) {
            auto *row = result.scanLine(y);
            for (int x = 0; x < result.width(); ++x) {
                const auto src = inverse.map(QPointF(x + .5, y + .5));
                const int sx = int(std::floor(src.x())), sy = int(std::floor(src.y()));
                if (uint(sx) < uint(image.width()) && uint(sy) < uint(image.height())) row[x] = source[qsizetype(sy) * stride + sx];
            }
        }
        auto updated = original.meta;
        const auto origin = MapIO::pixelToWorld(inverse.map(QPointF(0, result.height())), image.height(), original.meta);
        updated.x = origin.x(); updated.y = origin.y(); updated.yaw += degrees * std::acos(-1.) / 180.;
        return geometryData(original, std::move(result), updated);
    });
    auto *command = new RotateCommand(this, std::move(data), I18n::text("이미지 회전")); push(command, command->byteSize());
}
void Document::cropImage(const QRect &requested) {
    if (empty()) return;
    const auto area = requested.intersected(image().rect());
    if (area.isEmpty()) throw std::runtime_error(I18n::utf8("남길 영역을 먼저 선택하세요."));
    if (area == image().rect()) return;
    auto data = work(worker_, I18n::text("지도 자르는 중…"), [original = map_, area] {
        auto result = original.image.copy(area);
        if (result.isNull()) throw std::runtime_error(I18n::utf8("자르기 이미지 메모리 할당 실패"));
        auto updated = original.meta;
        const auto origin = MapIO::pixelToWorld(QPointF(area.x(), area.y() + area.height()), original.image.height(), original.meta);
        updated.x = origin.x(); updated.y = origin.y();
        return geometryData(original, std::move(result), updated);
    });
    auto *command = new RotateCommand(this, std::move(data), I18n::text("지도 자르기")); push(command, command->byteSize());
}
QRect Document::knownBounds() const {
    return work(worker_, I18n::text("지도 여백 찾는 중…"), [image = image(), meta = meta()] {
        bool known[256];
        for (int value = 0; value < 256; ++value) { const auto p = MapIO::probability(value, meta); known[value] = p < meta.free || p > meta.occupied; }
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            const auto *row = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x) if (known[row[x]]) {
                left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = y;
            }
        }
        return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
    });
}
void Document::push(QUndoCommand *command, qsizetype bytes) {
    // Redo branches and commands evicted by the count limit no longer own memory.
    historyBytes_ = 0;
    const int first = undo_.undoLimit() > 0 ? qMax(0, undo_.index() + 1 - undo_.undoLimit()) : 0;
    for (int i = first; i < undo_.index(); ++i) {
        const auto *entry = undo_.command(i);
        if (auto *pixel = dynamic_cast<const PixelCommand *>(entry)) historyBytes_ += pixel->byteSize();
        else if (auto *geometry = dynamic_cast<const RotateCommand *>(entry)) historyBytes_ += geometry->byteSize();
        else historyBytes_ += 512;
    }
    if (historyBytes_ + bytes > 96 * 1024 * 1024) {
        historyBaselineDirty_ = dirty();
        undo_.clear(); historyBytes_ = 0;
    }
    historyBytes_ += bytes; undo_.push(command);
}
void Document::refreshDiskHashes() {
    if (!map_.disk.captured) {
        if (map_.yamlPath.isEmpty() && map_.imagePath.isEmpty()) map_.disk = {{}, {}, true};
        else map_.disk = work(worker_, I18n::text("지도 파일 확인 중…"), [yaml = map_.yamlPath, image = map_.imagePath] { return MapIO::fingerprint(yaml, image); });
    }
    yamlHash_ = map_.disk.yaml; imageHash_ = map_.disk.image;
}
MapIO::SaveResult Document::save(const QString &newPath) {
    if (empty()) throw std::runtime_error(I18n::utf8("열린 지도가 없습니다."));
    const auto path = newPath.isEmpty() ? map_.yamlPath : newPath;
    const bool same = !map_.yamlPath.isEmpty() && MapIO::sameFilePath(path, map_.yamlPath);
    const MapIO::DiskSnapshot expected{yamlHash_, imageHash_, same};
    if (same && !dirty()) {
        const auto unchanged = work(worker_, I18n::text("지도 파일 확인 중…"), [map = map_, expected] { return MapIO::fingerprint(map.yamlPath, map.imagePath) == expected; });
        if (!unchanged) throw std::runtime_error(I18n::utf8("열어 둔 지도 파일이 외부에서 변경되었거나 삭제되었습니다. 다시 열거나 다른 이름으로 저장하세요."));
        return {map_.yamlPath, map_.imagePath, {}, expected};
    }
    auto saved = work(worker_, I18n::text("지도 저장 중…"), [map = map_, path, same, raw = allowRaw_, expected] { return MapIO::savePair(map, path, same ? map.imagePath : QString(), raw, expected); });
    map_.yamlPath = saved.yamlPath; map_.imagePath = saved.imagePath; map_.hasYaml = true; map_.disk = saved.disk;
    needsPair_ = false; historyBaselineDirty_ = false;
    refreshDiskHashes(); undo_.setClean(); emit changed();
    return saved;
}
