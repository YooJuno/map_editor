#pragma once
#include "document.h"
#include <QWidget>
#include <QPolygonF>
#include <QTransform>

namespace Raster {
void stroke(QImage &image, QPoint from, QPoint to, int diameter, int gray);
void rectangle(QImage &image, QPoint from, QPoint to, int diameter, int gray, bool filled);
void flood(QImage &image, QPoint start, int gray);
void polygon(QImage &image, const QPolygonF &points, int gray);
}
class Canvas : public QWidget {
    Q_OBJECT
public:
    enum Tool { Pen, Line, Rectangle, Fill, Hand, Picker, Polygon, Crop, Origin };
    explicit Canvas(Document *document, QWidget *parent = nullptr);
    void setTool(Tool tool);
    void setBrushSize(int size) { brushSize_ = size; update(); }
    void setGray(int gray) { gray_ = gray; update(); }
    void setFilled(bool filled) { filled_ = filled; }
    void finishEditing();
    bool undoPolygonPoint();
    void setDocument(Document *document);
    void setMaskLayers(Document *keepout, Document *speed);
    void setMaskOpacity(double opacity) { maskOpacity_ = qBound(0., opacity, 1.); update(); }
    void setMaskVisible(bool keepout, bool speed) { showKeepout_ = keepout; showSpeed_ = speed; update(); }
    void fit();
    void actualSize();
    void zoom(double factor);
    double scale() const { return scale_; }
    bool isEditing() const { return drawing_; }
    void setRotationPreview(double degrees);
    void clearRotationPreview();
    bool rotationPreview() const { return rotationPreview_; }
    void setCropSelection(QRect area);
    QRect cropSelection() const { return cropSelection_; }
    void setOriginPreview(const MapIO::Metadata &meta) { originPreview_ = true; originMeta_ = meta; update(); }
    void clearOriginPreview() { originPreview_ = false; update(); }
    void setAxes(bool visible) { axes_ = visible; update(); }
    QPointF mapPoint(QPointF pos) const { return (pos - offset_) / scale_; }
    QPointF screenPoint(QPointF pos) const { return offset_ + pos * scale_; }
signals:
    void positionChanged(QPointF pixel);
    void zoomChanged(double scale);
    void fileDropped(const QString &path);
    void colorPicked(int gray);
    void editingChanged();
    void paintRequested();
    void cropChanged(QRect area);
    void geometryApplyRequested();
    void previewCancelRequested();
    void originPicked(QPointF pixel);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    QPoint pixelPoint(QPointF pos) const;
    bool insideImage(QPointF position) const;
    int activeGray() const { return gray_; }
    void zoomAt(double factor, QPointF anchor);
    void finishStroke();
    void cancelStroke();
    void finishPolygon();
    bool editableVisible() const;
    QSize displaySize() const;
    Qt::Edges cropEdgesAt(QPointF position) const;
    static Qt::CursorShape cropResizeCursor(Qt::Edges edges);
    void updateCropCursor(QPointF position);
    Document *doc_;
    Document *base_;
    Document *keepout_ = nullptr, *speed_ = nullptr;
    double maskOpacity_ = .45;
    bool showKeepout_ = true, showSpeed_ = true;
    QPolygonF polygon_;
    QPointF polygonHover_;
    Tool tool_ = Pen;
    int brushSize_ = 8, gray_ = 0;
    double scale_ = 1;
    QPointF offset_, panStart_, mouse_;
    QPoint start_, last_;
    bool drawing_ = false, panning_ = false, space_ = false, inside_ = false, axes_ = true, filled_ = false;
    bool fitted_ = true;
    QImage draft_;
    bool rotationPreview_ = false, selectingCrop_ = false;
    QTransform previewTransform_;
    QSize previewSize_;
    QRect cropSelection_;
    QPoint cropStart_;
    QRect cropBeforeDrag_;
    bool movingCrop_ = false;
    Qt::Edges cropResizeEdges_;
    bool originPreview_ = false, draggingOrigin_ = false;
    MapIO::Metadata originMeta_;
};
