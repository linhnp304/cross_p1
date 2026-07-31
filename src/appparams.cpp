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
    o[QStringLiteral("rx")]             = endpointsToJson(rx);
    o[QStringLiteral("tx")]             = endpointsToJson(tx);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}
