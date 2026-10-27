#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFontDatabase>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Mapwright"); QCoreApplication::setApplicationName("Mapwright"); QCoreApplication::setApplicationVersion("1.0.0");
    app.setStyle("Fusion");
    const auto families = QFontDatabase::families();
    for (const auto &family : {QString("Noto Sans CJK KR"), QString("Malgun Gothic"), QString("Apple SD Gothic Neo"), QString("Noto Sans")}) {
        if (families.contains(family)) { app.setFont(QFont(family, 10)); break; }
    }
    QFile style(":/style.qss"); if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
    QCommandLineParser parser; parser.setApplicationDescription("PGM + YAML occupancy map editor"); parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("map", "PGM or YAML map file", "[map]");
    parser.addOption({"screenshot", "Save a UI screenshot and exit (for local QA).", "path"}); parser.process(app);
    MainWindow window; window.show();
    if (!parser.positionalArguments().isEmpty()) window.openPath(parser.positionalArguments().first());
    if (parser.isSet("screenshot")) QTimer::singleShot(500, &app, [&] {
        window.canvas()->fit();
        if (!window.grab().save(parser.value("screenshot"))) { std::fprintf(stderr, "Screenshot failed\n"); app.exit(1); }
        else app.exit(0);
    });
    return app.exec();
}
