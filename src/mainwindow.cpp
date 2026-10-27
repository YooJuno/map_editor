#include "mainwindow.h"
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
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

QDoubleSpinBox *spin(const QString &name, double min, double max, int digits, double step) {
    auto *box = new NumberBox;
    box->setObjectName(name);
    box->setRange(min, max);
    box->setDecimals(digits);
    box->setSingleStep(step);
    box->setKeyboardTracking(false);
    box->setLocale(QLocale::c());
    box->setMinimumWidth(115);
    return box;
}

QFrame *separator() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), doc_(this), keepout_(this, true), speedMask_(this, true), canvas_(new Canvas(&doc_)) {
    setObjectName("mainWindow");
    setMinimumSize(1020, 640);
    resize(1280, 800);
    setWindowIcon(style()->standardIcon(QStyle::SP_FileIcon));
    setCentralWidget(canvas_);
    canvas_->setMaskLayers(&keepout_, &speedMask_);
    makeActions();

    auto *fileBar = addToolBar("파일 및 보기");
    fileBar->setObjectName("fileToolbar");
    fileBar->setMovable(false);
    fileBar->setIconSize({16, 16});
    fileBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fileBar->addAction(newAction_);
    fileBar->addAction(openAction_);
    fileBar->addAction(saveAction_);
    fileBar->addAction(saveAsAction_);
    fileBar->addSeparator();
    fileBar->addAction(undoAction_);
    fileBar->addAction(redoAction_);
    fileBar->addSeparator();
    fileBar->addAction(fitAction_);
    fileBar->addAction(actualAction_);
    fileBar->addAction(zoomOutAction_);
    fileBar->addAction(zoomInAction_);

    addToolBarBreak();
    auto *drawBar = addToolBar("그리기");
    drawBar->setObjectName("drawingToolbar");
    drawBar->setMovable(false);
    toolPanel_ = makeTools();
    drawBar->addWidget(toolPanel_);
    addToolBarBreak();
    auto *layers = addToolBar("레이어");
    layers->setObjectName("layersToolbar"); layers->setMovable(false);
    layers->addWidget(makeLayers());

    propertiesDock_ = new QDockWidget("지도 속성", this);
    propertiesDock_->setObjectName("propertiesDock");
    propertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    propertiesDock_->setWidget(makeProperties());
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);
    resizeDocks({propertiesDock_}, {270}, Qt::Horizontal);
    viewMenu_->addAction(propertiesDock_->toggleViewAction());

    coordinates_ = new QLabel("준비");
    coordinates_->setMargin(3);
    coordinates_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addPermanentWidget(coordinates_, 1);
    statusBar()->setMinimumHeight(25);
    zoomLabel_ = new QLabel;
    zoomLabel_->setMinimumWidth(55);
    zoomLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(zoomLabel_);

    connect(canvas_, &Canvas::zoomChanged, this, [this](double scale) {
        zoomLabel_->setText(QString::number(scale * 100, 'f', scale < .1 ? 1 : 0) + "%");
    });
    connect(canvas_, &Canvas::fileDropped, this, [this](const QString &path) {
        if (activeLayer_ > 0) openMaskPath(path); else openPath(path);
    });
    connect(canvas_, &Canvas::colorPicked, this, &MainWindow::sampleColor);
    connect(canvas_, &Canvas::editingChanged, this, &MainWindow::refresh);
    connect(canvas_, &Canvas::positionChanged, this, [this](QPointF point) {
        if (doc_.empty() || activeDocument()->empty() || point.x() < 0 || point.y() < 0 || point.x() >= doc_.image().width() || point.y() >= doc_.image().height()) return;
        const QPoint cell(int(std::floor(point.x())), int(std::floor(point.y())));
        const auto world = MapIO::pixelToWorld(QPointF(cell) + QPointF(.5, .5), doc_.image().height(), doc_.meta());
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
    refresh();
}

QWidget *MainWindow::makeTools() {
    auto *panel = new QWidget;
    auto *row = new QHBoxLayout(panel);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(4);
    tools_ = new QButtonGroup(this);
    const QStringList labels{"펜", "지우개", "직선", "사각형", "채우기", "이동", "스포이드", "다각형"};
    const QStringList keys{"B", "E", "L", "R", "G", "H", "I", "P"};
    const QStringList names{"penTool", "eraserTool", "lineTool", "rectangleTool", "fillTool", "handTool", "pickerTool", "polygonTool"};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(labels[i]);
        button->setObjectName(names[i]);
        button->setCheckable(true);
        button->setToolTip(labels[i] + " (" + keys[i] + ")");
        button->setMinimumHeight(26);
        tools_->addButton(button, i);
        row->addWidget(button);
        if (i == Canvas::Pen) button->setChecked(true);
    }
    connect(tools_, &QButtonGroup::idClicked, this, &MainWindow::selectTool);
    row->addWidget(separator());
    colorPanel_ = new QWidget;
    auto *colorRow = new QHBoxLayout(colorPanel_); colorRow->setContentsMargins(0, 0, 0, 0); colorRow->setSpacing(4);
    colorRow->addWidget(new QLabel("색"));
    colors_ = new QButtonGroup(this);
    for (int i = 0; i < 3; ++i) {
        auto *button = new QPushButton;
        button->setObjectName("color" + QString::number(i));
        button->setCheckable(true);
        button->setFixedSize(39, 26);
        colors_->addButton(button, i);
        colorRow->addWidget(button);
    }
    connect(colors_, &QButtonGroup::idClicked, this, [this](int index) { chooseColor(index); canvas_->setFocus(); });
    resetGray_ = new QPushButton("초기화");
    resetGray_->setObjectName("resetGray");
    resetGray_->setToolTip("회색을 YAML 임계값 기준으로 되돌리기");
    colorRow->addWidget(resetGray_);
    row->addWidget(colorPanel_);
    connect(resetGray_, &QPushButton::clicked, this, [this] { sampledGray_ = -1; refreshPalette(); canvas_->setFocus(); });
    row->addWidget(separator());
    row->addWidget(new QLabel("굵기"));
    size_ = new QSpinBox;
    size_->setObjectName("brushSize");
    size_->setRange(1, 512);
    size_->setValue(penSize_);
    size_->setSuffix(" px");
    size_->setFixedWidth(82);
    row->addWidget(size_);
    connect(size_, &QSpinBox::valueChanged, this, [this](int value) {
        if (currentTool_ == Canvas::Eraser) eraserSize_ = value;
        else penSize_ = value;
        canvas_->setBrushSize(value);
    });
    filled_ = new QCheckBox("내부 채움");
    filled_->setObjectName("filledRectangle");
    filled_->setToolTip("사각형 내부를 선택한 색으로 채우기");
    filled_->setEnabled(false);
    row->addWidget(filled_);
    connect(filled_, &QCheckBox::toggled, canvas_, &Canvas::setFilled);
    row->addWidget(separator());
    eraserPanel_ = new QWidget;
    auto *eraserRow = new QHBoxLayout(eraserPanel_); eraserRow->setContentsMargins(0, 0, 0, 0);
    eraserRow->addWidget(new QLabel("지우개 색"));
    eraser_ = new QComboBox;
    eraser_->setObjectName("eraserColor");
    eraser_->addItems({"회색", "흰색", "검정"});
    eraser_->setMinimumWidth(90);
    eraser_->setEnabled(false);
    eraserRow->addWidget(eraser_);
    row->addWidget(eraserPanel_);
    connect(eraser_, &QComboBox::currentIndexChanged, this, [this] { refreshPalette(); });
    row->addStretch();
    return panel;
}

