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
}
Canvas::Canvas(Document *doc, QWidget *parent) : QWidget(parent), doc_(doc) {
    setObjectName("mapCanvas"); setMinimumSize(300, 300); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); setAcceptDrops(true);
    connect(doc, &Document::loaded, this, [this] { cancelStroke(); fit(); });
    connect(doc, &Document::changed, this, qOverload<>(&Canvas::update));
}
void Canvas::setTool(Tool tool) { finishStroke(); tool_ = tool; setCursor(tool == Hand ? Qt::OpenHandCursor : Qt::CrossCursor); update(); }
void Canvas::fit() {
    if (doc_->empty()) return;
    fitted_ = true;
    scale_ = qBound(0.005, qMin((width() - 96.) / doc_->image().width(), (height() - 96.) / doc_->image().height()), 64.);
    offset_ = QPointF((width() - doc_->image().width() * scale_) / 2, (height() - doc_->image().height() * scale_) / 2);
    emit zoomChanged(scale_); update();
}
void Canvas::actualSize() { if (!doc_->empty()) zoomAt(1. / scale_, QPointF(width() / 2., height() / 2.)); }
void Canvas::zoom(double factor) { zoomAt(factor, QPointF(width() / 2., height() / 2.)); }
void Canvas::zoomAt(double factor, QPointF anchor) {
    if (doc_->empty()) return;
    const auto pixel = mapPoint(anchor);
    fitted_ = false;
    scale_ = qBound(0.005, scale_ * factor, 64.);
    offset_ = anchor - pixel * scale_;
    emit zoomChanged(scale_); update();
}
void Canvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#55585c"));
    if (doc_->empty()) {
        p.setPen(QColor("#e1e2e4"));
        p.drawText(rect(), Qt::AlignCenter, QString("PGM / YAML 열기  ·  %1\n파일을 여기에 끌어 놓을 수도 있습니다.")
            .arg(QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)));
        return;
    }
    const auto &image = drawing_ ? draft_ : doc_->image();
    const QRectF target(offset_, QSizeF(image.size()) * scale_);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(target, image);
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
            const double yaw = doc_->meta().yaw, len = 52;
            auto arrow = [&](QPointF end, const QColor &color, const QString &label) {
                p.setPen(QPen(color, 2)); p.drawLine(origin, end); p.setBrush(color); p.drawEllipse(end, 3, 3); p.drawText(end + QPointF(5, -5), label);
            };
            arrow(origin + QPointF(std::cos(yaw), std::sin(yaw)) * len, QColor("#cc564d"), "X");
            arrow(origin + QPointF(std::sin(yaw), -std::cos(yaw)) * len, QColor("#24865f"), "Y");
            p.setPen(QPen(QColor("#176b50"), 2)); p.setBrush(Qt::white); p.drawEllipse(origin, 4, 4);
        }
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
    if (doc_->empty()) return;
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
        position.x() >= offset_.x() + doc_->image().width() * scale_ ||
        position.y() >= offset_.y() + doc_->image().height() * scale_) return false;
    const auto point = mapPoint(position);
    return point.x() >= 0 && point.y() >= 0 && point.x() < doc_->image().width() && point.y() < doc_->image().height();
}
void Canvas::mousePressEvent(QMouseEvent *e) {
    setFocus(); if (doc_->empty()) return;
    mouse_ = e->position(); inside_ = true;
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (space_ || tool_ == Hand))) {
        panning_ = true; fitted_ = false; panStart_ = e->position(); setCursor(Qt::ClosedHandCursor); return;
    }
    if (e->button() != Qt::LeftButton || !insideImage(e->position())) return;
    if (tool_ == Picker) {
        const auto point = pixelPoint(e->position());
        emit colorPicked(doc_->image().constScanLine(point.y())[point.x()]);
        return;
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
    else if (drawing_) {
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
    if (e->button() == Qt::LeftButton && drawing_) { mouseMoveEvent(e); finishStroke(); }
}
void Canvas::finishStroke() {
    if (!drawing_) return;
    drawing_ = false;
    const QStringList names{"펜", "지우개", "직선", "사각형", "영역 채우기", "이동"};
    doc_->editImage(draft_, names[int(tool_)]); draft_ = {}; emit editingChanged(); update();
}
void Canvas::cancelStroke() { drawing_ = panning_ = space_ = false; draft_ = {}; emit editingChanged(); update(); }
void Canvas::wheelEvent(QWheelEvent *e) { zoomAt(std::pow(1.0015, e->angleDelta().y()), e->position()); e->accept(); }
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) { space_ = true; setCursor(Qt::OpenHandCursor); e->accept(); }
    else if (e->key() == Qt::Key_Escape) { cancelStroke(); e->accept(); }
    else QWidget::keyPressEvent(e);
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { space_ = false; if (!panning_) setCursor(tool_ == Hand ? Qt::OpenHandCursor : Qt::CrossCursor); e->accept(); }
    else QWidget::keyReleaseEvent(e);
}
void Canvas::leaveEvent(QEvent *) { inside_ = false; update(); }
void Canvas::focusOutEvent(QFocusEvent *e) {
    finishStroke();
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
