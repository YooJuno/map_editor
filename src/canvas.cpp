#include "canvas.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <cmath>
#include <vector>
#include <algorithm>

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
void dot(QImage &im, QPoint p, int d, int gray) {
    d = qMax(1, d);
    // The center of an odd diameter lies at a pixel center; even lies on an edge.
    const double centerX = p.x() + (d % 2 ? .5 : 0), centerY = p.y() + (d % 2 ? .5 : 0), r = d / 2.;
    const int y0 = qMax(0, int(std::floor(centerY - r))), y1 = qMin(im.height() - 1, int(std::ceil(centerY + r)) - 1);
    for (int y = y0; y <= y1; ++y) {
        const auto dy = y + .5 - centerY;
        const auto span = std::sqrt(qMax(0., r * r - dy * dy));
        const int left = qMax(0, int(std::ceil(centerX - span - .5)));
        const int right = qMin(im.width() - 1, int(std::floor(centerX + span - .5)));
        if (left <= right) std::fill(im.scanLine(y) + left, im.scanLine(y) + right + 1, uchar(gray));
    }
}
}
void stroke(QImage &im, QPoint from, QPoint to, int d, int gray) {
    const double margin = qMax(1, d) / 2. + 1;
    if (!clipSegment(from, to, QRectF(0, 0, im.width() - 1, im.height() - 1).adjusted(-margin, -margin, margin, margin))) return;
    int x = from.x(), y = from.y(), dx = std::abs(to.x() - x), sx = x < to.x() ? 1 : -1;
    int dy = -std::abs(to.y() - y), sy = y < to.y() ? 1 : -1, err = dx + dy;
    for (;;) {
        dot(im, {x, y}, d, gray);
        if (x == to.x() && y == to.y()) break;
        const int e = 2 * err;
        if (e >= dy) { err += dy; x += sx; }
        if (e <= dx) { err += dx; y += sy; }
    }
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
    for (int y = top; y <= bottom; ++y) {
        const double scan = y + .5;
        std::vector<double> intersections;
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
}
void Canvas::setTool(Tool tool) { finishEditing(); tool_ = tool; setCursor(tool == Hand ? Qt::OpenHandCursor : Qt::CrossCursor); update(); }
void Canvas::setDocument(Document *document) { finishEditing(); doc_ = document ? document : base_; update(); }
void Canvas::setMaskLayers(Document *keepout, Document *speed) {
    keepout_ = keepout; speed_ = speed;
    for (auto *layer : {keepout, speed}) if (layer) connect(layer, &Document::changed, this, qOverload<>(&Canvas::update));
}
bool Canvas::editableVisible() const {
    return (doc_ == base_ || maskOpacity_ > 0) && (doc_ != keepout_ || showKeepout_) && (doc_ != speed_ || showSpeed_);
}
void Canvas::finishEditing() { if (tool_ == Polygon && drawing_) finishPolygon(); else finishStroke(); }
bool Canvas::undoPolygonPoint() {
    if (tool_ != Polygon || !drawing_ || polygon_.isEmpty()) return false;
    polygon_.removeLast();
    if (polygon_.isEmpty()) cancelStroke();
    else { draft_ = doc_->image().copy(); update(); }
    return true;
}
void Canvas::fit() {
    if (base_->empty()) return;
    fitted_ = true;
    scale_ = qBound(0.005, qMin((width() - 96.) / base_->image().width(), (height() - 96.) / base_->image().height()), 64.);
    offset_ = QPointF((width() - base_->image().width() * scale_) / 2, (height() - base_->image().height() * scale_) / 2);
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
    p.fillRect(rect(), QColor("#55585c"));
    if (base_->empty()) {
        p.setPen(QColor("#e1e2e4"));
        p.drawText(rect(), Qt::AlignCenter, QString("PGM / YAML 열기  ·  %1\n파일을 여기에 끌어 놓을 수도 있습니다.")
            .arg(QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)));
        return;
    }
    const auto &image = drawing_ && doc_ == base_ ? draft_ : base_->image();
    const QRectF target(offset_, QSizeF(image.size()) * scale_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(target, image);
    auto overlay = [&](Document *layer, bool visible, bool speed) {
        if (!layer || layer->empty() || !visible || layer->image().size() != image.size()) return;
        const auto &mask = drawing_ && doc_ == layer ? draft_ : layer->image();
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
    p.setPen(QColor("#2e3033")); p.drawRect(target);
    if (scale_ >= 10) {
        p.save(); p.setClipRect(target);
        p.setPen(QColor(0, 0, 0, 32));
        const int x0 = qMax(0, int(-offset_.x() / scale_)), x1 = qMin(image.width(), int((width() - offset_.x()) / scale_) + 1);
        const int y0 = qMax(0, int(-offset_.y() / scale_)), y1 = qMin(image.height(), int((height() - offset_.y()) / scale_) + 1);
        for (int x = x0; x <= x1; ++x) p.drawLine(QPointF(offset_.x() + x * scale_, target.top()), QPointF(offset_.x() + x * scale_, target.bottom()));
        for (int y = y0; y <= y1; ++y) p.drawLine(QPointF(target.left(), offset_.y() + y * scale_), QPointF(target.right(), offset_.y() + y * scale_));
        p.restore();
    }
    if (axes_) {
        const auto origin = screenPoint(QPointF(0, image.height()));
        if (rect().adjusted(-80, -80, 80, 80).contains(origin.toPoint())) {
            p.setRenderHint(QPainter::Antialiasing, true);
            const double yaw = base_->meta().yaw, len = 52;
            auto arrow = [&](QPointF end, const QColor &color, const QString &label) {
                p.setPen(QPen(color, 2)); p.drawLine(origin, end); p.setBrush(color); p.drawEllipse(end, 3, 3); p.drawText(end + QPointF(5, -5), label);
            };
            arrow(origin + QPointF(std::cos(yaw), std::sin(yaw)) * len, QColor("#cc564d"), "X");
            arrow(origin + QPointF(std::sin(yaw), -std::cos(yaw)) * len, QColor("#24865f"), "Y");
            p.setPen(QPen(QColor("#176b50"), 2)); p.setBrush(Qt::white); p.drawEllipse(origin, 4, 4);
        }
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
    }
    if (inside_ && !panning_ && (tool_ == Pen || tool_ == Eraser) && insideImage(mouse_)) {
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
void Canvas::mousePressEvent(QMouseEvent *e) {
    setFocus(); if (doc_->empty()) return;
    mouse_ = e->position(); inside_ = true;
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (space_ || tool_ == Hand))) {
        panning_ = true; fitted_ = false; panStart_ = e->position(); setCursor(Qt::ClosedHandCursor); return;
    }
    if (e->button() != Qt::LeftButton || !insideImage(e->position()) || !editableVisible()) return;
    if (tool_ == Picker) {
        const auto point = pixelPoint(e->position());
        emit colorPicked(doc_->image().constScanLine(point.y())[point.x()]);
        return;
    }
    if (tool_ == Polygon) {
        if (!drawing_) { drawing_ = true; draft_ = doc_->image().copy(); emit editingChanged(); }
        const auto point = mapPoint(e->position());
        if (polygon_.size() >= 3 && QLineF(screenPoint(polygon_.first()), e->position()).length() <= 7) { finishPolygon(); return; }
        if (polygon_.isEmpty() || QLineF(polygon_.last(), point).length() > .01) polygon_ << point;
        polygonHover_ = point; update(); return;
    }
    drawing_ = true; start_ = last_ = pixelPoint(e->position()); draft_ = doc_->image().copy();
    emit editingChanged();
    if (tool_ == Fill) { Raster::flood(draft_, start_, activeGray()); finishStroke(); }
    else if (tool_ == Pen || tool_ == Eraser) Raster::stroke(draft_, start_, start_, brushSize_, activeGray());
    update();
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    mouse_ = e->position(); inside_ = true;
    if (doc_->empty()) return;
    emit positionChanged(mapPoint(e->position()));
    if (panning_) { offset_ += e->position() - panStart_; panStart_ = e->position(); }
    else if (drawing_ && tool_ == Polygon) {
        polygonHover_ = mapPoint(e->position());
        draft_ = doc_->image().copy();
        auto preview = polygon_; preview << polygonHover_;
        Raster::polygon(draft_, preview, activeGray());
    } else if (drawing_) {
        const auto current = pixelPoint(e->position());
        if (tool_ == Pen || tool_ == Eraser) Raster::stroke(draft_, last_, current, brushSize_, activeGray());
        else {
            draft_ = doc_->image().copy();
            if (tool_ == Line) Raster::stroke(draft_, start_, current, brushSize_, activeGray());
            else if (tool_ == Rectangle) Raster::rectangle(draft_, start_, current, brushSize_, activeGray(), filled_);
        }
        last_ = current;
    }
    update();
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (panning_ && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        panning_ = false; setCursor(tool_ == Hand || space_ ? Qt::OpenHandCursor : Qt::CrossCursor);
    }
    if (e->button() == Qt::LeftButton && drawing_ && tool_ != Polygon) { mouseMoveEvent(e); finishStroke(); }
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (tool_ == Polygon && e->button() == Qt::LeftButton && drawing_) finishPolygon();
    else QWidget::mouseDoubleClickEvent(e);
}
void Canvas::finishPolygon() {
    if (!drawing_) return;
    if (polygon_.size() < 3) { cancelStroke(); return; }
    draft_ = doc_->image().copy(); Raster::polygon(draft_, polygon_, activeGray());
    finishStroke();
}
void Canvas::finishStroke() {
    if (!drawing_) return;
    drawing_ = false;
    const QStringList names{"펜", "지우개", "직선", "사각형", "영역 채우기", "이동", "스포이드", "다각형"};
    doc_->editImage(draft_, names[int(tool_)]); draft_ = {}; polygon_.clear(); emit editingChanged(); update();
}
void Canvas::cancelStroke() { drawing_ = panning_ = space_ = false; draft_ = {}; polygon_.clear(); emit editingChanged(); update(); }
void Canvas::wheelEvent(QWheelEvent *e) { zoomAt(std::pow(1.0015, e->angleDelta().y()), e->position()); e->accept(); }
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (tool_ == Polygon && drawing_ && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) { finishPolygon(); e->accept(); }
    else if (tool_ == Polygon && drawing_ && e->key() == Qt::Key_Backspace) {
        undoPolygonPoint();
        e->accept();
    } else if (e->key() == Qt::Key_Space) { space_ = true; setCursor(Qt::OpenHandCursor); e->accept(); }
    else if (e->key() == Qt::Key_Escape) { cancelStroke(); e->accept(); }
    else QWidget::keyPressEvent(e);
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { space_ = false; if (!panning_) setCursor(tool_ == Hand ? Qt::OpenHandCursor : Qt::CrossCursor); e->accept(); }
    else QWidget::keyReleaseEvent(e);
}
void Canvas::leaveEvent(QEvent *) { inside_ = false; update(); }
void Canvas::focusOutEvent(QFocusEvent *e) {
    if (tool_ != Polygon) finishStroke();
    panning_ = space_ = false;
    setCursor(tool_ == Hand ? Qt::OpenHandCursor : Qt::CrossCursor);
    QWidget::focusOutEvent(e);
}
void Canvas::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile()) e->acceptProposedAction();
}
void Canvas::dropEvent(QDropEvent *e) {
    if (e->mimeData()->hasUrls()) { emit fileDropped(e->mimeData()->urls().first().toLocalFile()); e->acceptProposedAction(); }
}