QWidget *MainWindow::makeLayers() {
    auto *panel = new QWidget;
    auto *row = new QHBoxLayout(panel); row->setContentsMargins(4, 2, 4, 2); row->setSpacing(8);
    row->addWidget(new QLabel("편집 레이어"));
    layer_ = new QComboBox; layer_->setObjectName("activeLayer");
    layer_->addItems({"원본 지도", "진입 금지 마스크", "속도 제한 마스크"}); layer_->setMinimumWidth(165);
    row->addWidget(layer_);
    connect(layer_, &QComboBox::currentIndexChanged, this, &MainWindow::selectLayer);
    keepoutVisible_ = new QCheckBox("진입 금지 표시"); keepoutVisible_->setObjectName("keepoutVisible"); keepoutVisible_->setChecked(true);
    speedVisible_ = new QCheckBox("속도 제한 표시"); speedVisible_->setObjectName("speedVisible"); speedVisible_->setChecked(true);
    row->addWidget(keepoutVisible_); row->addWidget(speedVisible_);
    row->addWidget(new QLabel("불투명도"));
    opacity_ = new QSlider(Qt::Horizontal); opacity_->setObjectName("maskOpacity"); opacity_->setRange(0, 100); opacity_->setValue(45); opacity_->setFixedWidth(105);
    opacityLabel_ = new QLabel("45%"); opacityLabel_->setMinimumWidth(35);
    row->addWidget(opacity_); row->addWidget(opacityLabel_);
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
        canvas_->setMaskOpacity(value / 100.); opacityLabel_->setText(QString::number(value) + "%"); refresh();
    });
    auto visibility = [this] {
        canvas_->finishEditing();
        canvas_->setMaskVisible(keepoutVisible_->isChecked(), speedVisible_->isChecked()); refresh();
    };
    connect(keepoutVisible_, &QCheckBox::toggled, this, visibility);
    connect(speedVisible_, &QCheckBox::toggled, this, visibility);
    row->addStretch();
    return panel;
}

