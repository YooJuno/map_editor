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
class QToolBar;
class QToolButton;

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
    void changeEvent(QEvent *) override;
private:
    QWidget *makeTools();
    QWidget *makeToolRail();
    QWidget *makeNavigation();
    QWidget *makeGeometry();
    void selectWorkspace(int mode);
    void resetGeometry();
    bool applyGeometry();
    bool geometryPending() const;
    void updateCropFields(QRect area);
    void changeTheme(bool dark);
    void changeLanguage(const QString &code);
    void refreshThemeIcons();
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
    static bool maskInUse(const Document &mask) { return !mask.empty() && (mask.dirty() || !mask.map().yamlPath.isEmpty()); }
    bool hasMasks() const { return maskInUse(keepout_) || maskInUse(speedMask_); }
    bool hasDirtyMasks() const { return keepout_.dirty() || speedMask_.dirty() || (activeLayer_ > 0 && canvas_->isEditing()); }
    void detachCleanMasks();
    void checkSavePaths(Document *document, const QString &yamlPath);
    bool applyMetadata();
    void report(const std::exception &e);
    void markField(const QString &key);
    Document doc_;
    Document keepout_, speedMask_;
    Canvas *canvas_;
    QButtonGroup *tools_, *workspaces_;
    QButtonGroup *colors_, *maskColors_;
    QComboBox *negate_;
    QDoubleSpinBox *resolution_, *x_, *y_, *yaw_, *free_, *occupied_;
    QSpinBox *size_;
    QCheckBox *filled_;
    QDockWidget *propertiesDock_;
    QWidget *toolPanel_, *toolRail_, *propertiesPanel_;
    QWidget *baseProperties_, *maskProperties_, *colorPanel_, *maskColorPanel_, *brushPanel_, *layersPanel_;
    QComboBox *layer_;
    QSpinBox *speedPercent_;
    QSlider *opacity_;
    QCheckBox *keepoutVisible_, *speedVisible_;
    QLabel *opacityLabel_, *maskHelp_, *speedLabel_, *geometryHint_;
    QPushButton *openMask_, *saveMask_, *closeMasks_, *clearMask_;
    QLabel *fileLabel_, *details_, *coordinates_, *zoomLabel_, *radLabel_, *paletteNote_, *metadataState_;
    QLabel *documentTitle_, *documentState_, *toolTitle_, *toolContext_;
    QPushButton *apply_, *revert_, *resetGray_;
    QAction *saveAction_, *saveAsAction_, *saveAllAction_, *undoAction_, *redoAction_;
    QToolButton *saveOptionsButton_;
    QAction *openAction_;
    QAction *darkThemeAction_, *lightThemeAction_;
    QToolButton *themeButton_;
    QToolButton *languageButton_;
    bool uiReady_ = false;
    QMenu *viewMenu_;
    QAction *rotateAction_, *normalizeAction_, *fitAction_, *actualAction_, *zoomInAction_, *zoomOutAction_;
    QToolBar *drawingRail_;
    QWidget *geometryPanel_, *rotationPanel_, *cropPanel_, *paintColorPanel_;
    QDoubleSpinBox *rotationAngle_;
    QSlider *rotationSlider_;
    QSpinBox *cropX_, *cropY_, *cropWidth_, *cropHeight_;
    QPushButton *applyGeometry_, *resetGeometry_;
    QLabel *geometryStatus_, *originHelp_, *geometryLockHint_;
    QWidget *toolDivider_;
    int workspaceMode_ = 0;
    bool changingGeometry_ = false;
    QSet<QString> pending_;
    bool syncing_ = false;
    int colorIndex_ = 1;
    int sampledGray_ = -1;
    int lastPaintTool_ = Canvas::Pen;
    int currentTool_ = Canvas::Pen;
    int penSize_ = 8;
    int activeLayer_ = 0;
    int mapTool_ = Canvas::Pen, maskTool_ = Canvas::Polygon;
    bool changingLayers_ = false;
};
