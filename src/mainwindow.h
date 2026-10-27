#pragma once
#include "canvas.h"
#include <QMainWindow>
#include <QSet>
class QDoubleSpinBox;
class QPushButton;
class QComboBox;
class QLabel;
class QSpinBox;
class QSlider;
class QButtonGroup;

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
    QSlider *sizeSlider_;
    QLabel *fileLabel_, *details_, *coordinates_, *zoomLabel_, *radLabel_, *paletteNote_, *metadataState_;
    QPushButton *apply_, *resetGray_;
    QSet<QString> pending_;
    bool syncing_ = false;
    int colorIndex_ = 1;
    int sampledGray_ = -1;
    int lastPaintTool_ = Canvas::Pen;
};
