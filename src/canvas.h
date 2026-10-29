#pragma once
#include "document.h"
#include <QWidget>
#include <QPolygonF>
#include <QTransform>
#include <QTimer>

namespace Raster {
void stroke(QImage &image, QPoint from, QPoint to, int diameter, int gray);
void rectangle(QImage &image, QPoint from, QPoint to, int diameter, int gray, bool filled);
void flood(QImage &image, QPoint start, int gray);
void polygon(QImage &image, const QPolygonF &points, int gray);
}
class Canvas : public QWidget {
    Q_OBJECT
public:
    enum Tool { Pen, Line, Rectangle, Fill, Hand, Picker, Polygon, Crop, Origin, Rotate };
    explicit Canvas(Document *document, QWidget *parent = nullptr);
    void setTool(Tool tool);
    void setBrushSize(int size) { if (brushSize_ != size) { brushSize_ = size; update(); } }
    void setGray(int gray) { if (gray_ != gray) { gray_ = gray; update(); } }
    void setFilled(bool filled) { filled_ = filled; }
    void finishEditing();
    bool undoPolygonPoint();
    int polygonPointCount() const { return tool_ == Polygon && drawing_ ? int(polygon_.size()) : 0; }
    void cancelPolygon() { if (tool_ == Polygon && drawing_) cancelStroke(); }
    void setDocument(Document *document);
    void setWorkQueue(WorkQueue *queue) { worker_ = queue; }
    void setEditLocked(bool locked) { editLocked_ = locked; }
    void setMaskLayers(Document *keepout, Document *speed);
    void setMaskOpacity(double opacity) { opacity = qBound(0., opacity, 1.); if (maskOpacity_ != opacity) { maskOpacity_ = opacity; update(); } }
    void setMaskVisible(bool keepout, bool speed) { if (showKeepout_ != keepout || showSpeed_ != speed) { showKeepout_ = keepout; showSpeed_ = speed; update(); } }
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
    void setOriginPreview(const MapIO::Metadata &meta) { if (!originPreview_ || originMeta_ != meta) { originPreview_ = true; originMeta_ = meta; update(); } }
    void clearOriginPreview() { if (originPreview_ || draggingOrigin_ || rotatingOrigin_) { originPreview_ = draggingOrigin_ = rotatingOrigin_ = false; update(); } }
    void setAxes(bool visible) { if (axes_ != visible) { axes_ = visible; update(); } }
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
    void originYawDragged(QPointF pivotPixel, double degrees);
    void rotationDragged(double degrees);
    void operationFailed(const QString &message);
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
    bool prepareDraft();
    void restoreDraft();
    void updateShapePreview();
    QRect strokeBounds(QPoint from, QPoint to) const;
    bool editableVisible() const;
    QSize displaySize() const;
    Qt::Edges cropEdgesAt(QPointF position) const;
    static Qt::CursorShape cropResizeCursor(Qt::Edges edges);
    void updateCropCursor(QPointF position);
    QPointF originPosition() const;
    int originHandleAt(QPointF position) const;
    QPointF rotationCenter() const;
    double rotationRadius() const;
    void updateGeometryCursor(QPointF position);
    void updateAngleDrag(QPointF position, Qt::KeyboardModifiers modifiers);
    void stopGeometryDrag();
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
    QRect draftArea_;
    qint64 draftSourceKey_ = 0;
    QTimer previewTimer_, draftIdleTimer_;
    WorkQueue *worker_ = nullptr;
    bool editLocked_ = false;
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
    bool rotatingImage_ = false, rotatingOrigin_ = false;
    bool angleDragMoved_ = false, originGrabbed_ = false;
    QPointF originGrabOffset_, rotationDragCenter_, originDragPixel_, originPressPosition_;
    double previewDegrees_ = 0, previousPointerAngle_ = 0, dragDegrees_ = 0;
};
