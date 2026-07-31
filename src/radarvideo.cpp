#include "radarvideo.h"

#include "geo.h"

#include <QPainter>

#include <cmath>

namespace {

/// Bán kính ảnh nền tạp. 512 cho ảnh 1024x1024 (4 MB): đủ mịn ở mức phóng
/// thường dùng, mà một lượt làm mờ vẫn chỉ đụng 1 triệu điểm ảnh. Tăng lên là
/// nét hơn khi phóng to nhưng làm mờ đắt lên theo bình phương.
constexpr int kRadius = 512;

/// Màu lân quang của nền tạp, cùng tông xanh với lưới cự ly.
constexpr int kInkR = 120, kInkG = 255, kInkB = 120;

/// Quá ngưỡng này thì coi như mất dữ liệu / vừa bắt đầu, không lấp khoảng hở
/// (nếu không, một lần gián đoạn sẽ quét một vệt sáng vòng quanh màn hình).
constexpr int kMaxGapSteps = rawpkt::kAzimuthSteps / 32;   // 128 nấc ~ 11 độ

/// Trần số tia phụ cho một lượt quét, chặn chi phí ở trường hợp xấu nhất.
constexpr int kMaxSubRays = 48;

} // namespace

RadarVideo::RadarVideo()
    : m_radius(kRadius)
{
    // Alpha nhân sẵn: với màu đặc thì thành phần màu = màu * alpha / 255.
    for (int a = 0; a < 256; ++a) {
        m_palette[a] = qRgba(kInkR * a / 255, kInkG * a / 255, kInkB * a / 255, a);
    }

    m_image = QImage(m_radius * 2, m_radius * 2, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);

    // Vòng bán kính j ứng với các ô cự ly [j*N/R, (j+1)*N/R). Lấy mức lớn nhất
    // trong khoảng đó để mục tiêu nhỏ không bị mất khi thu 1024 ô về 512 vòng.
    m_binLo.resize(m_radius);
    m_binHi.resize(m_radius);
    for (int j = 0; j < m_radius; ++j) {
        const qint32 lo = qint32(qint64(j) * rawpkt::kBinsV / m_radius);
        const qint32 hi = qint32(qint64(j + 1) * rawpkt::kBinsV / m_radius);
        m_binLo[j] = qBound(0, lo, rawpkt::kBinsV - 1);
        m_binHi[j] = qBound(m_binLo[j] + 1, hi, rawpkt::kBinsV);
    }
}

void RadarVideo::clear()
{
    m_image.fill(Qt::transparent);
    m_hasInk       = false;
    m_hasSweep     = false;
    m_lastAngleDeg = -1.0;
    m_lastAzimuth  = -1;
}

void RadarVideo::addSweep(const rawpkt::RawVSweep &s)
{
    const double angle = rawpkt::azimuthToDeg(s.azimuth);

    // Khoảng hở góc so với lượt trước. Ăng ten chỉ quay một chiều nên hiệu
    // luôn dương sau khi bù vòng.
    int gap = 0;
    if (m_lastAzimuth >= 0) {
        gap = (int(s.azimuth) - m_lastAzimuth + rawpkt::kAzimuthSteps)
              % rawpkt::kAzimuthSteps;
        if (gap > kMaxGapSteps)
            gap = 0;
    }

    if (gap <= 0) {
        drawRay(angle, s);
    } else {
        // Bề rộng cung ở vành ngoài quyết định cần bao nhiêu tia phụ để không
        // hở. Ở tốc độ 6 vòng/phút với 400 gói/giây thì gap = 1 nấc, cung rộng
        // 0.8 điểm ảnh — chỉ một tia, không tốn thêm gì.
        const double arcPx = m_radius * gap * 2.0 * geo::kPi / rawpkt::kAzimuthSteps;
        const int rays = qBound(1, int(std::ceil(arcPx)), kMaxSubRays);
        const double step = gap * 360.0 / rawpkt::kAzimuthSteps / rays;
        const double from = angle - gap * 360.0 / rawpkt::kAzimuthSteps;
        for (int k = 1; k <= rays; ++k)
            drawRay(from + step * k, s);
    }

    m_lastAzimuth  = qint32(s.azimuth);
    m_lastAngleDeg = angle;
    m_last         = s;
    m_hasSweep     = true;
    m_hasInk       = true;
}

void RadarVideo::drawRay(double angleDeg, const rawpkt::RawVSweep &s)
{
    // Phương vị 0 là hướng bắc, tăng theo chiều kim đồng hồ; trục y của ảnh
    // hướng xuống nên bắc là -y.
    const double rad = angleDeg * geo::kDeg2Rad;
    const double dx  =  std::sin(rad);
    const double dy  = -std::cos(rad);

    const int size = m_radius * 2;
    auto *bits = reinterpret_cast<QRgb *>(m_image.bits());
    const int stride = m_image.bytesPerLine() / int(sizeof(QRgb));

    // Ghi đè chứ không trộn: tia quét đi qua là làm mới hẳn ô đó, đúng như
    // yêu cầu "giá trị mới đè lên giá trị cũ" ở chế độ không làm mờ.
    const auto put = [bits, stride, size](int x, int y, QRgb c) {
        if (x >= 0 && x < size && y >= 0 && y < size)
            bits[y * stride + x] = c;
    };

    int prevX = m_radius;
    int prevY = m_radius;

    for (int j = 0; j < m_radius; ++j) {
        int level = 0;
        for (int i = m_binLo[j], end = m_binHi[j]; i < end; ++i)
            level = qMax(level, int(s.video[size_t(i)]));

        const int x = int(std::lround(m_radius + dx * j));
        const int y = int(std::lround(m_radius + dy * j));
        const QRgb c = m_palette[level];
        put(x, y, c);

        // Bước chéo để hở một điểm ảnh ở mỗi bên; các tia kề nhau không lấp
        // hết chỗ đó, thành ra vành ngoài nổi vân hoa. Lấp luôn một góc là
        // tia trở thành liền mạch theo 4 hướng.
        if (x != prevX && y != prevY)
            put(x, prevY, c);

        prevX = x;
        prevY = y;
    }
}

void RadarVideo::fade(int fadeSeconds, int elapsedMs)
{
    if (fadeSeconds <= 0 || elapsedMs <= 0 || !m_hasInk)
        return;

    // Giảm theo cấp số nhân: sau `fadeSeconds` thì mức sáng nhất (255) tụt
    // xuống dưới 1, tức tắt hẳn ở thang 8 bit.
    const double k = std::pow(1.0 / 255.0, elapsedMs / (fadeSeconds * 1000.0));
    int a = int(std::lround(k * 255.0));
    a = qBound(0, a, 254);   // 255 là không giảm gì, vệt sẽ đứng mãi

    // DestinationIn nhân alpha của ảnh với alpha của màu tô — một lượt quét
    // toàn ảnh đã được QPainter tối ưu sẵn, nhanh hơn nhiều so với tự lặp.
    QPainter p(&m_image);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.fillRect(m_image.rect(), QColor(0, 0, 0, a));
    p.end();

    if (a == 0)
        m_hasInk = false;
}
