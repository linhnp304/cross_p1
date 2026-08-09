#include "maps/vectormap.h"

#include "maps/geo.h"
#include "maps/prjprojection.h"
#include "maps/shapefile.h"

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTransform>

#include <cmath>

namespace {

/// Một hình đã quy về hệ world: nhiều phần, mỗi phần là một dãy điểm.
using WorldShape = QVector<QPolygonF>;

/// Sai số giản lược của từng mức chi tiết, tính bằng đơn vị world (1 đơn vị
/// world = 40.075 km ở xích đạo, nên các mức dưới đây là 120 m, 480 m và 1,9 km).
/// Mỗi mức thô gấp 4 lần mức trước, đủ dày để mức đang dùng luôn bám sát sai số
/// cho phép, mà không tốn quá nhiều bộ nhớ.
constexpr double kLodTol[VectorMap::kLodCount] = {0.0, 3.0e-6, 1.2e-5, 4.8e-5};

/// Sai số hình học coi như mắt không nhận ra, tính bằng điểm ảnh.
constexpr double kLodPixelError = 0.75;

/// Lớp đường được cắt thành từng đoạn chừng này điểm. Chi phí vẽ một nét phụ
/// thuộc bề rộng vùng nó trải ra trên màn hình chứ không chỉ số điểm, nên một
/// đường biên giới dài cả nghìn km mà để nguyên thì lúc phóng to sẽ rất chậm.
/// Cắt nhỏ ra thì phần nằm ngoài khung nhìn bị loại ngay từ hộp bao.
constexpr int kLineChunk = 256;

/// Bề rộng nét (điểm ảnh) — giữ đúng như bản C#.
constexpr double kProvincePx = 1.0;
constexpr double kAirRoutePx = 1.0;
constexpr double kNationPx   = 1.6;
constexpr double kCoastPx    = 1.4;
constexpr double kIslandPx   = 1.0;

/// Ngưỡng hiện nhãn, tính bằng điểm ảnh trên 1 độ kinh tuyến (như bản C#).
constexpr double kPlaceLabelPxPerDeg   = 18.0;
constexpr double kRouteLabelPxPerDeg   = 30.0;
constexpr double kAirportPxPerDeg      = 18.0;

/// Bỏ bớt điểm nằm sát nhau. Điểm đầu và điểm cuối luôn được giữ để hình
/// không bị hụt đuôi hay hở vòng.
QPolygonF decimate(const QPolygonF &src, double tol)
{
    if (tol <= 0.0 || src.size() <= 2)
        return src;

    QPolygonF out;
    out.reserve(src.size());
    out << src.first();
    const double tol2 = tol * tol;
    for (int i = 1; i < src.size() - 1; ++i) {
        const QPointF d = src[i] - out.last();
        if (d.x() * d.x() + d.y() * d.y() >= tol2)
            out << src[i];
    }
    out << src.last();
    return out;
}

/// Đọc một .shp, quy toạ độ về kinh/vĩ độ theo .prj rồi đổi sang hệ world.
QVector<WorldShape> readWorldShapes(const QString &shpPath)
{
    const QVector<ShpShape> raw = shapefile::read(shpPath);
    const PrjProjection proj = PrjProjection::forShapefile(shpPath);

    QVector<WorldShape> out;
    out.reserve(raw.size());
    for (const ShpShape &s : raw) {
        WorldShape shape;
        shape.reserve(s.parts.size());
        for (const QVector<QPointF> &part : s.parts) {
            QPolygonF poly;
            poly.reserve(part.size());
            for (const QPointF &pt : part) {
                const QPointF ll = proj.isIdentity() ? pt : proj.toLngLat(pt);
                poly << geo::toWorld(ll.y(), ll.x());
            }
            shape.push_back(std::move(poly));
        }
        if (!shape.isEmpty())
            out.push_back(std::move(shape));
    }
    return out;
}

/// Nạp nội dung tệp văn bản, tự nhận UTF-16 hay UTF-8 theo dấu BOM.
/// Diadanh.txt là UTF-16LE còn Airport2.dat là UTF-8 có BOM.
QStringList readTextLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QByteArray b = f.readAll();
    const auto encoding = QStringConverter::encodingForData(b);
    QStringDecoder decoder(encoding.value_or(QStringConverter::Utf8));

