#include "app/appparams.h"

#include "app/appinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace {

QString kindToString(TxKind k)
{
    switch (k) {
    case TxKind::Track:    return QStringLiteral("track");
    case TxKind::Command:  return QStringLiteral("command");
    case TxKind::CtrlSync: return QStringLiteral("ctrlsync");
    case TxKind::Plot:     break;
    }
    return QStringLiteral("plot");
}

TxKind kindFromString(const QString &s)
{
    if (s == QLatin1String("track"))    return TxKind::Track;
    if (s == QLatin1String("command"))  return TxKind::Command;
    if (s == QLatin1String("ctrlsync")) return TxKind::CtrlSync;
    return TxKind::Plot;
}

QJsonObject endpointToJson(const NetEndpoint &e)
{
    QJsonObject o;
    o[QStringLiteral("name")]       = e.name;
    o[QStringLiteral("localIp")]    = e.localIp;
    o[QStringLiteral("remoteIp")]   = e.remoteIp;
    o[QStringLiteral("localPort")]  = int(e.localPort);
    o[QStringLiteral("remotePort")] = int(e.remotePort);
    o[QStringLiteral("kind")]       = kindToString(e.kind);
    o[QStringLiteral("broadcast")]  = e.broadcast;
    // Ô "Gửi" cố ý không lưu — xem chú thích ở NetEndpoint::enabled.
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
    e.kind       = kindFromString(o.value(QStringLiteral("kind")).toString());
    e.broadcast  = o.value(QStringLiteral("broadcast")).toBool(false);
    e.enabled    = e.alwaysSends();
    return e;
}

// --- giá trị các lệnh điều khiển -------------------------------------------

/// Lưu theo **tên trường** chứ không theo thứ tự: file cũ vẫn đọc được nguyên
/// vẹn khi bảng trường thêm bớt, và mở file ra là đọc hiểu được.
QJsonObject controlToJson(const cmdproto::Values &c)
{
    QJsonObject root;
    for (int g = 0; g < cmdproto::GroupCount; ++g) {
        const cmdproto::Packet &p = cmdproto::kPackets[g];
        QJsonObject o;
        for (int i = 0; i < p.fieldCount; ++i) {
            if (!p.fields[i].sends())
                continue;   // trường chỉ nhận trạng thái, không có gì để lưu
            // Qua double vì JSON không có kiểu số nguyên 32 bit không dấu.
            o[QString::fromLatin1(p.fields[i].name)] = double(c.v[g][i]);
        }
        root[QString::fromLatin1(p.key)] = o;
    }
    return root;
}

void controlFromJson(const QJsonObject &root, cmdproto::Values &c)
{
    for (int g = 0; g < cmdproto::GroupCount; ++g) {
        const cmdproto::Packet &p = cmdproto::kPackets[g];
        const QJsonObject o = root.value(QString::fromLatin1(p.key)).toObject();
        for (int i = 0; i < p.fieldCount; ++i) {
            const cmdproto::Field &f = p.fields[i];
            const QJsonValue v = o.value(QString::fromLatin1(f.name));
            if (!v.isDouble())
                continue;   // thiếu khoá thì giữ nguyên giá trị mặc định
            // Đi vòng qua thang giao diện để giá trị sửa tay ngoài dải bị kẹp
            // đúng như khi gõ vào ô nhập.
            c.v[g][i] = cmdproto::fromUi(f, cmdproto::toUi(f, quint32(v.toDouble())));
        }
    }
}

// --- cấu hình kit ADF4159 ---------------------------------------------------
//
// Bảng tên khoá nằm ở adf4159::visit(), không chép lại ở đây: bốn chục trường mà
// giữ khớp hai danh sách bằng mắt thì sớm muộn cũng lệch một cái, mà lệch thì
// chỉ hiện ra ở chỗ "mở phần mềm lên thấy sai đúng một ô".

QJsonObject adfToJson(const adf4159::Settings &s)
{
    QJsonObject o;
    // visit() nhận tham chiếu sửa được nên phải có một bản chép; hàm này không
    // đụng gì vào giá trị nên bản chép ấy về nguyên vẹn.
    adf4159::Settings copy = s;
    adf4159::visit(copy, [&o](const char *name, auto &value) {
        o[QString::fromLatin1(name)] = QJsonValue(value);
    });
    return o;
}

