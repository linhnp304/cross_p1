#include "appparams.h"

#include "appinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>

namespace {

QJsonObject endpointToJson(const NetEndpoint &e)
{
    QJsonObject o;
    o[QStringLiteral("name")]       = e.name;
    o[QStringLiteral("localIp")]    = e.localIp;
    o[QStringLiteral("remoteIp")]   = e.remoteIp;
    o[QStringLiteral("localPort")]  = int(e.localPort);
    o[QStringLiteral("remotePort")] = int(e.remotePort);
    // Ô "Gửi" cố ý không lưu — xem chú thích ở NetEndpoint::enabled.
    o[QStringLiteral("kind")]       = e.kind == TxKind::Track
                                          ? QStringLiteral("track")
                                          : QStringLiteral("plot");
    o[QStringLiteral("broadcast")]  = e.broadcast;
    return o;
}

NetEndpoint endpointFromJson(const QJsonObject &o)
{
    NetEndpoint e;
    e.name       = o.value(QStringLiteral("name")).toString();
    e.localIp    = o.value(QStringLiteral("localIp")).toString(e.localIp);
    e.remoteIp   = o.value(QStringLiteral("remoteIp")).toString(e.remoteIp);
    e.localPort  = quint16(qBound(0, o.value(QStringLiteral("localPort")).toInt(), 65535));
    e.remotePort = quint16(qBound(0, o.value(QStringLiteral("remotePort")).toInt(), 65535));
    e.kind       = o.value(QStringLiteral("kind")).toString() == QLatin1String("track")
                       ? TxKind::Track : TxKind::Plot;
    e.broadcast  = o.value(QStringLiteral("broadcast")).toBool(false);
    return e;
}

QJsonArray endpointsToJson(const QVector<NetEndpoint> &list)
{
    QJsonArray a;
    for (const NetEndpoint &e : list)
        a.append(endpointToJson(e));
    return a;
}

QVector<NetEndpoint> endpointsFromJson(const QJsonArray &a)
{
    QVector<NetEndpoint> list;
    list.reserve(a.size());
    for (const QJsonValue &v : a) {
        if (v.isObject())
            list.push_back(endpointFromJson(v.toObject()));
    }
    return list;
}

// --- tham số tính tâm chùm xung ---

QJsonObject beamToJson(const BeamParams &b)
{
    QJsonObject o;
    o[QStringLiteral("cxMin")]        = b.cxMin;
    o[QStringLiteral("cxMax")]        = b.cxMax;
    o[QStringLiteral("doplerMin")]    = b.doplerMin;
    o[QStringLiteral("doplerMax")]    = b.doplerMax;
    o[QStringLiteral("deltaRange")]   = b.deltaRange;
    o[QStringLiteral("deltaDopler")]  = b.deltaDopler;
    o[QStringLiteral("numWait")]      = b.numWait;
    o[QStringLiteral("numLose")]      = b.numLose;
    o[QStringLiteral("useAmplitude")] = b.useAmplitude;
    o[QStringLiteral("showSec")]      = b.showSec;
    return o;
}

void beamFromJson(const QJsonObject &o, BeamParams &b)
{
    b.cxMin        = o.value(QStringLiteral("cxMin")).toInt(b.cxMin);
    b.cxMax        = o.value(QStringLiteral("cxMax")).toInt(b.cxMax);
    b.doplerMin    = o.value(QStringLiteral("doplerMin")).toInt(b.doplerMin);
    b.doplerMax    = o.value(QStringLiteral("doplerMax")).toInt(b.doplerMax);
    b.deltaRange   = o.value(QStringLiteral("deltaRange")).toInt(b.deltaRange);
    b.deltaDopler  = o.value(QStringLiteral("deltaDopler")).toInt(b.deltaDopler);
    b.numWait      = o.value(QStringLiteral("numWait")).toInt(b.numWait);
    b.numLose      = o.value(QStringLiteral("numLose")).toInt(b.numLose);
    b.useAmplitude = o.value(QStringLiteral("useAmplitude")).toBool(b.useAmplitude);
    b.showSec      = o.value(QStringLiteral("showSec")).toInt(b.showSec);
    b.clamp();
}

// --- tham số bám quỹ đạo ---

QString initRuleToString(TrackInitRule r)
{
    switch (r) {
    case TrackInitRule::R2of2: return QStringLiteral("2/2");
    case TrackInitRule::R3of3: return QStringLiteral("3/3");
    case TrackInitRule::R2of3: break;
    }
    return QStringLiteral("2/3");
}

TrackInitRule initRuleFromString(const QString &s, TrackInitRule fallback)
{
    if (s == QLatin1String("2/2")) return TrackInitRule::R2of2;
    if (s == QLatin1String("3/3")) return TrackInitRule::R3of3;
    if (s == QLatin1String("2/3")) return TrackInitRule::R2of3;
    return fallback;
}

QJsonObject trackToJson(const TrackParams &t)
{
    QJsonObject o;
    o[QStringLiteral("initRule")]    = initRuleToString(t.initRule);
    o[QStringLiteral("coastScans")]  = t.coastScans;
    o[QStringLiteral("vMin")]        = t.vMin;
    o[QStringLiteral("vMax")]        = t.vMax;
    o[QStringLiteral("gateRangeM")]  = t.gateRangeM;
    o[QStringLiteral("gateAzmDeg")]  = t.gateAzmDeg;
    o[QStringLiteral("gateSigma")]   = t.gateSigma;
    o[QStringLiteral("gateMaxM")]    = t.gateMaxM;
    o[QStringLiteral("sigmaRangeM")] = t.sigmaRangeM;
    o[QStringLiteral("sigmaAzmDeg")] = t.sigmaAzmDeg;
    o[QStringLiteral("sigmaAccel")]  = t.sigmaAccel;
    o[QStringLiteral("staleSec")]    = t.staleSec;
    o[QStringLiteral("drawWindow")]  = t.drawWindow;
    // Hai chốt an toàn: chỉ có ở đây, không đưa lên giao diện.
    o[QStringLiteral("maxTracks")]     = t.maxTracks;
    o[QStringLiteral("minInitRangeM")] = t.minInitRangeM;
    return o;
}

void trackFromJson(const QJsonObject &o, TrackParams &t)
{
    t.initRule    = initRuleFromString(o.value(QStringLiteral("initRule")).toString(),
                                       t.initRule);
    t.coastScans  = o.value(QStringLiteral("coastScans")).toInt(t.coastScans);
    t.vMin        = o.value(QStringLiteral("vMin")).toDouble(t.vMin);
    t.vMax        = o.value(QStringLiteral("vMax")).toDouble(t.vMax);
    t.gateRangeM  = o.value(QStringLiteral("gateRangeM")).toDouble(t.gateRangeM);
    t.gateAzmDeg  = o.value(QStringLiteral("gateAzmDeg")).toDouble(t.gateAzmDeg);
    t.gateSigma   = o.value(QStringLiteral("gateSigma")).toDouble(t.gateSigma);
    t.gateMaxM    = o.value(QStringLiteral("gateMaxM")).toDouble(t.gateMaxM);
    t.sigmaRangeM = o.value(QStringLiteral("sigmaRangeM")).toDouble(t.sigmaRangeM);
    t.sigmaAzmDeg = o.value(QStringLiteral("sigmaAzmDeg")).toDouble(t.sigmaAzmDeg);
    t.sigmaAccel  = o.value(QStringLiteral("sigmaAccel")).toDouble(t.sigmaAccel);
    t.staleSec    = o.value(QStringLiteral("staleSec")).toInt(t.staleSec);
    t.drawWindow  = o.value(QStringLiteral("drawWindow")).toBool(t.drawWindow);
    t.maxTracks     = o.value(QStringLiteral("maxTracks")).toInt(t.maxTracks);
    t.minInitRangeM = o.value(QStringLiteral("minInitRangeM")).toDouble(t.minInitRangeM);
    t.clamp();
}

} // namespace