    QString text = decoder.decode(b);
    if (text.startsWith(QChar(0xFEFF)))   // phòng khi bộ giải mã giữ lại BOM
        text.remove(0, 1);

    static const QRegularExpression newline(QStringLiteral("[\\r\\n]+"));
    return text.split(newline, Qt::SkipEmptyParts);
}

} // namespace

// ------------------------------------------------------------- bảng màu ----

VectorMapTheme VectorMapTheme::fromBrightness(int percent)
{
    const double t = qBound(0.0, percent / 100.0, 1.0);

    // Nội suy giữa màu lúc tối nhất và lúc sáng nhất. Một số ký hiệu đi ngược
    // chiều (tối dần khi nền sáng lên) để lúc nào cũng tương phản với nền.
    auto lerp = [t](quint32 dark, quint32 light) {
        auto ch = [t](quint32 d, quint32 l) {
            return int(std::lround(d + (double(l) - double(d)) * t));
        };
        return QColor(ch((dark >> 16) & 0xFF, (light >> 16) & 0xFF),
                      ch((dark >> 8) & 0xFF, (light >> 8) & 0xFF),
                      ch(dark & 0xFF, light & 0xFF));
    };

    VectorMapTheme th;
    th.sea           = lerp(0x17181A, 0x8A8D8E);
    th.land          = lerp(0x1E1F21, 0x767879);
    th.river         = lerp(0x1E4A5C, 0x4E92AB);   // lớp mới, không có ở bản mẫu
    th.province      = lerp(0x2B2D30, 0x54575A);
    th.nation        = lerp(0xA0702D, 0xD9A44F);
    th.coast         = lerp(0x2A7C96, 0x52B9D4);
    th.label         = lerp(0x3D4145, 0x2E3134);
    th.placeDot      = lerp(0x45494E, 0x232527);
    // Ba màu dưới đây sáng hơn bản mẫu: ở độ sáng vừa, màu cũ gần trùng màu
    // nền đất nên gần như không nhìn ra.
    th.airRoute      = lerp(0x5E7A8E, 0x8CAFC8);
    th.airRouteLabel = lerp(0x7C97AB, 0xA8C4DA);
    th.airport       = lerp(0x8CA3B2, 0xC8D8E4);
    return th;
}

// ------------------------------------------------------------- nạp dữ liệu -

