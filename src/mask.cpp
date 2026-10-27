#include "mask.h"
#include <cmath>
#include <stdexcept>

namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
void checkReference(const MapIO::Map &reference) {
    if (reference.image.isNull()) fail("원본 지도를 먼저 열어 주세요.");
    if (std::abs(std::remainder(reference.meta.yaw, 2 * std::acos(-1.))) > 1e-9)
        fail("Nav2 필터는 회전된 원점(Yaw)을 지원하지 않습니다. Yaw가 0인 지도를 사용하세요.");
}
QString key(Mask::Kind kind) { return kind == Mask::Keepout ? "keepout" : "speed_percent"; }
}
namespace Mask {
void checkSaveTarget(const QString &yamlPath, const QString &imagePath, const QList<MapIO::Map> &protectedMaps) {
    for (const auto &map : protectedMaps) {
        for (const auto &path : {map.yamlPath, map.imagePath}) {
            if (!path.isEmpty() && (MapIO::sameFilePath(yamlPath, path) || MapIO::sameFilePath(imagePath, path)))
                fail("다른 레이어의 파일에는 덮어쓸 수 없습니다. 별도의 이름으로 저장하세요: " + path);
        }
    }
}
QString kindName(Kind kind) { return kind == Keepout ? "진입 금지" : "속도 제한"; }
bool compatible(const MapIO::Map &mask, const MapIO::Map &reference) {
    auto close = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
    return mask.image.size() == reference.image.size() && close(mask.meta.resolution, reference.meta.resolution) &&
        close(mask.meta.x, reference.meta.x) && close(mask.meta.y, reference.meta.y) &&
        std::abs(std::remainder(mask.meta.yaw - reference.meta.yaw, 2 * std::acos(-1.))) <= 1e-9;
}
MapIO::Map create(const MapIO::Map &reference, Kind kind) {
    checkReference(reference);
    MapIO::Map mask;
    mask.image = QImage(reference.image.size(), QImage::Format_Grayscale8);
    if (mask.image.isNull()) fail("마스크 메모리를 할당할 수 없습니다.");
    mask.image.fill(0);
    mask.meta = reference.meta;
    mask.meta.mode = "raw";
    mask.meta.yaw = 0;
    mask.meta.negate = 0;
    mask.extra = YAML::Node(YAML::NodeType::Map);
    mask.extra["map_editor_filter"] = key(kind).toStdString();
    return mask;
}
MapIO::Map load(const QString &path, const MapIO::Map &reference, Kind kind) {
    checkReference(reference);
    auto mask = MapIO::load(path, true);
    if (!mask.hasYaml) fail("마스크는 좌표 정보가 있는 YAML 파일로 열어 주세요.");
    if (!compatible(mask, reference)) fail("마스크의 크기·해상도·원점이 원본 지도와 다릅니다. 같은 지도에서 만든 마스크를 선택하세요.");
    if (MapIO::sameFilePath(mask.yamlPath, reference.yamlPath) || MapIO::sameFilePath(mask.imagePath, reference.imagePath))
        fail("원본 지도 파일을 마스크로 사용할 수 없습니다. 별도의 마스크를 선택하세요.");
    if (mask.extra["map_editor_filter"] && mask.extra["map_editor_filter"].as<std::string>() != key(kind).toStdString())
        fail("선택한 레이어와 마스크 종류가 다릅니다.");
    if (mask.meta.mode == "trinary") {
        for (int y = 0; y < mask.image.height(); ++y) {
            auto *row = mask.image.scanLine(y);
            for (int x = 0; x < mask.image.width(); ++x) {
                const auto p = MapIO::probability(row[x], mask.meta);
                row[x] = p > mask.meta.occupied ? 100 : (p < mask.meta.free ? 0 : 255);
            }
        }
    }
    for (int y = 0; y < mask.image.height(); ++y) {
        const auto *row = mask.image.constScanLine(y);
        for (int x = 0; x < mask.image.width(); ++x) {
            if (row[x] > 100 && row[x] != 255) fail("마스크 값은 0~100 또는 미확인(255)이어야 합니다.");
            if (kind == Keepout && row[x] != 0 && row[x] != 100 && row[x] != 255)
                fail("진입 금지 마스크는 0(제한 없음)과 100(진입 금지)을 사용합니다. 가중치 마스크는 지원하지 않습니다.");
        }
    }
    mask.meta.mode = "raw";
    mask.meta.negate = 0;
    mask.meta.yaw = 0;
    mask.extra["map_editor_filter"] = key(kind).toStdString();
    return mask;
}
}
