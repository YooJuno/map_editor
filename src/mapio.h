#pragma once
#include <QImage>
#include <QPointF>
#include <QString>
#include <yaml-cpp/yaml.h>

namespace MapIO {
constexpr qint64 MaxPixels = 32 * 1024 * 1024;
struct Metadata {
    double resolution = 0.05;
    double x = 0, y = 0, yaw = 0;
    int negate = 0;
    double occupied = 0.65, free = 0.196;
    QString mode = "trinary";
    bool operator==(const Metadata &b) const;
    bool operator!=(const Metadata &b) const { return !(*this == b); }
};
struct Map {
    QImage image;
    Metadata meta;
    YAML::Node extra;
    QString yamlPath, imagePath;
    bool hasYaml = false;
};
struct SaveResult {
    QString yamlPath, imagePath, backupDirectory;
};
// Throws std::runtime_error with a user-readable diagnostic.
QString validate(const Metadata &meta);
bool sameFilePath(const QString &first, const QString &second);
QImage readPgm(const QString &path);
QByteArray encodePgm(const QImage &image);
Map load(const QString &path);
QByteArray encodeYaml(const Map &map, const QString &relativeImage);
SaveResult savePair(const Map &map, const QString &yamlPath, const QString &imagePath = {});
double probability(int gray, const Metadata &meta);
int unknownGray(const Metadata &meta);
QImage trinary(const QImage &image, const Metadata &meta);
QPointF pixelToWorld(QPointF pixel, int height, const Metadata &meta);
QPointF worldToPixel(QPointF world, int height, const Metadata &meta);
}
