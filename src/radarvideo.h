#pragma once

#include "rawpacket.h"

#include <QImage>
#include <QVector>

/// Ảnh nền tạp tích luỹ, vẽ trong hệ toạ độ ra đa đã **chuẩn hoá**.
///
/// Tâm ảnh là tâm đài, bán kính ảnh (radius()) luôn ứng với cự ly tối đa — dù
/// cự ly tối đa là bao nhiêu. Nhờ vậy đổi cự ly tối đa lúc đang chạy chỉ là đổi
/// tỉ lệ lúc dán ảnh lên bản đồ: không phải dựng lại ảnh, không mất nền tạp
/// đang có, và kéo/phóng bản đồ cũng không đụng gì tới ảnh này.
///
/// Ảnh ở dạng ARGB32 nhân sẵn alpha để hai việc nặng nhất — làm mờ toàn ảnh và
/// dán lên panel — đi đúng đường tối ưu của QPainter.
class RadarVideo
{
public:
    RadarVideo();

    /// Bán kính ảnh tính bằng điểm ảnh. Ảnh vuông cạnh 2*radius.
    int radius() const { return m_radius; }

    const QImage &image() const { return m_image; }

    /// Xoá sạch nền tạp đang hiện.
    void clear();

    /// Vẽ một lượt quét vào ảnh. Khoảng hở góc so với lượt quét trước được lấp
    /// bằng các tia phụ, nên vành ngoài không bị rỗ khi tốc độ quay nhanh.
    void addSweep(const rawpkt::RawVSweep &s);

    /// Làm mờ dần toàn ảnh. `fadeSeconds` là thời gian để một vệt sáng nhất tắt
    /// hẳn; 0 nghĩa là không làm mờ (giá trị mới đè lên giá trị cũ).
    void fade(int fadeSeconds, int elapsedMs);

    /// True khi trên ảnh còn nét — dùng để khỏi làm mờ một ảnh đã trống trơn.
    bool hasInk() const { return m_hasInk; }

    /// Góc tia quét mới nhất (độ, 0 = hướng bắc). -1 khi chưa có dữ liệu.
    double lastAngleDeg() const { return m_lastAngleDeg; }

    /// Lượt quét mới nhất, để cửa sổ biên độ vẽ lại.
    const rawpkt::RawVSweep &lastSweep() const { return m_last; }
    bool hasSweep() const { return m_hasSweep; }

private:
    void drawRay(double angleDeg, const rawpkt::RawVSweep &s);

    int    m_radius = 512;
    QImage m_image;

    /// Với mỗi vòng bán kính j, khoảng ô cự ly [lo, hi) rơi vào đúng vòng đó.
    /// Chỉ phụ thuộc bán kính ảnh nên tính sẵn một lần.
    QVector<qint32> m_binLo, m_binHi;

    /// Màu ứng với 256 mức biên độ, đã nhân sẵn alpha.
    QRgb m_palette[256];

    bool   m_hasInk       = false;
    bool   m_hasSweep     = false;
    double m_lastAngleDeg = -1.0;
    qint32 m_lastAzimuth  = -1;

    rawpkt::RawVSweep m_last;
};
