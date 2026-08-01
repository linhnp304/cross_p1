#include "appsettings.h"

#include "appinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

QString ringToString(RingMode m)
{
    switch (m) {
    case RingMode::R5:  return QStringLiteral("5km");
    case RingMode::R1:  return QStringLiteral("1km");
    case RingMode::R05: return QStringLiteral("0.5km");
    case RingMode::R01: return QStringLiteral("0.1km");
    case RingMode::Off: break;
    }
    return QStringLiteral("off");
}

RingMode ringFromString(const QString &s, RingMode fallback)
{
    if (s == QLatin1String("5km"))   return RingMode::R5;
    if (s == QLatin1String("1km"))   return RingMode::R1;
    if (s == QLatin1String("0.5km")) return RingMode::R05;
    if (s == QLatin1String("0.1km")) return RingMode::R01;
    if (s == QLatin1String("off"))   return RingMode::Off;
    return fallback;
}

QString azimuthToString(AzimuthMode m)
{
    switch (m) {
    case AzimuthMode::A30: return QStringLiteral("30");
    case AzimuthMode::A10: return QStringLiteral("10");
    case AzimuthMode::A5:  return QStringLiteral("5");
    case AzimuthMode::Off: break;
    }
    return QStringLiteral("off");
}

AzimuthMode azimuthFromString(const QString &s, AzimuthMode fallback)
{
    if (s == QLatin1String("30"))  return AzimuthMode::A30;
    if (s == QLatin1String("10"))  return AzimuthMode::A10;
    if (s == QLatin1String("5"))   return AzimuthMode::A5;
    if (s == QLatin1String("off")) return AzimuthMode::Off;
    return fallback;
}

/// Thư mục gốc hợp lệ là thư mục có ít nhất một thư mục con chứa tileset.json.
/// Kiểm tra chặt như vậy để lúc dò ngược lên thư mục cha không vớ phải một
/// thư mục "maps/mt" rỗng nằm sẵn đâu đó.
bool looksLikeTileBase(const QString &dir)
{
    const QDir d(dir);
    if (!d.exists())
        return false;
    const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &s : subs) {
        if (QFileInfo::exists(d.filePath(s) + QStringLiteral("/tileset.json")))
            return true;
    }
    return false;
}

/// Thư mục bản đồ TC hợp lệ là thư mục có lớp nền tỉnh/thành phố.
bool looksLikeVectorDir(const QString &dir)
{
    return QFileInfo::exists(QDir(dir).filePath(QStringLiteral("VNM_adm1.shp")));
}

/// Đường dẫn tuyệt đối thì dùng nguyên; tương đối thì tính từ thư mục chứa file
/// chạy. Lúc phát triển, file chạy nằm trong build/ còn dữ liệu để ở gốc repo,
/// nên dò thêm vài cấp cha. Không thấy chỗ nào hợp lệ thì trả về vị trí chuẩn
/// (ngay cạnh file chạy) để thông báo lỗi chỉ đúng nơi cần đặt dữ liệu.
QString resolveDataDir(const QString &want, bool (*looksRight)(const QString &))
{
    if (QDir::isAbsolutePath(want))
        return QDir::cleanPath(want);

    const QString appDir = QCoreApplication::applicationDirPath();
    QString firstGuess;
    for (const char *up : {"", "/..", "/../..", "/../../.."}) {
        const QString p = QDir::cleanPath(appDir + QLatin1String(up)
                                          + QLatin1Char('/') + want);
        if (firstGuess.isEmpty())
            firstGuess = p;
        if (looksRight(p))
            return p;
    }
    return firstGuess;
}

} // namespace

QStringList AppSettings::defaultClassifyNames()
{
    // Danh sách mẫu theo mô tả giai đoạn. Người dùng sửa thẳng trong file cấu
    // hình; thứ tự trong danh sách chính là giá trị track_classify.
    return {
        QStringLiteral("Phantom 1"),
        QStringLiteral("Phantom 2"),
        QStringLiteral("Phantom 3"),
        QStringLiteral("Phantom 4"),
        QStringLiteral("Phantom 5"),
    };
}

QString AppSettings::classifyName(quint32 classify) const
{
    if (classify == 0 || classify > quint32(classifyNames.size()))
        return {};
    return classifyNames.at(int(classify) - 1);
}

QString AppSettings::filePath()
{
    return QCoreApplication::applicationDirPath() + QLatin1Char('/')
         + appinfo::configFileName();
}

QString AppSettings::resolvedTilesDir() const
{
    const QByteArray env = qgetenv(appinfo::tilesDirEnvVar());
    if (!env.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(env));

    const QString want = tilesDir.isEmpty() ? QStringLiteral("maps/mt") : tilesDir;
    return resolveDataDir(want, looksLikeTileBase);
}

QString AppSettings::resolvedStyleDir() const
{
    return QDir::cleanPath(resolvedTilesDir() + QLatin1Char('/') + mapStyle);
}

QString AppSettings::resolvedVectorDir() const
{
    const QString want = vectorDir.isEmpty() ? QStringLiteral("maps/tc") : vectorDir;
    return resolveDataDir(want, looksLikeVectorDir);
}

