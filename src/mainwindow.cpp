#include "mainwindow.h"
#include "icons.h"
#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr double Rad = 3.14159265358979323846 / 180.;

class NumberBox : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
protected:
    QString textFromValue(double value) const override {
        auto text = QLocale::c().toString(value, 'f', decimals());
        if (text.contains('.')) {
            while (text.endsWith('0')) text.chop(1);
            if (text.endsWith('.')) text.chop(1);
        }
        return text;
    }
};

class DrawingButton : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option; initStyleOption(&option);
        option.text.clear(); option.icon = QIcon();
        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
        const bool iconOnly = property("iconOnly").toBool(), locked = property("locked").toBool();
        icon().paint(&painter, QRect((width() - 22) / 2, iconOnly ? (height() - 22) / 2 : 8, 22, 22), Qt::AlignCenter,
            isEnabled() && !locked ? QIcon::Normal : QIcon::Disabled);
        if (iconOnly) return;
        auto labelFont = font(); labelFont.setPointSizeF(qMax(8., font().pointSizeF() - 1.));
        painter.setFont(labelFont);
        painter.setPen(isEnabled() ? QColor(Theme::isDark() ? (isChecked() ? "#e1e7ef" : "#adb3bd") : (isChecked() ? "#34485f" : "#737a84")) : QColor(Theme::isDark() ? "#737c89" : "#a6abb4"));
        painter.drawText(QRect(0, 32, width(), 19), Qt::AlignHCenter | Qt::AlignTop, text());
        if (locked) {
            painter.setPen(QPen(Theme::isDark() ? QColor("#9ba6b7") : QColor("#7b879a"), 1.2)); painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(width() - 18, 11, 9, 7), 1, 1);
            painter.drawArc(QRectF(width() - 16, 5, 5, 9), 0, 180 * 16);
        }
    }
};

QLabel *sectionTitle(const QString &text) {
    auto *label = new QLabel(text); label->setObjectName("sectionTitle"); return label;
}

QToolButton *actionButton(QAction *action, const QString &name = {}) {
    auto *button = new QToolButton; button->setDefaultAction(action);
    button->setObjectName(name); button->setIconSize({18, 18});
    return button;
}

QDoubleSpinBox *spin(const QString &name, double min, double max, int digits, double step) {
    auto *box = new NumberBox;
    box->setObjectName(name);
    box->setRange(min, max);
    box->setDecimals(digits);
    box->setSingleStep(step);
    box->setKeyboardTracking(false);
    box->setLocale(QLocale::c());
    box->setMinimumWidth(125);
    return box;
}

QFrame *separator() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    line->setObjectName("toolbarDivider");
    line->setFixedWidth(1);
    line->setFixedHeight(22);
    return line;
}
QFrame *rule() {
    auto *line = new QFrame; line->setFrameShape(QFrame::HLine); line->setObjectName("sectionRule"); return line;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), doc_(this), keepout_(this, true), speedMask_(this, true), canvas_(new Canvas(&doc_)) {
    setObjectName("mainWindow");
    setMinimumSize(1060, 700);
    resize(1400, 900);
    setWindowIcon(Icons::get(Icons::Map));
    setCentralWidget(canvas_);
    canvas_->setMaskLayers(&keepout_, &speedMask_);
    makeActions();
    tools_ = new QButtonGroup(this);

    auto *fileBar = addToolBar("파일");
    fileBar->setObjectName("fileToolbar");
    fileBar->setMovable(false);
    fileBar->setFixedHeight(48);
    fileBar->setIconSize({18, 18});
    fileBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *brand = new QWidget; brand->setObjectName("brand");
    auto *brandRow = new QHBoxLayout(brand); brandRow->setContentsMargins(6, 0, 24, 0); brandRow->setSpacing(10);
    auto *brandIcon = new QLabel; brandIcon->setPixmap(Icons::get(Icons::Map, QColor("#abb9cc")).pixmap(26, 26));
    brandIcon->setObjectName("brandIcon");
    auto *brandLabel = new QLabel("Map Editor"); brandLabel->setObjectName("brandLabel");
    brandRow->addWidget(brandIcon); brandRow->addWidget(brandLabel); fileBar->addWidget(brand);
    fileBar->addWidget(actionButton(openAction_));
    auto *leftSpace = new QWidget; leftSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); fileBar->addWidget(leftSpace);
    documentTitle_ = new QLabel; documentTitle_->setObjectName("documentTitle");
    documentTitle_->setMaximumWidth(300); fileBar->addWidget(documentTitle_);
    documentState_ = new QLabel; documentState_->setObjectName("documentState"); fileBar->addWidget(documentState_);
    auto *rightSpace = new QWidget; rightSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); fileBar->addWidget(rightSpace);
    fileBar->addWidget(actionButton(undoAction_)); fileBar->addWidget(actionButton(redoAction_));
    fileBar->addWidget(separator());
    auto *themeMenu = new QMenu("테마", this); auto *themeGroup = new QActionGroup(this);
    darkThemeAction_ = themeMenu->addAction("다크 모드"); darkThemeAction_->setObjectName("darkTheme");
    lightThemeAction_ = themeMenu->addAction("라이트 모드"); lightThemeAction_->setObjectName("lightTheme");
    for (auto *action : {darkThemeAction_, lightThemeAction_}) { action->setCheckable(true); themeGroup->addAction(action); }
    connect(darkThemeAction_, &QAction::triggered, this, [this] { changeTheme(true); });
    connect(lightThemeAction_, &QAction::triggered, this, [this] { changeTheme(false); });
    viewMenu_->addMenu(themeMenu);
    themeButton_ = new QToolButton; themeButton_->setObjectName("themeButton"); themeButton_->setMenu(themeMenu);
    themeButton_->setPopupMode(QToolButton::InstantPopup); themeButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    themeButton_->setAccessibleName("테마 선택");
    themeButton_->setToolTip("다크 / 라이트 테마 선택"); fileBar->addWidget(themeButton_);
    auto *saveButton = actionButton(saveAction_, "primarySave");
    saveButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto *saveMenu = new QMenu(saveButton); saveMenu->addAction(saveAsAction_);
    saveMenu->addAction("모두 저장", this, [this] { saveAll(); });
    saveButton->setMenu(saveMenu); saveButton->setPopupMode(QToolButton::MenuButtonPopup);
    fileBar->addWidget(saveButton);

    addToolBarBreak();
    auto *drawBar = addToolBar("그리기");
    drawBar->setObjectName("drawingToolbar");
    drawBar->setMovable(false);
    drawBar->setFixedHeight(48);
    toolPanel_ = makeTools();
    drawBar->addWidget(toolPanel_);
    auto *navigation = new QToolBar("작업", this); navigation->setObjectName("navigationRail"); navigation->setMovable(false);
    navigation->setAllowedAreas(Qt::LeftToolBarArea); addToolBar(Qt::LeftToolBarArea, navigation);
    navigation->addWidget(makeNavigation());
    addToolBarBreak(Qt::LeftToolBarArea);
    auto *rail = new QToolBar("도구", this); rail->setObjectName("toolsRail"); rail->setMovable(false);
    drawingRail_ = rail;
    rail->setAllowedAreas(Qt::LeftToolBarArea); addToolBar(Qt::LeftToolBarArea, rail);
    toolRail_ = makeToolRail(); rail->addWidget(toolRail_);

    propertiesDock_ = new QDockWidget("속성 패널", this);
    propertiesDock_->setObjectName("propertiesDock");
    propertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesDock_->setFeatures(QDockWidget::DockWidgetClosable);
    auto *inspectorHeader = new QWidget; inspectorHeader->setObjectName("inspectorHeader");
    auto *inspectorRow = new QHBoxLayout(inspectorHeader); inspectorRow->setContentsMargins(20, 12, 12, 12);
    inspectorRow->addWidget(sectionTitle("문서")); inspectorRow->addStretch();
    auto *hideInspector = new QToolButton; hideInspector->setObjectName("hideInspector"); hideInspector->setIcon(Icons::get(Icons::Chevron));
    hideInspector->setToolTip("속성 패널 접기 · 보기 메뉴에서 다시 열기"); inspectorRow->addWidget(hideInspector);
    propertiesDock_->setTitleBarWidget(inspectorHeader);
    connect(hideInspector, &QToolButton::clicked, propertiesDock_, &QWidget::hide);
    propertiesDock_->setWidget(makeProperties());
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);
    resizeDocks({propertiesDock_}, {304}, Qt::Horizontal);
    viewMenu_->addAction(propertiesDock_->toggleViewAction());

    coordinates_ = new QLabel("YAML / PGM 지도 편집"); coordinates_->setObjectName("coordinates");
    coordinates_->setMargin(5);
    coordinates_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addPermanentWidget(coordinates_, 1);
    statusBar()->setMinimumHeight(34);
    zoomLabel_ = new QLabel;
    zoomLabel_->setObjectName("zoomLabel"); zoomLabel_->setMinimumWidth(48);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    auto *zoomPanel = new QWidget; auto *zoomRow = new QHBoxLayout(zoomPanel); zoomRow->setContentsMargins(0, 0, 8, 0); zoomRow->setSpacing(3);
    zoomRow->addWidget(actionButton(zoomOutAction_)); zoomRow->addWidget(zoomLabel_);
    zoomRow->addWidget(actionButton(zoomInAction_)); zoomRow->addWidget(actionButton(fitAction_));
    auto *actual = actionButton(actualAction_); actual->setToolButtonStyle(Qt::ToolButtonTextOnly); zoomRow->addWidget(actual);
    statusBar()->addPermanentWidget(zoomPanel);

    connect(canvas_, &Canvas::zoomChanged, this, [this](double scale) {
        zoomLabel_->setText(QString::number(scale * 100, 'f', scale < .1 ? 1 : 0) + "%");
    });
    connect(canvas_, &Canvas::fileDropped, this, [this](const QString &path) {
        if (activeLayer_ > 0) openMaskPath(path); else openPath(path);
    });
    connect(canvas_, &Canvas::colorPicked, this, &MainWindow::sampleColor);
    connect(canvas_, &Canvas::editingChanged, this, &MainWindow::refresh);
    connect(canvas_, &Canvas::cropChanged, this, &MainWindow::updateCropFields);
    connect(canvas_, &Canvas::geometryApplyRequested, this, [this] { if (workspaceMode_ == 5) applyMetadata(); else if (workspaceMode_ >= 3) applyGeometry(); });
    connect(canvas_, &Canvas::previewCancelRequested, this, [this] { if (workspaceMode_ == 5) revertMetadata(); else resetGeometry(); });
    connect(canvas_, &Canvas::originPicked, this, [this](QPointF point) {
        if (workspaceMode_ != 5 || hasMasks()) return;
        auto meta = editedMetadata(); meta.x = meta.y = 0;
        const auto offset = MapIO::pixelToWorld(point, doc_.image().height(), meta);
        x_->setValue(-offset.x()); y_->setValue(-offset.y());
    });
    connect(canvas_, &Canvas::positionChanged, this, [this](QPointF point) {
        if (doc_.empty() || activeDocument()->empty() || point.x() < 0 || point.y() < 0 || point.x() >= doc_.image().width() || point.y() >= doc_.image().height()) return;
        const QPoint cell(int(std::floor(point.x())), int(std::floor(point.y())));
        const auto world = MapIO::pixelToWorld(QPointF(cell) + QPointF(.5, .5), doc_.image().height(), workspaceMode_ == 5 ? editedMetadata() : doc_.meta());
        const int gray = activeDocument()->image().constScanLine(cell.y())[cell.x()];
        if (activeLayer_ > 0) {
            const QString state = gray == 255 ? "미확인" : gray == 0 ? "제한 없음" : activeLayer_ == 1 ? "진입 금지" : QString("최대 속도의 %1%").arg(gray);
            coordinates_->setText(QString("픽셀 %1, %2    좌표 %3, %4 m    %5").arg(cell.x()).arg(cell.y())
                .arg(world.x(), 0, 'f', 3).arg(world.y(), 0, 'f', 3).arg(state));
            return;
        }
        const auto probability = MapIO::probability(gray, doc_.meta());
        const auto role = probability > doc_.meta().occupied ? "장애물" : (probability < doc_.meta().free ? "빈 공간" : "미확인");
        coordinates_->setText(QString("픽셀 %1, %2    좌표 %3, %4 m    값 %5 (%6)")
            .arg(cell.x()).arg(cell.y()).arg(world.x(), 0, 'f', 3).arg(world.y(), 0, 'f', 3).arg(gray).arg(role));
    });
    connect(&doc_, &Document::changed, this, &MainWindow::refresh);
    connect(&keepout_, &Document::changed, this, &MainWindow::refresh);
    connect(&speedMask_, &Document::changed, this, &MainWindow::refresh);
    connect(&doc_, &Document::loaded, this, [this] {
        changingLayers_ = true;
        keepout_.clear(); speedMask_.clear(); activeLayer_ = 0;
        mapTool_ = Canvas::Pen; maskTool_ = Canvas::Polygon;
        workspaceMode_ = 0; resetGeometry(); workspaces_->button(0)->setChecked(true);
        { QSignalBlocker block(layer_); layer_->setCurrentIndex(0); }
        canvas_->setDocument(&doc_);
        changingLayers_ = false;
        pending_.clear();
        sampledGray_ = -1;
        refreshFields();
        tools_->button(Canvas::Pen)->click();
        canvas_->setFocus();
    });
    const auto geometry = QSettings().value("windowGeometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    refreshThemeIcons();
    refresh();
}

