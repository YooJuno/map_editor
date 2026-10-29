#pragma once
#include <QString>
#include <string>
class QObject;

namespace I18n {
QString language();
void setLanguage(const QString &code);
QString text(const char *source);
QString label(const QString &value);
std::string utf8(const char *source);
// Retranslate existing widgets without rebuilding the editor or its documents.
void localize(QObject *root);
}