QWidget *MainWindow::makeProperties() {
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(250);
    auto *panel = new QWidget;
    propertiesPanel_ = panel;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    fileLabel_ = new QLabel;
    fileLabel_->setWordWrap(true);
    fileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(fileLabel_);
    details_ = new QLabel;
    layout->addWidget(details_);
    baseProperties_ = new QWidget;
    auto *baseLayout = new QVBoxLayout(baseProperties_); baseLayout->setContentsMargins(0, 0, 0, 0); baseLayout->setSpacing(12);
    auto *resolutionForm = new QFormLayout;
    resolution_ = spin("resolution", .000000001, 1000000, 9, .01);
    resolution_->setSuffix(" m/px");
    resolutionForm->addRow("해상도", resolution_);
    baseLayout->addLayout(resolutionForm);
    auto *originGroup = new QGroupBox("원점 (Origin)");
    originGroup->setToolTip("지도의 왼쪽 아래 모서리에 해당하는 월드 좌표");
    auto *origin = new QFormLayout(originGroup);
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
    auto *thresholds = new QFormLayout(advanced);
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
    revert_ = new QPushButton("되돌리기"); revert_->setObjectName("revertMetadata");
    buttons->addWidget(apply_); buttons->addWidget(revert_);
    baseLayout->addLayout(buttons);
    connect(apply_, &QPushButton::clicked, this, [this] { applyMetadata(); });
    connect(revert_, &QPushButton::clicked, this, &MainWindow::revertMetadata);
    layout->addWidget(baseProperties_);
    maskProperties_ = new QWidget;
    auto *maskLayout = new QVBoxLayout(maskProperties_); maskLayout->setContentsMargins(0, 0, 0, 0); maskLayout->setSpacing(10);
    maskHelp_ = new QLabel; maskHelp_->setWordWrap(true); maskLayout->addWidget(maskHelp_);
    speedLabel_ = new QLabel("새 구역 속도 (최대 속도 대비)"); speedLabel_->setWordWrap(true); maskLayout->addWidget(speedLabel_);
    speedPercent_ = new QSpinBox; speedPercent_->setObjectName("speedPercent"); speedPercent_->setRange(1, 100); speedPercent_->setValue(30); speedPercent_->setSuffix(" %"); maskLayout->addWidget(speedPercent_);
    connect(speedPercent_, &QSpinBox::valueChanged, this, [this] { if (activeLayer_ == 2) canvas_->setGray(speedPercent_->value()); refreshPalette(); });
    openMask_ = new QPushButton("마스크 열기…"); openMask_->setObjectName("openMask"); maskLayout->addWidget(openMask_);
    saveMask_ = new QPushButton("마스크 저장"); saveMask_->setObjectName("saveMask"); maskLayout->addWidget(saveMask_);
    clearMask_ = new QPushButton("모든 구역 지우기"); clearMask_->setObjectName("clearMask"); maskLayout->addWidget(clearMask_);
    connect(openMask_, &QPushButton::clicked, this, &MainWindow::openMask);
    connect(saveMask_, &QPushButton::clicked, this, [this] { save(); });
    connect(clearMask_, &QPushButton::clicked, this, &MainWindow::clearMask);
    auto *instructions = new QLabel("다각형: 점을 찍고 Enter로 완료\nEsc로 취소 · 지우개로 구역 삭제"); instructions->setWordWrap(true); maskLayout->addWidget(instructions);
    layout->addWidget(maskProperties_);
    closeMasks_ = new QPushButton("마스크 모두 닫기"); closeMasks_->setObjectName("closeMasks"); layout->addWidget(closeMasks_);
    connect(closeMasks_, &QPushButton::clicked, this, &MainWindow::closeMasks);
    layout->addStretch();
    paletteNote_ = new QLabel;
    paletteNote_->setWordWrap(true);
    paletteNote_->setObjectName("paletteStatus");
    layout->addWidget(paletteNote_);
    auto *axes = new QCheckBox("좌표축 표시");
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
    newAction_ = file->addAction(style()->standardIcon(QStyle::SP_FileIcon), "새 지도", this, &MainWindow::newMap);
    newAction_->setShortcut(QKeySequence::New);
    openAction_ = file->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "원본 열기", this, &MainWindow::open);
    openAction_->setShortcut(QKeySequence::Open);
    saveAction_ = file->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "저장", this, [this] { save(); });
    saveAction_->setObjectName("saveAction"); saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction_ = file->addAction("다른 이름으로 저장", this, [this] { save(true); });
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    file->addAction("모두 저장", this, [this] { saveAll(); });
    file->addSeparator();
    auto *exit = file->addAction("종료", this, &QWidget::close); exit->setShortcut(QKeySequence::Quit);

    auto *edit = menuBar()->addMenu("편집(&E)");
    undoAction_ = edit->addAction(style()->standardIcon(QStyle::SP_ArrowBack), "실행 취소", this, &MainWindow::undoEdit);
    undoAction_->setObjectName("undoAction"); undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = edit->addAction(style()->standardIcon(QStyle::SP_ArrowForward), "다시 실행", this, &MainWindow::redoEdit);
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
    fitAction_ = view->addAction("화면 맞춤", canvas_, &Canvas::fit);
    actualAction_ = view->addAction("100%", canvas_, &Canvas::actualSize);
    zoomInAction_ = view->addAction("확대", this, [this] { canvas_->zoom(1.25); });
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction_ = view->addAction("축소", this, [this] { canvas_->zoom(.8); });
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);

    auto *help = menuBar()->addMenu("도움말(&H)");
    help->addAction("단축키", this, [this] {
        QMessageBox::information(this, "단축키", "B  펜\nE  지우개\nL  직선\nR  사각형\nG  채우기\nH  이동\nI  스포이드\n\n1 / 2 / 3  흰색 / 검정 / 회색\n[ / ]  굵기 조절\nF  화면 맞춤\n0  100%\nEsc  그리는 중인 획 취소\n\n휠  확대·축소\n가운데 버튼 또는 Space+드래그  이동\n\nCtrl (macOS Cmd)+O  열기\nCtrl/Cmd+S  저장\nCtrl/Cmd+Shift+S  다른 이름으로 저장\nCtrl/Cmd+Z  실행 취소\nCtrl/Cmd+Shift+Z  다시 실행");
    });
    help->addAction("정보", this, [this] {
        QMessageBox::about(this, "Map Editor", QString("<b>Map Editor %1</b><p>PGM / YAML 지도 편집기</p><p>Qt 6 · MIT License</p>").arg(MAPWRIGHT_VERSION));
    });

    auto canvasKey = [this](const QString &key, auto callback) {
        auto *action = new QAction(canvas_);
        action->setShortcut(QKeySequence(key)); action->setShortcutContext(Qt::WidgetShortcut);
        canvas_->addAction(action); connect(action, &QAction::triggered, this, callback);
    };
    const QStringList keys{"B", "E", "L", "R", "G", "H", "I", "P"};
    for (int i = 0; i < keys.size(); ++i) canvasKey(keys[i], [this, i] { tools_->button(i)->click(); });
    for (int i = 0; i < 3; ++i) canvasKey(QString::number(i + 1), [this, i] { chooseColor(i); });
    canvasKey("F", [this] { canvas_->fit(); }); canvasKey("0", [this] { canvas_->actualSize(); });
    canvasKey("[", [this] { if (size_->isEnabled()) size_->setValue(size_->value() - 1); });
    canvasKey("]", [this] { if (size_->isEnabled()) size_->setValue(size_->value() + 1); });
}