bool NetEndpoint::acceptsAnyHost() const
{
    return remoteIp.trimmed().isEmpty()
        || remoteIp.trimmed() == QLatin1String("0.0.0.0");
}

double AppParams::rmaxMeters(double fs, int b, int tc)
{
    if (b <= 0)
        return 0.0;
    return 75.0 * fs * tc / b;
}

double AppParams::rmaxKm() const
{
    // Làm tròn tới mét trước rồi mới đổi sang km, để giá trị khớp đúng với ô
    // nhập cự ly tối đa (3 chữ số thập phân).
    return std::round(rmaxMeters()) / 1000.0;
}

QVector<NetEndpoint> AppParams::defaultRx()
{
    // Cổng remote để 0 = nhận từ bất kỳ cổng nguồn nào; công cụ tạo giả dữ liệu
    // gửi đi từ cổng ngẫu nhiên nên nếu chốt cổng ở đây là không nhận được gì.
    return {
        {QStringLiteral("UDP-RAW_V"),  QStringLiteral("127.0.0.1"),
         QStringLiteral("127.0.0.1"), 6001, 0},
        {QStringLiteral("UDP-RAW_P"),  QStringLiteral("127.0.0.1"),
         QStringLiteral("127.0.0.1"), 6002, 0},
        {QStringLiteral("UDP-STATUS"), QStringLiteral("127.0.0.1"),
         QStringLiteral("127.0.0.1"), 6003, 0},
    };
}