void MainWindow::changeTheme(bool dark) {
    Theme::apply(dark); QSettings().setValue("colorTheme", dark ? "dark" : "light");
    refreshThemeIcons(); refresh(); canvas_->update();
}
void MainWindow::refreshThemeIcons() {
    const bool dark = Theme::isDark();
    darkThemeAction_->setChecked(dark); lightThemeAction_->setChecked(!dark);
    themeButton_->setText(dark ? "다크" : "라이트"); themeButton_->setIcon(Icons::get(dark ? Icons::Moon : Icons::Sun));
    themeButton_->setToolTip(dark ? "다크 모드 · 테마 선택" : "라이트 모드 · 테마 선택");
    setWindowIcon(Icons::get(Icons::Map));
    findChild<QLabel *>("brandIcon")->setPixmap(Icons::get(Icons::Map, QColor(dark ? "#abb9cc" : "#5d708a")).pixmap(26, 26));
    findChild<QToolButton *>("hideInspector")->setIcon(Icons::get(Icons::Chevron));
    const Icons::Name toolIcons[]{Icons::Pen, Icons::Line, Icons::Rectangle, Icons::Fill, Icons::Hand, Icons::Picker, Icons::Polygon};
    for (int i = 0; i <= Canvas::Polygon; ++i) tools_->button(i)->setIcon(Icons::get(toolIcons[i]));
    const Icons::Name navigationIcons[]{Icons::Map, Icons::Polygon, Icons::Polygon, Icons::Rotate, Icons::Crop, Icons::Origin};
    for (int i = 0; i < 6; ++i) workspaces_->button(i)->setIcon(Icons::get(navigationIcons[i], QColor(i == 1 ? (dark ? "#dba397" : "#b56b61") : i == 2 ? (dark ? "#a0bfe0" : "#6586b0") : (dark ? "#c5cbd4" : "#535d6c"))));
    openAction_->setIcon(Icons::get(Icons::Open));
    saveAction_->setIcon(Icons::get(Icons::Save, QColor(dark ? "#e1e7ef" : "#4b5f7b")));
    undoAction_->setIcon(Icons::get(Icons::Undo)); redoAction_->setIcon(Icons::get(Icons::Redo));
    fitAction_->setIcon(Icons::get(Icons::Fit)); zoomInAction_->setIcon(Icons::get(Icons::Plus)); zoomOutAction_->setIcon(Icons::get(Icons::Minus));
}

QWidget *MainWindow::makeNavigation() {
    auto *panel = new QWidget; auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(7, 12, 7, 12); column->setSpacing(7);
    workspaces_ = new QButtonGroup(this);
    const QStringList labels{"지도", "진입 금지", "속도 제한", "회전", "자르기", "원점"};
    const QStringList names{"mapWorkspace", "keepoutWorkspace", "speedWorkspace", "rotateWorkspace", "cropWorkspace", "originWorkspace"};
    const Icons::Name icons[]{Icons::Map, Icons::Polygon, Icons::Polygon, Icons::Rotate, Icons::Crop, Icons::Origin};
    for (int i = 0; i < labels.size(); ++i) {
        if (i == 3) {
            column->addSpacing(10); column->addWidget(rule());
            geometryLockHint_ = new QLabel; geometryLockHint_->setObjectName("navigationHint"); geometryLockHint_->setAlignment(Qt::AlignCenter); geometryLockHint_->setWordWrap(true);
            column->addWidget(geometryLockHint_); column->addSpacing(10);
        }
        auto *button = new DrawingButton(labels[i]); button->setObjectName(names[i]); button->setProperty("navigation", true);
        button->setCheckable(true); button->setFixedSize(68, 60);
        button->setIcon(Icons::get(icons[i], QColor(i == 1 ? "#dba397" : i == 2 ? "#a0bfe0" : "#c5cbd4")));
        button->setToolTip(i == 1 ? "진입 금지 마스크 만들기 / 편집" : i == 2 ? "속도 제한 마스크 만들기 / 편집" : labels[i]);
        button->setAccessibleName(labels[i]); workspaces_->addButton(button, i); column->addWidget(button);
    }
    workspaces_->button(0)->setChecked(true);
    connect(workspaces_, &QButtonGroup::idClicked, this, &MainWindow::selectWorkspace);
    column->addStretch(); return panel;
}