void MainWindow::selectTool(int tool) {
    if (tool == Canvas::Polygon && activeLayer_ == 0) return;
    canvas_->finishEditing();
    currentTool_ = tool;
    if (tool <= Canvas::Fill || tool == Canvas::Polygon) lastPaintTool_ = tool;
    canvas_->setTool(Canvas::Tool(tool));
    const int width = tool == Canvas::Eraser ? eraserSize_ : penSize_;
    { QSignalBlocker block(size_); size_->setValue(width); }
    canvas_->setBrushSize(width);
    size_->setEnabled(tool == Canvas::Pen || tool == Canvas::Eraser || tool == Canvas::Line || tool == Canvas::Rectangle);
    filled_->setEnabled(tool == Canvas::Rectangle);
    eraser_->setEnabled(tool == Canvas::Eraser);
    canvas_->setFilled(activeLayer_ > 0 || filled_->isChecked());
    canvas_->setFocus();
}

void MainWindow::undoEdit() {
    if (canvas_->undoPolygonPoint()) return;
    canvas_->finishEditing();
    if (activeLayer_ == 0 && !pending_.isEmpty()) revertMetadata();
    else if (!(activeLayer_ == 0 && hasMasks() && doc_.undoChangesGeometry())) activeDocument()->undo()->undo();
}
void MainWindow::redoEdit() {
    canvas_->finishEditing();
    if (pending_.isEmpty() && !(activeLayer_ == 0 && hasMasks() && doc_.redoChangesGeometry())) activeDocument()->undo()->redo();
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
        canvas_->setGray(activeLayer_ == 1 ? 100 : speedPercent_->value());
        canvas_->setEraserGray(0); canvas_->setFilled(true);
        paletteNote_->setText(activeLayer_ == 1 ? "빨강: 진입 금지" : QString("그리기: 최대 속도의 %1%").arg(speedPercent_->value()));
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
        button->setText(QString::number(values[i]));
        button->setToolTip(QString("%1 — %2 (%3)").arg(QStringList{"흰색", "검정", "회색"}[i], roles[i]).arg(values[i]));
        button->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px solid #8c8c8c; padding: 2px; } QPushButton:checked { border: 3px solid #397cbb; }")
            .arg(shades[i], i == 1 || (i == 2 && gray < 120) ? "white" : "black"));
    }
    paletteNote_->setText(QString("회색 %1 (%2)%3").arg(gray).arg(grayRole).arg(sampledGray_ >= 0 ? " · 추출값" : ""));
    resetGray_->setVisible(sampledGray_ >= 0);
    eraser_->setItemText(0, QString("회색 %1").arg(gray));
    canvas_->setEraserGray(eraser_->currentIndex() == 0 ? gray : (eraser_->currentIndex() == 1 ? 255 : 0));
    chooseColor(colorIndex_);
}
void MainWindow::chooseColor(int index) {
    if (activeLayer_ > 0) return;
    colorIndex_ = index; colors_->button(index)->setChecked(true);
    canvas_->setGray(index == 0 ? 255 : (index == 1 ? 0 : grayBrushValue()));
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
    if (!applyMetadata()) return false;
    for (auto *document : {&doc_, &keepout_, &speedMask_}) if (document->dirty() && !saveDocument(document)) return false;
    return true;
}
int MainWindow::grayBrushValue() const { return sampledGray_ >= 0 ? sampledGray_ : MapIO::unknownGray(doc_.meta()); }
void MainWindow::sampleColor(int gray) {
    if (activeLayer_ > 0) {
        if (gray == 255) { coordinates_->setText("미확인 셀입니다."); return; }
        if (activeLayer_ == 2 && gray > 0 && gray <= 100) speedPercent_->setValue(gray);
        tools_->button(gray == 0 ? Canvas::Eraser : (lastPaintTool_ == Canvas::Eraser ? Canvas::Polygon : lastPaintTool_))->click();
        coordinates_->setText(gray == 0 ? "제한 없는 영역 — 지우개 선택" : activeLayer_ == 1 ? "진입 금지 구역" : QString("최대 속도의 %1%").arg(gray));
        return;
    }
    if (gray < 0 || gray > 255) return;
    if (gray == 255) { colorIndex_ = 0; eraser_->setCurrentIndex(1); }
    else if (gray == 0) { colorIndex_ = 1; eraser_->setCurrentIndex(2); }
    else { sampledGray_ = gray; colorIndex_ = 2; eraser_->setCurrentIndex(0); }
    refreshPalette();
    tools_->button(lastPaintTool_)->click();
    coordinates_->setText(QString("추출값 %1").arg(gray));
}

