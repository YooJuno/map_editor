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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), doc_(this), canvas_(new Canvas(&doc_)) {
    setObjectName("mainWindow");
    setMinimumSize(1020, 640);
    resize(1280, 800);
    setWindowIcon(style()->standardIcon(QStyle::SP_FileIcon));
    setCentralWidget(canvas_);
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
    connect(canvas_, &Canvas::fileDropped, this, &MainWindow::openPath);
    connect(canvas_, &Canvas::colorPicked, this, &MainWindow::sampleColor);
    connect(canvas_, &Canvas::editingChanged, this, &MainWindow::refresh);
    connect(canvas_, &Canvas::positionChanged, this, [this](QPointF point) {
        if (doc_.empty() || point.x() < 0 || point.y() < 0 || point.x() >= doc_.image().width() || point.y() >= doc_.image().height()) return;
        const QPoint cell(int(std::floor(point.x())), int(std::floor(point.y())));
        const auto world = MapIO::pixelToWorld(QPointF(cell) + QPointF(.5, .5), doc_.image().height(), doc_.meta());
        const int gray = doc_.image().constScanLine(cell.y())[cell.x()];
        const auto probability = MapIO::probability(gray, doc_.meta());
        const auto role = probability > doc_.meta().occupied ? "장애물" : (probability < doc_.meta().free ? "빈 공간" : "미확인");
        coordinates_->setText(QString("픽셀 %1, %2    좌표 %3, %4 m    값 %5 (%6)")
            .arg(cell.x()).arg(cell.y()).arg(world.x(), 0, 'f', 3).arg(world.y(), 0, 'f', 3).arg(gray).arg(role));
    });
    connect(&doc_, &Document::changed, this, &MainWindow::refresh);
    connect(&doc_, &Document::loaded, this, [this] {
        pending_.clear();
        sampledGray_ = -1;
        refreshFields();
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
    const QStringList labels{"펜", "지우개", "직선", "사각형", "채우기", "이동", "스포이드"};
    const QStringList keys{"B", "E", "L", "R", "G", "H", "I"};
    const QStringList names{"penTool", "eraserTool", "lineTool", "rectangleTool", "fillTool", "handTool", "pickerTool"};
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
    row->addWidget(new QLabel("색"));
    colors_ = new QButtonGroup(this);
    for (int i = 0; i < 3; ++i) {
        auto *button = new QPushButton;
        button->setObjectName("color" + QString::number(i));
        button->setCheckable(true);
        button->setFixedSize(39, 26);
        colors_->addButton(button, i);
        row->addWidget(button);
    }
    connect(colors_, &QButtonGroup::idClicked, this, [this](int index) { chooseColor(index); canvas_->setFocus(); });
    resetGray_ = new QPushButton("초기화");
    resetGray_->setObjectName("resetGray");
    resetGray_->setToolTip("회색을 YAML 임계값 기준으로 되돌리기");
    row->addWidget(resetGray_);
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
    row->addWidget(new QLabel("지우개 색"));
    eraser_ = new QComboBox;
    eraser_->setObjectName("eraserColor");
    eraser_->addItems({"회색", "흰색", "검정"});
    eraser_->setMinimumWidth(90);
    eraser_->setEnabled(false);
    row->addWidget(eraser_);
    connect(eraser_, &QComboBox::currentIndexChanged, this, [this] { refreshPalette(); });
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

    auto *resolutionForm = new QFormLayout;
    resolution_ = spin("resolution", .000000001, 1000000, 9, .01);
    resolution_->setSuffix(" m/px");
    resolutionForm->addRow("해상도", resolution_);
    layout->addLayout(resolutionForm);
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
    layout->addWidget(originGroup);

    auto *advancedToggle = new QToolButton;
    advancedToggle->setObjectName("advancedToggle");
    advancedToggle->setText("점유 설정");
    advancedToggle->setCheckable(true);
    advancedToggle->setArrowType(Qt::RightArrow);
    advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layout->addWidget(advancedToggle);
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
    layout->addWidget(advanced);
    advanced->hide();
    connect(advancedToggle, &QToolButton::toggled, this, [advancedToggle, advanced](bool open) {
        advanced->setVisible(open);
        advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });

    metadataState_ = new QLabel;
    metadataState_->setObjectName("metadataState");
    metadataState_->setWordWrap(true);
    layout->addWidget(metadataState_);
    auto *buttons = new QHBoxLayout;
    apply_ = new QPushButton("적용"); apply_->setObjectName("applyMetadata");
    revert_ = new QPushButton("되돌리기"); revert_->setObjectName("revertMetadata");
    buttons->addWidget(apply_); buttons->addWidget(revert_);
    layout->addLayout(buttons);
    connect(apply_, &QPushButton::clicked, this, [this] { applyMetadata(); });
    connect(revert_, &QPushButton::clicked, this, &MainWindow::revertMetadata);
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
    openAction_ = file->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "열기", this, &MainWindow::open);
    openAction_->setShortcut(QKeySequence::Open);
    saveAction_ = file->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "저장", this, [this] { save(); });
    saveAction_->setObjectName("saveAction"); saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction_ = file->addAction("다른 이름으로 저장", this, [this] { save(true); });
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
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
    const QStringList keys{"B", "E", "L", "R", "G", "H", "I"};
    for (int i = 0; i < keys.size(); ++i) canvasKey(keys[i], [this, i] { tools_->button(i)->click(); });
    for (int i = 0; i < 3; ++i) canvasKey(QString::number(i + 1), [this, i] { chooseColor(i); });
    canvasKey("F", [this] { canvas_->fit(); }); canvasKey("0", [this] { canvas_->actualSize(); });
    canvasKey("[", [this] { if (size_->isEnabled()) size_->setValue(size_->value() - 1); });
    canvasKey("]", [this] { if (size_->isEnabled()) size_->setValue(size_->value() + 1); });
}

