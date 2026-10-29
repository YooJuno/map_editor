#include "mainwindow.h"
#include "theme.h"
#include "i18n.h"
#include "icons.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFontDatabase>
#include <QStyle>
#include <QSettings>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Mapwright"); QCoreApplication::setApplicationName("Mapwright"); QCoreApplication::setApplicationVersion(MAPWRIGHT_VERSION);
    QGuiApplication::setApplicationDisplayName("Map Editor");
    QGuiApplication::setDesktopFileName("mapwright");
    app.setWindowIcon(Icons::application());
    app.setStyle("Fusion");
    const auto families = QFontDatabase::families();
    for (const auto &family : {QString("Noto Sans CJK KR"), QString("Malgun Gothic"), QString("Apple SD Gothic Neo"), QString("Noto Sans")}) {
        if (families.contains(family)) { app.setFont(QFont(family, 10)); break; }
    }
    Theme::apply(QSettings().value("colorTheme", "dark").toString() != "light");
    I18n::setLanguage(QSettings().value("language", "ko").toString());
    QCommandLineParser parser; parser.setApplicationDescription("PGM + YAML occupancy map editor"); parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("map", "PGM or YAML map file", "[map]");
    parser.addOption({"screenshot", "Save a UI screenshot and exit (for local QA).", "path"});
    parser.addOption({"keepout", "Open a keepout mask over the reference map.", "yaml"});
    parser.addOption({"speed-mask", "Open a percentage speed mask over the reference map.", "yaml"});
    parser.process(app);
    if ((parser.isSet("keepout") || parser.isSet("speed-mask")) && parser.positionalArguments().isEmpty()) parser.showHelp(1);
    MainWindow window; window.show();
    if (!parser.positionalArguments().isEmpty()) window.openPath(parser.positionalArguments().first());
    if (parser.isSet("keepout")) window.openMaskPath(parser.value("keepout"), Mask::Keepout);
    if (parser.isSet("speed-mask")) window.openMaskPath(parser.value("speed-mask"), Mask::SpeedPercent);
    if (parser.isSet("screenshot")) QTimer::singleShot(500, &app, [&] {
        window.canvas()->fit();
        if (!window.grab().save(parser.value("screenshot"))) { std::fprintf(stderr, "Screenshot failed\n"); app.exit(1); }
        else app.exit(0);
    });
    return app.exec();
}