QWidget *MainWindow::makeGeometry() {
    auto *panel = new QWidget; auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(16);
    rotationPanel_ = new QWidget; auto *rotation = new QVBoxLayout(rotationPanel_); rotation->setContentsMargins(0, 0, 0, 0); rotation->setSpacing(12);
    rotation->addWidget(sectionTitle("이미지 회전"));
    auto *explanation = new QLabel("슬라이더나 숫자로 조정한 뒤 적용하세요.\n원점과 Yaw도 함께 보정합니다."); explanation->setWordWrap(true); explanation->setObjectName("instructions"); rotation->addWidget(explanation);
    rotationAngle_ = spin("rotationAngle", -180, 180, 3, .1); rotationAngle_->setSuffix(" °"); rotation->addWidget(rotationAngle_);
    rotationSlider_ = new QSlider(Qt::Horizontal); rotationSlider_->setObjectName("rotationSlider"); rotationSlider_->setRange(-1800, 1800); rotationSlider_->setSingleStep(1); rotationSlider_->setPageStep(50); rotation->addWidget(rotationSlider_);
    auto *ticks = new QHBoxLayout; ticks->addWidget(new QLabel("−180°")); ticks->addStretch(); ticks->addWidget(new QLabel("0°")); ticks->addStretch(); ticks->addWidget(new QLabel("180°")); rotation->addLayout(ticks);
    auto *quick = new QHBoxLayout;
    for (int angle : {-90, 90}) {
        auto *button = new QPushButton(angle < 0 ? "−90°" : "+90°"); quick->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, angle] { rotationAngle_->setValue(std::remainder(rotationAngle_->value() + angle, 360.)); });
    }
    rotation->addLayout(quick);
    connect(rotationAngle_, &QDoubleSpinBox::valueChanged, this, [this](double angle) {
        if (changingGeometry_) return;
        { QSignalBlocker block(rotationSlider_); rotationSlider_->setValue(qRound(angle * 10)); }
        if (workspaceMode_ == 3) { canvas_->setRotationPreview(angle); refresh(); }
    });
    connect(rotationSlider_, &QSlider::valueChanged, this, [this](int value) { if (!changingGeometry_) rotationAngle_->setValue(value / 10.); });
    layout->addWidget(rotationPanel_);
    cropPanel_ = new QWidget; auto *crop = new QVBoxLayout(cropPanel_); crop->setContentsMargins(0, 0, 0, 0); crop->setSpacing(12);
    crop->addWidget(sectionTitle("지도 자르기"));
    auto *cropHelp = new QLabel("지도에서 남길 영역을 드래그하세요.\n자른 위치에 맞춰 원점을 보정합니다."); cropHelp->setWordWrap(true); cropHelp->setObjectName("instructions"); crop->addWidget(cropHelp);
    auto *form = new QFormLayout; form->setVerticalSpacing(10); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    cropX_ = new QSpinBox; cropY_ = new QSpinBox; cropWidth_ = new QSpinBox; cropHeight_ = new QSpinBox;
    const QStringList names{"cropX", "cropY", "cropWidth", "cropHeight"};
    int i = 0; for (auto *box : {cropX_, cropY_, cropWidth_, cropHeight_}) { box->setObjectName(names[i++]); box->setRange(0, MapIO::MaxPixels); box->setSuffix(" px"); box->setKeyboardTracking(false); }
    form->addRow("왼쪽 X", cropX_); form->addRow("위쪽 Y", cropY_); form->addRow("가로", cropWidth_); form->addRow("세로", cropHeight_); crop->addLayout(form);
    for (auto *box : {cropX_, cropY_, cropWidth_, cropHeight_}) connect(box, &QSpinBox::valueChanged, this, [this] {
        if (!changingGeometry_ && workspaceMode_ == 4) canvas_->setCropSelection(QRect(cropX_->value(), cropY_->value(), cropWidth_->value(), cropHeight_->value()));
    });
    auto *automatic = new QPushButton("미확인 여백 자동 선택"); automatic->setObjectName("autoCrop"); crop->addWidget(automatic);
    connect(automatic, &QPushButton::clicked, this, [this] {
        const auto bounds = doc_.knownBounds();
        if (bounds.isEmpty()) coordinates_->setText("모두 미확인 영역입니다. 남길 영역을 직접 선택하세요.");
        else { canvas_->setCropSelection(bounds); coordinates_->setText("미확인 여백을 제외한 영역을 선택했습니다. 적용 전 조정할 수 있습니다."); }
    });
    layout->addWidget(cropPanel_);
    geometryStatus_ = new QLabel; geometryStatus_->setWordWrap(true); geometryStatus_->setObjectName("instructions"); layout->addWidget(geometryStatus_);
    auto *buttons = new QHBoxLayout;
    applyGeometry_ = new QPushButton("적용"); applyGeometry_->setObjectName("applyGeometry"); applyGeometry_->setProperty("primary", true);
    resetGeometry_ = new QPushButton("초기화"); resetGeometry_->setObjectName("resetGeometry"); buttons->addWidget(applyGeometry_); buttons->addWidget(resetGeometry_); layout->addLayout(buttons);
    connect(applyGeometry_, &QPushButton::clicked, this, [this] { applyGeometry(); });
    connect(resetGeometry_, &QPushButton::clicked, this, &MainWindow::resetGeometry);
    return panel;
}

QWidget *MainWindow::makeToolRail() {
    auto *panel = new QWidget;
    auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(8, 12, 8, 12); column->setSpacing(5);
    const QStringList labels{"펜", "직선", "사각형", "채우기", "이동", "스포이드", "다각형"};
    const QStringList keys{"B", "L", "R", "G", "H", "I", "P"};
    const QStringList names{"penTool", "lineTool", "rectangleTool", "fillTool", "handTool", "pickerTool", "polygonTool"};
    const Icons::Name icons[]{Icons::Pen, Icons::Line, Icons::Rectangle, Icons::Fill, Icons::Hand, Icons::Picker, Icons::Polygon};
    for (int i = 0; i < labels.size(); ++i) {
        if (i == Canvas::Picker) continue;
        auto *button = new DrawingButton(labels[i]);
        button->setObjectName(names[i]);
        button->setProperty("drawingTool", true); button->setProperty("iconOnly", true); button->setIcon(Icons::get(icons[i]));
        button->setCheckable(true);
        button->setToolTip(labels[i] + " (" + keys[i] + ")");
        button->setAccessibleName(labels[i]); button->setFixedSize(44, 44);
        tools_->addButton(button, i);
        if (i == Canvas::Hand) {
            auto *divider = new QFrame; divider->setFrameShape(QFrame::HLine); divider->setObjectName("railDivider");
            column->addSpacing(6); column->addWidget(divider); column->addSpacing(6);
        }
        column->addWidget(button);
        if (i == Canvas::Pen) button->setChecked(true);
    }
    connect(tools_, &QButtonGroup::idClicked, this, &MainWindow::selectTool);
    column->addStretch();
    return panel;
}

QWidget *MainWindow::makeTools() {
    auto *panel = new QWidget;
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *row = new QHBoxLayout(panel);
    row->setContentsMargins(12, 4, 12, 4); row->setSpacing(12);
    toolTitle_ = new QLabel("펜"); toolTitle_->setObjectName("toolTitle"); toolTitle_->setMinimumWidth(52);
    row->addWidget(toolTitle_);
    toolDivider_ = separator(); row->addWidget(toolDivider_);
    paintColorPanel_ = new QWidget;
    auto *paintRow = new QHBoxLayout(paintColorPanel_); paintRow->setContentsMargins(0, 0, 0, 0); paintRow->setSpacing(10);
    row->addWidget(paintColorPanel_);
    colorPanel_ = new QWidget;
    auto *colorRow = new QHBoxLayout(colorPanel_); colorRow->setContentsMargins(0, 0, 0, 0); colorRow->setSpacing(4);
    colors_ = new QButtonGroup(this);
    for (int i = 0; i < 3; ++i) {
        auto *button = new QPushButton;
        button->setObjectName("color" + QString::number(i));
        button->setCheckable(true);
        button->setProperty("swatch", true); button->setFixedSize(34, 32); button->setIconSize({22, 22});
        button->setAccessibleName(QStringList{"흰색", "검정", "회색"}[i]);
        colors_->addButton(button, i);
        colorRow->addWidget(button);
    }
    connect(colors_, &QButtonGroup::idClicked, this, [this](int index) { chooseColor(index); canvas_->setFocus(); });
    resetGray_ = new QPushButton("회색 복원");
    resetGray_->setObjectName("resetGray");
    resetGray_->setToolTip("회색을 YAML 임계값 기준으로 되돌리기");
    colorRow->addWidget(resetGray_);
    paintRow->addWidget(colorPanel_);
    connect(resetGray_, &QPushButton::clicked, this, [this] { sampledGray_ = -1; refreshPalette(); canvas_->setFocus(); });
    brushPanel_ = new QWidget;
    auto *brushRow = new QHBoxLayout(brushPanel_); brushRow->setContentsMargins(0, 0, 0, 0); brushRow->setSpacing(8);
    size_ = new QSpinBox;
    size_->setObjectName("brushSize");
    size_->setAccessibleName("브러시 굵기"); size_->setToolTip("브러시 굵기 (px) · [ / ]");
    size_->setRange(1, 512);
    size_->setValue(penSize_);
    size_->setSuffix(" px");
    size_->setFixedWidth(88);
    brushRow->addWidget(size_); row->addWidget(brushPanel_);
    connect(size_, &QSpinBox::valueChanged, this, [this](int value) {
        penSize_ = value;
        canvas_->setBrushSize(value);
    });
    filled_ = new QCheckBox("내부 채움");
    filled_->setObjectName("filledRectangle");
    filled_->setToolTip("사각형 내부를 선택한 색으로 채우기");
    filled_->setEnabled(false);
    row->addWidget(filled_);
    connect(filled_, &QCheckBox::toggled, canvas_, &Canvas::setFilled);
    maskColorPanel_ = new QWidget; auto *maskRow = new QHBoxLayout(maskColorPanel_); maskRow->setContentsMargins(0, 0, 0, 0); maskRow->setSpacing(4);
    maskColors_ = new QButtonGroup(this);
    for (int i = 0; i < 2; ++i) {
        auto *button = new QPushButton(i == 0 ? "구역 그리기" : "제한 해제"); button->setCheckable(true); button->setProperty("swatch", true);
        button->setObjectName(i == 0 ? "paintZone" : "clearZone"); maskColors_->addButton(button, i); maskRow->addWidget(button);
    }
    maskColors_->button(0)->setChecked(true); paintRow->addWidget(maskColorPanel_);
    connect(maskColors_, &QButtonGroup::idClicked, this, [this] { refreshPalette(); canvas_->setFocus(); });
    auto *picker = new QPushButton; picker->setObjectName("pickerTool"); picker->setCheckable(true); picker->setFixedSize(34, 32);
    picker->setAccessibleName("스포이드"); picker->setIcon(Icons::get(Icons::Picker)); picker->setIconSize({20, 20}); picker->setToolTip("스포이드 · 실제 픽셀값 가져오기 (I)");
    tools_->addButton(picker, Canvas::Picker); paintRow->addWidget(picker);
    row->addStretch();
    toolContext_ = new QLabel; toolContext_->setObjectName("toolContext"); row->addWidget(toolContext_);
    return panel;
}