void MainWindow::selectTool(int tool) {
    canvas_->finishEditing();
    currentTool_ = tool;
    if (tool <= Canvas::Fill) lastPaintTool_ = tool;
    canvas_->setTool(Canvas::Tool(tool));
    const int width = tool == Canvas::Eraser ? eraserSize_ : penSize_;
    { QSignalBlocker block(size_); size_->setValue(width); }
    canvas_->setBrushSize(width);
    size_->setEnabled(tool == Canvas::Pen || tool == Canvas::Eraser || tool == Canvas::Line || tool == Canvas::Rectangle);
    filled_->setEnabled(tool == Canvas::Rectangle);
    eraser_->setEnabled(tool == Canvas::Eraser);
    canvas_->setFocus();
}

void MainWindow::undoEdit() {
    canvas_->finishEditing();
    if (!pending_.isEmpty()) revertMetadata();
    else doc_.undo()->undo();
}
void MainWindow::redoEdit() {
    canvas_->finishEditing();
    if (pending_.isEmpty()) doc_.undo()->redo();
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
    colorIndex_ = index; colors_->button(index)->setChecked(true);
    canvas_->setGray(index == 0 ? 255 : (index == 1 ? 0 : grayBrushValue()));
}
int MainWindow::grayBrushValue() const { return sampledGray_ >= 0 ? sampledGray_ : MapIO::unknownGray(doc_.meta()); }
void MainWindow::sampleColor(int gray) {
    if (gray < 0 || gray > 255) return;
    if (gray == 255) { colorIndex_ = 0; eraser_->setCurrentIndex(1); }
    else if (gray == 0) { colorIndex_ = 1; eraser_->setCurrentIndex(2); }
    else { sampledGray_ = gray; colorIndex_ = 2; eraser_->setCurrentIndex(0); }
    refreshPalette();
    tools_->button(lastPaintTool_)->click();
    coordinates_->setText(QString("추출값 %1").arg(gray));
}

