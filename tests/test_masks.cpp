#include "mainwindow.h"
#include "mask.h"
#include "theme.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QMessageBox>
#include <QMouseEvent>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <cmath>

class MaskTest : public QObject {
    Q_OBJECT
    static MapIO::Map reference() {
        MapIO::Map map; map.image = QImage(32, 24, QImage::Format_Grayscale8); map.image.fill(255);
        map.meta.x = -2.5; map.meta.y = -1.25; map.meta.resolution = .05;
        map.extra = YAML::Node(YAML::NodeType::Map); return map;
    }
    static bool throws(const std::function<void()> &operation) {
        try { operation(); } catch (const std::exception &) { return true; } return false;
    }
    static void rectangle(Canvas *canvas, QPointF a, QPointF b) {
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint(a).toPoint());
        QTest::mouseMove(canvas, canvas->screenPoint(b).toPoint());
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint(b).toPoint());
    }
    static QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
    static MapIO::Map centeredReference(double yaw = 0) {
        auto map = reference(); map.meta.x = map.meta.y = 0; map.meta.yaw = yaw;
        const auto center = MapIO::pixelToWorld({16, 12}, 24, map.meta); map.meta.x = -center.x(); map.meta.y = -center.y(); return map;
    }
    static void mouse(Canvas *canvas, QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent event(type, point, QPointF(canvas->mapToGlobal(point.toPoint())), button, buttons, mods);
        QCoreApplication::sendEvent(canvas, &event);
    }
    static void dragScreen(Canvas *canvas, QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier, Qt::MouseButton button = Qt::LeftButton) {
        mouse(canvas, QEvent::MouseButtonPress, from, button, button, mods);
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, button, mods);
        mouse(canvas, QEvent::MouseButtonRelease, to, button, Qt::NoButton, mods);
    }