void MainWindow::refresh() {
    if (changingLayers_) return;
    auto *document = activeDocument();
    const bool empty = doc_.empty(), mask = activeLayer_ > 0;
    const bool changed = doc_.dirty() || keepout_.dirty() || speedMask_.dirty() || !pending_.isEmpty() || canvas_->isEditing();
    const auto name = document->map().yamlPath.isEmpty() ? (mask ? "새 " + Mask::kindName(activeLayer_ == 1 ? Mask::Keepout : Mask::SpeedPercent) + " 마스크" : "새 지도") : QFileInfo(document->map().yamlPath).fileName();
    setWindowTitle((empty ? "Map Editor" : name + " - Map Editor") + "[*]");
    setWindowModified(changed);
    fileLabel_->setText(empty ? "열린 지도 없음" : name);
    fileLabel_->setToolTip(document->map().yamlPath);
    details_->setText(empty ? QString() : QString("%1 × %2 px\n%3 × %4 m%5").arg(doc_.image().width()).arg(doc_.image().height())
        .arg(doc_.image().width() * doc_.meta().resolution, 0, 'f', 2).arg(doc_.image().height() * doc_.meta().resolution, 0, 'f', 2)
        .arg(document->map().hasYaml ? "" : "\nYAML 없음 (저장 시 생성)"));
    const bool visible = !mask || ((activeLayer_ == 1 ? keepoutVisible_->isChecked() : speedVisible_->isChecked()) && opacity_->value() > 0);
    toolPanel_->setEnabled(!empty && visible);
    propertiesPanel_->setEnabled(!empty);
    layer_->setEnabled(!empty);
    baseProperties_->setVisible(!mask); maskProperties_->setVisible(mask);
    colorPanel_->setVisible(!mask); eraserPanel_->setVisible(!mask); filled_->setVisible(!mask);
    tools_->button(Canvas::Polygon)->setVisible(mask); tools_->button(Canvas::Polygon)->setEnabled(mask);
    for (auto *field : {resolution_, x_, y_, yaw_}) field->setEnabled(!hasMasks());
    geometryHint_->setVisible(hasMasks());
    closeMasks_->setVisible(hasMasks());
    speedPercent_->setVisible(activeLayer_ == 2); speedLabel_->setVisible(activeLayer_ == 2);
    maskHelp_->setText(activeLayer_ == 1 ? "빨강으로 표시된 구역은 진입 금지입니다." : "파랑 영역은 설정한 비율로 속도를 제한합니다.");
    maskHelp_->setStyleSheet(activeLayer_ == 1 ? "color: #b63830;" : "color: #2366b4;");
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
    undoAction_->setEnabled(!blockUndo && ((!mask && !pending_.isEmpty()) || document->undo()->canUndo() || canvas_->isEditing()));
    redoAction_->setEnabled(!blockRedo && !canvas_->isEditing() && pending_.isEmpty() && document->undo()->canRedo());
    undoAction_->setToolTip(blockUndo ? "마스크를 닫은 뒤 원점 변경을 취소할 수 있습니다." : pending_.isEmpty() ? document->undo()->undoText() : "적용 전 속성 변경 취소");
    for (auto *action : {rotateAction_, normalizeAction_, fitAction_, actualAction_, zoomInAction_, zoomOutAction_}) action->setEnabled(!empty);
    rotateAction_->setEnabled(!empty && !mask && !hasMasks()); normalizeAction_->setEnabled(!empty && !mask);
    refreshFields();
    const auto meta = editedMetadata();
    const auto error = MapIO::validate(meta);
    radLabel_->setText(QString::number(meta.yaw, 'g', 10) + " rad");
    metadataState_->setText(pending_.isEmpty() ? "" : (error.isEmpty() ? "변경됨 (적용 전)" : error));
    metadataState_->setStyleSheet(error.isEmpty() ? "" : "color: #a63030;");
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
void MainWindow::newMap() {
    QDialog dialog(this); dialog.setWindowTitle("새 지도");
    QFormLayout layout(&dialog);
    QSpinBox width, height;
    width.setRange(1, 16000); height.setRange(1, 16000); width.setValue(800); height.setValue(600);
    auto *resolution = spin("newResolution", .000001, 1000, 6, .01); resolution->setValue(.05); resolution->setSuffix(" m/px");
    layout.addRow("가로 (px)", &width); layout.addRow("세로 (px)", &height); layout.addRow("해상도", resolution);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !maybeSave()) return;
    try { doc_.newMap(width.value(), height.value(), resolution->value()); }
    catch (const std::exception &error) { report(error); }
}
void MainWindow::rotate() {
    if (doc_.empty() || hasMasks() || !applyMetadata()) return;
    bool accepted = false;
    const double degrees = QInputDialog::getDouble(this, "이미지 회전", "시계 방향 (°). 원점과 Yaw도 함께 보정합니다.", 90, -360, 360, 3, &accepted);
    if (!accepted) return;
    try { doc_.rotateImage(degrees); canvas_->fit(); }
    catch (const std::exception &error) { report(error); }
}
bool MainWindow::save(bool as) {
    canvas_->finishEditing();
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
    if (!doc_.dirty() && !keepout_.dirty() && !speedMask_.dirty() && pending_.isEmpty()) return true;
    const auto answer = QMessageBox::warning(this, "저장하지 않은 변경", "변경 내용을 저장할까요?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return saveAll();
    return answer == QMessageBox::Discard;
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSave()) { QSettings().setValue("windowGeometry", saveGeometry()); event->accept(); }
    else event->ignore();
}
