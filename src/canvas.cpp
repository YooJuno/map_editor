#include "i18n.h"
#include "canvas.h"
#include "icons.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

namespace { constexpr double Rad = 3.14159265358979323846 / 180.; }

namespace Raster {
namespace {
bool clipSegment(QPoint &a, QPoint &b, const QRectF &bounds) {
    const double dx = double(b.x()) - a.x(), dy = double(b.y()) - a.y();
    double low = 0, high = 1;
    auto edge = [&](double p, double q) {
        if (p == 0) return q >= 0;
        const double t = q / p;
        if (p < 0) low = qMax(low, t); else high = qMin(high, t);
        return low <= high;
    };
    if (!edge(-dx, a.x() - bounds.left()) || !edge(dx, bounds.right() - a.x()) ||
        !edge(-dy, a.y() - bounds.top()) || !edge(dy, bounds.bottom() - a.y())) return false;
    const auto start = a;
    a = {qRound(start.x() + low * dx), qRound(start.y() + low * dy)};
    b = {qRound(start.x() + high * dx), qRound(start.y() + high * dy)};
    return true;
}
struct BrushSpan { int y, left, right; };
const std::vector<BrushSpan> &brushSpans(int diameter) {
    thread_local int cachedDiameter = 0;
    thread_local std::vector<BrushSpan> spans;
    if (diameter == cachedDiameter) return spans;
    spans.clear(); spans.reserve(diameter);
    const double center = diameter % 2 ? .5 : 0., radius = diameter / 2.;
    for (int y = -diameter / 2; y < -diameter / 2 + diameter; ++y) {
        const double dy = y + .5 - center, span = std::sqrt(qMax(0., radius * radius - dy * dy));
        spans.push_back({y, int(std::ceil(center - span - .5)), int(std::floor(center + span - .5))});
    }
    cachedDiameter = diameter; return spans;
}
}
void stroke(QImage &im, QPoint from, QPoint to, int d, int gray) {
    if (im.isNull()) return;
    d = qMax(1, d);
    const double margin = d / 2. + 1;
    if (!clipSegment(from, to, QRectF(0, 0, im.width() - 1, im.height() - 1).adjusted(-margin, -margin, margin, margin))) return;
    const auto &brush = brushSpans(d);
    // Bresenham centers are monotonic. Merge each scanline's overlapping stamps,
    // then write it once when the next row evicts it from this bounded ring.
    struct Row { int y = -1, left = 0, right = -1; };
    std::vector<Row> rows(size_t(qMin(d, im.height())));
    auto *data = im.bits(); const auto stride = im.bytesPerLine();
    auto flush = [&](Row &row) {
        if (row.y >= 0 && row.left <= row.right)
            std::fill(data + qsizetype(row.y) * stride + row.left, data + qsizetype(row.y) * stride + row.right + 1, uchar(gray));
    };
    int x = from.x(), y = from.y(), dx = std::abs(to.x() - x), sx = x < to.x() ? 1 : -1;
    int dy = -std::abs(to.y() - y), sy = y < to.y() ? 1 : -1, err = dx + dy;
    for (;;) {
        for (const auto &span : brush) {
            const int line = y + span.y;
            if (uint(line) >= uint(im.height())) continue;
            const int left = qMax(0, x + span.left), right = qMin(im.width() - 1, x + span.right);
            if (left > right) continue;
            auto &row = rows[size_t(line) % rows.size()];
            if (row.y != line) { flush(row); row = {line, left, right}; }
            else { row.left = qMin(row.left, left); row.right = qMax(row.right, right); }
        }
        if (x == to.x() && y == to.y()) break;
        const int e = 2 * err;
        if (e >= dy) { err += dy; x += sx; }
        if (e <= dx) { err += dx; y += sy; }
    }
    for (auto &row : rows) flush(row);
}
void rectangle(QImage &im, QPoint a, QPoint b, int d, int gray, bool filled) {
    const auto outline = QRect(a, b).normalized();
    const auto area = outline.intersected(im.rect());
    if (filled) {
        for (int y = area.top(); y <= area.bottom(); ++y) std::fill(im.scanLine(y) + area.left(), im.scanLine(y) + area.right() + 1, uchar(gray));
    } else {
        stroke(im, outline.topLeft(), outline.topRight(), d, gray); stroke(im, outline.topRight(), outline.bottomRight(), d, gray);
        stroke(im, outline.bottomRight(), outline.bottomLeft(), d, gray); stroke(im, outline.bottomLeft(), outline.topLeft(), d, gray);
    }
}
void flood(QImage &im, QPoint start, int gray) {
    if (!im.rect().contains(start)) return;
    const int target = im.constScanLine(start.y())[start.x()];
    if (target == gray) return;
    std::vector<QPoint> pending{start};
    while (!pending.empty()) {
        const auto p = pending.back(); pending.pop_back();
        auto *row = im.scanLine(p.y());
        if (row[p.x()] != target) continue;
        int left = p.x(), right = p.x();
        while (left > 0 && row[left - 1] == target) --left;
        while (right + 1 < im.width() && row[right + 1] == target) ++right;
        std::fill(row + left, row + right + 1, uchar(gray));
        for (int y : {p.y() - 1, p.y() + 1}) {
            if (y < 0 || y >= im.height()) continue;
            const auto *next = im.constScanLine(y);
            bool span = false;
            for (int x = left; x <= right; ++x) {
                if (next[x] == target) { if (!span) pending.emplace_back(x, y); span = true; }
                else span = false;
            }
        }
    }
}
void polygon(QImage &im, const QPolygonF &points, int gray) {
    if (points.size() < 3) return;
    const auto bounds = points.boundingRect();
    const int top = qMax(0, int(std::floor(bounds.top()))), bottom = qMin(im.height() - 1, int(std::ceil(bounds.bottom())));
    std::vector<double> intersections; intersections.reserve(size_t(points.size()));
    for (int y = top; y <= bottom; ++y) {
        const double scan = y + .5;
        intersections.clear();
        for (qsizetype i = 0; i < points.size(); ++i) {
            const auto &a = points[i], &b = points[(i + 1) % points.size()];
            if ((a.y() > scan) != (b.y() > scan))
                intersections.push_back(a.x() + (scan - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
        }
        std::sort(intersections.begin(), intersections.end());
        auto *row = im.scanLine(y);
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            const int left = qMax(0, int(std::ceil(intersections[i] - .5)));
            const int right = qMin(im.width() - 1, int(std::ceil(intersections[i + 1] - .5)) - 1);
            if (left <= right) std::fill(row + left, row + right + 1, uchar(gray));
        }
    }
}
}
Canvas::Canvas(Document *doc, QWidget *parent) : QWidget(parent), doc_(doc), base_(doc) {
    setObjectName("mapCanvas"); setMinimumSize(300, 300); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); setAcceptDrops(true);
    connect(doc, &Document::loaded, this, [this] { cancelStroke(); fit(); });
    connect(doc, &Document::changed, this, qOverload<>(&Canvas::update));
    previewTimer_.setSingleShot(true); previewTimer_.setInterval(16);
    connect(&previewTimer_, &QTimer::timeout, this, &Canvas::updateShapePreview);
    draftIdleTimer_.setSingleShot(true); draftIdleTimer_.setInterval(30000);
    connect(&draftIdleTimer_, &QTimer::timeout, this, [this] { if (!drawing_) { draft_ = {}; draftSourceKey_ = 0; } });
}
void Canvas::setTool(Tool tool) {
    finishEditing(); selectingCrop_ = false; stopGeometryDrag(); tool_ = tool; setToolTip({});
    if (tool >= Crop) { draft_ = {}; draftArea_ = {}; draftSourceKey_ = 0; draftIdleTimer_.stop(); }
    setCursor(tool == Hand || tool == Rotate ? Qt::OpenHandCursor : Qt::CrossCursor); update();
}
void Canvas::setDocument(Document *document) {
    finishEditing(); doc_ = document ? document : base_;
    draft_ = {}; draftArea_ = {}; draftSourceKey_ = 0; draftIdleTimer_.stop(); update();
}
void Canvas::setMaskLayers(Document *keepout, Document *speed) {
    keepout_ = keepout; speed_ = speed;
    for (auto *layer : {keepout, speed}) if (layer) connect(layer, &Document::changed, this, qOverload<>(&Canvas::update));
}
bool Canvas::editableVisible() const {
    return (doc_ == base_ || maskOpacity_ > 0) && (doc_ != keepout_ || showKeepout_) && (doc_ != speed_ || showSpeed_);
}
void Canvas::finishEditing() { if (tool_ == Polygon && drawing_) finishPolygon(); else finishStroke(); stopGeometryDrag(); }
QSize Canvas::displaySize() const { return rotationPreview_ ? previewSize_ : base_->image().size(); }
void Canvas::setRotationPreview(double degrees) {
    if (base_->empty()) return;
    if (!rotatingImage_) finishEditing();
    rotationPreview_ = true; previewDegrees_ = degrees;
    previewTransform_ = QImage::trueMatrix(QTransform().rotate(degrees), base_->image().width(), base_->image().height());
    previewSize_ = previewTransform_.mapRect(QRectF(QPointF(), QSizeF(base_->image().size()))).toAlignedRect().size();
    if (rotatingImage_) {
        offset_ = rotationDragCenter_ - QPointF(previewSize_.width(), previewSize_.height()) * (scale_ / 2.);
        fitted_ = false; update();
    } else fit();
}
void Canvas::clearRotationPreview() {
    if (!rotationPreview_) return;
    rotationPreview_ = rotatingImage_ = false; fit();
}
QPointF Canvas::originPosition() const {
    return screenPoint(MapIO::worldToPixel({0, 0}, base_->image().height(), originPreview_ ? originMeta_ : base_->meta()));
}
int Canvas::originHandleAt(QPointF position) const {
    const auto origin = originPosition();
    if (QLineF(origin, position).length() <= 11) return 1;
    const auto yaw = (originPreview_ ? originMeta_ : base_->meta()).yaw;
    if (QLineF(origin + QPointF(std::cos(yaw), std::sin(yaw)) * 52, position).length() <= 10) return 2;
    if (QLineF(origin + QPointF(std::sin(yaw), -std::cos(yaw)) * 52, position).length() <= 10) return 3;
    return 0;
}
QPointF Canvas::rotationCenter() const {
    const auto size = displaySize(); return screenPoint(QPointF(size.width() / 2., size.height() / 2.));
}
double Canvas::rotationRadius() const {
    return qMax(32., qMin(qMin(base_->image().width(), base_->image().height()) * scale_ / 2. + 28., qMin(width(), height()) / 2. - 28.));
}
void Canvas::stopGeometryDrag() { draggingOrigin_ = rotatingOrigin_ = rotatingImage_ = false; }
void Canvas::updateGeometryCursor(QPointF position) {
    if (tool_ == Rotate) {
        setCursor(rotatingImage_ || panning_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
        setToolTip(I18n::text("드래그하여 이미지 회전 · Shift로 15°씩 조절"));
    } else if (tool_ == Origin) {
        const int handle = originHandleAt(position);
        setCursor(draggingOrigin_ ? Qt::SizeAllCursor : rotatingOrigin_ ? Qt::ClosedHandCursor : handle == 1 ? Qt::SizeAllCursor : handle > 1 ? Qt::OpenHandCursor : Qt::CrossCursor);
        setToolTip(handle == 1 ? I18n::text("드래그하여 원점 이동") : handle > 1 ? I18n::text("드래그하여 좌표계 회전 · Shift로 15°씩 조절") : QString());
    }
}
void Canvas::updateAngleDrag(QPointF position, Qt::KeyboardModifiers modifiers) {
    const auto delta = position - rotationDragCenter_;
    if (QLineF(QPointF(), delta).length() < 12) return;
    const double angle = std::atan2(delta.y(), delta.x()) / Rad;
    const double step = std::remainder(angle - previousPointerAngle_, 360.); previousPointerAngle_ = angle;
    if (std::abs(step) > 1e-9) angleDragMoved_ = true;
    if (!angleDragMoved_) return;
    dragDegrees_ += step;
    const double degrees = std::remainder(modifiers.testFlag(Qt::ShiftModifier) ? std::round(dragDegrees_ / 15.) * 15. : dragDegrees_, 360.);
    if (rotatingImage_) emit rotationDragged(degrees);
    else emit originYawDragged(originDragPixel_, degrees);
}
void Canvas::setCropSelection(QRect area) {
    const auto clipped = area.intersected(base_->image().rect());
    if (clipped == cropSelection_) return;
    cropSelection_ = clipped; emit cropChanged(cropSelection_); update();
}
Qt::Edges Canvas::cropEdgesAt(QPointF position) const {
    if (cropSelection_.isEmpty()) return {};
    const QRectF area(screenPoint(QPointF(cropSelection_.topLeft())), QSizeF(cropSelection_.size()) * scale_);
    const QPointF corners[]{area.topLeft(), area.topRight(), area.bottomRight(), area.bottomLeft()};
    const Qt::Edges cornersEdges[]{Qt::LeftEdge | Qt::TopEdge, Qt::RightEdge | Qt::TopEdge,
        Qt::RightEdge | Qt::BottomEdge, Qt::LeftEdge | Qt::BottomEdge};
    Qt::Edges result; double distance = 9.;
    for (int i = 0; i < 4; ++i) {
        const auto d = QLineF(corners[i], position).length();
        if (d <= distance) { distance = d; result = cornersEdges[i]; }
    }
    if (result) return result;
    distance = 7.;
    auto edge = [&](Qt::Edge side, double d, bool alongSegment) {
        if (alongSegment && d <= distance) { distance = d; result = side; }
    };
    const bool verticalSpan = position.y() >= area.top() && position.y() <= area.bottom();
    const bool horizontalSpan = position.x() >= area.left() && position.x() <= area.right();
    edge(Qt::LeftEdge, std::abs(position.x() - area.left()), verticalSpan);
    edge(Qt::RightEdge, std::abs(position.x() - area.right()), verticalSpan);
    edge(Qt::TopEdge, std::abs(position.y() - area.top()), horizontalSpan);
    edge(Qt::BottomEdge, std::abs(position.y() - area.bottom()), horizontalSpan);
    return result;
}
Qt::CursorShape Canvas::cropResizeCursor(Qt::Edges edges) {
    const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
    const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
    if (horizontal && vertical) return edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    return horizontal ? Qt::SizeHorCursor : vertical ? Qt::SizeVerCursor : Qt::CrossCursor;
}
void Canvas::updateCropCursor(QPointF position) {
    const auto edges = cropEdgesAt(position);
    if (edges) setCursor(cropResizeCursor(edges));
    else setCursor(cropSelection_ != base_->image().rect() && cropSelection_.contains(pixelPoint(position)) ? Qt::SizeAllCursor : Qt::CrossCursor);
}
bool Canvas::prepareDraft() {
    draftIdleTimer_.stop(); previewTimer_.stop();
    if (draft_.size() != doc_->image().size() || draft_.format() != QImage::Format_Grayscale8) draft_ = doc_->image().copy();
    else if (draftSourceKey_ != doc_->image().cacheKey()) {
        const auto *source = doc_->image().constBits(); auto *target = draft_.bits();
        if (!target) { draft_ = {}; emit operationFailed(I18n::text("지도 메모리를 할당할 수 없습니다.")); return false; }
        for (int y = 0; y < draft_.height(); ++y)
            std::memcpy(target + qsizetype(y) * draft_.bytesPerLine(), source + qsizetype(y) * doc_->image().bytesPerLine(), size_t(draft_.width()));
    }
    if (draft_.isNull()) { emit operationFailed(I18n::text("지도 메모리를 할당할 수 없습니다.")); return false; }
    draftSourceKey_ = doc_->image().cacheKey(); draftArea_ = {};
    return true;
}
void Canvas::restoreDraft() {
    if (draftArea_.isEmpty()) return;
    if (draft_.isNull()) { draftArea_ = {}; draftSourceKey_ = 0; return; }
    if (draftSourceKey_ != doc_->image().cacheKey() || draft_.size() != doc_->image().size()) { prepareDraft(); return; }
    auto *target = draft_.bits(); const auto *source = doc_->image().constBits();
    if (!target) { draft_ = {}; draftArea_ = {}; draftSourceKey_ = 0; return; }
    for (int y = draftArea_.top(); y <= draftArea_.bottom(); ++y)
        std::memcpy(target + qsizetype(y) * draft_.bytesPerLine() + draftArea_.x(), source + qsizetype(y) * doc_->image().bytesPerLine() + draftArea_.x(), size_t(draftArea_.width()));
    draftArea_ = {};
}
QRect Canvas::strokeBounds(QPoint from, QPoint to) const {
    const int margin = brushSize_ / 2 + 2;
    return QRect(from, to).normalized().adjusted(-margin, -margin, margin, margin).intersected(doc_->image().rect());
}
void Canvas::updateShapePreview() {
    if (!drawing_ || doc_->empty()) return;
    previewTimer_.stop(); restoreDraft();
    if (draft_.isNull()) return;
    if (tool_ == Polygon) {
        auto points = polygon_;
        if (inside_ && !points.isEmpty() && polygonHover_ != points.last()) points << polygonHover_;
        draftArea_ = points.boundingRect().intersected(QRectF(doc_->image().rect())).toAlignedRect().intersected(doc_->image().rect());
        Raster::polygon(draft_, points, activeGray());
    } else {
        draftArea_ = tool_ == Rectangle && filled_ ? QRect(start_, last_).normalized().intersected(doc_->image().rect()) : strokeBounds(start_, last_);
        if (tool_ == Line) Raster::stroke(draft_, start_, last_, brushSize_, activeGray());
        else if (tool_ == Rectangle) Raster::rectangle(draft_, start_, last_, brushSize_, activeGray(), filled_);
    }
    update();
}
bool Canvas::undoPolygonPoint() {
    if (tool_ != Polygon || !drawing_ || polygon_.isEmpty()) return false;
    polygon_.removeLast();
    if (polygon_.isEmpty()) cancelStroke();
    else { polygonHover_ = polygon_.last(); updateShapePreview(); emit editingChanged(); }
    return true;
}
void Canvas::fit() {
    if (base_->empty()) return;
    fitted_ = true;
    const auto size = displaySize();
    scale_ = qBound(0.005, qMin((width() - 96.) / size.width(), (height() - 96.) / size.height()), 64.);
    offset_ = QPointF((width() - size.width() * scale_) / 2, (height() - size.height() * scale_) / 2);
    emit zoomChanged(scale_); update();
}
void Canvas::actualSize() { if (!base_->empty()) zoomAt(1. / scale_, QPointF(width() / 2., height() / 2.)); }
void Canvas::zoom(double factor) { zoomAt(factor, QPointF(width() / 2., height() / 2.)); }
void Canvas::zoomAt(double factor, QPointF anchor) {
    if (base_->empty()) return;
    const auto pixel = mapPoint(anchor);
    fitted_ = false;
    scale_ = qBound(0.005, scale_ * factor, 64.);
    offset_ = anchor - pixel * scale_;
    emit zoomChanged(scale_); update();
}
void Canvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(Theme::isDark() ? "#191c21" : "#303236"));
    if (base_->empty()) {
        const int center = height() / 2;
        Icons::get(Icons::Map, QColor("#959daa")).paint(&p, QRect(width() / 2 - 26, center - 88, 52, 52));
        auto heading = font(); heading.setPointSizeF(16); heading.setWeight(QFont::Medium); p.setFont(heading);
        p.setPen(QColor("#e2e5ea"));
        p.drawText(QRect(0, center - 12, width(), 34), Qt::AlignCenter, I18n::text("지도를 열어 시작하세요"));
        auto body = font(); body.setPointSizeF(10); p.setFont(body); p.setPen(QColor("#a5abb6"));
        p.drawText(QRect(0, center + 28, width(), 28), Qt::AlignCenter, I18n::text("YAML 또는 PGM 파일을 여기에 끌어 놓으세요"));
        body.setPointSizeF(9); p.setFont(body); p.setPen(QColor("#838e9f"));
        p.drawText(QRect(0, center + 66, width(), 24), Qt::AlignCenter,
            QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText) + I18n::text("  ·  지도 열기"));
        return;
    }
    if (rotationPreview_) {
        const QRectF target(offset_, QSizeF(previewSize_) * scale_);
        const auto gray = MapIO::unknownGray(base_->meta()); p.fillRect(target, QColor(gray, gray, gray));
        p.save(); p.translate(offset_); p.scale(scale_, scale_); p.setTransform(previewTransform_, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false); p.drawImage(QPointF(), base_->image()); p.restore();
        p.setPen(QPen(QColor(147, 160, 180, 150), 1, Qt::DashLine));
        p.drawLine(QPointF(target.center().x(), target.top()), QPointF(target.center().x(), target.bottom()));
        p.drawLine(QPointF(target.left(), target.center().y()), QPointF(target.right(), target.center().y()));
        if (tool_ == Rotate) {
            p.setRenderHint(QPainter::Antialiasing, true);
            const auto center = rotationCenter(); const auto radius = rotationRadius();
            const auto handle = center + QPointF(std::sin(previewDegrees_ * Rad), -std::cos(previewDegrees_ * Rad)) * radius;
            p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(25, 35, 50, 145), 3.5)); p.drawEllipse(center, radius, radius);
            p.setPen(QPen(QColor(210, 220, 235, rotatingImage_ ? 235 : 200), 1.25));
            p.drawEllipse(center, radius, radius);
            p.setPen(QPen(QColor(25, 35, 50, 160), 3)); p.drawLine(center, handle);
            p.setPen(QPen(QColor("#c8d4e4"), 1.25)); p.drawLine(center, handle);
            p.setBrush(QColor("#202832")); p.drawEllipse(center, 4, 4);
            p.setPen(QPen(QColor("#536b88"), 2));
            p.setBrush(QColor("#e4edf8")); p.drawEllipse(handle, rotatingImage_ ? 7 : 6, rotatingImage_ ? 7 : 6);
        }
        return;
    }
    const auto &image = drawing_ && doc_ == base_ && !draft_.isNull() ? draft_ : base_->image();
    const QRectF target(offset_, QSizeF(image.size()) * scale_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(target, image);
    auto overlay = [&](Document *layer, bool visible, bool speed) {
        if (!layer || layer->empty() || !visible || layer->image().size() != image.size()) return;
        const auto &mask = drawing_ && doc_ == layer && !draft_.isNull() ? draft_ : layer->image();
        QImage indexed(mask.constBits(), mask.width(), mask.height(), mask.bytesPerLine(), QImage::Format_Indexed8);
        QList<QRgb> palette(256, qRgba(0, 0, 0, 0));
        for (int i = 1; i <= 100; ++i)
            palette[i] = speed ? qRgba(20 + i, 75 + i, 205 + i / 2, 255) : qRgba(225, 45, 40, 255);
        palette[255] = qRgba(80, 80, 80, 90);
        indexed.setColorTable(palette);
        p.save(); p.setOpacity(maskOpacity_); p.drawImage(target, indexed); p.restore();
    };
    overlay(speed_, showSpeed_, true);
    overlay(keepout_, showKeepout_, false);
    p.setPen(QColor("#111419")); p.drawRect(target);
    if (scale_ >= 10) {
        p.save(); p.setClipRect(target);
        p.setPen(QColor(0, 0, 0, 32));
        const int x0 = qMax(0, int(-offset_.x() / scale_)), x1 = qMin(image.width(), int((width() - offset_.x()) / scale_) + 1);
        const int y0 = qMax(0, int(-offset_.y() / scale_)), y1 = qMin(image.height(), int((height() - offset_.y()) / scale_) + 1);
        for (int x = x0; x <= x1; ++x) p.drawLine(QPointF(offset_.x() + x * scale_, target.top()), QPointF(offset_.x() + x * scale_, target.bottom()));
        for (int y = y0; y <= y1; ++y) p.drawLine(QPointF(target.left(), offset_.y() + y * scale_), QPointF(target.right(), offset_.y() + y * scale_));
        p.restore();
    }
    if (axes_ || tool_ == Origin) {
        const auto &axesMeta = originPreview_ ? originMeta_ : base_->meta();
        const auto origin = screenPoint(MapIO::worldToPixel(QPointF(0, 0), image.height(), axesMeta));
        if (QRectF(rect()).adjusted(-80, -80, 80, 80).contains(origin)) {
            p.setRenderHint(QPainter::Antialiasing, true);
            const double yaw = axesMeta.yaw, len = 52;
            if (tool_ == Origin) {
                p.setPen(QPen(QColor(100, 130, 165, 110), 1, Qt::DashLine)); p.setBrush(Qt::NoBrush);
                p.drawEllipse(origin, len, len);
                if (draggingOrigin_ || originHandleAt(mouse_) == 1) { p.setBrush(QColor(140, 165, 200, 45)); p.drawEllipse(origin, 12, 12); }
            }
            auto arrow = [&](QPointF end, const QColor &color, const QString &label) {
                p.setPen(QPen(color, 2)); p.drawLine(origin, end); p.setBrush(color);
                const int radius = tool_ == Origin ? 5 : 3; p.drawEllipse(end, radius, radius); p.drawText(end + QPointF(8, -6), label);
            };
            arrow(origin + QPointF(std::cos(yaw), std::sin(yaw)) * len, QColor("#cc564d"), "X");
            arrow(origin + QPointF(std::sin(yaw), -std::cos(yaw)) * len, QColor("#6486a6"), "Y");
            p.setPen(QPen(QColor("#91aac5"), 2)); p.setBrush(Qt::white); p.drawEllipse(origin, 4, 4);
            if (tool_ == Origin) { p.setPen(QColor("#576d87")); p.drawText(origin + QPointF(9, 18), "0, 0"); }
        }
    }
    if (tool_ == Crop && !cropSelection_.isEmpty()) {
        const QRectF selection(screenPoint(QPointF(cropSelection_.topLeft())), QSizeF(cropSelection_.size()) * scale_);
        QPainterPath shade; shade.addRect(target); shade.addRect(selection);
        p.fillPath(shade, QColor(0, 0, 0, 155));
        p.setPen(QPen(QColor("#c4d1e3"), 1.5, Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawRect(selection);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#e6edf7"));
        for (const auto corner : {selection.topLeft(), selection.topRight(), selection.bottomLeft(), selection.bottomRight()})
            p.drawRect(QRectF(corner - QPointF(3, 3), QSizeF(6, 6)));
        if (selection.width() > 24) for (double y : {selection.top(), selection.bottom()})
            p.drawRect(QRectF(selection.center().x() - 4, y - 2, 8, 4));
        if (selection.height() > 24) for (double x : {selection.left(), selection.right()})
            p.drawRect(QRectF(x - 2, selection.center().y() - 4, 4, 8));
    }
    if (tool_ == Polygon && drawing_ && !polygon_.isEmpty()) {
        p.save(); p.translate(offset_); p.scale(scale_, scale_);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#f5a623"), 1.5 / scale_));
        p.setBrush(Qt::NoBrush); p.drawPolyline(polygon_);
        p.drawLine(polygon_.last(), polygonHover_);
        p.drawLine(polygonHover_, polygon_.first());
        p.setBrush(QColor("#ffffff"));
        for (const auto &point : polygon_) p.drawEllipse(point, 3. / scale_, 3. / scale_);
        p.restore();
        if (polygon_.size() >= 3) {
            p.setPen(QPen(QColor("#f5a623"), 2)); p.setBrush(Qt::NoBrush); p.drawEllipse(screenPoint(polygon_.first()), 7, 7);
        }
        auto hintFont = font(); hintFont.setPointSizeF(qMax(8., hintFont.pointSizeF() - 1.)); p.setFont(hintFont);
        const auto hint = polygon_.size() < 3 ? I18n::text("점 3개 이상을 찍으세요 · Esc 취소") : I18n::text("완료 버튼 또는 우클릭 · Enter 완료 · Esc 취소");
        const int hintWidth = qMin(width() - 24, p.fontMetrics().horizontalAdvance(hint) + 28);
        const QRectF bubble((width() - hintWidth) / 2., height() - 46, hintWidth, 30);
        p.setPen(QColor("#536174")); p.setBrush(QColor(30, 35, 42, 235)); p.drawRoundedRect(bubble, 6, 6);
        p.setPen(QColor("#e4eaf3")); p.drawText(bubble, Qt::AlignCenter, p.fontMetrics().elidedText(hint, Qt::ElideRight, hintWidth - 20));
    }
    if (inside_ && !panning_ && tool_ == Pen && insideImage(mouse_)) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const double r = qMax(2., brushSize_ * scale_ / 2.);
        const auto point = pixelPoint(mouse_);
        const auto center = screenPoint(QPointF(point) + QPointF(brushSize_ % 2 ? .5 : 0, brushSize_ % 2 ? .5 : 0));
        p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white, 3)); p.drawEllipse(center, r, r);
        p.setPen(QPen(Qt::black, 1)); p.drawEllipse(center, r, r);
    }
}
void Canvas::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (base_->empty()) return;
    stopGeometryDrag();
    if (fitted_) fit();
    else if (e->oldSize().isValid()) {
        offset_ += QPointF((e->size().width() - e->oldSize().width()) / 2., (e->size().height() - e->oldSize().height()) / 2.);
        update();
    }
}
QPoint Canvas::pixelPoint(QPointF pos) const {
    const auto pixel = mapPoint(pos);
    return {int(std::floor(qBound(-100000000., pixel.x(), 100000000.))), int(std::floor(qBound(-100000000., pixel.y(), 100000000.)))};
}
bool Canvas::insideImage(QPointF position) const {
    // Compare viewport edges first: the inverse transform can round an exact
    // right/bottom edge slightly inward at fractional zoom levels.
    if (position.x() < offset_.x() || position.y() < offset_.y() ||
        position.x() >= offset_.x() + base_->image().width() * scale_ ||
        position.y() >= offset_.y() + base_->image().height() * scale_) return false;
    const auto point = mapPoint(position);
    return point.x() >= 0 && point.y() >= 0 && point.x() < base_->image().width() && point.y() < base_->image().height();
}
QRect Canvas::brushCursorRect(QPointF position) const {
    if (!insideImage(position)) return {};
    const auto point = pixelPoint(position);
    const auto center = screenPoint(QPointF(point) + QPointF(brushSize_ % 2 ? .5 : 0, brushSize_ % 2 ? .5 : 0));
    const double radius = qMax(2., brushSize_ * scale_ / 2.) + 3;
    return QRectF(center - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)).toAlignedRect().intersected(rect());
}
void Canvas::mousePressEvent(QMouseEvent *e) {
    setFocus(); if (base_->empty()) return;
    mouse_ = e->position(); inside_ = true;
    if (!editLocked_ && e->button() == Qt::RightButton && tool_ == Polygon && drawing_) {
        if (polygon_.size() >= 3) finishPolygon();
        e->accept(); return;
    }
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (space_ || tool_ == Hand || (rotationPreview_ && tool_ != Rotate)))) {
        stopGeometryDrag();
        panning_ = true; fitted_ = false; panStart_ = e->position(); setCursor(Qt::ClosedHandCursor); return;
    }
    if (e->button() != Qt::LeftButton || editLocked_ || !editableVisible()) return;
    if (tool_ == Rotate) {
        if (!rotationPreview_) return;
        rotationDragCenter_ = rotationCenter(); const auto delta = e->position() - rotationDragCenter_;
        if (QLineF(QPointF(), delta).length() < 12) return;
        rotatingImage_ = true; angleDragMoved_ = false; dragDegrees_ = previewDegrees_; previousPointerAngle_ = std::atan2(delta.y(), delta.x()) / Rad;
        setCursor(Qt::ClosedHandCursor); update(); return;
    }
    if (tool_ == Origin) {
        const int handle = originHandleAt(e->position());
        if (handle > 1) {
            rotatingOrigin_ = true; angleDragMoved_ = false; rotationDragCenter_ = originPosition(); originDragPixel_ = mapPoint(rotationDragCenter_);
            dragDegrees_ = (originPreview_ ? originMeta_ : base_->meta()).yaw / Rad;
            const auto delta = e->position() - rotationDragCenter_; previousPointerAngle_ = std::atan2(delta.y(), delta.x()) / Rad;
        } else {
            draggingOrigin_ = true; originGrabbed_ = handle == 1; originPressPosition_ = e->position();
            originGrabOffset_ = originGrabbed_ ? mapPoint(originPosition()) - mapPoint(e->position()) : QPointF();
            if (!originGrabbed_) emit originPicked(mapPoint(e->position()));
        }
        updateGeometryCursor(e->position()); update(); return;
    }
    if (tool_ == Crop) {
        cropStart_ = pixelPoint(e->position()); selectingCrop_ = true; movingCrop_ = false; cropBeforeDrag_ = cropSelection_;
        cropResizeEdges_ = cropEdgesAt(e->position());
        if (cropResizeEdges_) { setCursor(cropResizeCursor(cropResizeEdges_)); return; }
        if (!cropSelection_.isEmpty()) {
            if (cropSelection_ != base_->image().rect() && cropSelection_.contains(cropStart_)) { movingCrop_ = true; setCursor(Qt::SizeAllCursor); return; }
        }
        if (!insideImage(e->position())) { selectingCrop_ = false; return; }
        setCropSelection(QRect(cropStart_, cropStart_)); return;
    }
    if (!insideImage(e->position())) return;
    if (tool_ == Picker) {
        const auto point = pixelPoint(e->position());
        emit colorPicked(doc_->empty() ? 0 : doc_->image().constScanLine(point.y())[point.x()]);
        return;
    }
    if (doc_->empty()) { emit paintRequested(); if (doc_->empty()) return; }
    if (tool_ == Polygon) {
        if (!drawing_) { if (!prepareDraft()) return; drawing_ = true; }
        const auto point = mapPoint(e->position());
        if (polygon_.size() >= 3 && QLineF(screenPoint(polygon_.first()), e->position()).length() <= 7) { finishPolygon(); return; }
        if (polygon_.isEmpty() || QLineF(polygon_.last(), point).length() > .01) polygon_ << point;
        polygonHover_ = point; updateShapePreview();
        emit editingChanged(); update(); return;
    }
    if (!prepareDraft()) return;
    drawing_ = true; start_ = last_ = pixelPoint(e->position());
    emit editingChanged();
    if (tool_ == Fill) {
        draftArea_ = doc_->image().rect();
        try {
            if (worker_) {
                auto image = std::move(draft_); draft_ = {};
                draft_ = worker_->run(I18n::text("영역 채우는 중…"), [image = std::move(image), start = start_, gray = activeGray()]() mutable { Raster::flood(image, start, gray); return image; });
            } else Raster::flood(draft_, start_, activeGray());
            finishStroke();
        } catch (const std::exception &error) { cancelStroke(); emit operationFailed(QString::fromUtf8(error.what())); }
    }
    else if (tool_ == Pen) { draftArea_ = strokeBounds(start_, start_); Raster::stroke(draft_, start_, start_, brushSize_, activeGray()); }
    update();
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    const bool hover = tool_ < Crop && !drawing_ && !panning_ && !selectingCrop_ && !draggingOrigin_ && !rotatingImage_ && !rotatingOrigin_;
    const auto oldCursor = hover && tool_ == Pen && inside_ ? brushCursorRect(mouse_) : QRect();
    mouse_ = e->position(); inside_ = true;
    if (base_->empty()) return;
    if (editLocked_) { if (panning_) { offset_ += e->position() - panStart_; panStart_ = e->position(); update(); } return; }
    if (!rotationPreview_) emit positionChanged(mapPoint(e->position()));
    if (panning_) { offset_ += e->position() - panStart_; panStart_ = e->position(); }
    else if (rotatingImage_ || rotatingOrigin_) updateAngleDrag(e->position(), e->modifiers());
    else if (selectingCrop_) {
        auto point = pixelPoint(e->position());
        point.setX(qBound(0, point.x(), base_->image().width() - 1));
        point.setY(qBound(0, point.y(), base_->image().height() - 1));
        if (cropResizeEdges_) {
            const auto pointer = mapPoint(e->position());
            const int x = qRound(qBound(0., pointer.x(), double(base_->image().width())));
            const int y = qRound(qBound(0., pointer.y(), double(base_->image().height())));
            int left = cropBeforeDrag_.x(), top = cropBeforeDrag_.y();
            int right = left + cropBeforeDrag_.width(), bottom = top + cropBeforeDrag_.height();
            if (cropResizeEdges_.testFlag(Qt::LeftEdge)) left = qBound(0, x, right - 1);
            if (cropResizeEdges_.testFlag(Qt::RightEdge)) right = qBound(left + 1, x, base_->image().width());
            if (cropResizeEdges_.testFlag(Qt::TopEdge)) top = qBound(0, y, bottom - 1);
            if (cropResizeEdges_.testFlag(Qt::BottomEdge)) bottom = qBound(top + 1, y, base_->image().height());
            setCropSelection(QRect(left, top, right - left, bottom - top));
        } else if (movingCrop_) {
            const auto delta = point - cropStart_;
            const int x = qBound(0, cropBeforeDrag_.x() + delta.x(), base_->image().width() - cropBeforeDrag_.width());
            const int y = qBound(0, cropBeforeDrag_.y() + delta.y(), base_->image().height() - cropBeforeDrag_.height());
            setCropSelection(QRect(QPoint(x, y), cropBeforeDrag_.size()));
        } else setCropSelection(QRect(cropStart_, point).normalized());
    }
    else if (draggingOrigin_) {
        if (!originGrabbed_ || QLineF(originPressPosition_, e->position()).length() >= 1.) emit originPicked(mapPoint(e->position()) + originGrabOffset_);
    }
    else if (drawing_ && tool_ == Polygon) {
        polygonHover_ = mapPoint(e->position());
        if (!previewTimer_.isActive()) previewTimer_.start();
    } else if (drawing_) {
        const auto current = pixelPoint(e->position());
        if (tool_ == Pen) {
            draftArea_ = draftArea_.united(strokeBounds(last_, current));
            Raster::stroke(draft_, last_, current, brushSize_, activeGray());
        }
        last_ = current;
        if (tool_ == Line || tool_ == Rectangle) if (!previewTimer_.isActive()) previewTimer_.start();
    }
    if (tool_ == Crop && !selectingCrop_ && !panning_ && !space_) updateCropCursor(e->position());
    if ((tool_ == Rotate || tool_ == Origin) && !panning_ && !space_) updateGeometryCursor(e->position());
    if (hover) {
        if (tool_ == Pen) {
            const auto area = oldCursor.united(brushCursorRect(mouse_));
            if (!area.isEmpty()) update(area);
        }
    } else update();
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (editLocked_ && !panning_) return;
    if (e->button() == Qt::LeftButton && (draggingOrigin_ || rotatingOrigin_ || rotatingImage_)) {
        mouseMoveEvent(e); stopGeometryDrag(); updateGeometryCursor(e->position()); update(); return;
    }
    if (e->button() == Qt::LeftButton && selectingCrop_) { mouseMoveEvent(e); selectingCrop_ = false; updateCropCursor(e->position()); return; }
    if (panning_ && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        panning_ = false; setCursor(tool_ == Hand || tool_ == Rotate || space_ ? Qt::OpenHandCursor : Qt::CrossCursor);
    }
    if (e->button() == Qt::LeftButton && drawing_ && tool_ != Polygon) { mouseMoveEvent(e); finishStroke(); }
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (editLocked_) return;
    if (tool_ == Polygon && e->button() == Qt::LeftButton && drawing_) finishPolygon();
    else QWidget::mouseDoubleClickEvent(e);
}
void Canvas::finishPolygon() {
    if (!drawing_) return;
    if (polygon_.size() < 3) { cancelStroke(); return; }
    polygonHover_ = polygon_.last(); updateShapePreview();
    finishStroke();
}
void Canvas::finishStroke() {
    if (!drawing_) return;
    previewTimer_.stop();
    if (tool_ == Line || tool_ == Rectangle) updateShapePreview();
    drawing_ = false;
    const QStringList names{I18n::text("펜"), I18n::text("직선"), I18n::text("사각형"), I18n::text("영역 채우기"), I18n::text("이동"), I18n::text("스포이드"), I18n::text("다각형")};
    try { if (!draftArea_.isEmpty()) doc_->editImage(draft_, names[int(tool_)], draftArea_); }
    catch (const std::exception &error) { restoreDraft(); emit operationFailed(QString::fromUtf8(error.what())); }
    draftSourceKey_ = doc_->image().cacheKey(); draftArea_ = {}; polygon_.clear(); draftIdleTimer_.start(); emit editingChanged(); update();
}
void Canvas::cancelStroke() {
    previewTimer_.stop(); drawing_ = panning_ = space_ = false;
    if (draftSourceKey_ != doc_->image().cacheKey()) { draft_ = {}; draftArea_ = {}; draftSourceKey_ = 0; }
    else restoreDraft();
    polygon_.clear(); draftIdleTimer_.start(); emit editingChanged(); update();
}
void Canvas::wheelEvent(QWheelEvent *e) { if (!rotatingImage_ && !rotatingOrigin_ && !draggingOrigin_) zoomAt(std::pow(1.0015, e->angleDelta().y()), e->position()); e->accept(); }
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (editLocked_ && e->key() != Qt::Key_Space) { e->accept(); return; }
    if ((tool_ == Crop || tool_ == Origin || rotationPreview_) && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) { stopGeometryDrag(); emit geometryApplyRequested(); e->accept(); }
    else if ((rotationPreview_ || tool_ == Origin) && e->key() == Qt::Key_Escape) { stopGeometryDrag(); emit previewCancelRequested(); e->accept(); }
    else if (tool_ == Crop && e->key() == Qt::Key_Escape) { selectingCrop_ = false; setCropSelection({}); e->accept(); }
    else if (tool_ == Polygon && drawing_ && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) { finishPolygon(); e->accept(); }
    else if (tool_ == Polygon && drawing_ && e->key() == Qt::Key_Backspace) {
        undoPolygonPoint();
        e->accept();
    } else if (e->key() == Qt::Key_Space) { space_ = true; setCursor(Qt::OpenHandCursor); e->accept(); }
    else if (e->key() == Qt::Key_Escape) { cancelStroke(); e->accept(); }
    else QWidget::keyPressEvent(e);
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { space_ = false; if (!panning_) setCursor(tool_ == Hand || tool_ == Rotate ? Qt::OpenHandCursor : Qt::CrossCursor); e->accept(); }
    else QWidget::keyReleaseEvent(e);
}
void Canvas::leaveEvent(QEvent *) { inside_ = false; if (drawing_ && tool_ == Polygon) updateShapePreview(); update(); }
void Canvas::focusOutEvent(QFocusEvent *e) {
    if (tool_ != Polygon && !editLocked_) finishStroke();
    panning_ = space_ = false; stopGeometryDrag();
    setCursor(tool_ == Hand || tool_ == Rotate ? Qt::OpenHandCursor : Qt::CrossCursor);
    QWidget::focusOutEvent(e);
}
void Canvas::dragEnterEvent(QDragEnterEvent *e) {
    if (!editLocked_ && e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile()) e->acceptProposedAction();
}
void Canvas::dropEvent(QDropEvent *e) {
    if (!editLocked_ && e->mimeData()->hasUrls()) { emit fileDropped(e->mimeData()->urls().first().toLocalFile()); e->acceptProposedAction(); }
}
