#pragma once
#include "canvas.h"
#include "mask.h"
#include <QMainWindow>
#include <QSet>
class QDoubleSpinBox;
class QPushButton;
class QComboBox;
class QLabel;
class QSpinBox;
class QButtonGroup;
class QAction;
class QCheckBox;
class QDockWidget;
class QMenu;
class QSlider;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openPath(const QString &path);
    Canvas *canvas() const { return canvas_; }
    Document *activeDocument() { return activeLayer_ == 1 ? &keepout_ : activeLayer_ == 2 ? &speedMask_ : &doc_; }
    void openMaskPath(const QString &path);
    void openMaskPath(const QString &path, Mask::Kind kind);
protected:
    void closeEvent(QCloseEvent *) override;
private:
    QWidget *makeTools();
    QWidget *makeProperties();
    QWidget *makeLayers();
    void makeActions();
    void selectTool(int tool);
    void undoEdit();
    void redoEdit();
    void revertMetadata();
    MapIO::Metadata editedMetadata() const;
    void refresh();
    void refreshFields();
    void refreshPalette();
    void chooseColor(int index);
    void sampleColor(int gray);
    int grayBrushValue() const;
    void newMap();
    void rotate();
    void open();
    bool save(bool as = false);
    bool saveDocument(Document *document, bool as = false);
    bool saveAll();
    bool maybeSave();
    bool maybeSaveMasks();
    void selectLayer(int index);
    void openMask();
    void closeMasks();
    void clearMask();
    bool hasMasks() const { return !keepout_.empty() || !speedMask_.empty(); }
    void checkSavePaths(Document *document, const QString &yamlPath);
    bool applyMetadata();
    void report(const std::exception &e);
    void markField(const QString &key);
    Document doc_;
    Document keepout_, speedMask_;
    Canvas *canvas_;
    QButtonGroup *tools_;
    QButtonGroup *colors_;
    QComboBox *eraser_, *negate_;
    QDoubleSpinBox *resolution_, *x_, *y_, *yaw_, *free_, *occupied_;
    QSpinBox *size_;
    QCheckBox *filled_;
    QDockWidget *propertiesDock_;
    QWidget *toolPanel_, *propertiesPanel_;
    QWidget *baseProperties_, *maskProperties_, *colorPanel_, *eraserPanel_;
    QComboBox *layer_;
    QSpinBox *speedPercent_;
    QSlider *opacity_;
    QCheckBox *keepoutVisible_, *speedVisible_;
    QLabel *opacityLabel_, *maskHelp_, *speedLabel_, *geometryHint_;
    QPushButton *openMask_, *saveMask_, *closeMasks_, *clearMask_;
    QLabel *fileLabel_, *details_, *coordinates_, *zoomLabel_, *radLabel_, *paletteNote_, *metadataState_;
    QPushButton *apply_, *revert_, *resetGray_;
    QAction *saveAction_, *saveAsAction_, *undoAction_, *redoAction_;
    QAction *newAction_, *openAction_;
    QMenu *viewMenu_;
    QAction *rotateAction_, *normalizeAction_, *fitAction_, *actualAction_, *zoomInAction_, *zoomOutAction_;
    QSet<QString> pending_;
    bool syncing_ = false;
    int colorIndex_ = 1;
    int sampledGray_ = -1;
    int lastPaintTool_ = Canvas::Pen;
    int currentTool_ = Canvas::Pen;
    int penSize_ = 8, eraserSize_ = 20;
    int activeLayer_ = 0;
    int mapTool_ = Canvas::Pen, maskTool_ = Canvas::Polygon;
    bool changingLayers_ = false;
};
