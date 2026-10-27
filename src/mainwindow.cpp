#include "mainwindow.h"
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr double Rad = 3.14159265358979323846 / 180.;
QLabel *section(const QString &title, QVBoxLayout *layout) {
    auto *label = new QLabel(title); label->setObjectName("sectionTitle"); layout->addWidget(label); return label;
}
QLabel *hint(const QString &text, QVBoxLayout *layout) {
    auto *label = new QLabel(text); label->setWordWrap(true); label->setObjectName("hint"); layout->addWidget(label); return label;
}
QDoubleSpinBox *spin(const QString &name, double min, double max, int digits, double step) {
    auto *s = new QDoubleSpinBox; s->setObjectName(name); s->setRange(min, max); s->setDecimals(digits); s->setSingleStep(step);
    s->setKeyboardTracking(false); s->setMinimumWidth(140); s->setLocale(QLocale::c()); return s;
}
QFrame *separator() { auto *f = new QFrame; f->setFrameShape(QFrame::HLine); f->setObjectName("separator"); return f; }
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), doc_(this), canvas_(new Canvas(&doc_)) {
    setObjectName("mainWindow"); setMinimumSize(1020, 700); resize(1390, 880);
    setWindowIcon(QIcon(":/mapwright.png"));
    auto *root = new QWidget; auto *layout = new QVBoxLayout(root); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto *header = new QFrame; header->setObjectName("header");
    auto *row = new QHBoxLayout(header); row->setContentsMargins(22, 13, 22, 13); row->setSpacing(14);
    auto *mark = new QLabel; mark->setPixmap(QPixmap(":/mapwright.png").scaled(38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation)); row->addWidget(mark);
    auto *brand = new QVBoxLayout; brand->setSpacing(1);
    auto *name = new QLabel("Mapwright"); name->setObjectName("brand"); brand->addWidget(name);
    auto *tag = new QLabel("OCCUPANCY MAP EDITOR"); tag->setObjectName("brandTag"); brand->addWidget(tag); row->addLayout(brand);
    row->addSpacing(20);
    auto *fileInfo = new QVBoxLayout; fileInfo->setSpacing(2);
    fileLabel_ = new QLabel("열린 지도 없음"); fileLabel_->setObjectName("fileName"); fileInfo->addWidget(fileLabel_);
    details_ = new QLabel("PGM + YAML · ROS / Nav2"); details_->setObjectName("hint"); fileInfo->addWidget(details_); row->addLayout(fileInfo); row->addStretch();
    auto *openButton = new QPushButton("지도 열기"); openButton->setObjectName("openButton"); connect(openButton, &QPushButton::clicked, this, &MainWindow::open); row->addWidget(openButton);
    auto *saveAs = new QPushButton("다른 이름으로 저장"); connect(saveAs, &QPushButton::clicked, this, [this] { save(true); }); row->addWidget(saveAs);
    auto *saveButton = new QPushButton("저장"); saveButton->setObjectName("primary"); connect(saveButton, &QPushButton::clicked, this, [this] { save(); }); row->addWidget(saveButton);
    layout->addWidget(header);
    auto *splitter = new QSplitter; splitter->setChildrenCollapsible(false);
    splitter->addWidget(makeTools()); splitter->addWidget(canvas_); splitter->addWidget(makeProperties());
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setStretchFactor(2, 0);
    splitter->setSizes({225, 840, 290}); layout->addWidget(splitter, 1);
    setCentralWidget(root); makeActions();
    coordinates_ = new QLabel("마우스를 지도 위로 이동하세요"); coordinates_->setMargin(4); statusBar()->addPermanentWidget(coordinates_, 1); statusBar()->setMinimumHeight(30);
    auto *tip = new QLabel("휠 확대 · 가운데 버튼 / Space+드래그 이동"); tip->setObjectName("hint"); statusBar()->addPermanentWidget(tip);
    zoomLabel_ = new QLabel("100%"); zoomLabel_->setMinimumWidth(68); zoomLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter); statusBar()->addPermanentWidget(zoomLabel_);
    connect(canvas_, &Canvas::zoomChanged, this, [this](double s) { zoomLabel_->setText(QString::number(s * 100, 'f', s < .1 ? 1 : 0) + "%"); });
    connect(canvas_, &Canvas::fileDropped, this, &MainWindow::openPath);
    connect(canvas_, &Canvas::colorPicked, this, &MainWindow::sampleColor);
    connect(canvas_, &Canvas::positionChanged, this, [this](QPointF p) {
        if (!QRectF(doc_.image().rect()).contains(p)) { coordinates_->setText("지도 영역 밖"); return; }
        const QPoint cell(int(std::floor(p.x())), int(std::floor(p.y())));
        const auto world = MapIO::pixelToWorld(QPointF(cell) + QPointF(.5, .5), doc_.image().height(), doc_.meta());
        const auto gray = doc_.image().constScanLine(cell.y())[cell.x()];
        const auto prob = MapIO::probability(gray, doc_.meta());
        const QString role = prob > doc_.meta().occupied ? "장애물" : (prob < doc_.meta().free ? "빈 공간" : "미확인");
        coordinates_->setText(QString("px %1, %2   ·   world %3, %4 m   ·   %5 (%6)").arg(cell.x()).arg(cell.y()).arg(world.x(), 0, 'f', 3).arg(world.y(), 0, 'f', 3).arg(gray).arg(role));
    });
    connect(&doc_, &Document::changed, this, &MainWindow::refresh);
    connect(&doc_, &Document::loaded, this, [this] { pending_.clear(); sampledGray_ = -1; refreshFields(); canvas_->setFocus(); });
    const auto geometry = QSettings().value("windowGeometry").toByteArray(); if (!geometry.isEmpty()) restoreGeometry(geometry);
    refreshFields(); refresh();
}
QWidget *MainWindow::makeTools() {
    auto *scroll = new QScrollArea; scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); scroll->setMinimumWidth(215); scroll->setMaximumWidth(270);
    auto *panel = new QFrame; panel->setObjectName("toolsPanel");
    auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(18, 23, 18, 18); layout->setSpacing(10); layout->setSizeConstraint(QLayout::SetMinimumSize);
    section("편집 도구", layout);
    tools_ = new QButtonGroup(this); tools_->setExclusive(true);
    const QStringList labels{"펜 · B", "지우개 · E", "직선 · L", "사각형 · R", "채우기 · G", "이동 · H", "스포이드 · I"};
    const QStringList names{"penTool", "eraserTool", "lineTool", "rectangleTool", "fillTool", "handTool", "pickerTool"};
    auto *toolGrid = new QGridLayout; toolGrid->setSpacing(6);
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QPushButton(labels[i]); b->setCheckable(true); b->setObjectName(names[i]); b->setProperty("tool", true); b->setMinimumHeight(34);
        tools_->addButton(b, i); toolGrid->addWidget(b, i / 2, i % 2, 1, i == Canvas::Picker ? 2 : 1); if (i == 0) b->setChecked(true);
    }
    layout->addLayout(toolGrid);
    connect(tools_, &QButtonGroup::idClicked, this, [this](int id) {
        if (id >= Canvas::Pen && id <= Canvas::Fill) lastPaintTool_ = id;
        canvas_->setTool(Canvas::Tool(id)); canvas_->setFocus();
    });
    layout->addSpacing(8); layout->addWidget(separator()); section("세 가지 지도색", layout);
    colors_ = new QButtonGroup(this); auto *colorsRow = new QHBoxLayout; colorsRow->setSpacing(6);
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton; b->setCheckable(true); b->setObjectName("color" + QString::number(i)); b->setMinimumHeight(62);
        colors_->addButton(b, i); colorsRow->addWidget(b);
    }
    layout->addLayout(colorsRow); connect(colors_, &QButtonGroup::idClicked, this, [this](int i) { chooseColor(i); canvas_->setFocus(); });
    paletteNote_ = hint("", layout);
    resetGray_ = new QPushButton("회색 자동값으로 복원"); resetGray_->setObjectName("resetGray"); layout->addWidget(resetGray_);
    connect(resetGray_, &QPushButton::clicked, this, [this] { sampledGray_ = -1; refreshPalette(); canvas_->setFocus(); });
    layout->addSpacing(6); section("브러시 굵기", layout);
    auto *sizes = new QHBoxLayout; sizeSlider_ = new QSlider(Qt::Horizontal); sizeSlider_->setRange(1, 128); sizeSlider_->setValue(8); sizeSlider_->setObjectName("brushSlider");
    size_ = new QSpinBox; size_->setRange(1, 512); size_->setValue(8); size_->setSuffix(" px"); size_->setObjectName("brushSize"); size_->setMinimumWidth(80);
    sizes->addWidget(sizeSlider_, 1); sizes->addWidget(size_); layout->addLayout(sizes);
    connect(sizeSlider_, &QSlider::valueChanged, size_, &QSpinBox::setValue);
    connect(size_, &QSpinBox::valueChanged, this, [this](int v) { QSignalBlocker block(sizeSlider_); sizeSlider_->setValue(v); canvas_->setBrushSize(v); });
    auto *filled = new QCheckBox("사각형 내부 채우기"); connect(filled, &QCheckBox::toggled, canvas_, &Canvas::setFilled); layout->addWidget(filled);
    layout->addSpacing(6); section("지우개로 바꿀 색", layout);
    eraser_ = new QComboBox; eraser_->setObjectName("eraserColor"); eraser_->addItems({"회색 · 미확인", "흰색", "검은색"}); layout->addWidget(eraser_);
    connect(eraser_, &QComboBox::currentIndexChanged, this, [this] { refreshPalette(); });
    hint("지우개도 선택한 지도색으로 칠합니다. 투명 픽셀은 만들지 않습니다.", layout);
    layout->addStretch();
    auto *fit = new QPushButton("화면에 맞추기     F"); connect(fit, &QPushButton::clicked, canvas_, &Canvas::fit); layout->addWidget(fit);
    auto *actual = new QPushButton("실제 픽셀 크기     0"); connect(actual, &QPushButton::clicked, canvas_, &Canvas::actualSize); layout->addWidget(actual);
    scroll->setWidget(panel); return scroll;
}
QWidget *MainWindow::makeProperties() {
    auto *scroll = new QScrollArea; scroll->setObjectName("propertiesScroll"); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); scroll->setMinimumWidth(280); scroll->setMaximumWidth(335);
    auto *panel = new QFrame; panel->setObjectName("propertiesPanel"); auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(19, 23, 19, 18); layout->setSpacing(12);
    section("지도 속성", layout); hint("PGM 이미지와 YAML을 함께 저장합니다.", layout);
    auto *form = new QFormLayout; form->setSpacing(10); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    resolution_ = spin("resolution", .000000001, 1000000, 9, .01); resolution_->setSuffix(" m/px"); form->addRow("해상도", resolution_); layout->addLayout(form);
    layout->addWidget(separator()); section("Origin · 지도 원점", layout);
    auto *origin = new QFormLayout; origin->setSpacing(10); origin->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    x_ = spin("originX", -1e12, 1e12, 6, .1); x_->setSuffix(" m"); origin->addRow("X", x_);
    y_ = spin("originY", -1e12, 1e12, 6, .1); y_->setSuffix(" m"); origin->addRow("Y", y_);
    yaw_ = spin("originYaw", -1e9, 1e9, 6, 1); yaw_->setSuffix(" °"); origin->addRow("Yaw", yaw_); layout->addLayout(origin);
    radLabel_ = hint("", layout);
    hint("왼쪽 아래 모서리의 월드 위치입니다. Yaw는 좌표계 방향을 바꿉니다.", layout);
    apply_ = new QPushButton("속성 적용"); apply_->setObjectName("applyMetadata"); connect(apply_, &QPushButton::clicked, this, [this] { applyMetadata(); }); layout->addWidget(apply_);
    metadataState_ = hint("", layout); metadataState_->setObjectName("metadataState");
    auto *rotateButton = new QPushButton("이미지 자체 회전…"); rotateButton->setObjectName("rotateImage"); connect(rotateButton, &QPushButton::clicked, this, &MainWindow::rotate); layout->addWidget(rotateButton);
    hint("이미지를 회전하면 원점도 보정하여 월드 좌표를 유지합니다.", layout);
    layout->addWidget(separator()); section("점유 판정 · Trinary", layout);
    auto *thresholds = new QFormLayout; thresholds->setSpacing(10); thresholds->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    free_ = spin("freeThreshold", .000001, .999999, 6, .01); occupied_ = spin("occupiedThreshold", .000001, .999999, 6, .01);
    thresholds->addRow("Free", free_); thresholds->addRow("Occupied", occupied_); layout->addLayout(thresholds);
    negate_ = new QComboBox; negate_->setObjectName("negate"); negate_->addItems({"negate 0 · 검정 = 장애물", "negate 1 · 흰색 = 장애물"}); layout->addWidget(negate_);
    hint("기존 픽셀은 그대로 유지됩니다. 임계값을 바꾸면 점유 판정이 달라집니다.", layout);
    auto *normalize = new QPushButton("전체 지도를 3색으로 정리"); normalize->setObjectName("normalizeMap"); connect(normalize, &QPushButton::clicked, this, [this] {
        if (doc_.empty() || !applyMetadata()) return;
        doc_.editImage(MapIO::trinary(doc_.image(), doc_.meta()), "3색 정리");
        coordinates_->setText("세 가지 색으로 정리했습니다. 실행 취소할 수 있습니다.");
    }); layout->addWidget(normalize);
    auto *axes = new QCheckBox("원점 / 월드 축 표시"); axes->setChecked(true); connect(axes, &QCheckBox::toggled, canvas_, &Canvas::setAxes); layout->addWidget(axes);
    layout->addStretch(); hint("YAML 추가 필드는 보존합니다.\n주석과 서식은 저장 시 재작성됩니다.", layout);
    scroll->setWidget(panel);
    for (auto *s : {resolution_, x_, y_, yaw_, free_, occupied_}) connect(s, &QDoubleSpinBox::valueChanged, this, [this, s] { markField(s->objectName()); });
    connect(negate_, &QComboBox::currentIndexChanged, this, [this] { markField("negate"); });
    return scroll;
}
void MainWindow::makeActions() {
    auto *file = menuBar()->addMenu("파일(&F)");
    auto *newAction = file->addAction("새 지도…", this, &MainWindow::newMap); newAction->setShortcut(QKeySequence::New);
    auto *openAction = file->addAction("열기…", this, &MainWindow::open); openAction->setShortcut(QKeySequence::Open);
    auto *saveAction = file->addAction("저장", this, [this] { save(); }); saveAction->setShortcut(QKeySequence::Save);
    auto *saveAs = file->addAction("다른 이름으로 저장…", this, [this] { save(true); }); saveAs->setShortcut(QKeySequence::SaveAs);
    file->addSeparator(); auto *exit = file->addAction("종료", this, &QWidget::close); exit->setShortcut(QKeySequence::Quit);
    auto *edit = menuBar()->addMenu("편집(&E)");
    auto *undo = doc_.undo()->createUndoAction(this, "실행 취소"); undo->setShortcut(QKeySequence::Undo); edit->addAction(undo);
    auto *redo = doc_.undo()->createRedoAction(this, "다시 실행"); redo->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo)); edit->addAction(redo);
    edit->addSeparator(); edit->addAction("이미지 회전…", this, &MainWindow::rotate);
    auto *view = menuBar()->addMenu("보기(&V)");
    view->addAction("화면에 맞추기", canvas_, &Canvas::fit); view->addAction("100%", canvas_, &Canvas::actualSize);
    auto *in = view->addAction("확대", this, [this] { canvas_->zoom(1.25); }); in->setShortcut(QKeySequence::ZoomIn);
    auto *out = view->addAction("축소", this, [this] { canvas_->zoom(.8); }); out->setShortcut(QKeySequence::ZoomOut);
    auto *help = menuBar()->addMenu("도움말(&H)"); help->addAction("사용법", this, [this] {
        QMessageBox::information(this, "Mapwright 사용법", "1. PGM 또는 YAML을 엽니다. PGM 단독도 열 수 있습니다.\n2. 펜 / 지우개 / 직선 / 사각형 / 채우기를 선택합니다.\n3. 세 가지 색과 굵기를 고릅니다.\n4. 원점, 각도, 해상도를 수정한 뒤 적용합니다. 저장 시에도 자동 적용됩니다.\n5. 저장은 현재 쌍, 다른 이름으로 저장은 새 이름의 쌍을 만듭니다.\n\n스포이드(I)로 지도 픽셀을 클릭하면 원래 값 그대로 펜과 지우개에 적용합니다.\n회색 샘플은 자동 보정하지 않으며, 자동값 복원 또는 다른 지도 열기로 초기화합니다.\n\n캔버스 단축키: B 펜 · E 지우개 · L 직선 · R 사각형 · G 채우기 · H 이동 · I 스포이드\n1 흰색 · 2 검정 · 3 회색 · [ ] 굵기 · F 화면 맞춤 · 0 100%\n휠 확대 · 가운데 버튼 또는 Space+드래그 이동 · Esc 획 취소\nCtrl (macOS Cmd)+Z 실행 취소 · Shift+Ctrl/Cmd+Z 다시 실행\n\n저장 전 .mapwright-backups 폴더에 기존 파일을 백업합니다.\n각 파일은 안전하게 교체하지만 두 파일의 교체 전체가 원자적이지는 않습니다.\n전원 중단 시 백업 original.yaml / original.pgm과 paths.txt로 복원할 수 있습니다.");
    });
    help->addAction("Mapwright 정보", this, [this] { QMessageBox::about(this, "Mapwright", "<h2>Mapwright 1.0</h2><p>PGM + YAML 자율주행 지도 편집기</p><p>C++17 · Qt 6 Widgets · yaml-cpp</p><p>Ubuntu / Windows / macOS용 소스와 빌드 설정 포함</p><p>MIT License · Qt / yaml-cpp 고지는 THIRD_PARTY_NOTICES.md 참고</p>"); });
    // Single-letter shortcuts only apply to the canvas, never to number fields.
    auto canvasKey = [this](const QString &key, auto callback) {
        auto *a = new QAction(canvas_); a->setShortcut(QKeySequence(key)); a->setShortcutContext(Qt::WidgetShortcut); canvas_->addAction(a); connect(a, &QAction::triggered, this, callback);
    };
    const QStringList keys{"B", "E", "L", "R", "G", "H", "I"};
    for (int i = 0; i < keys.size(); ++i) canvasKey(keys[i], [this, i] { tools_->button(i)->click(); });
    for (int i = 0; i < 3; ++i) canvasKey(QString::number(i + 1), [this, i] { chooseColor(i); });
    canvasKey("F", [this] { canvas_->fit(); }); canvasKey("0", [this] { canvas_->actualSize(); });
    canvasKey("[", [this] { size_->setValue(size_->value() - 1); }); canvasKey("]", [this] { size_->setValue(size_->value() + 1); });
}
void MainWindow::markField(const QString &key) {
    if (syncing_) return;
    pending_.insert(key); metadataState_->setText("미적용 변경 · 저장할 때 자동 적용"); apply_->setEnabled(!doc_.empty());
    radLabel_->setText(QString("YAML 저장값: %1 rad").arg(yaw_->value() * Rad, 0, 'g', 9));
    setWindowModified(true);
}
void MainWindow::refreshFields() {
    if (!pending_.isEmpty()) return;
    syncing_ = true;
    const auto &m = doc_.meta();
    resolution_->setValue(m.resolution); x_->setValue(m.x); y_->setValue(m.y); yaw_->setValue(m.yaw / Rad);
    free_->setValue(m.free); occupied_->setValue(m.occupied); negate_->setCurrentIndex(m.negate);
    radLabel_->setText(QString("YAML 저장값: %1 rad").arg(m.yaw, 0, 'g', 9));
    syncing_ = false; apply_->setEnabled(false); metadataState_->setText("속성이 지도에 반영되어 있습니다.");
    refreshPalette();
}
void MainWindow::refreshPalette() {
    const auto &m = doc_.meta(); const int unknown = grayBrushValue();
    const auto p = MapIO::probability(unknown, m);
    const QString grayRole = p > m.occupied ? "장애물" : (p < m.free ? "빈 공간" : "미확인");
    const QStringList labels{QString("흰색\n%1").arg(m.negate ? "장애물" : "빈 공간"), QString("검정\n%1").arg(m.negate ? "빈 공간" : "장애물"), "회색\n" + grayRole};
    const QStringList shades{"#ffffff", "#111714", QColor(unknown, unknown, unknown).name()};
    for (int i = 0; i < 3; ++i) {
        auto *b = colors_->button(i); b->setText(labels[i]);
        b->setStyleSheet(QString("QPushButton { background:%1; color:%2; border:2px solid #a3b2aa; border-radius:8px; padding:5px 1px; font-size:11px; } QPushButton:checked { border:3px solid #13a478; }").arg(shades[i], i == 1 || (i == 2 && unknown < 120) ? "white" : "#17291f"));
    }
    paletteNote_->setText(QString("픽셀값 255 / 0 / %1\n%2").arg(unknown).arg(sampledGray_ >= 0 ? "스포이드 원본값 그대로 사용" : "회색은 현재 임계값에 맞춥니다."));
    resetGray_->setVisible(sampledGray_ >= 0);
    eraser_->setItemText(0, QString("회색 · %1").arg(unknown));
    canvas_->setEraserGray(eraser_->currentIndex() == 0 ? unknown : (eraser_->currentIndex() == 1 ? 255 : 0));
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
    coordinates_->setText(QString("스포이드: 픽셀값 %1 · 펜과 지우개에 원본값 적용").arg(gray));
}
void MainWindow::refresh() {
    const auto name = doc_.map().yamlPath.isEmpty() ? "새 지도" : QFileInfo(doc_.map().yamlPath).fileName();
    setWindowTitle((doc_.empty() ? "Mapwright" : name + " — Mapwright") + "[*]"); setWindowModified(doc_.dirty() || !pending_.isEmpty());
    fileLabel_->setText(doc_.empty() ? "열린 지도 없음" : name + (doc_.dirty() || !pending_.isEmpty() ? "  •" : ""));
    fileLabel_->setToolTip(doc_.map().yamlPath);
    if (!doc_.empty()) details_->setText(QString("%1 × %2 px   ·   %3 × %4 m   ·   %5").arg(doc_.image().width()).arg(doc_.image().height()).arg(doc_.image().width() * doc_.meta().resolution, 0, 'f', 2).arg(doc_.image().height() * doc_.meta().resolution, 0, 'f', 2).arg(doc_.map().hasYaml ? "PGM + YAML" : "PGM 단독 · YAML 생성 예정"));
    refreshFields();
}
bool MainWindow::applyMetadata() {
    for (auto *s : {resolution_, x_, y_, yaw_, free_, occupied_}) s->interpretText();
    if (pending_.isEmpty()) return true;
    if (doc_.empty()) { pending_.clear(); refreshFields(); return true; }
    auto m = doc_.meta();
    if (pending_.contains("resolution")) m.resolution = resolution_->value();
    if (pending_.contains("originX")) m.x = x_->value();
    if (pending_.contains("originY")) m.y = y_->value();
    if (pending_.contains("originYaw")) m.yaw = yaw_->value() * Rad;
    if (pending_.contains("freeThreshold")) m.free = free_->value();
    if (pending_.contains("occupiedThreshold")) m.occupied = occupied_->value();
    if (pending_.contains("negate")) m.negate = negate_->currentIndex();
    try { doc_.editMetadata(m); pending_.clear(); refreshFields(); refresh(); return true; }
    catch (const std::exception &e) { report(e); return false; }
}
void MainWindow::report(const std::exception &e) { QMessageBox::critical(this, "작업을 완료할 수 없습니다", QString::fromUtf8(e.what())); }
void MainWindow::open() {
    const auto path = QFileDialog::getOpenFileName(this, "PGM / YAML 지도 열기", QSettings().value("lastFolder").toString(), "지도 (*.yaml *.yml *.pgm *.PGM *.YAML *.YML);;모든 파일 (*)");
    if (!path.isEmpty()) openPath(path);
}
void MainWindow::openPath(const QString &path) {
    if (!maybeSave()) return;
    try { doc_.open(path); QSettings().setValue("lastFolder", QFileInfo(path).absolutePath()); coordinates_->setText("지도를 열었습니다. 원본 픽셀 값은 그대로 유지됩니다."); }
    catch (const std::exception &e) { report(e); }
}
void MainWindow::newMap() {
    QDialog dialog(this); dialog.setWindowTitle("새 지도"); QFormLayout layout(&dialog);
    QSpinBox width, height; width.setRange(1, 16000); height.setRange(1, 16000); width.setValue(800); height.setValue(600);
    auto *resolution = spin("newResolution", .000001, 1000, 6, .01); resolution->setValue(.05); resolution->setSuffix(" m/px");
    layout.addRow("가로 (px)", &width); layout.addRow("세로 (px)", &height); layout.addRow("해상도", resolution);
    QLabel note("최대 32 Mi 픽셀 · 회색(미확인)으로 시작합니다."); layout.addRow(&note);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !maybeSave()) return;
    try { doc_.newMap(width.value(), height.value(), resolution->value()); } catch (const std::exception &e) { report(e); }
}
void MainWindow::rotate() {
    if (doc_.empty() || !applyMetadata()) return;
    bool ok = false;
    const double degrees = QInputDialog::getDouble(this, "이미지 자체 회전", "시계 방향 각도 (°)\n원점과 yaw도 함께 보정합니다. 빈 모서리는 회색으로 채웁니다.\n90° 배수가 아닌 회전은 최근접 픽셀 재배치를 사용합니다.", 90, -360, 360, 3, &ok);
    if (ok) { try { doc_.rotateImage(degrees); canvas_->fit(); } catch (const std::exception &e) { report(e); } }
}
bool MainWindow::save(bool as) {
    if (doc_.empty() || !applyMetadata()) return false;
    QString path;
    if (as || doc_.map().yamlPath.isEmpty()) {
        const auto suggested = doc_.map().yamlPath.isEmpty() ? QSettings().value("lastFolder").toString() + "/map.yaml" : QFileInfo(doc_.map().yamlPath).dir().filePath(QFileInfo(doc_.map().yamlPath).completeBaseName() + "_edited.yaml");
        path = QFileDialog::getSaveFileName(this, "지도 쌍 저장 · 같은 이름의 .pgm도 생성됩니다", suggested, "YAML 지도 (*.yaml *.yml)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".yaml";
        const auto pgm = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + ".pgm");
        if (QFile::exists(pgm) && !MapIO::sameFilePath(path, doc_.map().yamlPath) && QMessageBox::question(this, "기존 PGM 덮어쓰기", "같은 이름의 PGM 파일도 백업 후 덮어씁니다.\n" + pgm, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    try {
        const auto saved = doc_.save(path); QSettings().setValue("lastFolder", QFileInfo(saved.yamlPath).absolutePath());
        coordinates_->setText("PGM + YAML 저장 완료" + (saved.backupDirectory.isEmpty() ? QString() : " · 기존 파일 백업됨"));
        coordinates_->setToolTip(saved.backupDirectory); return true;
    } catch (const std::exception &e) { report(e); return false; }
}
bool MainWindow::maybeSave() {
    if (!doc_.dirty() && pending_.isEmpty()) return true;
    const auto answer = QMessageBox::warning(this, "저장하지 않은 변경", "지도 또는 YAML 속성이 변경되었습니다. 저장할까요?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    return true;
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (maybeSave()) { QSettings().setValue("windowGeometry", saveGeometry()); e->accept(); }
    else e->ignore();
}