QWidget *MainWindow::makeLayers() {
    auto *panel = new QWidget;
    auto *column = new QVBoxLayout(panel); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(10);
    column->addWidget(sectionTitle("필터 표시"));
    layer_ = new QComboBox; layer_->setObjectName("activeLayer");
    layer_->addItems({"원본 지도", "진입 금지 마스크", "속도 제한 마스크"});
    layer_->setItemIcon(0, Icons::get(Icons::Map));
    layer_->setItemIcon(1, Icons::get(Icons::Polygon, QColor("#b56b61")));
    layer_->setItemIcon(2, Icons::get(Icons::Polygon, QColor("#6586b0")));
    layer_->setToolTip("편집할 지도 또는 필터 마스크를 선택합니다.");
    column->addWidget(layer_);
    layer_->hide();
    connect(layer_, &QComboBox::currentIndexChanged, this, &MainWindow::selectLayer);
    keepoutVisible_ = new QCheckBox("진입 금지"); keepoutVisible_->setObjectName("keepoutVisible"); keepoutVisible_->setChecked(true);
    speedVisible_ = new QCheckBox("속도 제한"); speedVisible_->setObjectName("speedVisible"); speedVisible_->setChecked(true);
    keepoutVisible_->setToolTip("빨강 마스크 표시"); speedVisible_->setToolTip("파랑 마스크 표시");
    auto *visibilityRow = new QHBoxLayout; visibilityRow->setSpacing(16);
    visibilityRow->addWidget(keepoutVisible_); visibilityRow->addWidget(speedVisible_); visibilityRow->addStretch();
    column->addLayout(visibilityRow);
    auto *row = new QHBoxLayout; row->setSpacing(10); row->addWidget(new QLabel("불투명도"));
    opacity_ = new QSlider(Qt::Horizontal); opacity_->setObjectName("maskOpacity"); opacity_->setRange(0, 100); opacity_->setValue(45);
    opacity_->setMinimumWidth(75);
    opacityLabel_ = new QLabel("45%"); opacityLabel_->setMinimumWidth(35);
    row->addWidget(opacity_); row->addWidget(opacityLabel_);
    column->addLayout(row);
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
        canvas_->setMaskOpacity(value / 100.); opacityLabel_->setText(QString::number(value) + "%"); refresh();
    });
    auto visibility = [this] {
        canvas_->finishEditing();
        canvas_->setMaskVisible(keepoutVisible_->isChecked(), speedVisible_->isChecked()); refresh();
    };
    connect(keepoutVisible_, &QCheckBox::toggled, this, visibility);
    connect(speedVisible_, &QCheckBox::toggled, this, visibility);
    return panel;
}

QWidget *MainWindow::makeProperties() {
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(294);
    scroll->setObjectName("inspectorScroll");
    auto *panel = new QWidget;
    panel->setObjectName("inspectorPanel");
    propertiesPanel_ = panel;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 8, 20, 18);
    layout->setSpacing(16);
    fileLabel_ = new QLabel;
    fileLabel_->setObjectName("fileName");
    fileLabel_->setWordWrap(true);
    fileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(fileLabel_);
    details_ = new QLabel;
    details_->setObjectName("documentDetails");
    layout->addWidget(details_);
    layout->addWidget(rule());
    layersPanel_ = makeLayers(); layout->addWidget(layersPanel_);
    layout->addWidget(rule());
    geometryPanel_ = makeGeometry(); layout->addWidget(geometryPanel_);
    baseProperties_ = new QWidget;
    auto *baseLayout = new QVBoxLayout(baseProperties_); baseLayout->setContentsMargins(0, 0, 0, 0); baseLayout->setSpacing(14);
    originHelp_ = new QLabel("월드 좌표 (0, 0)을 놓을 위치를 지도에서 클릭하거나 드래그한 뒤 적용하세요."); originHelp_->setWordWrap(true); originHelp_->setObjectName("instructions"); baseLayout->addWidget(originHelp_);
    baseLayout->addWidget(sectionTitle("지도 좌표"));
    auto *resolutionForm = new QFormLayout;
    resolutionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    resolution_ = spin("resolution", .000000001, 1000000, 9, .01);
    resolution_->setSuffix(" m/px");
    resolutionForm->addRow("해상도", resolution_);
    baseLayout->addLayout(resolutionForm);
    auto *originGroup = new QGroupBox("원점 · Origin");
    originGroup->setToolTip("지도의 왼쪽 아래 모서리에 해당하는 월드 좌표");
    auto *origin = new QFormLayout(originGroup);
    origin->setContentsMargins(0, 20, 0, 0); origin->setVerticalSpacing(10);
    origin->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    x_ = spin("originX", -1e12, 1e12, 6, .1);
    y_ = spin("originY", -1e12, 1e12, 6, .1);
    yaw_ = spin("originYaw", -1e9, 1e9, 6, 1);
    x_->setSuffix(" m"); y_->setSuffix(" m"); yaw_->setSuffix(" °");
    yaw_->setToolTip("YAML의 origin[2]. 도 단위로 입력하고 라디안으로 저장합니다.");
    origin->addRow("X", x_); origin->addRow("Y", y_); origin->addRow("Yaw", yaw_);
    radLabel_ = new QLabel;
    radLabel_->setObjectName("radians");
    origin->addRow("YAML", radLabel_);
    baseLayout->addWidget(originGroup);
    geometryHint_ = new QLabel("마스크를 닫으면 원점과 해상도를 변경할 수 있습니다."); geometryHint_->setWordWrap(true); baseLayout->addWidget(geometryHint_);

    auto *advancedToggle = new QToolButton;
    advancedToggle->setObjectName("advancedToggle");
    advancedToggle->setText("점유 설정");
    advancedToggle->setCheckable(true);
    advancedToggle->setArrowType(Qt::RightArrow);
    advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    baseLayout->addWidget(advancedToggle);
    auto *advanced = new QWidget;
    auto *thresholds = new QFormLayout(advanced); thresholds->setVerticalSpacing(10);
    thresholds->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    thresholds->setContentsMargins(0, 0, 0, 0);
    free_ = spin("freeThreshold", .000001, .999999, 6, .01);
    occupied_ = spin("occupiedThreshold", .000001, .999999, 6, .01);
    thresholds->addRow("Free", free_); thresholds->addRow("Occupied", occupied_);
    negate_ = new QComboBox;
    negate_->setObjectName("negate");
    negate_->addItems({"0 — 검정이 장애물", "1 — 흰색이 장애물"});
    thresholds->addRow("Negate", negate_);
    baseLayout->addWidget(advanced);
    advanced->hide();
    connect(advancedToggle, &QToolButton::toggled, this, [advancedToggle, advanced](bool open) {
        advanced->setVisible(open);
        advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });

    metadataState_ = new QLabel;
    metadataState_->setObjectName("metadataState");
    metadataState_->setWordWrap(true);
    baseLayout->addWidget(metadataState_);
    auto *buttons = new QHBoxLayout;
    apply_ = new QPushButton("적용"); apply_->setObjectName("applyMetadata");
    apply_->setProperty("primary", true);
    revert_ = new QPushButton("되돌리기"); revert_->setObjectName("revertMetadata");
    buttons->addWidget(apply_); buttons->addWidget(revert_);
    baseLayout->addLayout(buttons);
    connect(apply_, &QPushButton::clicked, this, [this] { applyMetadata(); });
    connect(revert_, &QPushButton::clicked, this, &MainWindow::revertMetadata);
    layout->addWidget(baseProperties_);
    maskProperties_ = new QWidget;
    auto *maskLayout = new QVBoxLayout(maskProperties_); maskLayout->setContentsMargins(0, 0, 0, 0); maskLayout->setSpacing(10);
    maskLayout->addWidget(sectionTitle("구역 편집"));
    maskHelp_ = new QLabel; maskHelp_->setWordWrap(true); maskLayout->addWidget(maskHelp_);
    speedLabel_ = new QLabel("새 구역 속도 (최대 속도 대비)"); speedLabel_->setWordWrap(true); maskLayout->addWidget(speedLabel_);
    speedPercent_ = new QSpinBox; speedPercent_->setObjectName("speedPercent"); speedPercent_->setRange(1, 100); speedPercent_->setValue(30); speedPercent_->setSuffix(" %"); maskLayout->addWidget(speedPercent_);
    connect(speedPercent_, &QSpinBox::valueChanged, this, [this] { if (activeLayer_ == 2) canvas_->setGray(speedPercent_->value()); refreshPalette(); });
    openMask_ = new QPushButton("마스크 열기…"); openMask_->setObjectName("openMask"); maskLayout->addWidget(openMask_);
    saveMask_ = new QPushButton("마스크 저장"); saveMask_->setObjectName("saveMask"); maskLayout->addWidget(saveMask_);
    saveMask_->setProperty("primary", true);
    clearMask_ = new QPushButton("모든 구역 지우기"); clearMask_->setObjectName("clearMask"); maskLayout->addWidget(clearMask_);
    connect(openMask_, &QPushButton::clicked, this, &MainWindow::openMask);
    connect(saveMask_, &QPushButton::clicked, this, [this] { save(); });
    connect(clearMask_, &QPushButton::clicked, this, &MainWindow::clearMask);
    auto *instructions = new QLabel("점을 찍고 Enter로 다각형 완성\nEsc 취소 · 제한 해제로 구역 삭제"); instructions->setObjectName("instructions"); instructions->setWordWrap(true); maskLayout->addWidget(instructions);
    layout->addWidget(maskProperties_);
    closeMasks_ = new QPushButton("마스크 모두 닫기"); closeMasks_->setObjectName("closeMasks"); layout->addWidget(closeMasks_);
    connect(closeMasks_, &QPushButton::clicked, this, &MainWindow::closeMasks);
    layout->addStretch();
    paletteNote_ = new QLabel;
    paletteNote_->setWordWrap(true);
    paletteNote_->setObjectName("paletteStatus");
    layout->addWidget(paletteNote_);
    auto *axes = new QCheckBox("좌표축 표시"); axes->setObjectName("showAxes");
    axes->setChecked(true);
    layout->addWidget(axes);
    connect(axes, &QCheckBox::toggled, canvas_, &Canvas::setAxes);
    scroll->setWidget(panel);
    for (auto *box : {resolution_, x_, y_, yaw_, free_, occupied_})
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, box] { markField(box->objectName()); });
    connect(negate_, &QComboBox::currentIndexChanged, this, [this] { markField("negate"); });
    return scroll;
}

