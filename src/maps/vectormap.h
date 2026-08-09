#pragma once

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;
class QRect;
class QTransform;

/// Bảng màu lớp bản đồ TC, nội suy theo độ sáng 0..100 (0 tối nhất).
/// Chuyển từ MapTheme.cs; riêng đường bay và sân bay được kéo sáng hơn bản mẫu.
struct VectorMapTheme {
    QColor sea, land, river;
    QColor province, nation, coast;
    QColor label, placeDot;
    QColor airRoute, airRouteLabel, airport;

    static VectorMapTheme fromBrightness(int percent);
};

/// Lớp nào được vẽ, và vẽ sáng tới đâu — lấy thẳng từ AppSettings.
struct VectorMapOptions {
    bool showAirRoutes  = true;
    bool showAirports   = true;
    bool showRivers     = true;
    bool showPlaceNames = true;
    bool showProvinces  = true;
    int  brightness     = 60;   ///< 0..100
};

/// Lớp bản đồ số tự dựng từ shapefile trong maps/tc ("TC" trên giao diện).
///
/// Chuyển từ MapData.cs + MapView.OnRender của bản C#/WPF. Toàn bộ hình được
/// dựng sẵn lúc nạp theo hệ toạ độ world [0,1] của geo.h — cùng hệ với nền
/// tile — nên phần vẽ chỉ việc đặt ma trận biến đổi rồi đổ ra.
///
/// Hình được giản lược sẵn ở vài mức chi tiết và kèm hộp bao từng hình, để lúc
/// thu nhỏ không phải nắn hàng trăm nghìn điểm, còn lúc phóng to thì bỏ qua
/// ngay những hình nằm ngoài khung nhìn.
class VectorMap
{
public:
    /// Số mức chi tiết dựng sẵn (0 = nguyên bản, càng lớn càng thô).
    static constexpr int kLodCount = 4;

    /// Nạp toàn bộ dữ liệu trong thư mục dir. Thiếu tệp thì bỏ qua lớp đó;
    /// trả về false khi không đọc được lớp nền nào — nơi gọi báo cho người dùng.
    bool load(const QString &dir);

    bool isValid() const { return m_valid; }
    QString directory() const { return m_dir; }

    /// Vẽ nền biển kín deviceRect rồi tới các lớp bản đồ. worldToScreen là ma
    /// trận đổi từ hệ world [0,1] sang toạ độ widget.
    void draw(QPainter &p, const QRect &deviceRect, const QTransform &worldToScreen,
              const VectorMapOptions &opt) const;

private:
    /// Một hình đã dựng sẵn, kèm hộp bao (hệ world) để loại nhanh khi vẽ.
    struct Shape {
        QPainterPath path;
        QRectF       box;
    };

    /// Một lớp hình học, giữ sẵn kLodCount phiên bản độ chi tiết khác nhau.
    struct Layer {
        QVector<Shape> lod[kLodCount];
    };

    struct MapLabel {
        QString text;
        QPointF world;
        bool    withDot = false;
    };

    struct AirportInfo {
        QString text;
        QPointF world;
        double  runwayHeading = 0.0;   ///< độ, 0 = hướng bắc
    };

    void loadPlaces(const QString &path);
    void loadAirports(const QString &path);

    /// Vẽ ký hiệu sân bay: vòng tròn và vạch đường băng theo hướng.
    static void drawAirportSymbol(QPainter &p, const QPointF &at, double headingDeg);

    QString m_dir;
    bool    m_valid = false;

    Layer m_provinces;   ///< VNM_adm1 — vừa tô nền đất, vừa kẻ địa giới tỉnh
    Layer m_landExtra;   ///< đảo + Hoàng Sa + Trường Sa — chỉ tô nền đất
    Layer m_islands;     ///< viền Hoàng Sa + Trường Sa
    Layer m_rivers;      ///< sông ngòi (dạng vùng, không phải đường)
    Layer m_nation;      ///< ranh giới quốc gia + đường biên giới
    Layer m_coast;       ///< bờ biển
    Layer m_airRoutes;   ///< đường bay dân dụng

    QVector<MapLabel>   m_places;
    QVector<MapLabel>   m_routeLabels;
    QVector<AirportInfo> m_airports;
};