bool AppSettings::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;

    const QJsonObject o = doc.object();
    mapVisible    = o.value(QStringLiteral("mapVisible")).toBool(mapVisible);
    mapBrightness = o.value(QStringLiteral("mapBrightness")).toInt(mapBrightness);
    siteLat       = o.value(QStringLiteral("siteLat")).toDouble(siteLat);
    siteLng       = o.value(QStringLiteral("siteLng")).toDouble(siteLng);
    maxRangeKm    = o.value(QStringLiteral("maxRangeKm")).toDouble(maxRangeKm);
    ringMode      = ringFromString(o.value(QStringLiteral("ringMode")).toString(),
                                   ringMode);
    azimuthMode   = azimuthFromString(o.value(QStringLiteral("azimuthMode")).toString(),
                                      azimuthMode);
    videoFadeSec  = o.value(QStringLiteral("videoFadeSec")).toInt(videoFadeSec);
    tilesDir      = o.value(QStringLiteral("tilesDir")).toString(tilesDir);
    mapStyle      = o.value(QStringLiteral("mapStyle")).toString(mapStyle);
    vectorDir     = o.value(QStringLiteral("vectorDir")).toString(vectorDir);
    tcAirRoutes   = o.value(QStringLiteral("tcAirRoutes")).toBool(tcAirRoutes);
    tcAirports    = o.value(QStringLiteral("tcAirports")).toBool(tcAirports);
    tcRivers      = o.value(QStringLiteral("tcRivers")).toBool(tcRivers);
    tcPlaceNames  = o.value(QStringLiteral("tcPlaceNames")).toBool(tcPlaceNames);
    tcProvinces   = o.value(QStringLiteral("tcProvinces")).toBool(tcProvinces);

    showTracks      = o.value(QStringLiteral("showTracks")).toBool(showTracks);
    showTrackInfo   = o.value(QStringLiteral("showTrackInfo")).toBool(showTrackInfo);
    showPlots       = o.value(QStringLiteral("showPlots")).toBool(showPlots);
    showPlotInfo    = o.value(QStringLiteral("showPlotInfo")).toBool(showPlotInfo);
    trackHistory    = o.value(QStringLiteral("trackHistory")).toInt(trackHistory);

    colors.fromJson(o.value(QStringLiteral("colors")).toObject());

    // Danh sách rỗng trong file (người dùng xoá sạch) thì giữ danh sách mặc
    // định — không có tên phân loại nào thì menu "Nhận dạng" thành menu trống.
    if (const QJsonArray a = o.value(QStringLiteral("classifyNames")).toArray(); !a.isEmpty()) {
        QStringList names;
        for (const QJsonValue &v : a) {
            if (const QString s = v.toString().trimmed(); !s.isEmpty())
                names << s;
        }
        if (!names.isEmpty())
            classifyNames = names;
    }

    // Chuyển đổi từ bố cục cũ (một bộ tile duy nhất ở maps/mt/tiles) sang bố
    // cục nhiều kiểu nền (maps/mt/<style>/). Chỉ đụng đúng giá trị mặc định cũ
    // mà phần mềm từng ghi ra, không động vào đường dẫn người dùng tự đặt.
    if (tilesDir == QLatin1String("maps/mt/tiles"))
        tilesDir = QStringLiteral("maps/mt");

    // Chặn giá trị vô lý từ file bị sửa tay.
    mapBrightness = qBound(0, mapBrightness, 100);
    siteLat       = qBound(-85.0, siteLat, 85.0);
    siteLng       = qBound(-180.0, siteLng, 180.0);
    maxRangeKm    = qBound(kMinRangeKm, maxRangeKm, kMaxRangeKm);
    videoFadeSec  = qBound(0, videoFadeSec, 10);
    trackHistory  = qBound(0, trackHistory, kMaxHistory);
    return true;
}

bool AppSettings::save() const
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject o;
    o[QStringLiteral("mapVisible")]    = mapVisible;
    o[QStringLiteral("mapBrightness")] = mapBrightness;
    o[QStringLiteral("siteLat")]       = siteLat;
    o[QStringLiteral("siteLng")]       = siteLng;
    o[QStringLiteral("maxRangeKm")]    = maxRangeKm;
    o[QStringLiteral("ringMode")]      = ringToString(ringMode);
    o[QStringLiteral("azimuthMode")]   = azimuthToString(azimuthMode);
    o[QStringLiteral("videoFadeSec")]  = videoFadeSec;
    o[QStringLiteral("tilesDir")]      = tilesDir;
    o[QStringLiteral("mapStyle")]      = mapStyle;
    o[QStringLiteral("vectorDir")]     = vectorDir;
    o[QStringLiteral("tcAirRoutes")]   = tcAirRoutes;
    o[QStringLiteral("tcAirports")]    = tcAirports;
    o[QStringLiteral("tcRivers")]      = tcRivers;
    o[QStringLiteral("tcPlaceNames")]  = tcPlaceNames;
    o[QStringLiteral("tcProvinces")]   = tcProvinces;

    o[QStringLiteral("showTracks")]      = showTracks;
    o[QStringLiteral("showTrackInfo")]   = showTrackInfo;
    o[QStringLiteral("showPlots")]       = showPlots;
    o[QStringLiteral("showPlotInfo")]    = showPlotInfo;
    o[QStringLiteral("trackHistory")]    = trackHistory;
    o[QStringLiteral("colors")]          = colors.toJson();
    o[QStringLiteral("classifyNames")]   = QJsonArray::fromStringList(classifyNames);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}
