#include "mapio.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
[[noreturn]] void fail(const QString &text) { throw std::runtime_error(text.toUtf8().constData()); }
QByteArray readFile(const QString &path, qint64 limit) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(QString("파일을 열 수 없습니다: %1\n%2").arg(path, file.errorString()));
    if (file.size() > limit) fail(QString("파일 크기 제한을 초과했습니다: %1").arg(path));
    auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) fail(file.errorString());
    return bytes;
}
bool space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f'; }
class Tokens {
public:
    explicit Tokens(const QByteArray &b) : bytes(b) {}
    QByteArray next() {
        for (;;) {
            while (pos < bytes.size() && space(bytes[pos])) ++pos;
            if (pos == bytes.size()) fail("PGM 헤더 또는 픽셀 데이터가 잘렸습니다.");
            if (bytes[pos] != '#') break;
            while (pos < bytes.size() && bytes[pos] != '\n' && bytes[pos] != '\r') ++pos;
        }
        auto start = pos;
        while (pos < bytes.size() && !space(bytes[pos]) && bytes[pos] != '#') ++pos;
        return bytes.mid(start, pos - start);
    }
    int integer() {
        const auto token = next();
        if (token.isEmpty()) fail("잘못된 PGM 정수입니다.");
        for (char c : token) if (c < '0' || c > '9') fail("잘못된 PGM 정수입니다.");
        bool ok = false;
        int value = token.toInt(&ok);
        if (!ok) fail("PGM 정수 범위를 초과했습니다.");
        return value;
    }
    const QByteArray &bytes;
    qsizetype pos = 0;
};
QString absolute(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
QString identity(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) return info.canonicalFilePath();
    const auto parent = info.dir().canonicalPath();
    return parent.isEmpty() ? absolute(path) : QDir(parent).filePath(info.fileName());
}
bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
    return identity(a).compare(identity(b), Qt::CaseInsensitive) == 0;
#else
    return identity(a) == identity(b);
#endif
}
void atomicWrite(const QString &path, const QByteArray &bytes) {
    QSaveFile out(path);
    out.setDirectWriteFallback(false);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        fail(QString("저장 실패: %1\n%2").arg(path, out.errorString()));
}
QString backup(const QString &yaml, const QString &pgm) {
    if (!QFile::exists(yaml) && !QFile::exists(pgm)) return {};
    const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    auto dir = QFileInfo(yaml).dir().filePath(".mapwright-backups/" + QFileInfo(yaml).completeBaseName() + "-" + stamp);
    if (!QDir().mkpath(dir)) fail("백업 폴더를 만들 수 없습니다: " + dir);
    // Fixed, distinct names also work when the original pair lives in different folders.
    for (const auto &entry : {qMakePair(yaml, QString("original.yaml")), qMakePair(pgm, QString("original.pgm"))}) {
        if (QFile::exists(entry.first) && !QFile::copy(entry.first, QDir(dir).filePath(entry.second)))
            fail("기존 파일을 백업할 수 없어 저장을 중단했습니다: " + entry.first);
    }
    atomicWrite(QDir(dir).filePath("paths.txt"), (yaml + "\n" + pgm + "\n").toUtf8());
    return dir;
}
void preflight(const QString &path) {
    const QFileInfo info(path);
    if (info.isSymLink()) fail("심볼릭 링크에는 덮어쓸 수 없습니다. 다른 이름으로 저장하세요: " + path);
    if (info.exists() && (!info.isFile() || !info.isWritable())) fail("쓰기 가능한 일반 파일이 아닙니다: " + path);
    QTemporaryFile probe(info.dir().filePath(".mapwright-write-XXXXXX"));
    if (!probe.open()) fail("폴더에 쓸 수 없습니다: " + info.absolutePath());
}
}