void adfFromJson(const QJsonObject &o, adf4159::Settings &s)
{
    adf4159::visit(s, [&o](const char *name, auto &value) {
        const QJsonValue v = o.value(QString::fromLatin1(name));
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
            if (v.isBool())
                value = v.toBool();
        } else if constexpr (std::is_same_v<T, int>) {
            if (v.isDouble())
                value = v.toInt();
        } else {
            if (v.isDouble())
                value = v.toDouble();
        }
        // Thiếu khoá thì giữ nguyên giá trị mặc định.
    });
    s.clamp();
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

// --- rẻ quạt xử lý và vùng cấm khởi tạo ---

QJsonArray sectorsToJson(const QVector<Sector> &list)
{
    QJsonArray a;
    for (const Sector &s : list) {
        QJsonObject o;
        o[QStringLiteral("on")]    = s.on;
        o[QStringLiteral("start")] = s.startDeg;
        o[QStringLiteral("stop")]  = s.stopDeg;
        a.append(o);
    }
    return a;
}

QVector<Sector> sectorsFromJson(const QJsonArray &a)
{
    QVector<Sector> list;
    list.reserve(a.size());
    for (const QJsonValue &v : a) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        Sector s;
        s.on       = o.value(QStringLiteral("on")).toBool(false);
        s.startDeg = qBound(0.0, o.value(QStringLiteral("start")).toDouble(0.0), 360.0);
        s.stopDeg  = qBound(0.0, o.value(QStringLiteral("stop")).toDouble(360.0), 360.0);
        list.push_back(s);
    }
    return list;
}

QJsonArray zonesToJson(const QVector<NoInitZone> &list)
{
    QJsonArray a;
    for (const NoInitZone &z : list) {
        QJsonObject o;
        o[QStringLiteral("on")]     = z.on;
        o[QStringLiteral("azm1")]   = z.azm1;
        o[QStringLiteral("azm2")]   = z.azm2;
        o[QStringLiteral("range1")] = z.range1;
        o[QStringLiteral("range2")] = z.range2;
        a.append(o);
    }
    return a;
}

