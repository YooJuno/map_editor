#pragma once
#include "mapio.h"

namespace Mask {
enum Kind { Keepout, SpeedPercent };
MapIO::Map create(const MapIO::Map &reference, Kind kind);
MapIO::Map load(const QString &path, const MapIO::Map &reference, Kind kind);
bool compatible(const MapIO::Map &mask, const MapIO::Map &reference);
QString kindName(Kind kind);
void checkSaveTarget(const QString &yamlPath, const QString &imagePath, const QList<MapIO::Map> &protectedMaps);
}