void MainWindow::makeActions() {
    auto *file = menuBar()->addMenu("파일(&F)");
    openAction_ = file->addAction(Icons::get(Icons::Open), "지도 열기", this, &MainWindow::open);
    openAction_->setShortcut(QKeySequence::Open);
    saveAction_ = file->addAction(Icons::get(Icons::Save, QColor("#e1e7ef")), "저장", this, [this] { save(); });
    saveAction_->setObjectName("saveAction"); saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction_ = file->addAction("다른 이름으로 저장", this, [this] { save(true); });
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    file->addAction("모두 저장", this, [this] { saveAll(); });
    file->addSeparator();
    auto *exit = file->addAction("종료", this, &QWidget::close); exit->setShortcut(QKeySequence::Quit);

    auto *edit = menuBar()->addMenu("편집(&E)");
    undoAction_ = edit->addAction(Icons::get(Icons::Undo), "실행 취소", this, &MainWindow::undoEdit);
    undoAction_->setObjectName("undoAction"); undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = edit->addAction(Icons::get(Icons::Redo), "다시 실행", this, &MainWindow::redoEdit);
    redoAction_->setObjectName("redoAction"); redoAction_->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));

    auto *map = menuBar()->addMenu("지도(&M)");
    rotateAction_ = map->addAction("이미지 회전…", this, &MainWindow::rotate);
    normalizeAction_ = map->addAction("3색으로 변환", this, [this] {
        canvas_->finishEditing();
        if (doc_.empty() || !applyMetadata()) return;
        doc_.editImage(MapIO::trinary(doc_.image(), doc_.meta()), "3색 변환");
        coordinates_->setText("3색으로 변환했습니다.");
    });

    auto *view = menuBar()->addMenu("보기(&V)"); viewMenu_ = view;
    fitAction_ = view->addAction(Icons::get(Icons::Fit), "화면 맞춤", canvas_, &Canvas::fit);
    actualAction_ = view->addAction("100%", canvas_, &Canvas::actualSize);
    zoomInAction_ = view->addAction(Icons::get(Icons::Plus), "확대", this, [this] { canvas_->zoom(1.25); });
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction_ = view->addAction(Icons::get(Icons::Minus), "축소", this, [this] { canvas_->zoom(.8); });
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);

    auto *help = menuBar()->addMenu("도움말(&H)");
    help->addAction("단축키", this, [this] {
        QMessageBox::information(this, "단축키", "B  펜\nL  직선\nR  사각형\nG  채우기\nH  이동\nI  스포이드\nP  다각형\nT  회전\nC  자르기\nO  원점 이동\n\n1 / 2 / 3  흰색 / 검정 / 회색\n[ / ]  굵기 조절\nF  화면 맞춤\n0  100%\nEnter  회전·자르기·원점 적용\nEsc  그리는 중인 획·미리보기 취소\n\n휠  확대·축소\n가운데 버튼 또는 Space+드래그  이동\n\nCtrl (macOS Cmd)+O  열기\nCtrl/Cmd+S  저장\nCtrl/Cmd+Shift+S  다른 이름으로 저장\nCtrl/Cmd+Z  실행 취소\nCtrl/Cmd+Shift+Z  다시 실행");
    });
    help->addAction("정보", this, [this] {
        QMessageBox::about(this, "Map Editor", QString("<b>Map Editor %1</b><p>PGM / YAML 지도 편집기</p><p>Qt 6 · MIT License</p>").arg(MAPWRIGHT_VERSION));
    });

    auto canvasKey = [this](const QString &key, auto callback) {
        auto *action = new QAction(canvas_);
        action->setShortcut(QKeySequence(key)); action->setShortcutContext(Qt::WidgetShortcut);
        canvas_->addAction(action); connect(action, &QAction::triggered, this, callback);
    };
    const QStringList keys{"B", "L", "R", "G", "H", "I", "P"};
    for (int i = 0; i < keys.size(); ++i) canvasKey(keys[i], [this, i] { tools_->button(i)->click(); });
    for (int i = 0; i < 3; ++i) canvasKey(QString::number(i + 1), [this, i] { chooseColor(i); });
    canvasKey("F", [this] { canvas_->fit(); }); canvasKey("0", [this] { canvas_->actualSize(); });
    canvasKey("T", [this] { selectWorkspace(3); }); canvasKey("C", [this] { selectWorkspace(4); }); canvasKey("O", [this] { selectWorkspace(5); });
    canvasKey("[", [this] { if (size_->isEnabled()) size_->setValue(size_->value() - 1); });
    canvasKey("]", [this] { if (size_->isEnabled()) size_->setValue(size_->value() + 1); });
}

void MainWindow::selectTool(int tool) {
    if (workspaceMode_ >= 3) selectWorkspace(activeLayer_);
    if (tool == Canvas::Polygon && activeLayer_ == 0) return;
    canvas_->finishEditing();
    currentTool_ = tool;
    const QStringList labels{"펜", "직선", "사각형", "채우기", "이동", "스포이드", "다각형"};
    toolTitle_->setText(labels.value(tool));
    if (tool <= Canvas::Fill || tool == Canvas::Polygon) lastPaintTool_ = tool;
    canvas_->setTool(Canvas::Tool(tool));
    const int width = penSize_;
    { QSignalBlocker block(size_); size_->setValue(width); }
    canvas_->setBrushSize(width);
    size_->setEnabled(tool == Canvas::Pen || tool == Canvas::Line || tool == Canvas::Rectangle);
    filled_->setEnabled(tool == Canvas::Rectangle);
    canvas_->setFilled(activeLayer_ > 0 || filled_->isChecked());
    refresh();
    canvas_->setFocus();
}

