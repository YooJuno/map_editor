#include "mainwindow.h"
#include "i18n.h"
#include "theme.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLockFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <random>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

class PaintLog : public QObject {
public:
    int count = 0;
    QRegion region;
    void clear() { count = 0; region = {}; }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) { ++count; region += static_cast<QPaintEvent *>(event)->region(); }
        return false;
    }
};

class StabilityTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static MapIO::Map map(int w = 64, int h = 48) {
        MapIO::Map result; result.image = QImage(w, h, QImage::Format_Grayscale8); result.image.fill(205);
        result.extra = YAML::Node(YAML::NodeType::Map); return result;
    }
    static QByteArray read(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
    static void write(const QString &path, const QByteArray &bytes) { QFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) qFatal("fixture write failed"); }
    static void mouse(Canvas *canvas, QEvent::Type type, QPointF pixel, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const auto point = canvas->screenPoint(pixel);
        QMouseEvent event(type, point, QPointF(canvas->mapToGlobal(point.toPoint())), button, buttons, Qt::NoModifier); QApplication::sendEvent(canvas, &event);
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("MapwrightTests"); QCoreApplication::setApplicationName("Stability");
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        qApp->setStyle("Fusion"); Theme::apply(true);
    }
    void init() { I18n::setLanguage("ko"); QCoreApplication::processEvents(); }
    void brushKeepsItsPixelFootprint() {
        std::mt19937 random(581);
        for (int sample = 0; sample < 120; ++sample) {
            auto image = map(32, 24).image; const QPoint a(int(random() % 32), int(random() % 24)), b(int(random() % 32), int(random() % 24)); const int diameter = 1 + int(random() % 16);
            QVector<QPoint> centers; int x = a.x(), y = a.y(), dx = std::abs(b.x() - x), sx = x < b.x() ? 1 : -1;
            int dy = -std::abs(b.y() - y), sy = y < b.y() ? 1 : -1, error = dx + dy;
            for (;;) { centers.append({x, y}); if (x == b.x() && y == b.y()) break; const int e = 2 * error; if (e >= dy) { error += dy; x += sx; } if (e <= dx) { error += dx; y += sy; } }
            Raster::stroke(image, a, b, diameter, 0);
            for (int row = 0; row < 24; ++row) for (int column = 0; column < 32; ++column) {
                bool covered = false;
                for (const auto &center : centers) {
                    const int ox = 2 * (column - center.x()) + (diameter % 2 ? 0 : 1), oy = 2 * (row - center.y()) + (diameter % 2 ? 0 : 1);
                    if (ox * ox + oy * oy <= diameter * diameter) { covered = true; break; }
                }
                QCOMPARE(int(image.constScanLine(row)[column]), covered ? 0 : 205);
            }
        }
    }
    void shrinkingPreviewAndCancellationLeaveNoGhosts() {
        Document doc; doc.replace(map(), false); Canvas canvas(&doc); canvas.resize(800, 600); canvas.show(); QCoreApplication::processEvents(); canvas.fit(); canvas.setTool(Canvas::Rectangle); canvas.setFilled(true);
        mouse(&canvas, QEvent::MouseButtonPress, {4, 4}, Qt::LeftButton, Qt::LeftButton);
        mouse(&canvas, QEvent::MouseMove, {50, 40}, Qt::NoButton, Qt::LeftButton); QTest::qWait(25);
        mouse(&canvas, QEvent::MouseMove, {12, 12}, Qt::NoButton, Qt::LeftButton);
        mouse(&canvas, QEvent::MouseButtonRelease, {12, 12}, Qt::LeftButton, Qt::NoButton);
        auto expected = map().image; Raster::rectangle(expected, {4, 4}, {12, 12}, 8, 0, true); QCOMPARE(doc.image(), expected);
        mouse(&canvas, QEvent::MouseButtonPress, {20, 20}, Qt::LeftButton, Qt::LeftButton);
        mouse(&canvas, QEvent::MouseMove, {55, 44}, Qt::NoButton, Qt::LeftButton); QTest::qWait(25); QTest::keyClick(&canvas, Qt::Key_Escape); QCOMPARE(doc.image(), expected);
        canvas.setTool(Canvas::Pen); mouse(&canvas, QEvent::MouseButtonPress, {30, 30}, Qt::LeftButton, Qt::LeftButton); mouse(&canvas, QEvent::MouseButtonRelease, {30, 30}, Qt::LeftButton, Qt::NoButton);
        Raster::stroke(expected, {30, 30}, {30, 30}, 8, 0); QCOMPARE(doc.image(), expected);
        doc.undo()->undo(); doc.undo()->redo(); QCOMPARE(doc.image(), expected);
    }
    void pointerHoverRepaintsOnlyTheBrushAndHandNeedsNoRepaint() {
        Document doc; doc.replace(map(256, 192), false); Canvas canvas(&doc); canvas.resize(800, 600); canvas.setAxes(false); canvas.show();
        QVERIFY(QTest::qWaitForWindowActive(&canvas)); canvas.actualSize(); QTest::qWait(30);
        PaintLog log; canvas.installEventFilter(&log);
        mouse(&canvas, QEvent::MouseMove, {100, 80}, Qt::NoButton, Qt::NoButton); QTest::qWait(25); log.clear();
        mouse(&canvas, QEvent::MouseMove, {102, 81}, Qt::NoButton, Qt::NoButton); QTest::qWait(25);
        QVERIFY(log.count > 0); const auto area = log.region.boundingRect();
        QVERIFY(area.contains(canvas.screenPoint({100.5, 80.5}).toPoint())); QVERIFY(area.contains(canvas.screenPoint({102.5, 81.5}).toPoint()));
        QVERIFY(area.width() * area.height() < canvas.width() * canvas.height() / 8); QVERIFY(!doc.dirty());
        canvas.setTool(Canvas::Hand); QTest::qWait(25); log.clear(); QSignalSpy position(&canvas, &Canvas::positionChanged);
        mouse(&canvas, QEvent::MouseMove, {105, 85}, Qt::NoButton, Qt::NoButton); QTest::qWait(25);
        QCOMPARE(position.count(), 1); QCOMPARE(log.count, 0); QVERIFY(!doc.dirty());
    }
    void plainPgmParsingKeepsCommentsWhitespaceAndIntegerValidation() {
        QTemporaryDir dir; const auto path = dir.filePath("plain.pgm");
        write(path, "P2# header\r\n3\t2\n255\n0000000000 000205# cell\r\n255\v17\f32\n10");
        const auto image = MapIO::readPgm(path); const int expected[]{0, 205, 255, 17, 32, 10};
        for (int i = 0; i < 6; ++i) QCOMPARE(int(image.constScanLine(i / 3)[i % 3]), expected[i]);
        for (const auto &token : {"-1", "+1", "1x", "2147483648", "9999999999999999999999999999", "1.0"}) {
            write(path, QByteArray("P2\n1 1\n255\n") + token); QVERIFY_EXCEPTION_THROWN(MapIO::readPgm(path), std::runtime_error);
        }
        write(path, "P2\n2147483648 1\n255\n0"); QVERIFY_EXCEPTION_THROWN(MapIO::readPgm(path), std::runtime_error);
    }
    void sparseHistoryRestoresOddStridePixelsAcrossGeometryAndBranches() {
        Document doc; auto original = map(67, 53); std::mt19937 random(25);
        for (int y = 0; y < original.image.height(); ++y) for (int x = 0; x < original.image.width(); ++x) original.image.scanLine(y)[x] = uchar(random());
        doc.replace(original, false); auto edited = original.image.copy();
        for (const auto point : {QPoint(3, 4), QPoint(61, 47), QPoint(20, 30)}) edited.scanLine(point.y())[point.x()] ^= 255;
        doc.editImage(edited, "sparse", QRect(3, 4, 59, 44)); QCOMPARE(doc.image(), edited);
        for (int i = 0; i < 3; ++i) { doc.undo()->undo(); QCOMPARE(doc.image(), original.image); QVERIFY(!doc.dirty()); doc.undo()->redo(); QCOMPARE(doc.image(), edited); }
        doc.cropImage(QRect(2, 3, 63, 47)); QCOMPARE(doc.image(), edited.copy(2, 3, 63, 47));
        doc.undo()->undo(); QCOMPARE(doc.image(), edited); doc.undo()->undo(); QCOMPARE(doc.image(), original.image);
        auto branch = original.image.copy(); branch.scanLine(9)[12] ^= 43; doc.editImage(branch, "branch", QRect(12, 9, 1, 1));
        QVERIFY(!doc.undo()->canRedo()); QCOMPARE(doc.image(), branch); doc.undo()->undo(); QCOMPARE(doc.image(), original.image); QVERIFY(!doc.dirty());
    }
    void trinaryMasksMatchEveryPixelClassification() {
        QTemporaryDir dir; const auto reference = map(256, 1);
        for (int negate : {0, 1}) {
            auto source = reference; source.image = reference.image.copy(); source.meta.negate = negate; source.meta.free = .15; source.meta.occupied = .82;
            for (int x = 0; x < 256; ++x) source.image.scanLine(0)[x] = uchar(x);
            const auto paths = MapIO::savePair(source, dir.filePath(QString("mask-%1.yaml").arg(negate)));
            for (auto kind : {Mask::Keepout, Mask::SpeedPercent}) {
                const auto converted = Mask::load(paths.yamlPath, reference, kind);
                for (int x = 0; x < 256; ++x) { const auto p = MapIO::probability(x, source.meta); QCOMPARE(int(converted.image.constScanLine(0)[x]), p > source.meta.occupied ? 100 : (p < source.meta.free ? 0 : 255)); }
            }
        }
    }
    void loadedSnapshotRejectsAnExternalChangeBeforeAdoption() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(map(), dir.filePath("base.yaml")); auto loaded = MapIO::load(paths.yamlPath);
        auto external = loaded.image.copy(); external.scanLine(2)[3] = 255; const auto externalBytes = MapIO::encodePgm(external); write(paths.imagePath, externalBytes);
        Document doc; doc.replace(loaded, false); auto edited = doc.image().copy(); edited.scanLine(4)[5] = 0; doc.editImage(edited, "edit");
        QVERIFY_EXCEPTION_THROWN(doc.save(), std::runtime_error); QCOMPARE(read(paths.imagePath), externalBytes); QVERIFY(doc.dirty());
    }
    void nonRegularInputIsRejectedBeforeReading() {
        QTemporaryDir dir; const auto path = dir.filePath("directory.pgm"); QVERIFY(QDir().mkdir(path));
        QVERIFY_EXCEPTION_THROWN(MapIO::readPgm(path), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(MapIO::fingerprint({}, path), std::runtime_error);
#ifdef Q_OS_UNIX
        const auto pipe = dir.filePath("pipe.pgm"); QCOMPARE(::mkfifo(QFile::encodeName(pipe).constData(), 0600), 0);
        QVERIFY_EXCEPTION_THROWN(MapIO::readPgm(pipe), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(MapIO::fingerprint({}, pipe), std::runtime_error);
#endif
    }
    void pairLockAndStreamingRoundTrip() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(map(511, 1025), dir.filePath("base.yaml"));
        Document doc; doc.open(paths.yamlPath); auto edited = doc.image().copy(); edited.scanLine(100)[127] = 0; doc.editImage(edited, "edit"); const auto before = read(paths.imagePath);
        QLockFile lock(paths.imagePath + ".mapwright.lock"); lock.setStaleLockTime(0); QVERIFY(lock.tryLock(0));
        QVERIFY_EXCEPTION_THROWN(doc.save(), std::runtime_error); QCOMPARE(read(paths.imagePath), before); lock.unlock();
        const auto saved = doc.save(); QCOMPARE(read(paths.imagePath), MapIO::encodePgm(edited)); QVERIFY(saved.disk == MapIO::fingerprint(paths.yamlPath, paths.imagePath)); QVERIFY(!doc.dirty());
    }
    void twoOpenDocumentsCannotSilentlyOverwriteOneAnother() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(map(), dir.filePath("base.yaml"));
        Document first, second; first.open(paths.yamlPath); second.open(paths.yamlPath);
        auto image = first.image().copy(); image.scanLine(2)[3] = 0; first.editImage(image, "first edit"); first.save();
        auto stale = second.image().copy(); stale.scanLine(4)[5] = 255; second.editImage(stale, "second edit");
        QVERIFY_EXCEPTION_THROWN(second.save(), std::runtime_error); QCOMPARE(MapIO::readPgm(paths.imagePath), image); QVERIFY(second.dirty());
    }
    void historyBudgetIgnoresEvictedAndDiscardedCommands() {
        // Forty 2 MiB random deltas fit under 96 MiB; duplicate before/after
        // patches would prematurely discard history at this size.
        Document doc; doc.replace(map(2048, 1024), false); std::mt19937 random(817);
        for (int i = 0; i < 50; ++i) {
            auto image = doc.image().copy(); auto *data = image.bits(); for (qsizetype offset = 0; offset < image.sizeInBytes(); ++offset) data[offset] = uchar(random());
            doc.editImage(image, "noise");
        }
        QCOMPARE(doc.undo()->count(), 40);
        for (int i = 0; i < 20; ++i) doc.undo()->undo();
        auto next = doc.image().copy(); next.scanLine(0)[0] ^= 1; doc.editImage(next, "branch", QRect(0, 0, 1, 1)); QCOMPARE(doc.undo()->count(), 21);
    }
    void workerReusesThreadKeepsUiResponsiveAndRejectsReentry() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(map(), dir.filePath("base.yaml")); const auto other = MapIO::savePair(map(16, 16), dir.filePath("other.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath); auto *doc = window.activeDocument(); const auto original = doc->image().copy();
        auto *queue = window.findChild<WorkQueue *>(); QVERIFY(queue); int ticks = 0; bool guarded = false;
        QTimer pulse; pulse.setInterval(5); connect(&pulse, &QTimer::timeout, [&] { ++ticks; }); pulse.start();
        QTimer::singleShot(20, [&] {
            guarded = !window.findChild<QAction *>("saveAction")->isEnabled() && !window.findChild<QPushButton *>("mapWorkspace")->isEnabled();
            QVERIFY(!window.close()); window.openPath(other.yamlPath); QCOMPARE(window.activeDocument(), doc);
            mouse(window.canvas(), QEvent::MouseButtonPress, {4, 4}, Qt::LeftButton, Qt::LeftButton); mouse(window.canvas(), QEvent::MouseButtonRelease, {4, 4}, Qt::LeftButton, Qt::NoButton);
            QVERIFY_EXCEPTION_THROWN(queue->run("nested", [] { return 1; }), std::logic_error);
            window.findChild<QAction *>("language_en")->trigger();
        });
        const auto guiThread = QThread::currentThreadId();
        const auto first = queue->run("test", [] { QThread::msleep(150); return QThread::currentThreadId(); });
        const auto second = queue->run("test", [] { return QThread::currentThreadId(); });
        QVERIFY(first != guiThread); QCOMPARE(first, second); QVERIFY(ticks > 5); QVERIFY(guarded); QCOMPARE(doc->image(), original); QVERIFY(!doc->dirty()); QVERIFY(window.menuBar()->isEnabled());
        QVERIFY_EXCEPTION_THROWN(queue->run("failure", []() -> int { throw std::runtime_error("test failure"); }), std::runtime_error);
        QVERIFY(!queue->isRunning()); QCOMPARE(queue->run("recovered", [] { return 7; }), 7); window.close();
    }
    void originPreviewIsNotCommittedByChangingWorkspace() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(map(), dir.filePath("base.yaml")); MainWindow window; window.openPath(paths.yamlPath);
        const auto meta = window.activeDocument()->meta(); window.findChild<QPushButton *>("originWorkspace")->click(); window.findChild<QDoubleSpinBox *>("originX")->setValue(-2.5);
        window.findChild<QPushButton *>("mapWorkspace")->click(); QVERIFY(window.activeDocument()->meta() == meta); QVERIFY(!window.activeDocument()->dirty()); QVERIFY(!window.findChild<QAction *>("saveAction")->isEnabled()); window.close();
    }
    void largeRotationProcessesViewEventsWithoutEditingTheSource() {
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window));
        window.activeDocument()->replace(map(4096, 4096), false);
        int ticks = 0; bool unchangedDuringWork = true; QTimer pulse; pulse.setInterval(5);
        connect(&pulse, &QTimer::timeout, [&] {
            ++ticks; unchangedDuringWork = unchangedDuringWork && window.activeDocument()->image().size() == QSize(4096, 4096);
            window.canvas()->zoom(1.001);
        }); pulse.start(); window.activeDocument()->rotateImage(12); pulse.stop();
        QVERIFY(ticks > 0); QVERIFY(unchangedDuringWork); QVERIFY(window.activeDocument()->dirty()); QVERIFY(window.activeDocument()->image().size() != QSize(4096, 4096));
        window.activeDocument()->undo()->undo(); QVERIFY(!window.activeDocument()->dirty()); window.close();
    }
    void hiddenPolygonCanStillFinish() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(map(), dir.filePath("base.yaml")); MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath); window.findChild<QPushButton *>("keepoutWorkspace")->click();
        for (const auto point : {QPointF(4, 4), QPointF(30, 4), QPointF(30, 24), QPointF(4, 24)}) mouse(window.canvas(), QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        window.findChild<QCheckBox *>("keepoutVisible")->setChecked(false); auto *finish = window.findChild<QPushButton *>("finishPolygon"); QVERIFY(finish->isEnabled()); finish->click();
        QVERIFY(!window.canvas()->isEditing()); QVERIFY(window.activeDocument()->dirty()); QCOMPARE(int(window.activeDocument()->image().constScanLine(8)[10]), 100);
        window.activeDocument()->save(dir.filePath("mask.yaml")); window.close();
    }
    void failedRotationKeepsSavedMasksAttached() {
        QTemporaryDir dir; const auto base = map(16384, 1); const auto paths = MapIO::savePair(base, dir.filePath("base.yaml")); const auto mask = MapIO::savePair(Mask::create(base, Mask::Keepout), dir.filePath("mask.yaml"), {}, true);
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath); auto *document = window.activeDocument(); window.openMaskPath(mask.yamlPath, Mask::Keepout); auto *layer = window.activeDocument();
        window.findChild<QPushButton *>("rotateWorkspace")->click(); window.findChild<QDoubleSpinBox *>("rotationAngle")->setValue(45);
        bool dismissed = false; QTimer dismiss; dismiss.setInterval(10); connect(&dismiss, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { dismissed = true; box->accept(); dismiss.stop(); } }); dismiss.start();
        window.findChild<QPushButton *>("applyGeometry")->click(); QVERIFY(dismissed); QVERIFY(!layer->empty()); QVERIFY(!layer->dirty()); QCOMPARE(document->image(), base.image);
        window.findChild<QPushButton *>("resetGeometry")->click(); window.close();
    }
    void cleanupTestCase() { I18n::setLanguage("ko"); }
};
QTEST_MAIN(StabilityTest)
#include "test_stability.moc"