bool VectorMap::load(const QString &dir)
{
    *this = VectorMap();
    m_dir = QDir::cleanPath(dir);   // giữ cả khi nạp hỏng, để báo lỗi đúng chỗ

    const QDir d(m_dir);
    auto path = [&d](const char *name) { return d.filePath(QLatin1String(name)); };

    // Dựng một lớp hình học ở đủ các mức chi tiết. Gọi nhiều lần cho cùng một
    // lớp thì các hình cộng dồn vào nhau.
    //
    // Lớp tô nền (closed) phải giữ nguyên cả hình: các vòng của cùng một hình
    // nằm chung một đường dẫn thì quy tắc tô mới cho ra lỗ đúng chỗ. Lớp đường
    // (không closed) chỉ kẻ nét nên cắt nhỏ được, và cắt thì vẽ nhanh hơn hẳn.
    auto build = [](Layer &layer, const QVector<WorldShape> &shapes, bool closed) {
        for (int lod = 0; lod < kLodCount; ++lod) {
            const double tol = kLodTol[lod];

            // Nhận một đường dẫn đã dựng xong vào lớp, bỏ qua hình quá nhỏ so
            // với mức chi tiết đang dựng.
            auto take = [&layer, lod, tol](QPainterPath &&p) {
                if (p.isEmpty())
                    return;
                const QRectF box = p.boundingRect();
                if (tol > 0.0 && qMax(box.width(), box.height()) < tol * 2.0)
                    return;
                layer.lod[lod].push_back({std::move(p), box});
            };

            for (const WorldShape &shape : shapes) {
                QPainterPath whole;
                whole.setFillRule(Qt::WindingFill);

                for (const QPolygonF &part : shape) {
                    const QPolygonF simple = decimate(part, tol);
                    if (simple.size() < (closed ? 3 : 2))
                        continue;
                    if (closed) {
                        whole.addPolygon(simple);
                        whole.closeSubpath();
                        continue;
                    }
                    // Cắt thành từng đoạn, gối nhau một điểm để nét không đứt.
                    for (int from = 0; from + 1 < simple.size(); from += kLineChunk) {
                        const int to = qMin(from + kLineChunk, simple.size() - 1);
                        QPainterPath seg;
                        seg.moveTo(simple[from]);
                        for (int i = from + 1; i <= to; ++i)
                            seg.lineTo(simple[i]);
                        take(std::move(seg));
                    }
                }
                if (closed)
                    take(std::move(whole));
            }
        }
    };

    // Nền đất: các tỉnh, đảo, Hoàng Sa, Trường Sa. Riêng lớp tỉnh giữ tách ra vì
    // còn dùng lại để kẻ địa giới, khỏi phải dựng hình hai lần.
    const QVector<WorldShape> provinces = readWorldShapes(path("VNM_adm1.shp"));
    const QVector<WorldShape> islandsHs = readWorldShapes(path("Hoang_Sa.shp"));
    const QVector<WorldShape> islandsTs = readWorldShapes(path("Truong_Sa.shp"));

    build(m_provinces, provinces, true);
    build(m_landExtra, readWorldShapes(path("Land-Islands.shp")), true);
    build(m_landExtra, islandsHs, true);
    build(m_landExtra, islandsTs, true);
    build(m_islands,   islandsHs, true);
    build(m_islands,   islandsTs, true);

    build(m_rivers, readWorldShapes(path("Rivers.shp")), true);

    build(m_nation, readWorldShapes(path("Ranhgoiquocgia.shp")), false);
    build(m_nation, readWorldShapes(path("Duongbiengioi.shp")), false);
    build(m_coast,  readWorldShapes(path("CoastLines.shp")), false);

    // Đường bay dân dụng + tên đường bay lấy từ .dbf đi kèm.
    const QVector<WorldShape> routes = readWorldShapes(path("AirRoutes.shp"));
    build(m_airRoutes, routes, false);
    const QStringList routeNames =
        shapefile::readDbfColumn(path("AirRoutes.dbf"), QStringLiteral("name"));
    for (int i = 0; i < routes.size() && i < routeNames.size(); ++i) {
        if (routeNames[i].isEmpty() || routes[i].isEmpty())
            continue;
        const QPolygonF &part = routes[i].first();
        if (part.isEmpty())
            continue;
        m_routeLabels.push_back({routeNames[i], part[part.size() / 2], false});
    }

    loadPlaces(path("Diadanh.txt"));
    loadAirports(path("Airport2.dat"));

    // Không có lớp nền nào đọc được thì coi như chưa cài dữ liệu.
    m_valid = !m_provinces.lod[0].isEmpty() || !m_landExtra.lod[0].isEmpty()
           || !m_coast.lod[0].isEmpty();
    return m_valid;
}

