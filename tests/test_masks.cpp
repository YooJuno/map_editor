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
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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
        QVERIFY(!window.findChild<QDoubleSpinBox *>("originX")->isEnabled());
        auto *keepout = window.activeDocument();
        window.findChild<QPushButton *>("rectangleTool")->click(); rectangle(canvas, {4.5, 3.5}, {12.5, 10.5});
        QCOMPARE(int(keepout->image().constScanLine(5)[6]), 100); QCOMPARE(base->image(), original);
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
        auto *layer = window.findChild<QComboBox *>("activeLayer"); layer->setCurrentIndex(1); layer->setCurrentIndex(0);
        QVERIFY(!window.findChild<QAction *>("undoAction")->isEnabled());
        window.findChild<QPushButton *>("closeMasks")->click(); QVERIFY(window.findChild<QAction *>("undoAction")->isEnabled());
        window.close();
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
        auto *mask = window.activeDocument(); QVERIFY(mask != doc); QCOMPARE(mask->image().size(), doc->image().size());
        QVERIFY(!window.findChild<QPushButton *>("cropWorkspace")->isEnabled());
        QVERIFY(!window.findChild<QPushButton *>("rotateWorkspace")->isEnabled());
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
};
QTEST_MAIN(MaskTest)
#include "test_masks.moc"