QVector<NoInitZone> zonesFromJson(const QJsonArray &a)
{
    QVector<NoInitZone> list;
    list.reserve(a.size());
    for (const QJsonValue &v : a) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        NoInitZone z;
        z.on     = o.value(QStringLiteral("on")).toBool(false);
        z.azm1   = qBound(0.0, o.value(QStringLiteral("azm1")).toDouble(0.0), 360.0);
        z.azm2   = qBound(0.0, o.value(QStringLiteral("azm2")).toDouble(0.0), 360.0);
        z.range1 = qMax(0.0, o.value(QStringLiteral("range1")).toDouble(0.0));
        z.range2 = qMax(0.0, o.value(QStringLiteral("range2")).toDouble(0.0));
        // File sửa tay có thể để cự ly ngược; bảng trên giao diện luôn giữ
        // range1 <= range2 nên chuẩn hoá luôn ở đây.
        if (z.range1 > z.range2)
            std::swap(z.range1, z.range2);
        list.push_back(z);
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
    o[QStringLiteral("azmOffsetDeg")] = b.azmOffsetDeg;
    o[QStringLiteral("rangeOffsetM")] = b.rangeOffsetM;
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
    b.azmOffsetDeg = o.value(QStringLiteral("azmOffsetDeg")).toDouble(b.azmOffsetDeg);
    b.rangeOffsetM = o.value(QStringLiteral("rangeOffsetM")).toDouble(b.rangeOffsetM);
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

rawpkt::VideoScale AppParams::videoScale() const
{
    // DataSend nói dùng công thức nào, ZFbeat / GainU là số chia của công thức
    // đó — cả ba nằm trong ba gói lệnh điều khiển khác nhau. Chỉ Multi_V là
    // tham số của riêng phần mềm này.
    rawpkt::VideoScale s;
    s.dataSend = control.byName(cmdproto::GroupCommon, "DataSend", 1);
    s.zfbeat   = control.byName(cmdproto::GroupDspR, "ZFbeat", 32768);
    s.gainU    = control.byName(cmdproto::GroupDspS, "GainU", 32768);
    s.multiV   = multiV;
    return s;
}

QStringList AppParams::rxNames()
{
    return {QStringLiteral("RAW_V"), QStringLiteral("RAW_P"),
            statusRowName(), plotRowName(), ctrlSyncRowName()};
}

QString AppParams::statusRowName()
{
    return QStringLiteral("Status");
}

QString AppParams::plotRowName()
{
    return QStringLiteral("Plot");
}

QString AppParams::ctrlSyncRowName()
{
    return QStringLiteral("CtrlSync_R");
}

bool AppParams::isStatusRow(const NetEndpoint &e)
{
    // Không phân biệt hoa thường: file params.json sửa tay có thể ghi "status",
    // mà đó vẫn là chính dòng ấy.
    return e.name.trimmed().compare(statusRowName(), Qt::CaseInsensitive) == 0;
}

bool AppParams::isCtrlSyncName(const QString &name)
{
    return name.trimmed().compare(ctrlSyncRowName(), Qt::CaseInsensitive) == 0;
}

bool AppParams::isCtrlSyncRow(const NetEndpoint &e)
{
    return isCtrlSyncName(e.name);
}

QString AppParams::statusLocalIp() const
{
    for (const NetEndpoint &e : rx) {
        if (isStatusRow(e) && !e.localIp.trimmed().isEmpty())
            return e.localIp.trimmed();
    }
    return fallbackLocalIp();
}

QString AppParams::ctrlSyncLocalIp() const
{
    for (const NetEndpoint &e : rx) {
        if (isCtrlSyncRow(e))
            return e.localIp.trimmed();
    }
    return {};
}

QString AppParams::ctrlSyncTxLocalIp() const
{
    for (const NetEndpoint &e : tx) {
        if (e.kind == TxKind::CtrlSync)
            return e.localIp.trimmed();
    }
    return {};
}

QStringList AppParams::txKindNames()
{
    return {QStringLiteral("Plot"), QStringLiteral("Track"),
            QStringLiteral("Command"), QStringLiteral("CtrlSync_S")};
}

QVector<NetEndpoint> AppParams::defaultTx()
{
    NetEndpoint e;
    // Card ra để trống = theo bảng định tuyến của hệ điều hành, giống dòng do
    // người dùng tự thêm.
    e.localIp    = QString();
    e.localPort  = 0;
    e.kind       = TxKind::Command;
    e.remotePort = 6103;
    e.enabled    = true;   // dòng Command luôn gửi
    return {e};
}

QVector<NetEndpoint> AppParams::defaultRx()
{
    // Ba dòng đầu của rxNames(); "Plot" cố ý không tạo sẵn — hệ thống thật
    // không gửi điểm dấu tới đây, chỉ công cụ kiểm tra bộ lọc Kalman mới dùng.
    //
    // Cổng remote để 0 = nhận từ bất kỳ cổng nguồn nào; công cụ tạo giả dữ liệu
    // gửi đi từ cổng ngẫu nhiên nên nếu chốt cổng ở đây là không nhận được gì.
    const QStringList names = rxNames();
    return {
        {names.at(0), QStringLiteral("127.0.0.1"),
         QStringLiteral("127.0.0.1"), 6001, 0},
        {names.at(1), QStringLiteral("127.0.0.1"),
         QStringLiteral("127.0.0.1"), 6002, 0},
        {names.at(2), QStringLiteral("127.0.0.1"),
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
    fs     = qBound(kFsMin, fs, kFsMax);
    b      = qBound(kBMin,  b,  kBMax);
    tc     = qBound(kTcMin, tc, kTcMax);
    multiV = qBound(kMultiVMin, multiV, kMultiVMax);

    // Ràng buộc của giao diện: nhận tự động thì luôn kèm tự cập nhật thang cự ly.
    if (autoFromStatus)
        autoRange = true;

    // Bảng rẻ quạt rỗng thì dựng lại một dòng cả vòng tròn: bảng không có dòng
    // nào thì không thêm được dòng mới bằng cách nào khác ngoài sửa file.
    if (sectors.isEmpty())
        sectors = defaultSectors();
    for (Sector &s : sectors) {
        s.startDeg = qBound(0.0, s.startDeg, 360.0);
        s.stopDeg  = qBound(0.0, s.stopDeg,  360.0);
    }

    for (NoInitZone &z : noInitZones) {
        z.azm1   = qBound(0.0, z.azm1, 360.0);
        z.azm2   = qBound(0.0, z.azm2, 360.0);
        z.range1 = qMax(0.0, z.range1);
        z.range2 = qMax(0.0, z.range2);
        if (z.range1 > z.range2)
            std::swap(z.range1, z.range2);
    }

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
    multiV = o.value(QStringLiteral("multiV")).toDouble(multiV);

    controlFromJson(o.value(QStringLiteral("control")).toObject(), control);
    adfFromJson(o.value(QStringLiteral("adf4159")).toObject(), adf);

    // File của giai đoạn trước để ZFbeat ngay trong tham số kỹ thuật. Nay giá
    // trị đó thuộc về lệnh CMD_DSP_R, nên chuyển sang chỗ mới thay vì bỏ đi —
    // người lắp đặt đã căn nó theo đúng đài của mình rồi.
    if (const QJsonValue old = o.value(QStringLiteral("zfbeat")); old.isDouble()) {
        const int at = cmdproto::indexOf(cmdproto::kPackets[cmdproto::GroupDspR],
                                         "ZFbeat");
        if (at >= 0 && !o.contains(QStringLiteral("control")))
            control.v[cmdproto::GroupDspR][at] =
                quint32(qBound(1.0, old.toDouble(), 4294967295.0));
    }

    autoFromStatus = o.value(QStringLiteral("autoFromStatus")).toBool(autoFromStatus);
    autoRange      = o.value(QStringLiteral("autoRange")).toBool(autoRange);

    if (o.value(QStringLiteral("sectors")).isArray()) {
        sectors = sectorsFromJson(o.value(QStringLiteral("sectors")).toArray());
    } else if (o.contains(QStringLiteral("sectorOn"))) {
        // Bố cục cũ: một rẻ quạt duy nhất nằm thẳng trong ba khoá này. Chuyển
        // thành dòng đầu của bảng thay vì bỏ đi — người lắp đặt đã chỉnh nó rồi.
        Sector s;
        s.on       = o.value(QStringLiteral("sectorOn")).toBool(false);
        s.startDeg = o.value(QStringLiteral("sectorStart")).toDouble(0.0);
        s.stopDeg  = o.value(QStringLiteral("sectorStop")).toDouble(360.0);
        sectors    = {s};
    }

    noInitZones = zonesFromJson(o.value(QStringLiteral("noInitZones")).toArray());
    showNoInitZones = o.value(QStringLiteral("showNoInitZones"))
                          .toBool(showNoInitZones);

    beamFromJson(o.value(QStringLiteral("beam")).toObject(), beam);
    trackFromJson(o.value(QStringLiteral("track")).toObject(), track);

    if (o.value(QStringLiteral("rx")).isArray())
        rx = endpointsFromJson(o.value(QStringLiteral("rx")).toArray());
    if (o.value(QStringLiteral("tx")).isArray())
        tx = endpointsFromJson(o.value(QStringLiteral("tx")).toArray());
    statusFollowsCommand = o.value(QStringLiteral("statusFollowsCommand"))
                               .toBool(statusFollowsCommand);

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
    o[QStringLiteral("multiV")]         = multiV;
    o[QStringLiteral("control")]        = controlToJson(control);
    o[QStringLiteral("adf4159")]        = adfToJson(adf);
    o[QStringLiteral("autoFromStatus")] = autoFromStatus;
    o[QStringLiteral("autoRange")]      = autoRange;
    o[QStringLiteral("sectors")]        = sectorsToJson(sectors);
    o[QStringLiteral("noInitZones")]    = zonesToJson(noInitZones);
    o[QStringLiteral("showNoInitZones")] = showNoInitZones;
    o[QStringLiteral("beam")]           = beamToJson(beam);
    o[QStringLiteral("track")]          = trackToJson(track);
    o[QStringLiteral("rx")]             = endpointsToJson(rx);
    o[QStringLiteral("tx")]             = endpointsToJson(tx);
    o[QStringLiteral("statusFollowsCommand")] = statusFollowsCommand;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}
