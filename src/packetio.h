#pragma once

// Đóng gói PlotTC và Track thành datagram để gửi đi hệ thống khác.
//
// Cách xếp byte giống hệt RAW_V/RAW_P: các từ 4 byte liền nhau, little-endian.
// Riêng track_lat và track_lng là **số thực 4 byte**, không phải số nguyên —
// đây là chỗ duy nhất trong cả bốn loại gói có kiểu khác, rất dễ bỏ sót.

#include "plottrack.h"

#include <QByteArray>
#include <QtEndian>

#include <cstring>

namespace packetio {

inline constexpr quint32 kHeader = 0x2d2d2d2du;

inline constexpr quint32 kCategoryPlot = 0x2021u;
inline constexpr int     kWordsPlot    = 26;
inline constexpr int     kSizePlot     = kWordsPlot * 4;

inline constexpr quint32 kCategoryTrack = 0x2051u;
inline constexpr int     kWordsTrack    = 38;
inline constexpr int     kSizeTrack     = kWordsTrack * 4;

/// Bộ ghi từng từ vào bộ đệm, tự đếm vị trí.
///
/// Có nó thì thứ tự trường trong hàm đóng gói đọc thẳng một mạch từ trên xuống,
/// so với bảng mô tả giao thức là khớp từng dòng — chứ tự cộng chỉ số thì thêm
/// bớt một trường ở giữa là lệch hết phần sau mà không ai thấy.
class Writer
{
public:
    explicit Writer(char *data) : m_data(data) {}

    void u32(quint32 v)
    {
        qToLittleEndian(v, reinterpret_cast<uchar *>(m_data) + m_at * 4);
        ++m_at;
    }

    /// Số thực 4 byte, giữ nguyên dạng bit rồi ghi như một từ.
    void f32(float v)
    {
        quint32 bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }

    void zeros(int n)
    {
        for (int i = 0; i < n; ++i)
            u32(0);
    }

    int count() const { return m_at; }

private:
    char *m_data;
    int   m_at = 0;
};

/// Gói tin điểm dấu tâm chùm — 26 từ.
inline QByteArray buildPlot(const PlotTC &p)
{
    QByteArray out(kSizePlot, Qt::Uninitialized);
    Writer w(out.data());

    w.u32(kHeader);
    w.u32(kCategoryPlot);
    w.u32(kSizePlot);
    w.u32(p.serial);
    w.u32(p.timeMs);
    w.u32(p.azm);
    w.u32(p.range);
    w.u32(p.iffReturnedMode);
    w.u32(p.iffCommander);
    w.u32(p.iffFlightId);
    w.u32(p.iffAltitude);
    w.u32(p.iffFuelLevel);
    w.u32(p.numCX);
    w.u32(p.azmStart);
    w.u32(p.azmStop);
    w.u32(p.numLoseTotal);
    w.u32(p.amplitudeAverage);
    w.u32(p.amplitudeCenter);
    w.u32(p.rangeStart);
    w.u32(p.doplerStart);
    w.zeros(5);            // reserved01..05
    w.u32(0);              // CheckSum — chưa dùng

    Q_ASSERT(w.count() == kWordsPlot);
    return out;
}

/// Gói tin quỹ đạo — 38 từ.
inline QByteArray buildTrack(const Track &t)
{
    QByteArray out(kSizeTrack, Qt::Uninitialized);
    Writer w(out.data());

    w.u32(kHeader);
    w.u32(kCategoryTrack);
    w.u32(kSizeTrack);
    w.u32(t.serial);
    w.u32(t.timeMs);
    w.u32(quint32(t.type));
    w.u32(quint32(t.status));
    w.u32(t.id);
    w.u32(t.top);
    w.u32(t.azm);
    w.u32(t.range);
    w.u32(t.velocity);
    w.u32(t.heading);
    w.u32(t.iffReturnedMode);
    w.u32(t.iffCommander);
    w.u32(t.iffFlightId);
    w.u32(t.iffAltitude);
    w.u32(t.iffFuelLevel);
    w.f32(t.lat);
    w.f32(t.lng);
    w.u32(t.classify);
    w.u32(t.altitudeManual);
    w.u32(t.amplitude);
    w.u32(0);              // reserved01
    w.u32(t.windowAzm1);
    w.u32(t.windowAzm2);
    w.u32(t.windowRange1);
    w.u32(t.windowRange2);
    w.zeros(9);            // reserved02..10
    w.u32(0);              // CheckSum — chưa dùng

    Q_ASSERT(w.count() == kWordsTrack);
    return out;
}

} // namespace packetio
