#pragma once
#include <QPalette>
#include <QApplication>
#include <QFile>
#include <QStyle>
#include <QVariant>

namespace Theme {
inline bool isDark() {
    const auto selected = qApp->property("darkTheme");
    return selected.isValid() ? selected.toBool() : qApp->palette().color(QPalette::Window).lightness() < 128;
}
inline QPalette darkPalette() {
    QPalette palette;
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        palette.setColor(group, QPalette::Window, QColor("#252d31"));
        palette.setColor(group, QPalette::WindowText, QColor(disabled ? "#77877e" : "#d9e1dc"));
        palette.setColor(group, QPalette::Base, QColor("#1c2327"));
        palette.setColor(group, QPalette::AlternateBase, QColor("#2a3438"));
        palette.setColor(group, QPalette::Text, QColor(disabled ? "#77877e" : "#d9e1dc"));
        palette.setColor(group, QPalette::Button, QColor("#2d373b"));
        palette.setColor(group, QPalette::ButtonText, QColor(disabled ? "#77877e" : "#d9e1dc"));
        palette.setColor(group, QPalette::Highlight, QColor("#3b5144"));
        palette.setColor(group, QPalette::HighlightedText, QColor("#edf5ef"));
        palette.setColor(group, QPalette::ToolTipBase, QColor("#2d373b"));
        palette.setColor(group, QPalette::ToolTipText, QColor("#d9e1dc"));
        palette.setColor(group, QPalette::PlaceholderText, QColor("#77877e"));
        palette.setColor(group, QPalette::Light, QColor("#48564f"));
        palette.setColor(group, QPalette::Midlight, QColor("#3b4746"));
        palette.setColor(group, QPalette::Mid, QColor("#313e3d"));
        palette.setColor(group, QPalette::Dark, QColor("#171e22"));
        palette.setColor(group, QPalette::Shadow, QColor("#0f1518"));
        palette.setColor(group, QPalette::Link, QColor("#b2c9b9"));
    }
    return palette;
}
inline void apply(bool dark) {
    qApp->setProperty("darkTheme", dark);
    qApp->setPalette(dark ? darkPalette() : qApp->style()->standardPalette());
    QFile sheet(dark ? ":/style.qss" : ":/style-light.qss");
    if (sheet.open(QIODevice::ReadOnly)) qApp->setStyleSheet(QString::fromUtf8(sheet.readAll()));
}
}