private slots:
    void initTestCase() {
        qApp->setStyle("Fusion"); Theme::apply(true);
        if (!qEnvironmentVariable("MAPEDITOR_PREVIEW_DIR").isEmpty()) qApp->setFont(QFont("Noto Sans CJK KR", 10));
        QFile style(":/style.qss"); if (style.open(QIODevice::ReadOnly)) qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    void rawMasksRoundTrip_data() {
        QTest::addColumn<int>("kind"); QTest::addColumn<int>("value");
        QTest::newRow("keepout") << int(Mask::Keepout) << 100;
        QTest::newRow("speed-30") << int(Mask::SpeedPercent) << 30;
        QTest::newRow("speed-1") << int(Mask::SpeedPercent) << 1;
        QTest::newRow("speed-100") << int(Mask::SpeedPercent) << 100;
    }
    void rawMasksRoundTrip() {
        QFETCH(int, kind); QFETCH(int, value); QTemporaryDir dir; const auto base = reference();
        Document mask(nullptr, true); mask.replace(Mask::create(base, Mask::Kind(kind)), false);
        QVERIFY(!mask.dirty()); auto im = mask.image().copy(); Raster::rectangle(im, {4, 3}, {12, 10}, 1, value, true);
        mask.editImage(im, "zone"); const auto saved = mask.save(dir.filePath("마스크.yaml"));
        const auto loaded = MapIO::load(saved.yamlPath, true);
        QCOMPARE(loaded.meta.mode, QString("raw")); QCOMPARE(loaded.meta.negate, 0);
        QCOMPARE(loaded.meta.x, base.meta.x); QCOMPARE(loaded.meta.y, base.meta.y); QCOMPARE(loaded.meta.resolution, base.meta.resolution);
        QCOMPARE(int(loaded.image.constScanLine(5)[6]), value); QCOMPARE(int(loaded.image.constScanLine(0)[0]), 0);
        QCOMPARE(Mask::load(saved.yamlPath, base, Mask::Kind(kind)).image, mask.image());
        QVERIFY(throws([&] { MapIO::load(saved.yamlPath); }));
    }
    void trinaryImport() {
        QTemporaryDir dir; const auto base = reference(); auto map = base;
        map.image.scanLine(3)[4] = 0; map.image.scanLine(4)[4] = 205;
        const auto paths = MapIO::savePair(map, dir.filePath("legacy.yaml"));
        const auto mask = Mask::load(paths.yamlPath, base, Mask::Keepout);
        QCOMPARE(int(mask.image.constScanLine(0)[0]), 0);
        QCOMPARE(int(mask.image.constScanLine(3)[4]), 100);
        QCOMPARE(int(mask.image.constScanLine(4)[4]), 255);
        QCOMPARE(mask.meta.mode, QString("raw"));
        Document doc(nullptr, true); doc.replace(mask, false);
        const auto original = read(paths.imagePath); doc.save(); QCOMPARE(read(paths.imagePath), original);
    }
    void rejectWrongGridAndKind() {
        QTemporaryDir dir; const auto base = reference(); auto mask = Mask::create(base, Mask::SpeedPercent);
        mask.meta.x += .05;
        auto saved = MapIO::savePair(mask, dir.filePath("offset.yaml"), {}, true);
        QVERIFY(throws([&] { Mask::load(saved.yamlPath, base, Mask::SpeedPercent); }));
        mask.meta.x = base.meta.x; mask.image.scanLine(2)[2] = 30;
        saved = MapIO::savePair(mask, dir.filePath("speed.yaml"), {}, true);
        QVERIFY(throws([&] { Mask::load(saved.yamlPath, base, Mask::Keepout); }));
        mask.extra.remove("map_editor_filter");
        saved = MapIO::savePair(mask, dir.filePath("weighted.yaml"), {}, true);
        QVERIFY(throws([&] { Mask::load(saved.yamlPath, base, Mask::Keepout); }));
        mask.image.scanLine(2)[2] = 101;
        saved = MapIO::savePair(mask, dir.filePath("invalid.yaml"), {}, true);
        QVERIFY(throws([&] { Mask::load(saved.yamlPath, base, Mask::SpeedPercent); }));
        auto rotated = base; rotated.meta.yaw = .5;
        QVERIFY(throws([&] { Mask::create(rotated, Mask::Keepout); }));
    }
    void protectOriginalAndOtherMaskFiles() {
        QTemporaryDir dir; auto base = reference();
        const auto paths = MapIO::savePair(base, dir.filePath("base.yaml")); base = MapIO::load(paths.yamlPath);
        const auto before = read(paths.imagePath);
        QVERIFY(throws([&] { Mask::checkSaveTarget(paths.yamlPath, dir.filePath("safe.pgm"), {base}); }));
        QVERIFY(throws([&] { Mask::checkSaveTarget(dir.filePath("safe.yaml"), paths.imagePath, {base}); }));
        QVERIFY(throws([&] { Mask::load(paths.yamlPath, base, Mask::Keepout); }));
        Mask::checkSaveTarget(dir.filePath("safe.yaml"), dir.filePath("safe.pgm"), {base});
        QCOMPARE(read(paths.imagePath), before);
    }
    void polygonFillingAndClipping() {
        QImage im(20, 20, QImage::Format_Grayscale8); im.fill(0);
        Raster::polygon(im, QPolygonF({{-5, 2}, {8, 2}, {8, 8}, {-5, 8}}), 100);
        QCOMPARE(int(im.constScanLine(3)[0]), 100); QCOMPARE(int(im.constScanLine(3)[7]), 100);
        QCOMPARE(int(im.constScanLine(3)[8]), 0); QCOMPARE(int(im.constScanLine(1)[0]), 0);
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x) QVERIFY(im.constScanLine(y)[x] == 0 || im.constScanLine(y)[x] == 100);
    }
    void polygonCommitCancelAndPointsUndo() {
        Document base; base.replace(reference(), false);
        Document mask(nullptr, true); mask.replace(Mask::create(base.map(), Mask::Keepout), false);
        Canvas canvas(&base); canvas.setMaskLayers(&mask, nullptr); canvas.setDocument(&mask); canvas.setTool(Canvas::Polygon); canvas.setGray(100);
        canvas.resize(640, 480); canvas.show(); QCoreApplication::processEvents(); canvas.fit();
        for (const auto point : {QPointF(2, 2), QPointF(12, 2), QPointF(12, 10)}) QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.screenPoint(point).toPoint());
        QVERIFY(canvas.isEditing()); QVERIFY(!mask.dirty()); QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(int(mask.image().constScanLine(4)[10]), 100); QVERIFY(mask.dirty());
        mask.undo()->undo(); QVERIFY(!mask.dirty());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.screenPoint({3, 3}).toPoint());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.screenPoint({9, 3}).toPoint());
        QVERIFY(canvas.undoPolygonPoint()); QVERIFY(canvas.undoPolygonPoint()); QVERIFY(!canvas.isEditing());
        QVERIFY(!mask.dirty());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.screenPoint({3, 3}).toPoint());
        QTest::keyClick(&canvas, Qt::Key_Escape); QVERIFY(!canvas.isEditing()); QVERIFY(!mask.dirty());
        QCOMPARE(base.image(), reference().image);
    }
    void overlayIsOnlyVisual() {
        Document base; base.replace(reference(), false);
        Document keepout(nullptr, true); keepout.replace(Mask::create(base.map(), Mask::Keepout), false);
        auto im = keepout.image().copy(); Raster::rectangle(im, {3, 3}, {12, 12}, 1, 100, true); keepout.editImage(im, "zone");
        Canvas canvas(&base); canvas.setMaskLayers(&keepout, nullptr); canvas.setDocument(&keepout); canvas.setTool(Canvas::Hand); canvas.setAxes(false);
        canvas.resize(640, 480); canvas.show(); QCoreApplication::processEvents(); canvas.fit();
        const auto point = canvas.screenPoint({6.5, 6.5}).toPoint();
        auto sample = [&] { const auto frame = canvas.grab(); return frame.toImage().pixelColor((QPointF(point) * frame.devicePixelRatio()).toPoint()); };
        canvas.setMaskOpacity(.5); auto colored = sample();
        QVERIFY(colored.red() > colored.green());
        canvas.setMaskOpacity(0); QCOMPARE(sample(), QColor(255, 255, 255));
        canvas.setMaskOpacity(.5); canvas.setMaskVisible(false, false);
        QCOMPARE(sample(), QColor(255, 255, 255));
        QCOMPARE(base.image(), reference().image); QCOMPARE(keepout.image(), im);
    }
    void guiBothMasksAndIndependentUndo() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *base = window.activeDocument(); const auto original = base->image().copy(); auto *canvas = window.canvas(); canvas->fit();
        auto *layer = window.findChild<QComboBox *>("activeLayer"); layer->setCurrentIndex(1);
        QVERIFY(window.findChild<QPushButton *>("polygonTool")->isChecked());
        QVERIFY(window.findChild<QDoubleSpinBox *>("originX")->isEnabled());
        auto *keepout = window.activeDocument();
        QVERIFY(keepout->empty());
        window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(canvas, {4.5, 3.5}, {12.5, 10.5});
        QCOMPARE(int(keepout->image().constScanLine(5)[6]), 100); QCOMPARE(base->image(), original);
        QVERIFY(!window.findChild<QDoubleSpinBox *>("originX")->isEnabled());
        layer->setCurrentIndex(2); auto *speed = window.activeDocument(); window.findChild<QSpinBox *>("speedPercent")->setValue(40);
        window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(canvas, {16.5, 4.5}, {26.5, 14.5});
        QCOMPARE(int(speed->image().constScanLine(8)[20]), 40); QCOMPARE(base->image(), original);
        keepout->save(dir.filePath("keepout.yaml")); speed->save(dir.filePath("speed.yaml"));
        if (const auto folder = qEnvironmentVariable("MAPEDITOR_PREVIEW_DIR"); !folder.isEmpty()) window.grab().save(QDir(folder).filePath("mask-editor-preview.png"));
        layer->setCurrentIndex(1); window.findChild<QAction *>("undoAction")->trigger();
        QCOMPARE(int(keepout->image().constScanLine(5)[6]), 0); QCOMPARE(int(speed->image().constScanLine(8)[20]), 40);
        window.findChild<QAction *>("redoAction")->trigger(); QVERIFY(!keepout->dirty());
        window.findChild<QCheckBox *>("keepoutVisible")->setChecked(false);
        const auto before = keepout->image().copy(); rectangle(canvas, {1.5, 1.5}, {3.5, 3.5}); QCOMPARE(keepout->image(), before);
        window.findChild<QPushButton *>("closeMasks")->click(); QCOMPARE(window.activeDocument(), base);
        QVERIFY(window.findChild<QDoubleSpinBox *>("originX")->isEnabled());
        QCOMPARE(read(paths.imagePath), MapIO::encodePgm(original)); window.close();
    }
    void geometryUndoCannotMisalignMasks() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *x = window.findChild<QDoubleSpinBox *>("originX"); x->setValue(-1.5);
        window.findChild<QPushButton *>("applyMetadata")->click(); window.activeDocument()->save();
        auto *layer = window.findChild<QComboBox *>("activeLayer"); layer->setCurrentIndex(1);
        auto *mask = window.activeDocument(); window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(window.canvas(), {3.5, 3.5}, {8.5, 8.5});
        layer->setCurrentIndex(0);
        QVERIFY(!window.findChild<QAction *>("undoAction")->isEnabled());
        mask->save(dir.filePath("mask.yaml")); QVERIFY(window.findChild<QAction *>("undoAction")->isEnabled());
        window.findChild<QAction *>("undoAction")->trigger(); QVERIFY(mask->empty());
        window.activeDocument()->save(); window.close();
    }
    void closeWarnsForInactiveDirtyMask() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *layer = window.findChild<QComboBox *>("activeLayer"); layer->setCurrentIndex(1); auto *mask = window.activeDocument();
        window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(window.canvas(), {3.5, 3.5}, {9.5, 9.5});
        layer->setCurrentIndex(0);
        QTimer::singleShot(0, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Cancel)->click(); });
        QVERIFY(!window.close()); QVERIFY(window.isVisible()); QVERIFY(mask->dirty());
        mask->save(dir.filePath("mask.yaml")); QVERIFY(window.close());
    }
    void cropPreservesWorldCoordinatesAndUndo() {
        for (const double yaw : {0., .73, -1.4}) {
            auto map = reference(); map.meta.yaw = yaw;
            for (int y = 0; y < map.image.height(); ++y) for (int x = 0; x < map.image.width(); ++x) map.image.scanLine(y)[x] = uchar((x + 7 * y) % 256);
            Document doc; doc.replace(map, false); const QRect area(4, 3, 19, 14);
            doc.cropImage(area); QCOMPARE(doc.image(), map.image.copy(area)); QCOMPARE(doc.meta().yaw, map.meta.yaw);
            for (auto point : {QPointF(.5, .5), QPointF(8.5, 9.5), QPointF(18.5, 13.5)})
                QVERIFY(QLineF(MapIO::pixelToWorld(point + QPointF(area.topLeft()), map.image.height(), map.meta), MapIO::pixelToWorld(point, doc.image().height(), doc.meta())).length() < 1e-10);
            QVERIFY(doc.undoChangesGeometry()); const auto afterMeta = doc.meta();
            doc.undo()->undo(); QCOMPARE(doc.image(), map.image); QVERIFY(doc.meta() == map.meta); QVERIFY(!doc.dirty());
            doc.undo()->redo(); QCOMPARE(doc.image(), map.image.copy(area)); QVERIFY(doc.meta() == afterMeta);
        }
    }
    void automaticCropOnlyRemovesUnknownMargins() {
        auto map = reference(); map.image.fill(205); Document doc; doc.replace(map, false);
        QVERIFY(doc.knownBounds().isEmpty());
        map.image.scanLine(5)[7] = 0; map.image.scanLine(16)[24] = 255; doc.replace(map, false);
        QCOMPARE(doc.knownBounds(), QRect(7, 5, 18, 12));
        map.meta.negate = 1; map.image.fill(MapIO::unknownGray(map.meta)); map.image.scanLine(5)[7] = 0; map.image.scanLine(16)[24] = 255;
        doc.replace(map, false); QCOMPARE(doc.knownBounds(), QRect(7, 5, 18, 12));
        doc.cropImage(map.image.rect()); QVERIFY(!doc.dirty());
        QVERIFY(throws([&] { doc.cropImage(QRect(100, 100, 5, 5)); }));
    }
    void guiRotationIsPreviewUntilApplied() {
        QTemporaryDir dir; auto paths = MapIO::savePair(reference(), dir.filePath("rotate.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *doc = window.activeDocument(); const auto original = doc->image().copy(); const auto meta = doc->meta();
        window.findChild<QPushButton *>("rotateWorkspace")->click();
        auto *slider = window.findChild<QSlider *>("rotationSlider"); auto *angle = window.findChild<QDoubleSpinBox *>("rotationAngle");
        slider->setValue(173); QCOMPARE(angle->value(), 17.3); QVERIFY(window.canvas()->rotationPreview());
        window.findChild<QPushButton *>("rotateWorkspace")->click(); QCOMPARE(angle->value(), 17.3);
        QCOMPARE(doc->image(), original); QVERIFY(!doc->dirty());
        angle->setValue(-12.345); QCOMPARE(slider->value(), -123);
        window.findChild<QPushButton *>("resetGeometry")->click(); QCOMPARE(angle->value(), 0.); QVERIFY(!doc->dirty());
        angle->setValue(90); window.findChild<QPushButton *>("applyGeometry")->click();
        QCOMPARE(doc->image().size(), QSize(24, 32)); QVERIFY(doc->dirty()); QCOMPARE(angle->value(), 0.);
        window.findChild<QAction *>("undoAction")->trigger(); QCOMPARE(doc->image(), original); QVERIFY(doc->meta() == meta);
        angle->setValue(90); window.findChild<QAction *>("saveAction")->trigger();
        QCOMPARE(MapIO::load(paths.yamlPath).image.size(), QSize(24, 32)); QVERIFY(!doc->dirty());
        window.close();
    }
    void guiCropAndLeftMaskNavigation() {
        QTemporaryDir dir; auto paths = MapIO::savePair(reference(), dir.filePath("crop.yaml"));
        MainWindow window; window.resize(1060, 700); window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *doc = window.activeDocument(); window.findChild<QPushButton *>("cropWorkspace")->click();
        auto *canvas = window.canvas(); canvas->fit(); rectangle(canvas, {3.5, 2.5}, {24.5, 18.5});
        QCOMPARE(canvas->cropSelection(), QRect(3, 2, 22, 17)); QVERIFY(!doc->dirty());
        QCOMPARE(window.findChild<QSpinBox *>("cropWidth")->value(), 22);
        QTest::keyClick(canvas, Qt::Key_Return); QCOMPARE(doc->image().size(), QSize(22, 17));
        doc->save(); window.findChild<QPushButton *>("keepoutWorkspace")->click();
        auto *mask = window.activeDocument(); QVERIFY(mask != doc); QVERIFY(mask->empty());
        QVERIFY(!window.findChild<QPushButton *>("cropWorkspace")->property("locked").toBool());
        QVERIFY(!window.findChild<QPushButton *>("rotateWorkspace")->property("locked").toBool());
        window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(canvas, {2.5, 2.5}, {10.5, 8.5});
        QCOMPARE(int(mask->image().constScanLine(4)[5]), 100);
        window.findChild<QPushButton *>("clearZone")->click(); rectangle(canvas, {2.5, 2.5}, {10.5, 8.5});
        QCOMPARE(int(mask->image().constScanLine(4)[5]), 0);
        mask->save(dir.filePath("mask.yaml")); window.close();
    }
    void guiOriginPickingUpdatesOnlyMetadata() {
        QTemporaryDir dir; auto paths = MapIO::savePair(reference(), dir.filePath("origin.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *doc = window.activeDocument(); const auto original = doc->image().copy(); const auto originalMeta = doc->meta();
        window.findChild<QPushButton *>("originWorkspace")->click(); auto *canvas = window.canvas(); canvas->fit();
        const auto point = canvas->screenPoint({10.5, 8.5}).toPoint(); const auto pixel = canvas->mapPoint(point);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, point); QVERIFY(!doc->dirty());
        window.findChild<QPushButton *>("applyMetadata")->click();
        QVERIFY(QLineF(MapIO::pixelToWorld(pixel, doc->image().height(), doc->meta()), QPointF(0, 0)).length() < 1e-6);
        QCOMPARE(doc->image(), original); QVERIFY(doc->dirty());
        window.findChild<QAction *>("undoAction")->trigger(); QVERIFY(doc->meta() == originalMeta); QCOMPARE(doc->image(), original);
        window.close();
    }
    void cropEdgesResizeOneAxis_data() {
        QTest::addColumn<QPointF>("from"); QTest::addColumn<QPointF>("to");
        QTest::addColumn<QRect>("expected"); QTest::addColumn<int>("cursor");
        const int horizontal = Qt::SizeHorCursor, vertical = Qt::SizeVerCursor;
        QTest::newRow("left-in") << QPointF(5.2, 9) << QPointF(8, 14.5) << QRect(8, 4, 15, 14) << horizontal;
        QTest::newRow("left-out") << QPointF(5, 13) << QPointF(2, 7) << QRect(2, 4, 21, 14) << horizontal;
        QTest::newRow("right-in") << QPointF(23.2, 10) << QPointF(20, 6) << QRect(5, 4, 15, 14) << horizontal;
        QTest::newRow("right-out") << QPointF(23, 14) << QPointF(28, 8) << QRect(5, 4, 23, 14) << horizontal;
        QTest::newRow("top-in") << QPointF(10, 3.8) << QPointF(19, 7) << QRect(5, 7, 18, 11) << vertical;
        QTest::newRow("top-out") << QPointF(18, 4) << QPointF(10, 1) << QRect(5, 1, 18, 17) << vertical;
        QTest::newRow("bottom-in") << QPointF(12, 18.2) << QPointF(18, 15) << QRect(5, 4, 18, 11) << vertical;
        QTest::newRow("bottom-out") << QPointF(19, 18) << QPointF(10, 22) << QRect(5, 4, 18, 18) << vertical;
        QTest::newRow("left-image-bound") << QPointF(5, 10) << QPointF(-1, 10) << QRect(0, 4, 23, 14) << horizontal;
        QTest::newRow("right-image-bound") << QPointF(23, 10) << QPointF(33.5, 10) << QRect(5, 4, 27, 14) << horizontal;
        QTest::newRow("top-image-bound") << QPointF(12, 4) << QPointF(12, -1) << QRect(5, 0, 18, 18) << vertical;
        QTest::newRow("bottom-image-bound") << QPointF(12, 18) << QPointF(12, 25) << QRect(5, 4, 18, 20) << vertical;
        QTest::newRow("left-minimum-width") << QPointF(5, 10) << QPointF(30, 10) << QRect(22, 4, 1, 14) << horizontal;
        QTest::newRow("right-minimum-width") << QPointF(23, 10) << QPointF(1, 10) << QRect(5, 4, 1, 14) << horizontal;
        QTest::newRow("top-minimum-height") << QPointF(12, 4) << QPointF(12, 22) << QRect(5, 17, 18, 1) << vertical;
        QTest::newRow("bottom-minimum-height") << QPointF(12, 18) << QPointF(12, 1) << QRect(5, 4, 18, 1) << vertical;
    }
    void cropEdgesResizeOneAxis() {
        QFETCH(QPointF, from); QFETCH(QPointF, to); QFETCH(QRect, expected); QFETCH(int, cursor);
        Document doc; doc.replace(reference(), false); const auto original = doc.image().copy();
        Canvas canvas(&doc); canvas.resize(800, 600); canvas.show(); QCoreApplication::processEvents(); canvas.fit();
        canvas.setTool(Canvas::Crop); canvas.setCropSelection(QRect(5, 4, 18, 14));
        QTest::mouseMove(&canvas, canvas.screenPoint(from).toPoint());
        QCOMPARE(int(canvas.cursor().shape()), cursor);
        rectangle(&canvas, from, to); QCOMPARE(canvas.cropSelection(), expected);
        QCOMPARE(doc.image(), original); QVERIFY(!doc.dirty());
    }
    void cropHandlesResizeAndMoveWithoutEditingPixels() {
        Document doc; doc.replace(reference(), false); Canvas canvas(&doc); canvas.resize(800, 600); canvas.show(); QCoreApplication::processEvents(); canvas.fit();
        canvas.setTool(Canvas::Crop); canvas.setCropSelection(QRect(4, 3, 16, 12));
        rectangle(&canvas, {4, 3}, {2, 1});
        QCOMPARE(canvas.cropSelection(), QRect(2, 1, 18, 14)); QVERIFY(!doc.dirty());
        rectangle(&canvas, {8, 7}, {11, 9}); QCOMPARE(canvas.cropSelection(), QRect(5, 3, 18, 14));
        canvas.setCropSelection(doc.image().rect());
        rectangle(&canvas, {32, 24}, {25, 18}); QCOMPARE(canvas.cropSelection(), QRect(0, 0, 25, 18));
        QVERIFY(!doc.dirty());
    }
    void geometryLockExplainsAndSavesMasksBeforeContinuing() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *base = window.activeDocument(); const auto original = base->image().copy();
        window.findChild<QPushButton *>("keepoutWorkspace")->click(); auto *mask = window.activeDocument();
        window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(window.canvas(), {3.5, 3.5}, {8.5, 8.5});
        const auto maskPath = dir.filePath("keepout.yaml"); mask->save(maskPath);
        rectangle(window.canvas(), {14.5, 4.5}, {20.5, 10.5}); QVERIFY(mask->dirty());
        bool explained = false;
        QTimer::singleShot(0, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                explained = box->informativeText().contains("좌표"); box->button(QMessageBox::Cancel)->click();
            }
        });
        window.findChild<QPushButton *>("rotateWorkspace")->click();
        QVERIFY(explained); QVERIFY(mask->dirty()); QVERIFY(!mask->empty()); QCOMPARE(window.activeDocument(), mask);
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                for (auto *button : box->buttons()) if (button->text().contains("계속")) { button->click(); break; }
        });
        window.findChild<QPushButton *>("cropWorkspace")->click();
        QVERIFY(!mask->empty()); QVERIFY(!mask->dirty()); QCOMPARE(window.activeDocument(), base); QCOMPARE(base->image(), original);
        QVERIFY(window.findChild<QSpinBox *>("cropWidth")->isVisible());
        const auto saved = Mask::load(maskPath, base->map(), Mask::Keepout);
        QCOMPARE(int(saved.image.constScanLine(6)[17]), 100);
        window.canvas()->setCropSelection(QRect(2, 2, 24, 18)); window.findChild<QPushButton *>("applyGeometry")->click();
        QVERIFY(mask->empty()); base->save();
        window.close();
    }
    void maskTabsAreLazyAndSavingFollowsChanges() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *base = window.activeDocument(); auto *save = window.findChild<QAction *>("saveAction");
        QVERIFY(!save->isEnabled()); QVERIFY(window.findChild<QAction *>("saveAllAction")->isEnabled() == false);
        for (const auto name : {"keepoutWorkspace", "speedWorkspace", "mapWorkspace", "speedWorkspace"}) {
            window.findChild<QPushButton *>(name)->click();
            if (window.activeDocument() != base) QVERIFY(window.activeDocument()->empty());
            QVERIFY(!save->isEnabled()); QVERIFY(!window.findChild<QPushButton *>("closeMasks")->isVisible());
            QVERIFY(!window.findChild<QPushButton *>("rotateWorkspace")->property("locked").toBool());
        }
        auto *mask = window.activeDocument(); window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(window.canvas(), {3.5, 3.5}, {10.5, 10.5});
        QVERIFY(mask->dirty()); QVERIFY(save->isEnabled()); QVERIFY(window.findChild<QPushButton *>("saveMask")->isEnabled());
        QVERIFY(window.findChild<QPushButton *>("rotateWorkspace")->property("locked").toBool());
        mask->save(dir.filePath("speed.yaml")); QVERIFY(!save->isEnabled());
        QVERIFY(!window.findChild<QPushButton *>("rotateWorkspace")->property("locked").toBool());
        window.findChild<QPushButton *>("rotateWorkspace")->click(); QVERIFY(!mask->empty());
        window.findChild<QDoubleSpinBox *>("rotationAngle")->setValue(90); window.findChild<QPushButton *>("applyGeometry")->click();
        QVERIFY(mask->empty()); QCOMPARE(base->image().size(), QSize(24, 32));
        QVERIFY(QFile::exists(dir.filePath("speed.yaml"))); base->save(); window.close();
    }
    void newMaskModeIsDrawableAfterClosingHiddenMasks() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        const auto maskPaths = MapIO::savePair(Mask::create(reference(), Mask::Keepout), dir.filePath("mask.yaml"), {}, true);
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        window.openMaskPath(maskPaths.yamlPath, Mask::Keepout);
        window.findChild<QCheckBox *>("keepoutVisible")->setChecked(false);
        window.findChild<QSlider *>("maskOpacity")->setValue(0);
        window.findChild<QPushButton *>("closeMasks")->click();
        window.findChild<QPushButton *>("keepoutWorkspace")->click();
        QVERIFY(window.activeDocument()->empty());
        QVERIFY(window.findChild<QCheckBox *>("keepoutVisible")->isChecked());
        QVERIFY(window.findChild<QSlider *>("maskOpacity")->value() > 0);
        auto *tool = window.findChild<QPushButton *>("rectangleTool"); QVERIFY(tool->isEnabled()); tool->click();
        rectangle(window.canvas(), {3.5, 3.5}, {10.5, 10.5}); QVERIFY(window.activeDocument()->dirty());
        window.activeDocument()->save(dir.filePath("new-mask.yaml")); window.close();
    }
    void guiOriginHandleDragsWithoutJumpOrPixelChanges() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(centeredReference(.37), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        window.findChild<QPushButton *>("originWorkspace")->click(); QCoreApplication::processEvents(); auto *canvas = window.canvas(); canvas->fit();
        auto *doc = window.activeDocument(); const auto original = doc->image().copy(); const auto meta = doc->meta();
        const auto originPixel = MapIO::worldToPixel({0, 0}, 24, meta); const auto origin = canvas->screenPoint(originPixel);
        dragScreen(canvas, origin, origin); QVERIFY(!window.findChild<QAction *>("saveAction")->isEnabled());
        const auto from = origin + QPointF(4, 3), to = from + QPointF(41, 29);
        const auto expected = originPixel + canvas->mapPoint(to) - canvas->mapPoint(from);
        dragScreen(canvas, from, to); QVERIFY(!doc->dirty()); QVERIFY(window.findChild<QAction *>("saveAction")->isEnabled());
        window.findChild<QPushButton *>("applyMetadata")->click();
        QVERIFY(QLineF(MapIO::worldToPixel({0, 0}, 24, doc->meta()), expected).length() < 4e-5); QCOMPARE(doc->image(), original);
        window.findChild<QAction *>("undoAction")->trigger(); QVERIFY(doc->meta() == meta);
        // Origins outside the image remain draggable, rather than clamping to a map edge.
        dragScreen(canvas, origin, {5, 5}); window.findChild<QPushButton *>("applyMetadata")->click();
        QVERIFY(QLineF(MapIO::worldToPixel({0, 0}, 24, doc->meta()), canvas->mapPoint({5, 5})).length() < 4e-5);
        window.findChild<QAction *>("undoAction")->trigger(); QVERIFY(doc->meta() == meta); QVERIFY(!doc->dirty());
        mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        QTest::keyClick(canvas, Qt::Key_Escape);
        mouse(canvas, QEvent::MouseMove, origin + QPointF(-52, 0), Qt::NoButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseButtonRelease, origin + QPointF(-52, 0), Qt::LeftButton, Qt::NoButton);
        QVERIFY(!window.findChild<QAction *>("saveAction")->isEnabled()); QVERIFY(doc->meta() == meta); window.close();
    }
    void guiOriginAxisRotationKeepsPivot_data() {
        QTest::addColumn<bool>("yAxis"); QTest::addColumn<double>("initialYaw"); QTest::addColumn<double>("turn"); QTest::addColumn<bool>("snap");
        QTest::newRow("x-clockwise") << false << 0. << 90. << false;
        QTest::newRow("y-counterclockwise") << true << .37 << -45. << false;
        QTest::newRow("shift-snap") << false << .37 << 32. << true;
    }
    void guiOriginAxisRotationKeepsPivot() {
        QFETCH(bool, yAxis); QFETCH(double, initialYaw); QFETCH(double, turn); QFETCH(bool, snap);
        QTemporaryDir dir; const auto paths = MapIO::savePair(centeredReference(initialYaw), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        window.findChild<QPushButton *>("originWorkspace")->click(); QCoreApplication::processEvents(); auto *canvas = window.canvas(); canvas->fit();
        auto *doc = window.activeDocument(); const auto image = doc->image().copy(); const auto meta = doc->meta();
        const auto pivot = MapIO::worldToPixel({0, 0}, 24, meta), origin = canvas->screenPoint(pivot);
        const auto direction = yAxis ? QPointF(std::sin(initialYaw), -std::cos(initialYaw)) : QPointF(std::cos(initialYaw), std::sin(initialYaw));
        const auto from = origin + direction * 52, to = origin + QTransform().rotate(turn).map(direction * 52);
        dragScreen(canvas, from, from); QVERIFY(!window.findChild<QAction *>("saveAction")->isEnabled());
        dragScreen(canvas, from, to, snap ? Qt::ShiftModifier : Qt::NoModifier);
        double expected = initialYaw * 180. / 3.14159265358979323846 + turn;
        if (snap) expected = std::round(expected / 15.) * 15.;
        QVERIFY(std::abs(window.findChild<QDoubleSpinBox *>("originYaw")->value() - expected) < 1e-5);
        QVERIFY(!doc->dirty()); QCOMPARE(doc->image(), image);
        QTest::keyClick(canvas, Qt::Key_Return);
        QVERIFY(QLineF(MapIO::worldToPixel({0, 0}, 24, doc->meta()), pivot).length() < 4e-5); QCOMPARE(doc->image(), image);
        window.findChild<QAction *>("undoAction")->trigger(); QVERIFY(doc->meta() == meta); QVERIFY(!doc->dirty());
        mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        QTest::keyClick(canvas, Qt::Key_Escape);
        mouse(canvas, QEvent::MouseMove, origin + QPointF(-52, 0), Qt::NoButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseButtonRelease, origin + QPointF(-52, 0), Qt::LeftButton, Qt::NoButton);
        QVERIFY(!window.findChild<QAction *>("saveAction")->isEnabled()); QVERIFY(doc->meta() == meta); window.close();
    }
    void guiImageRotationDragHasStableViewAndUndo() {
        QTemporaryDir dir; auto map = centeredReference(); map.image.scanLine(3)[7] = 0;
        const auto paths = MapIO::savePair(map, dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        window.findChild<QPushButton *>("rotateWorkspace")->click(); QCoreApplication::processEvents(); auto *canvas = window.canvas(); canvas->fit();
        auto *doc = window.activeDocument(); const auto meta = doc->meta(), originalMeta = meta; const auto image = doc->image().copy();
        auto *angle = window.findChild<QDoubleSpinBox *>("rotationAngle"); auto *slider = window.findChild<QSlider *>("rotationSlider");
        const auto center = canvas->screenPoint({16, 12}); const auto scale = canvas->scale();
        dragScreen(canvas, center + QPointF(0, -100), center + QPointF(100, 0));
        QCOMPARE(angle->value(), 90.); QCOMPARE(slider->value(), 900); QCOMPARE(canvas->scale(), scale);
        QVERIFY(QLineF(canvas->screenPoint({12, 16}), center).length() < 1e-8); QCOMPARE(doc->image(), image); QVERIFY(doc->meta() == meta); QVERIFY(!doc->dirty());
        QTest::keyClick(canvas, Qt::Key_Return); QCOMPARE(doc->image().size(), QSize(24, 32)); QVERIFY(doc->dirty());
        window.findChild<QAction *>("undoAction")->trigger(); QCOMPARE(doc->image(), image); QVERIFY(doc->meta() == originalMeta); QVERIFY(!doc->dirty());
        // Wrapped angles and Shift snapping use the same fields as the numeric controls.
        angle->setValue(170); const QPointF pivot(canvas->width() / 2., canvas->height() / 2.);
        dragScreen(canvas, pivot + QPointF(100, 0), pivot + QTransform().rotate(20).map(QPointF(100, 0)));
        QCOMPARE(angle->value(), -170.);
        window.findChild<QPushButton *>("resetGeometry")->click();
        const QPointF snapCenter(canvas->width() / 2., canvas->height() / 2.);
        dragScreen(canvas, snapCenter + QPointF(100, 0), snapCenter + QTransform().rotate(32).map(QPointF(100, 0)), Qt::ShiftModifier);
        QCOMPARE(angle->value(), 30.); QTest::keyClick(canvas, Qt::Key_Escape); QCOMPARE(angle->value(), 0.);
        const auto before = canvas->screenPoint({16, 12}); dragScreen(canvas, before, before + QPointF(21, 13), Qt::NoModifier, Qt::MiddleButton);
        QCOMPARE(angle->value(), 0.); QVERIFY(QLineF(canvas->screenPoint({16, 12}), before + QPointF(21, 13)).length() < 1e-8);
        window.close();
    }
    void guiCancelStopsRotationGesture() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(centeredReference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        window.findChild<QPushButton *>("rotateWorkspace")->click(); QCoreApplication::processEvents(); auto *canvas = window.canvas();
        const QPointF center(canvas->width() / 2., canvas->height() / 2.);
        mouse(canvas, QEvent::MouseButtonPress, center + QPointF(100, 0), Qt::LeftButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseMove, center + QPointF(0, 100), Qt::NoButton, Qt::LeftButton);
        QCOMPARE(window.findChild<QDoubleSpinBox *>("rotationAngle")->value(), 90.);
        QTest::keyClick(canvas, Qt::Key_Escape);
        mouse(canvas, QEvent::MouseMove, center + QPointF(-100, 0), Qt::NoButton, Qt::LeftButton);
        mouse(canvas, QEvent::MouseButtonRelease, center + QPointF(-100, 0), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(window.findChild<QDoubleSpinBox *>("rotationAngle")->value(), 0.); QVERIFY(!window.activeDocument()->dirty()); window.close();
    }
    void guiFourPointPolygonCanFinishWithButtonOrRightClick_data() {
        QTest::addColumn<bool>("button"); QTest::newRow("finish-button") << true; QTest::newRow("right-click") << false;
    }
    void guiFourPointPolygonCanFinishWithButtonOrRightClick() {
        QFETCH(bool, button); QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *base = window.activeDocument(); const auto original = base->image().copy(); window.findChild<QPushButton *>("keepoutWorkspace")->click();
        auto *mask = window.activeDocument(); auto *canvas = window.canvas(); auto *finish = window.findChild<QPushButton *>("finishPolygon"); int count = 0;
        for (const auto point : {QPointF(4, 4), QPointF(20, 4), QPointF(20, 16), QPointF(4, 16)}) {
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint(point).toPoint()); ++count;
            QCOMPARE(canvas->polygonPointCount(), count); QVERIFY(finish->isVisible()); QCOMPARE(finish->isEnabled(), count >= 3);
            QVERIFY(window.findChild<QLabel *>("polygonCount")->text().contains(QString::number(count)));
        }
        QVERIFY(!mask->dirty()); QTest::mouseMove(canvas, canvas->screenPoint({28, 20}).toPoint());
        if (button) finish->click(); else QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, canvas->screenPoint({29, 20}).toPoint());
        QVERIFY(!canvas->isEditing()); QVERIFY(!finish->isVisible()); QCOMPARE(canvas->polygonPointCount(), 0);
        QVERIFY(mask->dirty()); QCOMPARE(mask->undo()->count(), 1); QCOMPARE(int(mask->image().constScanLine(8)[10]), 100);
        QCOMPARE(int(mask->image().constScanLine(18)[26]), 0); QCOMPARE(base->image(), original);
        window.findChild<QAction *>("undoAction")->trigger(); QVERIFY(!mask->dirty());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({3, 3}).toPoint());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({12, 3}).toPoint());
        QTest::mouseClick(canvas, Qt::RightButton); QCOMPARE(canvas->polygonPointCount(), 2); QVERIFY(canvas->isEditing());
        window.findChild<QToolButton *>("cancelPolygon")->click(); QVERIFY(!canvas->isEditing()); QVERIFY(!mask->dirty());
        window.close();
    }
};
QTEST_MAIN(MaskTest)
#include "test_masks.moc"
