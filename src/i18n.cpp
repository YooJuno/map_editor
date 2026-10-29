#include "i18n.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaProperty>
#include <QSignalBlocker>
#include <QTranslator>

namespace {
class Translator : public QTranslator {
public:
    QString code = "ko";
    QJsonObject catalog;
    QHash<QString, QString> sources;
    Translator() {
        QFile file(":/i18n.json");
        if (file.open(QIODevice::ReadOnly)) catalog = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = catalog.begin(); it != catalog.end(); ++it) {
            sources.insert(it.key(), it.key());
            const auto entry = it.value().toObject();
            for (auto value = entry.begin(); value != entry.end(); ++value)
                if (!sources.contains(value.value().toString())) sources.insert(value.value().toString(), it.key());
        }
    }
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override {
        const auto key = QString::fromUtf8(source);
        const auto entry = catalog.value(key).toObject();
        return entry.value(code).toString();
    }
};
Translator &translator() { static Translator value; return value; }

QString localized(const QString &value) {
    auto &t = translator();
    const auto source = t.sources.value(value, value);
    return t.catalog.value(source).toObject().value(t.code).toString(source);
}
void localizeObject(QObject *object) {
    // Remember each property's source to handle translations that share a label.
    // If refresh() changed its value, rediscover the source before translating.
    for (const char *property : {"text", "title", "windowTitle", "toolTip", "accessibleName", "placeholderText"}) {
        const int index = object->metaObject()->indexOfProperty(property);
        if (index < 0 || !object->metaObject()->property(index).isWritable()) continue;
        const auto value = object->property(property).toString();
        if (value.isEmpty()) continue;
        const QByteArray sourceKey = QByteArray("_i18n_source_") + property, lastKey = QByteArray("_i18n_last_") + property;
        auto source = object->property(sourceKey).toString();
        if (source.isEmpty() || value != object->property(lastKey).toString()) source = translator().sources.value(value, value);
        const auto translated = localized(source);
        object->setProperty(sourceKey, source); object->setProperty(lastKey, translated);
        if (value != translated) object->setProperty(property, translated);
    }
    if (auto *combo = qobject_cast<QComboBox *>(object)) {
        QSignalBlocker block(combo);
        for (int i = 0; i < combo->count(); ++i) {
            constexpr int sourceRole = Qt::UserRole + 101;
            auto source = combo->itemData(i, sourceRole).toString();
            if (source.isEmpty()) { source = translator().sources.value(combo->itemText(i), combo->itemText(i)); combo->setItemData(i, source, sourceRole); }
            combo->setItemText(i, localized(source));
        }
    }
    for (auto *child : object->children()) localizeObject(child);
}
}

QString I18n::language() { return translator().code; }
void I18n::setLanguage(const QString &code) {
    const auto selected = QStringList{"ko", "en", "ja", "zh"}.contains(code) ? code : QString("ko");
    auto &t = translator();
    if (t.code == selected && qApp->property("i18nInstalled").toBool()) return;
    QCoreApplication::removeTranslator(&t);
    t.code = selected;
    QCoreApplication::installTranslator(&t);
    qApp->setProperty("i18nInstalled", true);
    QLocale::setDefault(QLocale(selected == "zh" ? "zh_CN" : selected));
    QStringList preferred;
    if (selected == "ja") preferred = {"Noto Sans CJK JP", "Yu Gothic UI", "Hiragino Sans"};
    else if (selected == "zh") preferred = {"Noto Sans CJK SC", "Microsoft YaHei UI", "PingFang SC"};
    else preferred = {"Noto Sans CJK KR", "Malgun Gothic", "Apple SD Gothic Neo"};
    preferred << "Noto Sans";
    const auto families = QFontDatabase::families();
    for (const auto &family : preferred) if (families.contains(family)) { qApp->setFont(QFont(family, 10)); break; }
}
QString I18n::text(const char *source) { return QCoreApplication::translate("MapEditor", source); }
QString I18n::label(const QString &value) { return localized(value); }
std::string I18n::utf8(const char *source) { return text(source).toUtf8().toStdString(); }
void I18n::localize(QObject *root) { localizeObject(root); }
