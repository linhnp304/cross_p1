#pragma once

#include <QPointF>
#include <QString>

/// Đọc tệp .prj (WKT) đi kèm shapefile và chuyển toạ độ chiếu về kinh/vĩ độ.
///
/// Chỉ làm đúng những phép chiếu mà bộ dữ liệu trong maps/tc dùng tới:
/// GEOGCS (toạ độ đã là độ, giữ nguyên), Lambert Conformal Conic 2SP và
/// Transverse Mercator. Gặp phép chiếu khác thì trả về phép biến đổi rỗng —
/// lớp đó sẽ vẽ sai chỗ, nhưng phần còn lại của bản đồ vẫn chạy.
class PrjProjection
{
public:
    /// Nạp tệp .prj nằm cạnh shpPath (cùng tên, khác đuôi).
    static PrjProjection forShapefile(const QString &shpPath);

    /// True khi toạ độ trong tệp đã là kinh/vĩ độ, khỏi phải đổi.
    bool isIdentity() const { return m_kind == Kind::Geographic; }

    /// Đổi một điểm (x, y theo đơn vị ghi trong .prj) sang (kinh độ, vĩ độ).
    QPointF toLngLat(const QPointF &p) const;

private:
    enum class Kind { Geographic, Lcc, Tm };

    PrjProjection() = default;
    explicit PrjProjection(Kind kind, const QString &wkt);

    QPointF inverseLcc(double x, double y) const;
    QPointF inverseTm(double x, double y) const;

    double m(double phi) const;
    double t(double phi) const;
    double phiFromT(double t) const;
    double meridianArc(double phi) const;

    Kind   m_kind = Kind::Geographic;
    double m_a = 6378137.0;      ///< bán trục lớn (m)
    double m_e = 0.0, m_e2 = 0.0;
    double m_unit = 1.0;         ///< hệ số đổi đơn vị toạ độ chiếu về mét
    double m_fe = 0.0, m_fn = 0.0;   ///< false easting / northing (m)
    double m_lng0 = 0.0;         ///< kinh tuyến trục (rad)

    // Lambert Conformal Conic
    double m_n = 0.0, m_f = 0.0, m_rho0 = 0.0;
    // Transverse Mercator
    double m_k0 = 1.0, m_m0 = 0.0, m_e1 = 0.0, m_ep2 = 0.0;
};
