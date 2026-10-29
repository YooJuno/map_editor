#include "i18n.h"
#include "mainwindow.h"
#include "theme.h"
#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

class LanguageTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    static MapIO::Map reference() {
        MapIO::Map map; map.image = QImage(32, 24, QImage::Format_Grayscale8); map.image.fill(255);
        map.extra = YAML::Node(YAML::NodeType::Map); return map;
    }
    static void switchTo(MainWindow &window, const QString &code) {
        window.findChild<QAction *>("language_" + code)->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange); QCoreApplication::processEvents();
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("MapwrightTests"); QCoreApplication::setApplicationName("Languages");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        qApp->setStyle("Fusion"); Theme::apply(true);
    }
    void init() { I18n::setLanguage("ko"); QCoreApplication::processEvents(); }
    void liveSwitchKeepsMapMaskAndUndoHistory() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        auto *base = window.activeDocument(); auto im = base->image().copy(); im.scanLine(1)[1] = 0; base->editImage(im, I18n::text("펜"));
        window.findChild<QPushButton *>("keepoutWorkspace")->click(); auto *mask = window.activeDocument();
        window.findChild<QPushButton *>("rectangleTool")->click(); auto *canvas = window.canvas();
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({3.5, 3.5}).toPoint());
        QTest::mouseMove(canvas, canvas->screenPoint({12.5, 12.5}).toPoint());
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({12.5, 12.5}).toPoint());
        const auto maskImage = mask->image().copy(); const int undoIndex = mask->undo()->index();
        QVERIFY(mask->dirty()); QVERIFY(!maskImage.isNull());
        const QStringList codes{"en", "ja", "zh", "ko"};
        const QStringList rotate{"Rotate", "回転", "旋转", "회전"};
        const QStringList save{"Save mask", "マスクを保存", "保存掩码", "마스크 저장"};
        const QStringList pick{"Eyedropper", "スポイト", "取色器", "스포이드"};
        for (int i = 0; i < codes.size(); ++i) {
            switchTo(window, codes[i]);
            QCOMPARE(I18n::language(), codes[i]); QCOMPARE(QSettings().value("language").toString(), codes[i]);
            QVERIFY(window.findChild<QAction *>("language_" + codes[i])->isChecked());
            QCOMPARE(window.findChild<QPushButton *>("rotateWorkspace")->text(), rotate[i]);
            QCOMPARE(window.findChild<QAction *>("saveAction")->text(), save[i]);
            QVERIFY(window.findChild<QPushButton *>("pickerTool")->toolTip().startsWith(pick[i]));
            QCOMPARE(window.activeDocument(), mask); QCOMPARE(mask->image(), maskImage); QCOMPARE(base->image(), im);
            QCOMPARE(mask->undo()->index(), undoIndex); QVERIFY(mask->dirty()); QVERIFY(base->dirty());
            QVERIFY(window.findChild<QAction *>("saveAction")->isEnabled());
            QVERIFY(window.findChild<QPushButton *>("rotateWorkspace")->property("locked").toBool());
            // The native language names are stable in every UI language.
            QCOMPARE(window.findChild<QAction *>("language_ko")->text(), QString("한국어"));
            QCOMPARE(window.findChild<QAction *>("language_ja")->text(), QString("日本語"));
        }
        mask->save(dir.filePath("mask.yaml")); base->save(); window.close();
    }
    void rotationAndPendingOriginSurviveSwitching() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.openPath(paths.yamlPath); auto *doc = window.activeDocument(); const auto image = doc->image().copy();
        window.findChild<QPushButton *>("rotateWorkspace")->click(); auto *angle = window.findChild<QDoubleSpinBox *>("rotationAngle"); angle->setValue(12.345);
        for (const auto &code : {"en", "ja", "zh", "ko"}) {
            switchTo(window, code); QCOMPARE(angle->value(), 12.345); QVERIFY(window.canvas()->rotationPreview());
            QCOMPARE(doc->image(), image); QVERIFY(!doc->dirty()); QVERIFY(window.findChild<QAction *>("saveAction")->isEnabled());
        }
        window.findChild<QPushButton *>("resetGeometry")->click(); window.findChild<QPushButton *>("originWorkspace")->click();
        auto *x = window.findChild<QDoubleSpinBox *>("originX"); x->setValue(-1.234);
        switchTo(window, "ja"); QCOMPARE(x->value(), -1.234); QCOMPARE(doc->meta().x, 0.);
        QVERIFY(window.findChild<QPushButton *>("applyMetadata")->isEnabled());
        window.findChild<QPushButton *>("revertMetadata")->click(); window.close();
    }
    void startupLanguageAndStandardDialogs() {
        I18n::setLanguage("en"); MainWindow window;
        QCOMPARE(window.findChild<QPushButton *>("cropWorkspace")->text(), QString("Crop"));
        for (const auto &code : {"en", "ja", "zh", "ko"}) {
            switchTo(window, code);
            QMessageBox box(QMessageBox::Warning, "Map Editor", I18n::text("변경 내용을 저장할까요?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            QCOMPARE(box.button(QMessageBox::Save)->text().remove('&'), I18n::text("저장"));
            QCOMPARE(box.button(QMessageBox::Cancel)->text().remove('&'), I18n::text("취소"));
            auto meta = reference().meta; meta.resolution = 0;
            QCOMPARE(MapIO::validate(meta), I18n::text("resolution은 0보다 큰 유한한 값이어야 합니다."));
        }
        window.close();
    }
    void unfinishedPolygonSurvivesSwitchAndCancelsCleanly() {
        QTemporaryDir dir; const auto paths = MapIO::savePair(reference(), dir.filePath("base.yaml"));
        MainWindow window; window.show(); QVERIFY(QTest::qWaitForWindowActive(&window)); window.openPath(paths.yamlPath);
        window.findChild<QPushButton *>("speedWorkspace")->click(); auto *canvas = window.canvas();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({3.5, 3.5}).toPoint());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->screenPoint({12.5, 3.5}).toPoint());
        QVERIFY(canvas->isEditing()); QVERIFY(!window.activeDocument()->dirty());
        switchTo(window, "en"); QVERIFY(canvas->isEditing());
        QVERIFY(window.findChild<QAction *>("saveAction")->isEnabled());
        QTest::keyClick(canvas, Qt::Key_Escape); QVERIFY(!canvas->isEditing());
        QVERIFY(!window.findChild<QAction *>("saveAction")->isEnabled());
        QVERIFY(!window.findChild<QPushButton *>("rotateWorkspace")->property("locked").toBool());
        QVERIFY(!window.findChild<QPushButton *>("closeMasks")->isVisible()); window.close();
    }
    void cleanupTestCase() { I18n::setLanguage("ko"); }
};
QTEST_MAIN(LanguageTest)
#include "test_i18n.moc"