void MainWindow::undoEdit() {
    if (geometryPending()) { resetGeometry(); return; }
    if (canvas_->undoPolygonPoint()) return;
    canvas_->finishEditing();
    if (activeLayer_ == 0 && !pending_.isEmpty()) revertMetadata();
    else if (!(activeLayer_ == 0 && hasMasks() && doc_.undoChangesGeometry())) activeDocument()->undo()->undo();
    if (workspaceMode_ >= 3) resetGeometry();
}
void MainWindow::redoEdit() {
    canvas_->finishEditing();
    if (pending_.isEmpty() && !(activeLayer_ == 0 && hasMasks() && doc_.redoChangesGeometry())) activeDocument()->undo()->redo();
    if (workspaceMode_ >= 3) resetGeometry();
}
void MainWindow::revertMetadata() {
    pending_.clear(); refreshFields(); refresh();
}
void MainWindow::markField(const QString &key) {
    if (syncing_ || doc_.empty()) return;
    const bool original = key == "negate" ? negate_->currentIndex() == negate_->property("baseline").toInt()
        : findChild<QDoubleSpinBox *>(key)->value() == findChild<QDoubleSpinBox *>(key)->property("baseline").toDouble();
    if (original) pending_.remove(key); else pending_.insert(key);
    refresh();
}
void MainWindow::refreshFields() {
    if (!pending_.isEmpty()) return;
    syncing_ = true;
    const auto &m = doc_.meta();
    resolution_->setValue(m.resolution); x_->setValue(m.x); y_->setValue(m.y); yaw_->setValue(m.yaw / Rad);
    free_->setValue(m.free); occupied_->setValue(m.occupied); negate_->setCurrentIndex(m.negate);
    for (auto *box : {resolution_, x_, y_, yaw_, free_, occupied_}) box->setProperty("baseline", box->value());
    negate_->setProperty("baseline", negate_->currentIndex());
    syncing_ = false;
}
MapIO::Metadata MainWindow::editedMetadata() const {
    auto meta = doc_.meta();
    if (pending_.contains("resolution")) meta.resolution = resolution_->value();
    if (pending_.contains("originX")) meta.x = x_->value();
    if (pending_.contains("originY")) meta.y = y_->value();
    if (pending_.contains("originYaw")) meta.yaw = yaw_->value() * Rad;
    if (pending_.contains("freeThreshold")) meta.free = free_->value();
    if (pending_.contains("occupiedThreshold")) meta.occupied = occupied_->value();
    if (pending_.contains("negate")) meta.negate = negate_->currentIndex();
    return meta;
}
void MainWindow::refreshPalette() {
    if (activeLayer_ > 0) {
        canvas_->setGray(maskColors_->checkedId() == 1 ? 0 : activeLayer_ == 1 ? 100 : speedPercent_->value());
        canvas_->setFilled(true);
        paletteNote_->setText(maskColors_->checkedId() == 1 ? "그린 구역의 제한을 해제합니다." : activeLayer_ == 1 ? "빨강: 진입 금지" : QString("그리기: 최대 속도의 %1%").arg(speedPercent_->value()));
        maskColors_->button(0)->setText(activeLayer_ == 1 ? "진입 금지" : QString("속도 %1%").arg(speedPercent_->value()));
        return;
    }
    canvas_->setFilled(filled_->isChecked());
    const auto &meta = doc_.meta();
    const int gray = grayBrushValue();
    const auto p = MapIO::probability(gray, meta);
    const auto grayRole = p > meta.occupied ? "장애물" : (p < meta.free ? "빈 공간" : "미확인");
    const QStringList shades{"#ffffff", "#000000", QColor(gray, gray, gray).name()};
    const QStringList roles{meta.negate ? "장애물" : "빈 공간", meta.negate ? "빈 공간" : "장애물", grayRole};
    const int values[]{255, 0, gray};
    for (int i = 0; i < 3; ++i) {
        auto *button = colors_->button(i);
        button->setToolTip(QString("%1 — %2 (%3)").arg(QStringList{"흰색", "검정", "회색"}[i], roles[i]).arg(values[i]));
        QPixmap swatch(48, 48); swatch.fill(Qt::transparent);
        QPainter painter(&swatch); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#929aa7"), 1.5)); painter.setBrush(QColor(shades[i]));
        painter.drawEllipse(QRectF(2, 2, 44, 44)); painter.end();
        button->setIcon(QIcon(swatch));
    }
    paletteNote_->setText(QString("회색 %1 (%2)%3").arg(gray).arg(grayRole).arg(sampledGray_ >= 0 ? " · 추출값" : ""));
    resetGray_->setVisible(sampledGray_ >= 0);
    chooseColor(colorIndex_);
}
void MainWindow::chooseColor(int index) {
    if (activeLayer_ > 0) return;
    colorIndex_ = index; colors_->button(index)->setChecked(true);
    canvas_->setGray(index == 0 ? 255 : (index == 1 ? 0 : grayBrushValue()));
}

bool MainWindow::geometryPending() const {
    return !doc_.empty() && (workspaceMode_ == 3 ? std::abs(rotationAngle_->value()) > 1e-10 :
        workspaceMode_ == 4 && !canvas_->cropSelection().isEmpty() && canvas_->cropSelection() != doc_.image().rect());
}
void MainWindow::updateCropFields(QRect area) {
    if (!area.isEmpty() && !doc_.empty()) {
        changingGeometry_ = true;
        cropX_->setRange(0, doc_.image().width() - 1); cropY_->setRange(0, doc_.image().height() - 1);
        cropX_->setValue(area.x()); cropY_->setValue(area.y());
        cropWidth_->setRange(1, doc_.image().width() - area.x()); cropHeight_->setRange(1, doc_.image().height() - area.y());
        cropWidth_->setValue(area.width()); cropHeight_->setValue(area.height());
        changingGeometry_ = false;
    }
    refresh();
}
void MainWindow::resetGeometry() {
    changingGeometry_ = true;
    rotationAngle_->setValue(0); rotationSlider_->setValue(0);
    changingGeometry_ = false;
    canvas_->clearRotationPreview();
    if (workspaceMode_ == 3 && !doc_.empty()) canvas_->setRotationPreview(0);
    canvas_->setCropSelection(workspaceMode_ == 4 && !doc_.empty() ? doc_.image().rect() : QRect());
    refresh();
}
bool MainWindow::applyGeometry() {
    if (!geometryPending()) return true;
    if (hasMasks() || !applyMetadata()) return false;
    const auto angle = rotationAngle_->value(); const auto crop = canvas_->cropSelection();
    canvas_->clearRotationPreview();
    try {
        if (workspaceMode_ == 3) doc_.rotateImage(angle);
        else doc_.cropImage(crop);
        resetGeometry(); canvas_->fit(); coordinates_->setText(workspaceMode_ == 3 ? "회전을 적용했습니다." : "지도를 자르고 원점을 보정했습니다.");
        return true;
    } catch (const std::exception &error) {
        if (workspaceMode_ == 3) canvas_->setRotationPreview(angle);
        report(error); return false;
    }
}
void MainWindow::selectWorkspace(int mode) {
    if (doc_.empty()) { workspaces_->button(workspaceMode_)->setChecked(true); return; }
    if (mode == workspaceMode_) { propertiesDock_->show(); canvas_->setFocus(); return; }
    if (mode >= 3 && hasMasks()) {
        workspaces_->button(workspaceMode_)->setChecked(true); canvas_->finishEditing();
        const auto task = QStringList{"회전", "자르기", "원점 이동"}[mode - 3];
        const bool dirty = keepout_.dirty() || speedMask_.dirty();
        QMessageBox dialog(QMessageBox::Information, task, task + " 작업을 시작하려면 마스크를 먼저 닫아야 합니다.", QMessageBox::NoButton, this);
        dialog.setObjectName("geometryLockDialog");
        dialog.setInformativeText("열린 마스크가 지도와 같은 크기와 좌표를 사용하고 있어 편집을 잠갔습니다.\n저장한 마스크는 나중에 다시 열 수 있습니다.");
        auto *proceed = dialog.addButton(dirty ? "마스크 저장 후 계속" : "마스크 닫고 계속", QMessageBox::AcceptRole);
        auto *cancel = dialog.addButton(QMessageBox::Cancel); cancel->setText("취소"); dialog.setDefaultButton(cancel); dialog.exec();
        if (dialog.clickedButton() != proceed) return;
        for (auto *mask : {&keepout_, &speedMask_}) if (mask->dirty() && !saveDocument(mask)) return;
        closeMasks(); if (hasMasks()) return;
    }
    if (!applyMetadata()) { workspaces_->button(workspaceMode_)->setChecked(true); return; }
    const int target = mode <= 2 ? mode : 0;
    if (target != activeLayer_) layer_->setCurrentIndex(target);
    if (activeLayer_ != target) { workspaces_->button(workspaceMode_)->setChecked(true); return; }
    canvas_->finishEditing(); workspaceMode_ = mode;
    resetGeometry(); workspaces_->button(mode)->setChecked(true);
    if (mode == 3) canvas_->setTool(Canvas::Hand);
    else if (mode == 4) canvas_->setTool(Canvas::Crop);
    else if (mode == 5) canvas_->setTool(Canvas::Origin);
    else tools_->button(target == 0 ? mapTool_ : maskTool_)->click();
    propertiesDock_->show(); refresh(); canvas_->setFocus();
}

void MainWindow::selectLayer(int index) {
    if (changingLayers_ || index == activeLayer_) return;
    canvas_->finishEditing();
    auto resetSelection = [this] { QSignalBlocker block(layer_); layer_->setCurrentIndex(activeLayer_); };
    if (doc_.empty()) { resetSelection(); return; }
    if (activeLayer_ == 0 && !applyMetadata()) { resetSelection(); return; }
    try {
        auto *target = index == 1 ? &keepout_ : index == 2 ? &speedMask_ : &doc_;
        const bool created = target->empty();
        if (created) target->replace(Mask::create(doc_.map(), index == 1 ? Mask::Keepout : Mask::SpeedPercent), false);
        if (activeLayer_ == 0) mapTool_ = currentTool_; else maskTool_ = currentTool_;
        if (created && index > 0) maskTool_ = Canvas::Polygon;
        if (index > 0) maskColors_->button(0)->setChecked(true);
        workspaceMode_ = index; resetGeometry(); workspaces_->button(index)->setChecked(true);
        activeLayer_ = index;
        canvas_->setDocument(target);
        refresh();
        tools_->button(index == 0 ? mapTool_ : maskTool_)->click();
        canvas_->setFocus();
    } catch (const std::exception &error) { resetSelection(); report(error); }
}

