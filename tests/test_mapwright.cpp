#include "canvas.h"
#include "mainwindow.h"
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

class MapwrightTest : public QObject {
    Q_OBJECT
private:
    static void write(const QString &path, const QByteArray &data) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    static QByteArray read(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
    static QImage image(int w = 16, int h = 12, int gray = 205) { QImage im(w, h, QImage::Format_Grayscale8); im.fill(gray); return im; }
    static MapIO::Map map() { MapIO::Map m; m.image = image(); m.extra = YAML::Load("extra_field: {name: laboratory, enabled: true}\n"); return m; }
    static bool throws(const std::function<void()> &f) { try { f(); } catch (const std::exception &) { return true; } return false; }
private slots:
    void pgmBinaryByteIntegrity() {
        QTemporaryDir dir;
        QByteArray pixels; for (int i = 0; i < 256; ++i) pixels.append(char(i));
        auto path = dir.filePath("bytes.pgm"); write(path, "P5\n# comment\n16 16\n255\n" + pixels);
        const auto im = MapIO::readPgm(path); QCOMPARE(im.size(), QSize(16, 16));
        for (int i = 0; i < 256; ++i) QCOMPARE(int(im.constScanLine(i / 16)[i % 16]), i);
        write(path, MapIO::encodePgm(im)); QCOMPARE(MapIO::readPgm(path), im);
    }
    void pgmRasterStartsWithWhitespace_data() {
        QTest::addColumn<int>("value");
        for (int v : {9, 10, 13, 32, 35, 255}) QTest::newRow(qPrintable(QString::number(v))) << v;
    }
    void pgmRasterStartsWithWhitespace() {
        QFETCH(int, value); QTemporaryDir dir; auto path = dir.filePath("a.pgm");
        QByteArray bytes("P5\n2 1\n255\n"); bytes.append(char(value)); bytes.append(char(0)); write(path, bytes);
        QCOMPARE(int(MapIO::readPgm(path).constScanLine(0)[0]), value);
    }
    void pgmPlainAndCrlf() {
        QTemporaryDir dir; auto path = dir.filePath("a.pgm");
        write(path, "P2\n# plain\n3 2\n255\n0 205 # comment\n255\n17 32 10\n");
        auto im = MapIO::readPgm(path); QCOMPARE(int(im.constScanLine(1)[2]), 10);
        write(path, QByteArray("P5\r\n2 1\r\n255\r\n") + char(35) + char(10));
        QCOMPARE(int(MapIO::readPgm(path).constScanLine(0)[0]), 35);
        // A CR-only header followed by a first pixel of LF must retain that pixel.
        write(path, QByteArray("P5\r2 1\r255\r") + char(10) + char(0));
        QCOMPARE(int(MapIO::readPgm(path).constScanLine(0)[0]), 10);
    }
    void malformedPgm_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("negative") << QByteArray("P5\n-1 2\n255\n");
        QTest::newRow("zero") << QByteArray("P5\n0 2\n255\n");
        QTest::newRow("huge") << QByteArray("P5\n99999999999 2\n255\n");
        QTest::newRow("depth16") << QByteArray("P5\n1 1\n65535\n\0\0", 17);
        QTest::newRow("truncated") << QByteArray("P5\n2 1\n255\nA");
        QTest::newRow("extra-raster") << QByteArray("P5\n1 1\n255\nAB");
        QTest::newRow("out-of-range") << QByteArray("P2\n1 1\n255\n256\n");
        QTest::newRow("plain-extra") << QByteArray("P2\n1 1\n255\n10 20\n");
        QTest::newRow("rgb") << QByteArray("P6\n1 1\n255\nAAA");
        QTest::newRow("oversized") << QByteArray("P5\n65535 65535\n255\n");
    }
    void malformedPgm() { QFETCH(QByteArray, bytes); QTemporaryDir dir; auto p = dir.filePath("bad.pgm"); write(p, bytes); QVERIFY(throws([&] { MapIO::readPgm(p); })); }
    void yamlRoundTripAndBackup() {
        QTemporaryDir dir; auto original = map(); original.meta = {.031, -7.123456789123, 4.876543210987, 1.23456789012345, 0, .65, .196, "trinary"};
        auto saved = MapIO::savePair(original, dir.filePath("지도 한글.yaml"));
        auto loaded = MapIO::load(saved.yamlPath); QCOMPARE(loaded.image, original.image); QVERIFY(loaded.meta == original.meta);
        QCOMPARE(QString::fromStdString(loaded.extra["extra_field"]["name"].as<std::string>()), QString("laboratory"));
        QCOMPARE(loaded.extra["image"].as<std::string>(), std::string("지도 한글.pgm"));
        auto oldPgm = read(saved.imagePath), oldYaml = read(saved.yamlPath);
        loaded.image.scanLine(0)[0] = 0; loaded.meta.x = 100;
        saved = MapIO::savePair(loaded, saved.yamlPath, saved.imagePath);
        QVERIFY(!saved.backupDirectory.isEmpty());
        QCOMPARE(read(saved.backupDirectory + "/original.pgm"), oldPgm); QCOMPARE(read(saved.backupDirectory + "/original.yaml"), oldYaml);
        QCOMPARE(MapIO::load(saved.yamlPath).meta.x, 100.);
    }
    void pairedPathsAndPgmOnly() {
        QTemporaryDir dir; auto original = map(); auto p = dir.filePath("solo.pgm"); write(p, MapIO::encodePgm(original.image));
        auto solo = MapIO::load(p); QVERIFY(!solo.hasYaml); QCOMPARE(solo.yamlPath, dir.filePath("solo.yaml"));
        QDir().mkpath(dir.filePath("images"));
        auto saved = MapIO::savePair(original, dir.filePath("map.yaml"), dir.filePath("images/actual.pgm"));
        QCOMPARE(MapIO::load(saved.yamlPath).imagePath, saved.imagePath);
        write(dir.filePath("other.pgm"), MapIO::encodePgm(original.image));
        write(dir.filePath("other.yaml"), MapIO::encodeYaml(original, "images/actual.pgm"));
        QVERIFY(throws([&] { MapIO::load(dir.filePath("other.pgm")); }));
    }
    void unsupportedYaml() {
        QTemporaryDir dir; auto original = map(); auto saved = MapIO::savePair(original, dir.filePath("map.yaml"));
        auto root = YAML::Load(read(saved.yamlPath).toStdString()); root["mode"] = "raw"; YAML::Emitter e; e << root; write(saved.yamlPath, e.c_str());
        QVERIFY(throws([&] { MapIO::load(saved.yamlPath); }));
        root["mode"] = "trinary"; root["negate"] = true; YAML::Emitter e2; e2 << root; write(saved.yamlPath, e2.c_str());
        QCOMPARE(MapIO::load(saved.yamlPath).meta.negate, 1);
        root["resolution"] = -.1; YAML::Emitter e3; e3 << root; write(saved.yamlPath, e3.c_str());
        QVERIFY(throws([&] { MapIO::load(saved.yamlPath); }));
    }
    void trinarySemanticsAndNegate() {
        auto m = MapIO::Metadata(); auto im = image(256, 1); for (int x = 0; x < 256; ++x) im.scanLine(0)[x] = uchar(x);
        for (int negate : {0, 1}) {
            m.negate = negate; const auto result = MapIO::trinary(im, m);
            for (int x = 0; x < 256; ++x) {
                auto before = MapIO::probability(x, m), after = MapIO::probability(result.constScanLine(0)[x], m);
                QCOMPARE(before > m.occupied, after > m.occupied); QCOMPARE(before < m.free, after < m.free);
            }
        }
        m.free = .3; m.occupied = .4; const auto p = MapIO::probability(MapIO::unknownGray(m), m); QVERIFY(p >= m.free && p <= m.occupied);
        m.free = .5001; m.occupied = .5002; QVERIFY(!MapIO::validate(m).isEmpty());
    }
    void coordinateTransforms() {
        MapIO::Metadata m; m.x = -5; m.y = -2;
        QCOMPARE(MapIO::pixelToWorld({0, 100}, 100, m), QPointF(-5, -2));
        QCOMPARE(MapIO::pixelToWorld({10, 80}, 100, m), QPointF(-4.5, -1));
        m.yaw = std::acos(-1.) / 2;
        const auto p = MapIO::pixelToWorld({10, 100}, 100, m); QVERIFY(QLineF(p, {-5, -1.5}).length() < 1e-10);
        const QPointF source(12.5, 17.5); QVERIFY(QLineF(MapIO::worldToPixel(MapIO::pixelToWorld(source, 100, m), 100, m), source).length() < 1e-10);
    }
    void rasterTools() {
        auto im = image(); Raster::stroke(im, {1, 1}, {14, 10}, 1, 0);
        QCOMPARE(int(im.constScanLine(1)[1]), 0); QCOMPARE(int(im.constScanLine(10)[14]), 0);
        auto single = image(9, 9); Raster::stroke(single, {4, 4}, {4, 4}, 1, 255);
        int count = 0; for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) count += single.constScanLine(y)[x] == 255;
        QCOMPARE(count, 1);
        auto box = image(20, 20, 255); Raster::rectangle(box, {3, 3}, {16, 16}, 1, 0, false); Raster::flood(box, {10, 10}, 205);
        QCOMPARE(int(box.constScanLine(10)[10]), 205); QCOMPARE(int(box.constScanLine(0)[0]), 255); QCOMPARE(int(box.constScanLine(3)[3]), 0);
        Raster::flood(box, {10, 10}, 205); // Same color must terminate.
        Raster::rectangle(box, {5, 5}, {7, 7}, 1, 255, true); QCOMPARE(int(box.constScanLine(6)[6]), 255);
        Raster::stroke(box, {0, 0}, {0, 0}, 512, 0); QCOMPARE(int(box.constScanLine(0)[0]), 0);
    }
    void undoDirtyAndExternalChanges() {
        QTemporaryDir dir; auto saved = MapIO::savePair(map(), dir.filePath("a.yaml"));
        Document doc; doc.open(saved.yamlPath); QVERIFY(!doc.dirty());
        auto edited = doc.image().copy(); edited.scanLine(5)[4] = 0; doc.editImage(edited, "test"); QVERIFY(doc.dirty());
        doc.undo()->undo(); QVERIFY(!doc.dirty()); QCOMPARE(int(doc.image().constScanLine(5)[4]), 205);
        doc.undo()->redo(); doc.save(); QVERIFY(!doc.dirty());
        auto meta = doc.meta(); meta.yaw = .7; doc.editMetadata(meta); QVERIFY(doc.dirty()); doc.undo()->undo(); QVERIFY(!doc.dirty());
        write(saved.imagePath, MapIO::encodePgm(image(16, 12, 255)));
        QVERIFY(throws([&] { doc.save(); }));
        auto copy = doc.save(dir.filePath("copy.yaml")); QVERIFY(QFile::exists(copy.imagePath));
        const auto old = doc.image(); QVERIFY(throws([&] { doc.open(dir.filePath("missing.pgm")); })); QCOMPARE(doc.image(), old);
    }
    void largeMapAndThickBrush() {
        auto im = image(2048, 2048, 255);
        Raster::flood(im, {1024, 1024}, 205);
        QCOMPARE(int(im.constScanLine(2047)[2047]), 205);
        Raster::stroke(im, {300, 1024}, {1700, 1024}, 512, 0);
        QCOMPARE(int(im.constScanLine(1024)[1700]), 0);
        QCOMPARE(int(im.constScanLine(1024)[46]), 0);
        QCOMPARE(int(im.constScanLine(0)[0]), 205);
    }
    void rotationKeepsWorldPositions_data() {
        QTest::addColumn<double>("degrees"); for (auto d : {90., -90., 180., 27.5}) QTest::newRow(qPrintable(QString::number(d))) << d;
    }
    void rotationKeepsWorldPositions() {
        QFETCH(double, degrees); Document doc; doc.newMap(16, 12, .05); auto im = doc.image().copy();
        Raster::rectangle(im, {4, 3}, {10, 8}, 1, 0, true); doc.editImage(im, "paint");
        auto m = doc.meta(); m.x = -2.5; m.y = 1.8; m.yaw = .4; doc.editMetadata(m);
        const auto oldImage = doc.image().copy(); doc.rotateImage(degrees);
        const auto transform = QImage::trueMatrix(QTransform().rotate(degrees), 16, 12).inverted();
        for (int y = 0; y < doc.image().height(); ++y) for (int x = 0; x < doc.image().width(); ++x) {
            const QPointF p(x + .5, y + .5); const auto old = transform.map(p);
            const auto w1 = MapIO::pixelToWorld(old, 12, m), w2 = MapIO::pixelToWorld(p, doc.image().height(), doc.meta());
            QVERIFY2(QLineF(w1, w2).length() < 1e-8, "Rotated pixel world position changed");
        }
        doc.undo()->undo(); QCOMPARE(doc.image(), oldImage); QVERIFY(doc.meta() == m);
        doc.undo()->redo(); if (degrees == 90 || degrees == -90) QCOMPARE(doc.image().size(), QSize(12, 16));
    }
    void saveFailureKeepsOriginal() {
        QTemporaryDir dir; auto m = map(); auto saved = MapIO::savePair(m, dir.filePath("a.yaml"));
        const auto before = read(saved.yamlPath); QDir().mkdir(dir.filePath("folder.pgm"));
        QVERIFY(throws([&] { MapIO::savePair(m, saved.yamlPath, dir.filePath("folder.pgm")); })); QCOMPARE(read(saved.yamlPath), before);
        QVERIFY(throws([&] { MapIO::savePair(m, dir.filePath("missing/a.yaml")); }));
    }
    void guiPaintingAndMetadata() {
        QTemporaryDir dir; auto saved = MapIO::savePair(map(), dir.filePath("gui.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(saved.yamlPath); auto *canvas = window.canvas(); canvas->fit();
        auto *pen = window.findChild<QPushButton *>("penTool"); QVERIFY(pen); pen->click();
        const auto spot = canvas->screenPoint({8, 6}).toPoint();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, spot);
        QVERIFY(window.isWindowModified());
        auto *x = window.findChild<QDoubleSpinBox *>("originX"); QVERIFY(x); x->setValue(2.5);
        auto *yaw = window.findChild<QDoubleSpinBox *>("originYaw"); yaw->setValue(90);
        window.findChild<QPushButton *>("applyMetadata")->click();
        // Standard Save through the menu shortcut; verifies both output files.
        canvas->setFocus(); QTest::keySequence(canvas, QKeySequence::Save); QCoreApplication::processEvents();
        auto loaded = MapIO::load(saved.yamlPath); QCOMPARE(loaded.meta.x, 2.5); QVERIFY(std::abs(loaded.meta.yaw - std::acos(-1.) / 2) < 1e-10);
        QCOMPARE(int(loaded.image.constScanLine(6)[8]), 0); QVERIFY(!window.isWindowModified());
        window.close();
    }
    void guiEyedropperPreservesPixel_data() {
        QTest::addColumn<int>("gray");
        for (int gray : {0, 127, 204, 206, 254, 255}) QTest::newRow(qPrintable(QString::number(gray))) << gray;
    }
    void guiEyedropperPreservesPixel() {
        QFETCH(int, gray);
        QTemporaryDir dir; auto original = map(); original.image.scanLine(2)[2] = uchar(gray);
        auto saved = MapIO::savePair(original, dir.filePath("picker.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(saved.yamlPath);
        auto *canvas = window.canvas(); canvas->fit();
        window.findChild<QSpinBox *>("brushSize")->setValue(1);
        auto *picker = window.findChild<QPushButton *>("pickerTool"); QVERIFY(picker);
        const auto source = canvas->screenPoint({2.5, 2.5}).toPoint();
        // The keyboard shortcut must sample actual PGM bytes without editing the map.
        canvas->setFocus(); QTest::keyClick(canvas, Qt::Key_I); QVERIFY(picker->isChecked());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, source);
        QVERIFY(!window.isWindowModified()); QVERIFY(window.findChild<QPushButton *>("penTool")->isChecked());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({7.5, 4.5}).toPoint());
        auto *eraser = window.findChild<QPushButton *>("eraserTool"); eraser->click();
        picker->click(); QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, source);
        QVERIFY(eraser->isChecked());
        // Applying metadata must not auto-correct an explicitly sampled value.
        window.findChild<QDoubleSpinBox *>("originX")->setValue(1.5);
        window.findChild<QPushButton *>("applyMetadata")->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({12.5, 8.5}).toPoint());
        canvas->setFocus(); QTest::keySequence(canvas, QKeySequence::Save); QCoreApplication::processEvents();
        auto result = MapIO::load(saved.yamlPath);
        QCOMPARE(int(result.image.constScanLine(2)[2]), gray);
        QCOMPARE(int(result.image.constScanLine(4)[7]), gray);
        QCOMPARE(int(result.image.constScanLine(8)[12]), gray);
        QVERIFY(!window.isWindowModified()); window.close();
    }
    void guiEyedropperResetAndOutside() {
        QTemporaryDir dir; auto original = map(); original.image.scanLine(2)[2] = 206;
        auto saved = MapIO::savePair(original, dir.filePath("reset.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(saved.yamlPath);
        auto *canvas = window.canvas(); canvas->fit();
        auto *picker = window.findChild<QPushButton *>("pickerTool"); picker->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
        QVERIFY(picker->isChecked()); QVERIFY(!window.isWindowModified());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({2.5, 2.5}).toPoint());
        auto *reset = window.findChild<QPushButton *>("resetGray"); QVERIFY(reset->isVisible()); reset->click(); QVERIFY(!reset->isVisible());
        window.findChild<QSpinBox *>("brushSize")->setValue(1);
        window.findChild<QPushButton *>("color2")->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({7.5, 4.5}).toPoint());
        canvas->setFocus(); QTest::keySequence(canvas, QKeySequence::Save); QCoreApplication::processEvents();
        QCOMPARE(int(MapIO::load(saved.yamlPath).image.constScanLine(4)[7]), 205);
        window.close();
    }
};
QTEST_MAIN(MapwrightTest)
#include "test_mapwright.moc"
