#pragma once
#include "canvas.h"
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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openPath(const QString &path);
    Canvas *canvas() const { return canvas_; }
protected:
    void closeEvent(QCloseEvent *) override;
private:
    QWidget *makeTools();
    QWidget *makeProperties();
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
    bool maybeSave();
    bool applyMetadata();
    void report(const std::exception &e);
    void markField(const QString &key);
    Document doc_;
    Canvas *canvas_;
    QButtonGroup *tools_;
    QButtonGroup *colors_;
    QComboBox *eraser_, *negate_;
    QDoubleSpinBox *resolution_, *x_, *y_, *yaw_, *free_, *occupied_;
    QSpinBox *size_;
    QCheckBox *filled_;
    QDockWidget *propertiesDock_;
    QWidget *toolPanel_, *propertiesPanel_;
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
};