void MainWindow::openMask() {
    if (activeLayer_ == 0 || doc_.empty()) return;
    const auto path = QFileDialog::getOpenFileName(this, "마스크 YAML 열기", QSettings().value("lastFolder", QDir::homePath()).toString(), "YAML 마스크 (*.yaml *.yml)");
    if (!path.isEmpty()) openMaskPath(path);
}
void MainWindow::openMaskPath(const QString &path) {
    if (activeLayer_ == 0 || doc_.empty()) return;
    canvas_->finishEditing();
    try {
        auto map = Mask::load(path, doc_.map(), activeLayer_ == 1 ? Mask::Keepout : Mask::SpeedPercent);
        auto *target = activeDocument();
        auto *other = activeLayer_ == 1 ? &speedMask_ : &keepout_;
        Mask::checkSaveTarget(map.yamlPath, map.imagePath, {other->map()});
        if (target->dirty()) {
            const auto answer = QMessageBox::warning(this, "저장하지 않은 마스크", "현재 마스크를 저장할까요?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (answer != QMessageBox::Discard && (answer != QMessageBox::Save || !saveDocument(target))) return;
        }
        target->replace(std::move(map), false);
        refresh(); coordinates_->setText("마스크를 열었습니다.");
    } catch (const std::exception &error) { report(error); }
}
void MainWindow::openMaskPath(const QString &path, Mask::Kind kind) {
    layer_->setCurrentIndex(kind == Mask::Keepout ? 1 : 2);
    if (activeLayer_ == (kind == Mask::Keepout ? 1 : 2)) openMaskPath(path);
}
void MainWindow::clearMask() {
    if (activeLayer_ == 0 || activeDocument()->empty()) return;
    canvas_->finishEditing();
    auto blank = activeDocument()->image().copy(); blank.fill(0);
    activeDocument()->editImage(blank, "모든 구역 지우기");
}
bool MainWindow::maybeSaveMasks() {
    canvas_->finishEditing();
    if (!keepout_.dirty() && !speedMask_.dirty()) return true;
    const auto answer = QMessageBox::warning(this, "저장하지 않은 마스크", "마스크의 변경 내용을 저장할까요?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Discard) return true;
    if (answer != QMessageBox::Save) return false;
    for (auto *document : {&keepout_, &speedMask_}) if (document->dirty() && !saveDocument(document)) return false;
    return true;
}
void MainWindow::closeMasks() {
    if (!maybeSaveMasks()) return;
    changingLayers_ = true;
    keepout_.clear(); speedMask_.clear(); activeLayer_ = 0;
    maskTool_ = Canvas::Polygon;
    workspaceMode_ = 0; workspaces_->button(0)->setChecked(true);
    { QSignalBlocker block(layer_); layer_->setCurrentIndex(0); }
    canvas_->setDocument(&doc_);
    changingLayers_ = false;
    refresh(); tools_->button(mapTool_ == Canvas::Polygon ? Canvas::Pen : mapTool_)->click();
}
void MainWindow::checkSavePaths(Document *document, const QString &yamlPath) {
    const bool same = !document->map().yamlPath.isEmpty() && MapIO::sameFilePath(yamlPath, document->map().yamlPath);
    const auto imagePath = same ? document->map().imagePath : QFileInfo(yamlPath).dir().filePath(QFileInfo(yamlPath).completeBaseName() + ".pgm");
    QList<MapIO::Map> protectedMaps;
    for (auto *other : {&doc_, &keepout_, &speedMask_}) if (other != document && !other->empty()) protectedMaps << other->map();
    Mask::checkSaveTarget(yamlPath, imagePath, protectedMaps);
}
bool MainWindow::saveAll() {
    canvas_->finishEditing();
    if (!applyGeometry()) return false;
    if (!applyMetadata()) return false;
    for (auto *document : {&doc_, &keepout_, &speedMask_}) if (document->dirty() && !saveDocument(document)) return false;
    return true;
}
int MainWindow::grayBrushValue() const { return sampledGray_ >= 0 ? sampledGray_ : MapIO::unknownGray(doc_.meta()); }
void MainWindow::sampleColor(int gray) {
    if (activeLayer_ > 0) {
        if (gray == 255) { coordinates_->setText("미확인 셀입니다."); return; }
        if (activeLayer_ == 2 && gray > 0 && gray <= 100) speedPercent_->setValue(gray);
        maskColors_->button(gray == 0 ? 1 : 0)->click();
        tools_->button(lastPaintTool_)->click();
        coordinates_->setText(gray == 0 ? "제한 해제 브러시 선택" : activeLayer_ == 1 ? "진입 금지 구역" : QString("최대 속도의 %1%").arg(gray));
        return;
    }
    if (gray < 0 || gray > 255) return;
    if (gray == 255) { colorIndex_ = 0; }
    else if (gray == 0) { colorIndex_ = 1; }
    else { sampledGray_ = gray; colorIndex_ = 2; }
    refreshPalette();
    tools_->button(lastPaintTool_)->click();
    coordinates_->setText(QString("추출값 %1").arg(gray));
}

void MainWindow::refresh() {
    if (changingLayers_) return;
    auto *document = activeDocument();
    const bool empty = doc_.empty(), mask = activeLayer_ > 0, geometry = workspaceMode_ >= 3;
    const bool changed = doc_.dirty() || keepout_.dirty() || speedMask_.dirty() || !pending_.isEmpty() || canvas_->isEditing() || geometryPending();
    const auto name = document->map().yamlPath.isEmpty() ? (mask ? "새 " + Mask::kindName(activeLayer_ == 1 ? Mask::Keepout : Mask::SpeedPercent) + " 마스크" : QFileInfo(document->map().imagePath).fileName()) : QFileInfo(document->map().yamlPath).fileName();
    setWindowTitle((empty ? "Map Editor" : name + " - Map Editor") + "[*]");
    setWindowModified(changed);
    fileLabel_->setText(empty ? "열린 지도 없음" : name);
    fileLabel_->setToolTip(document->map().yamlPath);
    documentTitle_->setText(empty ? "" : documentTitle_->fontMetrics().elidedText(name, Qt::ElideMiddle, 260));
    documentTitle_->setToolTip(document->map().yamlPath);
    documentState_->setText(empty ? "" : geometryPending() ? "미리보기 · 적용 전" : changed ? "수정됨" : "저장됨");
    if (geometry) toolTitle_->setText(workspaceMode_ == 3 ? "회전" : workspaceMode_ == 4 ? "자르기" : "원점 이동");
    toolContext_->setText(geometry ? (workspaceMode_ == 3 ? "시계 방향이 +각도입니다." : workspaceMode_ == 4 ? "남길 영역을 드래그하세요." : "월드 좌표 (0, 0)의 위치를 클릭하거나 드래그하세요.") : mask ? (activeLayer_ == 1 ? "진입 금지 구역" : QString("최대 속도의 %1%").arg(speedPercent_->value())) : "");
    details_->setText(empty ? QString("지도를 열면 레이어와 좌표를\n여기에서 조정할 수 있습니다.") : QString("%1 × %2 px\n%3 × %4 m%5").arg(doc_.image().width()).arg(doc_.image().height())
        .arg(doc_.image().width() * doc_.meta().resolution, 0, 'f', 2).arg(doc_.image().height() * doc_.meta().resolution, 0, 'f', 2)
        .arg(document->map().hasYaml ? "" : "\nYAML 없음 (저장 시 생성)"));
    const bool visible = !mask || ((activeLayer_ == 1 ? keepoutVisible_->isChecked() : speedVisible_->isChecked()) && opacity_->value() > 0);
    toolPanel_->setEnabled(!empty && visible);
    toolTitle_->setVisible(geometry); toolDivider_->setVisible(geometry);
    drawingRail_->setVisible(!geometry);
    geometryLockHint_->setVisible(empty || hasMasks());
    geometryLockHint_->setText(empty ? "지도 열기 필요" : "마스크 열림\n좌표 편집 잠김");
    for (int i = 0; i < 6; ++i) {
        workspaces_->button(i)->setEnabled(!empty);
        workspaces_->button(i)->setProperty("locked", i >= 3 && hasMasks()); workspaces_->button(i)->update();
        if (i >= 3) workspaces_->button(i)->setToolTip(hasMasks() ? "마스크를 닫은 뒤 지도 크기와 좌표를 변경할 수 있습니다." : QStringList{"회전 미리보기", "지도 여백 자르기", "원점 위치 지정"}[i - 3]);
    }
    workspaces_->button(workspaceMode_)->setChecked(true);
    for (int i = 0; i <= Canvas::Polygon; ++i) tools_->button(i)->setEnabled(!empty && (visible || i == Canvas::Hand));
    propertiesPanel_->setEnabled(!empty);
    layer_->setEnabled(!empty);
    baseProperties_->setVisible(!empty && !mask && (!geometry || workspaceMode_ == 5)); maskProperties_->setVisible(!empty && mask);
    originHelp_->setVisible(workspaceMode_ == 5);
    geometryPanel_->setVisible(!empty && (workspaceMode_ == 3 || workspaceMode_ == 4));
    rotationPanel_->setVisible(workspaceMode_ == 3); cropPanel_->setVisible(workspaceMode_ == 4);
    const bool previewPending = geometryPending();
    applyGeometry_->setEnabled(previewPending && !hasMasks()); resetGeometry_->setEnabled(previewPending);
    geometryStatus_->setText(workspaceMode_ == 3 ? "미리보기입니다. 적용 전에는 원본 픽셀이 바뀌지 않습니다." : canvas_->cropSelection().isEmpty() ? "남길 영역을 선택하세요." : QString("남길 크기: %1 × %2 px\nEnter로 적용 · Esc로 선택 취소").arg(canvas_->cropSelection().width()).arg(canvas_->cropSelection().height()));
    layersPanel_->setVisible(!empty && hasMasks() && !geometry); paletteNote_->setVisible(!empty && !geometry);
    findChild<QCheckBox *>("showAxes")->setVisible(!empty && !geometry);
    const auto rules = propertiesPanel_->findChildren<QFrame *>("sectionRule");
    for (qsizetype i = 0; i < rules.size(); ++i) rules[i]->setVisible(!empty && (i == 0 || (hasMasks() && !geometry)));
    paintColorPanel_->setVisible(!geometry && currentTool_ != Canvas::Hand);
    colorPanel_->setVisible(!mask); maskColorPanel_->setVisible(mask);
    filled_->setVisible(!geometry && !mask && currentTool_ == Canvas::Rectangle);
    brushPanel_->setVisible(!geometry && (currentTool_ == Canvas::Pen || currentTool_ == Canvas::Line || currentTool_ == Canvas::Rectangle));
    tools_->button(Canvas::Polygon)->setVisible(mask); tools_->button(Canvas::Polygon)->setEnabled(!empty && mask && visible);
    for (auto *field : {resolution_, x_, y_, yaw_}) field->setEnabled(!hasMasks());
    geometryHint_->setVisible(hasMasks());
    closeMasks_->setVisible(hasMasks());
    speedPercent_->setVisible(activeLayer_ == 2); speedLabel_->setVisible(activeLayer_ == 2);
    maskHelp_->setText(activeLayer_ == 1 ? "빨강으로 표시된 구역은 진입 금지입니다." : "파랑 영역은 설정한 비율로 속도를 제한합니다.");
    maskHelp_->setStyleSheet(activeLayer_ == 1 ? (Theme::isDark() ? "color: #dda294;" : "color: #b63830;") : (Theme::isDark() ? "color: #a0c0e3;" : "color: #2366b4;"));
    keepoutVisible_->setEnabled(!keepout_.empty()); speedVisible_->setEnabled(!speedMask_.empty()); opacity_->setEnabled(hasMasks());
    { QSignalBlocker block(layer_);
      layer_->setItemText(0, "원본 지도" + QString(doc_.dirty() ? " *" : ""));
      layer_->setItemText(1, "진입 금지 마스크" + QString(keepout_.dirty() ? " *" : ""));
      layer_->setItemText(2, "속도 제한 마스크" + QString(speedMask_.dirty() ? " *" : "")); }
    saveAction_->setEnabled(!empty);
    saveAction_->setText(mask ? "마스크 저장" : "저장");
    saveAsAction_->setEnabled(!empty);
    const bool blockUndo = !mask && hasMasks() && doc_.undoChangesGeometry() && !canvas_->isEditing();
    const bool blockRedo = !mask && hasMasks() && doc_.redoChangesGeometry();
    undoAction_->setEnabled(!blockUndo && (previewPending || (!mask && !pending_.isEmpty()) || document->undo()->canUndo() || canvas_->isEditing()));
    redoAction_->setEnabled(!blockRedo && !previewPending && !canvas_->isEditing() && pending_.isEmpty() && document->undo()->canRedo());
    undoAction_->setToolTip(blockUndo ? "마스크를 닫은 뒤 원점 변경을 취소할 수 있습니다." : pending_.isEmpty() ? document->undo()->undoText() : "적용 전 속성 변경 취소");
    for (auto *action : {rotateAction_, normalizeAction_, fitAction_, actualAction_, zoomInAction_, zoomOutAction_}) action->setEnabled(!empty);
    rotateAction_->setEnabled(!empty && !mask && !hasMasks()); normalizeAction_->setEnabled(!empty && !mask);
    refreshFields();
    const auto meta = editedMetadata();
    if (workspaceMode_ == 5) canvas_->setOriginPreview(meta); else canvas_->clearOriginPreview();
    const auto error = MapIO::validate(meta);
    radLabel_->setText(QString::number(meta.yaw, 'g', 10) + " rad");
    metadataState_->setText(pending_.isEmpty() ? "" : (error.isEmpty() ? "변경됨 (적용 전)" : error));
    metadataState_->setVisible(!pending_.isEmpty());
    metadataState_->setStyleSheet(error.isEmpty() ? "" : (Theme::isDark() ? "color: #e0a091;" : "color: #a63030;"));
    apply_->setEnabled(!empty && !pending_.isEmpty() && error.isEmpty());
    revert_->setEnabled(!pending_.isEmpty());
    refreshPalette();
}

bool MainWindow::applyMetadata() {
    canvas_->finishEditing();
    for (auto *box : {resolution_, x_, y_, yaw_, free_, occupied_}) box->interpretText();
    if (pending_.isEmpty() || doc_.empty()) return true;
    try {
        const auto meta = editedMetadata();
        if (hasMasks() && (meta.resolution != doc_.meta().resolution || meta.x != doc_.meta().x || meta.y != doc_.meta().y || meta.yaw != doc_.meta().yaw))
            throw std::runtime_error("마스크를 닫은 뒤 원점과 해상도를 변경하세요.");
        doc_.editMetadata(meta);
        pending_.clear(); refresh();
        return true;
    } catch (const std::exception &error) { report(error); return false; }
}
void MainWindow::report(const std::exception &error) { QMessageBox::critical(this, "Map Editor", QString::fromUtf8(error.what())); }
void MainWindow::open() {
    const auto path = QFileDialog::getOpenFileName(this, "지도 열기", QSettings().value("lastFolder", QDir::homePath()).toString(),
        "지도 (*.yaml *.yml *.pgm *.PGM *.YAML *.YML);;모든 파일 (*)");
    if (!path.isEmpty()) openPath(path);
}
void MainWindow::openPath(const QString &path) {
    if (!maybeSave()) return;
    try {
        doc_.open(path);
        QSettings().setValue("lastFolder", QFileInfo(path).absolutePath());
        coordinates_->setText(QFileInfo(doc_.map().imagePath).fileName());
    } catch (const std::exception &error) { report(error); }
}
void MainWindow::rotate() {
    selectWorkspace(3);
}
bool MainWindow::save(bool as) {
    canvas_->finishEditing();
    if (!applyGeometry()) return false;
    if (doc_.empty() || !applyMetadata()) return false;
    return saveDocument(activeDocument(), as);
}
bool MainWindow::saveDocument(Document *document, bool as) {
    if (document->empty()) return true;
    const bool mask = document != &doc_;
    QString path;
    if (as || document->map().yamlPath.isEmpty()) {
        const auto baseName = doc_.map().yamlPath.isEmpty() ? QString("map") : QFileInfo(doc_.map().yamlPath).completeBaseName();
        const auto folder = doc_.map().yamlPath.isEmpty() ? QSettings().value("lastFolder", QDir::homePath()).toString() : QFileInfo(doc_.map().yamlPath).absolutePath();
        const auto suggested = document->map().yamlPath.isEmpty()
            ? QDir(folder).filePath(baseName + (document == &keepout_ ? "_keepout.yaml" : document == &speedMask_ ? "_speed.yaml" : ".yaml"))
            : QFileInfo(document->map().yamlPath).dir().filePath(QFileInfo(document->map().yamlPath).completeBaseName() + "_edited.yaml");
        path = QFileDialog::getSaveFileName(this, mask ? Mask::kindName(document == &keepout_ ? Mask::Keepout : Mask::SpeedPercent) + " 마스크 저장" : "지도 저장 (YAML + PGM)", suggested, "YAML 지도 (*.yaml *.yml)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".yaml";
        const auto pgm = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + ".pgm");
        if (QFile::exists(pgm) && !MapIO::sameFilePath(path, document->map().yamlPath) &&
            QMessageBox::question(this, "PGM 덮어쓰기", pgm + "\n\n기존 PGM 파일도 덮어쓸까요?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    try {
        checkSavePaths(document, path.isEmpty() ? document->map().yamlPath : path);
        const auto saved = document->save(path);
        QSettings().setValue("lastFolder", QFileInfo(saved.yamlPath).absolutePath());
        coordinates_->setText("저장: " + QFileInfo(saved.yamlPath).fileName());
        coordinates_->setToolTip(saved.backupDirectory);
        return true;
    } catch (const std::exception &error) { report(error); return false; }
}
bool MainWindow::maybeSave() {
    canvas_->finishEditing();
    for (auto *box : {resolution_, x_, y_, yaw_, free_, occupied_}) box->interpretText();
    if (!doc_.dirty() && !keepout_.dirty() && !speedMask_.dirty() && pending_.isEmpty() && !geometryPending()) return true;
    const auto answer = QMessageBox::warning(this, "저장하지 않은 변경", "변경 내용을 저장할까요?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return saveAll();
    return answer == QMessageBox::Discard;
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSave()) { QSettings().setValue("windowGeometry", saveGeometry()); event->accept(); }
    else event->ignore();
}
