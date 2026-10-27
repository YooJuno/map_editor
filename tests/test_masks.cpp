#include "mainwindow.h"
#include "mask.h"
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
        qApp->setStyle("Fusion"); qApp->setPalette(qApp->style()->standardPalette());
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
};
QTEST_MAIN(MaskTest)
#include "test_masks.moc"