QString AppParams::filePath()
{
    return QCoreApplication::applicationDirPath() + QLatin1Char('/')
         + appinfo::paramsFileName();
}

void AppParams::clampToRange()
{
    fs = qBound(kFsMin, fs, kFsMax);
    b  = qBound(kBMin,  b,  kBMax);
    tc = qBound(kTcMin, tc, kTcMax);
    if (zfbeat == 0)
        zfbeat = 1;   // là số chia, không được bằng 0

    // Ràng buộc của giao diện: nhận tự động thì luôn kèm tự cập nhật thang cự ly.
    if (autoFromStatus)
        autoRange = true;

    sectorStart = qBound(0.0, sectorStart, 360.0);
    sectorStop  = qBound(0.0, sectorStop,  360.0);

    beam.clamp();
    track.clamp();
}

bool AppParams::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;

    const QJsonObject o = doc.object();
    fs     = o.value(QStringLiteral("fs")).toDouble(fs);
    b      = o.value(QStringLiteral("b")).toInt(b);
    tc     = o.value(QStringLiteral("tc")).toInt(tc);
    // ZFbeat toàn dải unsigned int nên qua double mới không mất bit — JSON
    // không có kiểu số nguyên 32 bit không dấu.
    zfbeat = quint32(qBound(1.0, o.value(QStringLiteral("zfbeat")).toDouble(zfbeat),
                            4294967295.0));

    autoFromStatus = o.value(QStringLiteral("autoFromStatus")).toBool(autoFromStatus);
    autoRange      = o.value(QStringLiteral("autoRange")).toBool(autoRange);

    sectorOn    = o.value(QStringLiteral("sectorOn")).toBool(sectorOn);
    sectorStart = o.value(QStringLiteral("sectorStart")).toDouble(sectorStart);
    sectorStop  = o.value(QStringLiteral("sectorStop")).toDouble(sectorStop);

    beamFromJson(o.value(QStringLiteral("beam")).toObject(), beam);
    trackFromJson(o.value(QStringLiteral("track")).toObject(), track);

    if (o.value(QStringLiteral("rx")).isArray())
        rx = endpointsFromJson(o.value(QStringLiteral("rx")).toArray());
    if (o.value(QStringLiteral("tx")).isArray())
        tx = endpointsFromJson(o.value(QStringLiteral("tx")).toArray());

    clampToRange();
    return true;
}

bool AppParams::save() const
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject o;
    o[QStringLiteral("fs")]             = fs;
    o[QStringLiteral("b")]              = b;
    o[QStringLiteral("tc")]             = tc;
    o[QStringLiteral("zfbeat")]         = double(zfbeat);
    o[QStringLiteral("autoFromStatus")] = autoFromStatus;
    o[QStringLiteral("autoRange")]      = autoRange;
    o[QStringLiteral("sectorOn")]       = sectorOn;
    o[QStringLiteral("sectorStart")]    = sectorStart;
    o[QStringLiteral("sectorStop")]     = sectorStop;
    o[QStringLiteral("beam")]           = beamToJson(beam);
    o[QStringLiteral("track")]          = trackToJson(track);
    o[QStringLiteral("rx")]             = endpointsToJson(rx);
    o[QStringLiteral("tx")]             = endpointsToJson(tx);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}