void VectorMap::loadPlaces(const QString &path)
{
    // Diadanh.txt: tên <tab> vĩ độ <tab> kinh độ
    for (const QString &line : readTextLines(path)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() < 3)
            continue;
        bool okLat = false, okLng = false;
        const double lat = f[1].trimmed().toDouble(&okLat);
        const double lng = f[2].trimmed().toDouble(&okLng);
        if (!okLat || !okLng)
            continue;
        m_places.push_back({f[0].trimmed(), geo::toWorld(lat, lng), true});
    }

    // Bốn nhãn vùng biển không có trong tệp, ghi thẳng như bản mẫu.
    m_places.push_back({QStringLiteral("VỊNH BẮC BỘ"),   geo::toWorld(19.7, 107.3), false});
    m_places.push_back({QStringLiteral("BIỂN ĐÔNG"),     geo::toWorld(14.0, 111.0), false});
    m_places.push_back({QStringLiteral("QĐ Hoàng Sa"),   geo::toWorld(16.470, 112.0), false});
    m_places.push_back({QStringLiteral("QĐ Trường Sa"),  geo::toWorld(9.0, 114.0), false});
}

void VectorMap::loadAirports(const QString &path)
{
    // Airport2.dat: tên <tab> vĩ độ <tab> kinh độ <tab> … <tab> hướng đường băng
    constexpr int kHeadingField = 13;
    for (const QString &line : readTextLines(path)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() < 3)
            continue;
        bool okLat = false, okLng = false;
        const double lat = f[1].trimmed().toDouble(&okLat);
        const double lng = f[2].trimmed().toDouble(&okLng);
        if (!okLat || !okLng)
            continue;
        double heading = 0.0;
        if (f.size() > kHeadingField)
            heading = f[kHeadingField].trimmed().toDouble();
        m_airports.push_back({f[0].trimmed(), geo::toWorld(lat, lng), heading});
    }
}

// -------------------------------------------------------------------- vẽ ---

