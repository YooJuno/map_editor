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
        palette.setColor(group, QPalette::Window, QColor("#282b30"));
        palette.setColor(group, QPalette::WindowText, QColor(disabled ? "#7e848f" : "#e0e3e8"));
        palette.setColor(group, QPalette::Base, QColor("#1d2024"));
        palette.setColor(group, QPalette::AlternateBase, QColor("#2c3037"));
        palette.setColor(group, QPalette::Text, QColor(disabled ? "#7e848f" : "#e0e3e8"));
        palette.setColor(group, QPalette::Button, QColor("#2e3238"));
        palette.setColor(group, QPalette::ButtonText, QColor(disabled ? "#7e848f" : "#e0e3e8"));
        palette.setColor(group, QPalette::Highlight, QColor("#394655"));
        palette.setColor(group, QPalette::HighlightedText, QColor("#f1f4f8"));
        palette.setColor(group, QPalette::ToolTipBase, QColor("#2e3238"));
        palette.setColor(group, QPalette::ToolTipText, QColor("#e0e3e8"));
        palette.setColor(group, QPalette::PlaceholderText, QColor("#7e848f"));
        palette.setColor(group, QPalette::Light, QColor("#4d5561"));
        palette.setColor(group, QPalette::Midlight, QColor("#3d4149"));
        palette.setColor(group, QPalette::Mid, QColor("#353b45"));
        palette.setColor(group, QPalette::Dark, QColor("#191c21"));
        palette.setColor(group, QPalette::Shadow, QColor("#111419"));
        palette.setColor(group, QPalette::Link, QColor("#8d99aa"));
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
