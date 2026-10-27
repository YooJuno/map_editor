#pragma once
#include "document.h"
#include <QWidget>

namespace Raster {
void stroke(QImage &image, QPoint from, QPoint to, int diameter, int gray);
void rectangle(QImage &image, QPoint from, QPoint to, int diameter, int gray, bool filled);
void flood(QImage &image, QPoint start, int gray);
}
class Canvas : public QWidget {
    Q_OBJECT
public:
    enum Tool { Pen, Eraser, Line, Rectangle, Fill, Hand, Picker };
    explicit Canvas(Document *document, QWidget *parent = nullptr);
    void setTool(Tool tool);
    void setBrushSize(int size) { brushSize_ = size; update(); }
    void setGray(int gray) { gray_ = gray; update(); }
    void setEraserGray(int gray) { eraserGray_ = gray; }
    void setFilled(bool filled) { filled_ = filled; }
    void fit();
    void actualSize();
    void zoom(double factor);
    double scale() const { return scale_; }
    void setAxes(bool visible) { axes_ = visible; update(); }
    QPointF mapPoint(QPointF pos) const { return (pos - offset_) / scale_; }
    QPointF screenPoint(QPointF pos) const { return offset_ + pos * scale_; }
signals:
    void positionChanged(QPointF pixel);
    void zoomChanged(double scale);
    void fileDropped(const QString &path);
    void colorPicked(int gray);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    QPoint bounded(QPointF pos) const;
    int activeGray() const { return tool_ == Eraser ? eraserGray_ : gray_; }
    void zoomAt(double factor, QPointF anchor);
    void finishStroke();
    void cancelStroke();
    Document *doc_;
    Tool tool_ = Pen;
    int brushSize_ = 8, gray_ = 0, eraserGray_ = 205;
    double scale_ = 1;
    QPointF offset_, panStart_, mouse_;
    QPoint start_, last_;
    bool drawing_ = false, panning_ = false, space_ = false, inside_ = false, axes_ = true, filled_ = false;
    QImage draft_;
};
