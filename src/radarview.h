#pragma once

#include "appsettings.h"
#include "tilecache.h"
#include "vectormap.h"

#include <QPixmap>
#include <QPointF>
#include <QWidget>

class QSlider;
class QTransform;
class RadarVideo;

/// Panel 1 — màn hình hiển thị chính.
///
/// Vẽ nền bản đồ số, nền tạp ra đa, vòng tròn cự ly và đường chia phương vị
/// quanh tâm đài. Hỗ trợ kéo chuột để dịch khung nhìn, cuộn chuột / thanh
/// trượt để phóng to thu nhỏ.
///
/// Nền bản đồ và lưới cự ly được dựng sẵn vào hai QPixmap và chỉ dựng lại khi
/// khung nhìn hoặc cài đặt đổi. Lúc có dữ liệu, panel này vẽ lại ~25 lần mỗi
/// giây; dựng lại cả nền vector hay ghép lại hàng chục tile ở mỗi lần vẽ thì
/// không thể mượt được.
class RadarView : public QWidget
{
    Q_OBJECT

public:
    explicit RadarView(QWidget *parent = nullptr);

    void setSettings(const AppSettings &s);

    /// Nguồn ảnh nền tạp. Con trỏ do nơi khác giữ, phải sống lâu hơn panel này.
    void setVideo(const RadarVideo *video);

    /// Hẹn căn lại khung nhìn: tâm đài vào giữa, mức phóng vừa cự ly tối đa.
    /// Việc căn thực sự hoãn tới lần vẽ kế tiếp, khi widget đã có kích thước thật.
    void resetView();

signals:
    /// Phát khi con trỏ di chuyển trên panel (dùng cho thanh trạng thái).
    void cursorGeoChanged(double lat, double lng);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    // --- chuyển đổi toạ độ ---
    QPointF worldToScreen(const QPointF &w) const;
    QPointF screenToWorld(const QPointF &p) const;
    QPointF geoToScreen(double lat, double lng) const;

    /// Cùng phép biến đổi world -> widget, dạng ma trận để đưa cho QPainter.
    QTransform worldTransform() const;

    /// Số pixel trên màn hình ứng với 1 km mặt đất, tại vĩ độ tâm đài.
    double pixelsPerKm() const;

    /// Bán kính vòng cự ly tối đa trên màn hình, đo đúng theo cách vẽ vòng
    /// tròn — để mép ảnh nền tạp trùng khít với vòng ngoài cùng.
    double maxRangeRadiusPx() const;

    // --- vẽ ---
    /// Dựng lại hai lớp tĩnh (nền bản đồ, lưới cự ly) vào bộ nhớ đệm.
    void rebuildCaches();
    void invalidateCaches();

    /// Vẽ nền bản đồ số rồi phủ một lớp tối theo mức độ sáng đã cài.
    /// Trả về số tile thực sự vẽ được — 0 nghĩa là vùng đang xem chưa tải.
    /// Không phải const vì việc đọc tile có cập nhật bộ nhớ đệm.
    int drawMap(QPainter &p);

    /// Vẽ lớp bản đồ TC (dữ liệu vector trong maps/tc).
    void drawVectorMap(QPainter &p) const;

    /// Dòng nhắc ở đáy panel: ghi nguồn bản đồ, hoặc báo thiếu dữ liệu.
    QString mapNote(int tilesDrawn) const;

    void drawRangeRings(QPainter &p) const;
    void drawAzimuthLines(QPainter &p) const;
    void drawSiteMarker(QPainter &p) const;

    /// Dán ảnh nền tạp lên bản đồ, căn theo tâm đài và cự ly tối đa.
    void drawVideo(QPainter &p) const;

    /// Vệt quét hiện hành, từ tâm đài ra vòng cự ly tối đa.
    void drawSweepLine(QPainter &p) const;

    /// Vẽ một lớp vòng tròn. Bước tính theo đơn vị 0.1 km để so trùng bằng số
    /// nguyên (tránh sai số dấu phẩy động); skipTenthKm = 0 nghĩa là không bỏ
    /// vòng nào. Cả lớp bị bỏ qua nếu các vòng nằm quá sát nhau trên màn hình.
    /// Các lớp luôn dừng **hẳn bên trong** cự ly tối đa — vòng ngoài cùng do
    /// drawMaxRangeRing vẽ.
    void drawRingLayer(QPainter &p, int stepTenthKm, int skipTenthKm,
                       const QPen &pen, bool withLabels) const;

    /// Vòng tròn ở đúng cự ly tối đa, luôn vẽ và luôn nét đậm. Nhờ nó mà các
    /// đường chia độ luôn kết thúc trên một vòng, kể cả khi cự ly tối đa không
    /// chia hết cho bước vòng tròn đang chọn.
    void drawMaxRangeRing(QPainter &p) const;

    /// Tương tự cho một lớp đường chia phương vị, bước tính bằng độ.
    void drawAzimuthLayer(QPainter &p, int stepDeg, int skipDeg,
                          const QPen &pen, bool withLabels) const;

    void setZoom(double z, const QPointF &anchorScreen);
    void syncZoomSlider();
    void layoutZoomSlider();

    /// Thực hiện việc căn khung nhìn mà resetView() đã hẹn.
    void fitToRange();

    AppSettings m_settings;

    QPointF m_center{0.0, 0.0};   ///< tâm khung nhìn, toạ độ world
    double  m_zoom = 11.0;        ///< mức phóng kiểu tile XYZ (có thể lẻ)
    bool    m_needsFit = true;    ///< còn nợ một lần căn khung nhìn

    bool    m_dragging = false;
    QPointF m_dragLastPos;

    QSlider *m_zoomSlider = nullptr;
    bool     m_updatingSlider = false;

    TileCache m_tiles;
    VectorMap m_vector;

    const RadarVideo *m_video = nullptr;

    // Hai lớp tĩnh dựng sẵn. m_gridCache trong suốt để nền tạp lọt xuống dưới.
    QPixmap m_mapCache;
    QPixmap m_gridCache;
    bool    m_cachesDirty = true;
};
