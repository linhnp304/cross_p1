#include "maps/prjprojection.h"

#include "maps/geo.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cmath>

namespace {

/// Đơn vị của hệ toạ độ chiếu là UNIT cuối cùng trong WKT (Meter, Kilometer…).
/// UNIT đứng trước thuộc về GEOGCS và luôn là độ, nên phải bỏ qua.
double projectedUnit(const QString &wkt)
{
    static const QRegularExpression re(
        QStringLiteral("UNIT\\[\"([^\"]+)\",([0-9.eE+-]+)"),
        QRegularExpression::CaseInsensitiveOption);

    double unit = 1.0;
    auto it = re.globalMatch(wkt);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.captured(1).contains(QLatin1String("degree"), Qt::CaseInsensitive))
            continue;
        bool ok = false;
        const double v = m.captured(2).toDouble(&ok);
        if (ok)
            unit = v;
    }
    return unit;
}

/// Giá trị của PARAMETER["<name>", …] trong WKT; riêng SPHEROID lấy được cả
/// bán trục lớn (group 1) lẫn nghịch đảo độ dẹt (group 2).
double param(const QString &wkt, const QString &name, int group, double fallback)
{
    const QString pattern =
        name.compare(QLatin1String("SPHEROID"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("SPHEROID\\[\"[^\"]*\",([0-9.eE+-]+),([0-9.eE+-]+)")
            : QStringLiteral("PARAMETER\\[\"%1\",([0-9.eE+-]+)").arg(name);

    const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(wkt);
    if (!m.hasMatch())
        return fallback;

    bool ok = false;
    const double v = m.captured(qMin(group, m.lastCapturedIndex())).toDouble(&ok);
    return ok ? v : fallback;
}

} // namespace

PrjProjection PrjProjection::forShapefile(const QString &shpPath)
{
    const QString prj = QFileInfo(shpPath).path() + QLatin1Char('/')
                      + QFileInfo(shpPath).completeBaseName() + QStringLiteral(".prj");

    QFile f(prj);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QString wkt = QString::fromLatin1(f.readAll());
    if (!wkt.trimmed().startsWith(QLatin1String("PROJCS"), Qt::CaseInsensitive))
        return {};   // GEOGCS: toạ độ đã là độ

    if (wkt.contains(QLatin1String("Lambert_Conformal_Conic"), Qt::CaseInsensitive))
        return PrjProjection(Kind::Lcc, wkt);
    if (wkt.contains(QLatin1String("Transverse_Mercator"), Qt::CaseInsensitive))
        return PrjProjection(Kind::Tm, wkt);
    return {};
}

PrjProjection::PrjProjection(Kind kind, const QString &wkt)
    : m_kind(kind)
{
    m_a = param(wkt, QStringLiteral("SPHEROID"), 1, 6378137.0);
    const double invF = param(wkt, QStringLiteral("SPHEROID"), 2, 298.257223563);
    const double flat = 1.0 / invF;
    m_e2 = flat * (2.0 - flat);
    m_e  = std::sqrt(m_e2);

    m_unit = projectedUnit(wkt);
    m_fe   = param(wkt, QStringLiteral("false_easting"), 1, 0.0) * m_unit;
    m_fn   = param(wkt, QStringLiteral("false_northing"), 1, 0.0) * m_unit;
    m_lng0 = param(wkt, QStringLiteral("central_meridian"), 1, 0.0) * geo::kDeg2Rad;

    const double lat0 = param(wkt, QStringLiteral("latitude_of_origin"), 1, 0.0)
                      * geo::kDeg2Rad;

    if (kind == Kind::Lcc) {
        const double sp1 = param(wkt, QStringLiteral("standard_parallel_1"), 1, 15.0)
                         * geo::kDeg2Rad;
        const double sp2 = param(wkt, QStringLiteral("standard_parallel_2"), 1, 45.0)
                         * geo::kDeg2Rad;
        const double m1 = m(sp1), m2 = m(sp2);
        const double t0 = t(lat0), t1 = t(sp1), t2 = t(sp2);
        m_n    = (std::log(m1) - std::log(m2)) / (std::log(t1) - std::log(t2));
        m_f    = m1 / (m_n * std::pow(t1, m_n));
        m_rho0 = m_a * m_f * std::pow(t0, m_n);
    } else if (kind == Kind::Tm) {
        m_k0 = param(wkt, QStringLiteral("scale_factor"), 1, 1.0);
        m_m0 = meridianArc(lat0);
        const double sq = std::sqrt(1.0 - m_e2);
        m_e1  = (1.0 - sq) / (1.0 + sq);
        m_ep2 = m_e2 / (1.0 - m_e2);
    }
}

QPointF PrjProjection::toLngLat(const QPointF &p) const
{
    switch (m_kind) {
    case Kind::Lcc: return inverseLcc(p.x() * m_unit, p.y() * m_unit);
    case Kind::Tm:  return inverseTm(p.x() * m_unit, p.y() * m_unit);
    case Kind::Geographic: break;
    }
    return p;
}

// -------------------------------- Lambert Conformal Conic 2SP (EPSG 9802) ---

QPointF PrjProjection::inverseLcc(double x, double y) const
{
    const double xd = x - m_fe;
    const double yd = m_rho0 - (y - m_fn);
    const double rho = (m_n < 0 ? -1.0 : 1.0) * std::hypot(xd, yd);
    const double tt = std::pow(rho / (m_a * m_f), 1.0 / m_n);
    const double theta = std::atan2(xd, yd);
    return QPointF((theta / m_n + m_lng0) * geo::kRad2Deg,
                   phiFromT(tt) * geo::kRad2Deg);
}

// ------------------------------------- Transverse Mercator (EPSG 9807) ------

QPointF PrjProjection::inverseTm(double x, double y) const
{
    const double mm = m_m0 + (y - m_fn) / m_k0;
    const double mu = mm / (m_a * (1.0 - m_e2 / 4.0 - 3.0 * m_e2 * m_e2 / 64.0
                                 - 5.0 * m_e2 * m_e2 * m_e2 / 256.0));
    const double e1 = m_e1;
    const double e1_2 = e1 * e1, e1_3 = e1_2 * e1, e1_4 = e1_3 * e1;

    const double phi1 = mu
        + (3.0 * e1 / 2.0 - 27.0 * e1_3 / 32.0) * std::sin(2.0 * mu)
        + (21.0 * e1_2 / 16.0 - 55.0 * e1_4 / 32.0) * std::sin(4.0 * mu)
        + 151.0 * e1_3 / 96.0 * std::sin(6.0 * mu)
        + 1097.0 * e1_4 / 512.0 * std::sin(8.0 * mu);

    const double sin1 = std::sin(phi1), cos1 = std::cos(phi1), tan1 = std::tan(phi1);
    const double c1 = m_ep2 * cos1 * cos1;
    const double t1 = tan1 * tan1;
    const double n1 = m_a / std::sqrt(1.0 - m_e2 * sin1 * sin1);
    const double r1 = m_a * (1.0 - m_e2) / std::pow(1.0 - m_e2 * sin1 * sin1, 1.5);
    const double d = (x - m_fe) / (n1 * m_k0);
    const double d2 = d * d, d3 = d2 * d, d4 = d3 * d, d5 = d4 * d, d6 = d5 * d;

    const double lat = phi1 - n1 * tan1 / r1 *
        (d2 / 2.0
         - (5.0 + 3.0 * t1 + 10.0 * c1 - 4.0 * c1 * c1 - 9.0 * m_ep2) * d4 / 24.0
         + (61.0 + 90.0 * t1 + 298.0 * c1 + 45.0 * t1 * t1 - 252.0 * m_ep2
            - 3.0 * c1 * c1) * d6 / 720.0);

    const double lng = m_lng0 +
        (d - (1.0 + 2.0 * t1 + c1) * d3 / 6.0
           + (5.0 - 2.0 * c1 + 28.0 * t1 - 3.0 * c1 * c1 + 8.0 * m_ep2
              + 24.0 * t1 * t1) * d5 / 120.0) / cos1;

    return QPointF(lng * geo::kRad2Deg, lat * geo::kRad2Deg);
}

// ------------------------------------------------------------- hàm phụ -----

double PrjProjection::m(double phi) const
{
    const double s = std::sin(phi);
    return std::cos(phi) / std::sqrt(1.0 - m_e2 * s * s);
}

double PrjProjection::t(double phi) const
{
    const double es = m_e * std::sin(phi);
    return std::tan(geo::kPi / 4.0 - phi / 2.0)
         / std::pow((1.0 - es) / (1.0 + es), m_e / 2.0);
}

double PrjProjection::phiFromT(double tt) const
{
    // Lặp theo EPSG 9802: hội tụ sau vài vòng ở độ dẹt của Trái Đất.
    double phi = geo::kPi / 2.0 - 2.0 * std::atan(tt);
    for (int i = 0; i < 8; ++i) {
        const double es = m_e * std::sin(phi);
        phi = geo::kPi / 2.0
            - 2.0 * std::atan(tt * std::pow((1.0 - es) / (1.0 + es), m_e / 2.0));
    }
    return phi;
}

double PrjProjection::meridianArc(double phi) const
{
    const double e2 = m_e2, e4 = e2 * e2, e6 = e4 * e2;
    return m_a * ((1.0 - e2 / 4.0 - 3.0 * e4 / 64.0 - 5.0 * e6 / 256.0) * phi
                - (3.0 * e2 / 8.0 + 3.0 * e4 / 32.0 + 45.0 * e6 / 1024.0)
                      * std::sin(2.0 * phi)
                + (15.0 * e4 / 256.0 + 45.0 * e6 / 1024.0) * std::sin(4.0 * phi)
                - 35.0 * e6 / 3072.0 * std::sin(6.0 * phi));
}