void VectorMap::draw(QPainter &p, const QRect &deviceRect,
                     const QTransform &worldToScreen, const VectorMapOptions &opt) const
{
    const VectorMapTheme th = VectorMapTheme::fromBrightness(opt.brightness);
    p.fillRect(deviceRect, th.sea);
    if (!m_valid)
        return;

    const double scale = worldToScreen.m11();   // điểm ảnh trên 1 đơn vị world
    if (scale <= 0.0)
        return;

    p.save();   // trả lại bút, chổi tô và phông chữ như lúc vào cho nơi gọi

    // Mức chi tiết thô nhất mà sai số vẫn dưới ngưỡng mắt thường.
    int lod = 0;
    for (int i = kLodCount - 1; i > 0; --i) {
        if (kLodTol[i] <= kLodPixelError / scale) {
            lod = i;
            break;
        }
    }

    // Phần hệ world đang lọt khung nhìn, nới thêm chút để nét dày không bị cắt.
    const QTransform screenToWorld = worldToScreen.inverted();
    QRectF view = screenToWorld.mapRect(QRectF(deviceRect));
    view.adjust(-2.0 / scale, -2.0 / scale, 2.0 / scale, 2.0 / scale);

    p.save();
    p.setWorldTransform(worldToScreen, true);

    // Nét vẽ để ở chế độ "cosmetic": bề rộng tính bằng điểm ảnh màn hình nên
    // không phình ra theo mức phóng.
    // Nối góc kiểu vát chứ không bo tròn: bo tròn thì mỗi đỉnh của đường thành
    // một cung, hàng nghìn đỉnh làm việc vẽ chậm gấp đôi mà nhìn không khác.
    auto pen = [](const QColor &c, double px) {
        QPen pn(c);
        pn.setWidthF(px);
        pn.setCosmetic(true);
        pn.setJoinStyle(Qt::BevelJoin);
        pn.setCapStyle(Qt::RoundCap);
        return pn;
    };

    // Vùng tô còn được viền bằng chính màu của nó: vừa lấp khe sáng giữa hai
    // vùng kề nhau do khử răng cưa, vừa giữ cho hình nhỏ (đảo, khúc sông hẹp)
    // không biến mất khi thu nhỏ.
    auto fill = [&](const Layer &layer, const QColor &c) {
        p.setBrush(c);
        p.setPen(pen(c, 1.0));
        for (const Shape &s : layer.lod[lod]) {
            if (s.box.intersects(view))
                p.drawPath(s.path);
        }
    };

    auto stroke = [&](const Layer &layer, const QColor &c, double px) {
        p.setBrush(Qt::NoBrush);
        p.setPen(pen(c, px));
        for (const Shape &s : layer.lod[lod]) {
            if (s.box.intersects(view))
                p.drawPath(s.path);
        }
    };

    fill(m_provinces, th.land);
    fill(m_landExtra, th.land);
    if (opt.showRivers)
        fill(m_rivers, th.river);
    if (opt.showProvinces)
        stroke(m_provinces, th.province, kProvincePx);
    if (opt.showAirRoutes)
        stroke(m_airRoutes, th.airRoute, kAirRoutePx);
    stroke(m_nation,  th.nation, kNationPx);
    stroke(m_coast,   th.coast,  kCoastPx);
    stroke(m_islands, th.coast,  kIslandPx);

    p.restore();

    // ---- nhãn và ký hiệu: vẽ theo toạ độ màn hình, không co giãn theo zoom --

    const double pxPerDeg = scale / 360.0;   // 1 đơn vị world = 360 độ kinh
    const QRectF labelView = QRectF(deviceRect).adjusted(-80, -30, 80, 30);

    QFont font = p.font();

    if (opt.showPlaceNames && pxPerDeg > kPlaceLabelPxPerDeg) {
        font.setPixelSize(11);
        p.setFont(font);
        for (const MapLabel &lb : m_places) {
            const QPointF at = worldToScreen.map(lb.world);
            if (!labelView.contains(at))
                continue;
            if (lb.withDot) {
                p.setPen(Qt::NoPen);
                p.setBrush(th.placeDot);
                p.drawEllipse(at, 2.0, 2.0);
            }
            p.setPen(th.label);
            p.drawText(QRectF(at.x() + 4, at.y() - 18, 240, 16),
                       Qt::AlignLeft | Qt::AlignBottom, lb.text);
        }
    }

    if (opt.showAirRoutes && pxPerDeg > kRouteLabelPxPerDeg) {
        font.setPixelSize(10);
        p.setFont(font);
        p.setPen(th.airRouteLabel);
        for (const MapLabel &lb : m_routeLabels) {
            const QPointF at = worldToScreen.map(lb.world);
            if (!labelView.contains(at))
                continue;
            p.drawText(QRectF(at.x() + 2, at.y() + 2, 160, 14),
                       Qt::AlignLeft | Qt::AlignTop, lb.text);
        }
    }

    if (opt.showAirports && pxPerDeg > kAirportPxPerDeg) {
        font.setPixelSize(10);
        p.setFont(font);
        for (const AirportInfo &ap : m_airports) {
            const QPointF at = worldToScreen.map(ap.world);
            if (!labelView.contains(at))
                continue;
            p.setPen(QPen(th.airport, 1.4));
            p.setBrush(Qt::NoBrush);
            drawAirportSymbol(p, at, ap.runwayHeading);
            p.setPen(th.label);
            p.drawText(QRectF(at.x() + 6, at.y() - 5, 200, 14),
                       Qt::AlignLeft | Qt::AlignVCenter, ap.text);
        }
    }

    p.restore();
}

void VectorMap::drawAirportSymbol(QPainter &p, const QPointF &at, double headingDeg)
{
    p.drawEllipse(at, 5.0, 5.0);
    const double a = (headingDeg - 90.0) * geo::kDeg2Rad;   // 0 độ = hướng bắc
    const QPointF d(std::cos(a) * 7.0, std::sin(a) * 7.0);
    p.drawLine(at - d, at + d);
}