void MainWindow::refresh() {
    const bool empty = doc_.empty(), changed = doc_.dirty() || !pending_.isEmpty() || canvas_->isEditing();
    const auto name = doc_.map().yamlPath.isEmpty() ? "새 지도" : QFileInfo(doc_.map().yamlPath).fileName();
    setWindowTitle((empty ? "Map Editor" : name + " - Map Editor") + "[*]");
    setWindowModified(changed);
    fileLabel_->setText(empty ? "열린 지도 없음" : name);
    fileLabel_->setToolTip(doc_.map().yamlPath);
    details_->setText(empty ? QString() : QString("%1 × %2 px\n%3 × %4 m%5").arg(doc_.image().width()).arg(doc_.image().height())
        .arg(doc_.image().width() * doc_.meta().resolution, 0, 'f', 2).arg(doc_.image().height() * doc_.meta().resolution, 0, 'f', 2)
        .arg(doc_.map().hasYaml ? "" : "\nYAML 없음 (저장 시 생성)"));
    toolPanel_->setEnabled(!empty);
    propertiesPanel_->setEnabled(!empty);
    saveAction_->setEnabled(!empty);
    saveAsAction_->setEnabled(!empty);
    undoAction_->setEnabled(!pending_.isEmpty() || doc_.undo()->canUndo() || canvas_->isEditing());
    redoAction_->setEnabled(pending_.isEmpty() && doc_.undo()->canRedo());
    undoAction_->setToolTip(pending_.isEmpty() ? doc_.undo()->undoText() : "적용 전 속성 변경 취소");
    for (auto *action : {rotateAction_, normalizeAction_, fitAction_, actualAction_, zoomInAction_, zoomOutAction_}) action->setEnabled(!empty);
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
        doc_.editMetadata(editedMetadata());
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
    if (doc_.empty() || !applyMetadata()) return;
    bool accepted = false;
    const double degrees = QInputDialog::getDouble(this, "이미지 회전", "시계 방향 (°). 원점과 Yaw도 함께 보정합니다.", 90, -360, 360, 3, &accepted);
    if (!accepted) return;
    try { doc_.rotateImage(degrees); canvas_->fit(); }
    catch (const std::exception &error) { report(error); }
}
bool MainWindow::save(bool as) {
    if (doc_.empty() || !applyMetadata()) return false;
    QString path;
    if (as || doc_.map().yamlPath.isEmpty()) {
        const auto suggested = doc_.map().yamlPath.isEmpty()
            ? QDir(QSettings().value("lastFolder", QDir::homePath()).toString()).filePath("map.yaml")
            : QFileInfo(doc_.map().yamlPath).dir().filePath(QFileInfo(doc_.map().yamlPath).completeBaseName() + "_edited.yaml");
        path = QFileDialog::getSaveFileName(this, "지도 저장 (YAML + PGM)", suggested, "YAML 지도 (*.yaml *.yml)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".yaml";
        const auto pgm = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + ".pgm");
        if (QFile::exists(pgm) && !MapIO::sameFilePath(path, doc_.map().yamlPath) &&
            QMessageBox::question(this, "PGM 덮어쓰기", pgm + "\n\n기존 PGM 파일도 덮어쓸까요?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    try {
        const auto saved = doc_.save(path);
        QSettings().setValue("lastFolder", QFileInfo(saved.yamlPath).absolutePath());
        coordinates_->setText("저장: " + QFileInfo(saved.yamlPath).fileName());
        coordinates_->setToolTip(saved.backupDirectory);
        return true;
    } catch (const std::exception &error) { report(error); return false; }
}
bool MainWindow::maybeSave() {
    canvas_->finishEditing();
    for (auto *box : {resolution_, x_, y_, yaw_, free_, occupied_}) box->interpretText();
    if (!doc_.dirty() && pending_.isEmpty()) return true;
    const auto answer = QMessageBox::warning(this, "저장하지 않은 변경", "변경 내용을 저장할까요?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    return true;
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSave()) { QSettings().setValue("windowGeometry", saveGeometry()); event->accept(); }
    else event->ignore();
}