namespace MapIO {
bool sameFilePath(const QString &first, const QString &second) { return samePath(first, second); }
bool Metadata::operator==(const Metadata &b) const {
    return resolution == b.resolution && x == b.x && y == b.y && yaw == b.yaw && negate == b.negate && occupied == b.occupied && free == b.free && mode == b.mode;
}
QString validate(const Metadata &m, bool allowRaw) {
    if (!std::isfinite(m.resolution) || m.resolution <= 0) return "resolution은 0보다 큰 유한한 값이어야 합니다.";
    if (!std::isfinite(m.x) || !std::isfinite(m.y) || !std::isfinite(m.yaw)) return "origin 값은 유한한 숫자여야 합니다.";
    if (m.negate != 0 && m.negate != 1) return "negate는 0 또는 1이어야 합니다.";
    if (allowRaw && m.mode == "raw") return {};
    if (!std::isfinite(m.free) || !std::isfinite(m.occupied) || m.free <= 0 || m.occupied >= 1 || m.free >= m.occupied)
        return "임계값은 0 < free_thresh < occupied_thresh < 1이어야 합니다.";
    bool hasUnknown = false;
    for (int v = 1; v < 255; ++v) {
        const auto p = probability(v, m);
        if (p >= m.free && p <= m.occupied) { hasUnknown = true; break; }
    }
    if (!hasUnknown) return "두 임계값 사이에 회색 픽셀을 표현할 공간이 없습니다. 간격을 넓히세요.";
    if (m.mode != "trinary" && !(allowRaw && m.mode == "raw")) return "원본 지도는 trinary 모드만 지원합니다. raw 마스크는 원본 지도를 연 뒤 '마스크 열기'를 사용하세요.";
    return {};
}
QImage readPgm(const QString &path) {
    const auto bytes = readFile(path, MaxPixels * 12 + 1024 * 1024);
    Tokens tokens(bytes);
    const auto magic = tokens.next();
    if (magic != "P2" && magic != "P5") fail("P2 또는 P5 PGM 파일이 아닙니다.");
    int w = tokens.integer(), h = tokens.integer(), max = tokens.integer();
    if (w <= 0 || h <= 0 || qint64(w) * h > MaxPixels) fail("잘못된 지도 크기입니다. 최대 32 Mi 픽셀까지 지원합니다.");
    if (max < 1 || max > 65535) fail("PGM Maxval 범위는 1~65535입니다.");
    // Reject high depth instead of silently reducing map values on an unedited save.
    if (max != 255) fail("무손실 지도 편집을 위해 Maxval 255인 8-bit PGM만 지원합니다. 먼저 8-bit로 변환하세요.");
    QImage image(w, h, QImage::Format_Grayscale8);
    if (image.isNull()) fail("지도 메모리를 할당할 수 없습니다.");
    if (magic == "P5") {
        if (tokens.pos >= bytes.size() || !space(bytes[tokens.pos])) fail("PGM 바이너리 구분자가 없습니다.");
        // Consume only the delimiter: a first pixel of 10, 32, or 35 is image data.
        const char delim = bytes[tokens.pos++];
        if (delim == '\r' && tokens.pos < bytes.size() && bytes[tokens.pos] == '\n' && bytes.size() - tokens.pos == qint64(w) * h + 1) ++tokens.pos;
        if (bytes.size() - tokens.pos != qint64(w) * h) fail("PGM 픽셀 수가 크기와 다릅니다. 잘린 파일 또는 다중 이미지입니다.");
        for (int y = 0; y < h; ++y) std::memcpy(image.scanLine(y), bytes.constData() + tokens.pos + qint64(y) * w, size_t(w));
    } else {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            int v = tokens.integer();
            if (v > max) fail("PGM 픽셀 값이 Maxval보다 큽니다.");
            image.scanLine(y)[x] = uchar(v);
        }
        while (tokens.pos < bytes.size()) {
            if (space(bytes[tokens.pos])) ++tokens.pos;
            else if (bytes[tokens.pos] == '#') { while (tokens.pos < bytes.size() && bytes[tokens.pos] != '\n') ++tokens.pos; }
            else fail("PGM 뒤에 추가 픽셀 데이터가 있습니다.");
        }
    }
    return image;
}
QByteArray encodePgm(const QImage &image) {
    if (image.isNull() || image.format() != QImage::Format_Grayscale8 || qint64(image.width()) * image.height() > MaxPixels)
        fail("유효한 8-bit 지도 이미지가 없습니다.");
    QByteArray data = "P5\n# Mapwright map editor\n" + QByteArray::number(image.width()) + " " + QByteArray::number(image.height()) + "\n255\n";
    data.reserve(data.size() + image.width() * image.height());
    for (int y = 0; y < image.height(); ++y) data.append(reinterpret_cast<const char *>(image.constScanLine(y)), image.width());
    return data;
}
Map load(const QString &path, bool allowRaw) {
    Map map;
    QString yamlPath;
    const QFileInfo info(path);
    const auto ext = info.suffix().toLower();
    if (ext == "yaml" || ext == "yml") yamlPath = absolute(path);
    else if (ext == "pgm") {
        const auto stem = info.dir().filePath(info.completeBaseName());
        if (QFile::exists(stem + ".yaml")) yamlPath = absolute(stem + ".yaml");
        else if (QFile::exists(stem + ".yml")) yamlPath = absolute(stem + ".yml");
        else {
            map.imagePath = absolute(path);
            map.yamlPath = absolute(stem + ".yaml");
            map.image = readPgm(map.imagePath);
            map.extra = YAML::Node(YAML::NodeType::Map);
            return map;
        }
    } else fail(".yaml, .yml 또는 .pgm 파일을 선택하세요.");
    try {
        map.extra = YAML::Load(readFile(yamlPath, 1024 * 1024).toStdString());
        if (!map.extra.IsMap()) fail("YAML 최상위는 매핑이어야 합니다.");
        const auto &root = map.extra;
        auto image = QString::fromStdString(root["image"].as<std::string>());
        if (image.isEmpty()) fail("YAML image 경로가 비어 있습니다.");
        image = QDir::fromNativeSeparators(image);
        if (image.startsWith("~/")) image = QDir::home().filePath(image.mid(2));
        map.imagePath = absolute(QDir(QFileInfo(yamlPath).absolutePath()).absoluteFilePath(image));
        if (QFileInfo(map.imagePath).suffix().compare("pgm", Qt::CaseInsensitive) != 0) fail("YAML image는 .pgm 파일을 가리켜야 합니다.");
        if (ext == "pgm" && !samePath(map.imagePath, path)) fail("같은 이름의 YAML이 다른 PGM을 가리킵니다. 올바른 YAML을 직접 열어 주세요.");
        map.meta.resolution = root["resolution"].as<double>();
        const auto origin = root["origin"];
        if (!origin.IsSequence() || origin.size() != 3) fail("origin은 [x, y, yaw] 세 값이어야 합니다.");
        map.meta.x = origin[0].as<double>(); map.meta.y = origin[1].as<double>(); map.meta.yaw = origin[2].as<double>();
        map.meta.free = root["free_thresh"].as<double>(); map.meta.occupied = root["occupied_thresh"].as<double>();
        const auto neg = root["negate"].as<std::string>();
        if (neg == "true" || neg == "True" || neg == "TRUE") map.meta.negate = 1;
        else if (neg == "false" || neg == "False" || neg == "FALSE") map.meta.negate = 0;
        else map.meta.negate = root["negate"].as<int>();
        if (root["mode"]) map.meta.mode = QString::fromStdString(root["mode"].as<std::string>());
        const auto error = validate(map.meta, allowRaw);
        if (!error.isEmpty()) fail(error);
        map.image = readPgm(map.imagePath);
        map.yamlPath = yamlPath;
        map.hasYaml = true;
    } catch (const YAML::Exception &e) { fail(QString("YAML 형식 오류: %1").arg(QString::fromUtf8(e.what()))); }
    return map;
}
QByteArray encodeYaml(const Map &map, const QString &relativeImage, bool allowRaw) {
    const auto error = validate(map.meta, allowRaw);
    if (!error.isEmpty()) fail(error);
    auto root = map.extra.IsMap() ? YAML::Clone(map.extra) : YAML::Node(YAML::NodeType::Map);
    root["image"] = relativeImage.toUtf8().toStdString();
    root["resolution"] = map.meta.resolution;
    YAML::Node origin(YAML::NodeType::Sequence);
    origin.push_back(map.meta.x); origin.push_back(map.meta.y); origin.push_back(map.meta.yaw);
    origin.SetStyle(YAML::EmitterStyle::Flow);
    root["origin"] = origin;
    root["negate"] = map.meta.negate; root["free_thresh"] = map.meta.free; root["occupied_thresh"] = map.meta.occupied;
    root["mode"] = map.meta.mode.toStdString();
    YAML::Emitter out;
    out.SetDoublePrecision(std::numeric_limits<double>::max_digits10);
    out << root;
    if (!out.good()) fail("YAML 직렬화 실패");
    return QByteArray(out.c_str()) + "\n";
}
SaveResult savePair(const Map &map, const QString &yamlFile, const QString &imageFile, bool allowRaw) {
    const auto yamlPath = absolute(yamlFile);
    const QFileInfo yamlInfo(yamlPath);
    const auto suffix = yamlInfo.suffix().toLower();
    if (suffix != "yaml" && suffix != "yml") fail("저장할 YAML 파일명은 .yaml 또는 .yml로 끝나야 합니다.");
    const auto imagePath = imageFile.isEmpty() ? yamlInfo.dir().filePath(yamlInfo.completeBaseName() + ".pgm") : absolute(imageFile);
    if (QFileInfo(imagePath).suffix().compare("pgm", Qt::CaseInsensitive) != 0 || samePath(yamlPath, imagePath)) fail("PGM과 YAML은 서로 다른 경로여야 합니다.");
    const auto pgmBytes = encodePgm(map.image);
    const auto yamlBytes = encodeYaml(map, QDir::fromNativeSeparators(yamlInfo.dir().relativeFilePath(imagePath)), allowRaw);
    preflight(yamlPath); preflight(imagePath);
    // Prepare both files before touching either target.
    QSaveFile pgm(imagePath), yaml(yamlPath);
    pgm.setDirectWriteFallback(false); yaml.setDirectWriteFallback(false);
    if (!pgm.open(QIODevice::WriteOnly) || !yaml.open(QIODevice::WriteOnly) || pgm.write(pgmBytes) != pgmBytes.size() || yaml.write(yamlBytes) != yamlBytes.size())
        fail("임시 파일 기록에 실패했습니다. 기존 지도는 변경되지 않았습니다.");
    const auto backupDir = backup(yamlPath, imagePath);
    const bool imageExisted = QFile::exists(imagePath);
    if (!pgm.commit()) fail("PGM 저장에 실패했습니다. 기존 지도는 변경되지 않았습니다.");
    if (!yaml.commit()) {
        try {
            if (imageExisted) atomicWrite(imagePath, readFile(QDir(backupDir).filePath("original.pgm"), MaxPixels * 12 + 1024 * 1024));
            else if (!QFile::remove(imagePath)) fail("새 PGM 삭제 실패");
        } catch (const std::exception &) { fail("YAML 저장 및 PGM 복구에 실패했습니다. 백업에서 복원하세요: " + backupDir); }
        fail("YAML 저장에 실패하여 PGM을 원상복구했습니다. 백업: " + backupDir);
    }
    return {yamlPath, imagePath, backupDir};
}
double probability(int gray, const Metadata &m) { return m.negate ? gray / 255.0 : (255 - gray) / 255.0; }
int unknownGray(const Metadata &m) {
    if (probability(205, m) >= m.free && probability(205, m) <= m.occupied) return 205;
    const auto mid = (m.free + m.occupied) / 2;
    int best = 127; double distance = 2;
    for (int v = 1; v < 255; ++v) {
        auto p = probability(v, m);
        if (p >= m.free && p <= m.occupied && std::abs(p - mid) < distance) { best = v; distance = std::abs(p - mid); }
    }
    return best;
}
QImage trinary(const QImage &image, const Metadata &m) {
    auto result = image.copy();
    const int free = m.negate ? 0 : 255, occupied = m.negate ? 255 : 0, unknown = unknownGray(m);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const auto p = probability(image.constScanLine(y)[x], m);
        result.scanLine(y)[x] = uchar(p > m.occupied ? occupied : (p < m.free ? free : unknown));
    }
    return result;
}
QPointF pixelToWorld(QPointF p, int height, const Metadata &m) {
    const double a = p.x() * m.resolution, b = (height - p.y()) * m.resolution;
    return {m.x + a * std::cos(m.yaw) - b * std::sin(m.yaw), m.y + a * std::sin(m.yaw) + b * std::cos(m.yaw)};
}
QPointF worldToPixel(QPointF p, int height, const Metadata &m) {
    const auto d = p - QPointF(m.x, m.y);
    return {(d.x() * std::cos(m.yaw) + d.y() * std::sin(m.yaw)) / m.resolution,
            height - (-d.x() * std::sin(m.yaw) + d.y() * std::cos(m.yaw)) / m.resolution};
}
}
