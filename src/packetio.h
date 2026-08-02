#pragma once

// Đóng gói PlotTC và Track thành datagram để gửi đi hệ thống khác, và tách
// ngược lại lúc phát lại file ghi lưu.
//
// Cách xếp byte giống hệt RAW_V/RAW_P: các từ 4 byte liền nhau, little-endian.
// Riêng track_lat và track_lng là **số thực 4 byte**, không phải số nguyên —
// đây là chỗ duy nhất trong cả bốn loại gói có kiểu khác, rất dễ bỏ sót.
//
// Hàm đóng gói và hàm tách phải đọc song song với nhau từng dòng một: hai danh
// sách trường đó lệch nhau một ô là mọi trường phía sau sai hết mà không có gì
// báo. Vì vậy chúng để cạnh nhau trong file này chứ không tách ra hai nơi.

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

/// Bộ đọc ngược lại Writer, cùng cách tự đếm vị trí.
class Reader
{
public:
    Reader(const char *data, int words) : m_data(data), m_words(words) {}

    quint32 u32()
    {
        if (m_at >= m_words)
            return 0;
        return qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(m_data) + m_at++ * 4);
    }

    float f32()
    {
        const quint32 bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    void skip(int n) { m_at += n; }

private:
    const char *m_data;
    int         m_words;
    int         m_at = 0;
};

/// Một datagram có đúng là gói tin loại `category` cỡ `size` không.
inline bool matches(const char *data, int len, quint32 category, int size)
{
    if (len < size)
        return false;
    const auto *p = reinterpret_cast<const uchar *>(data);
    return qFromLittleEndian<quint32>(p) == kHeader
        && qFromLittleEndian<quint32>(p + 4) == category;
}

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

/// Tách một gói tin điểm dấu. False nếu không phải gói PlotTC.
///
/// Các trường chỉ dùng để hiển thị (lat/lng, bornMs) không nằm trong gói tin
/// nên giữ nguyên giá trị mặc định — nơi gọi tự tính lại từ azm/range.
inline bool parsePlot(const char *data, int len, PlotTC &out)
{
    if (!matches(data, len, kCategoryPlot, kSizePlot))
        return false;

    Reader r(data, kWordsPlot);
    r.skip(3);                       // Header, Category, Length
    out.serial           = r.u32();
    out.timeMs           = r.u32();
    out.azm              = r.u32();
    out.range            = r.u32();
    out.iffReturnedMode  = r.u32();
    out.iffCommander     = r.u32();
    out.iffFlightId      = r.u32();
    out.iffAltitude      = r.u32();
    out.iffFuelLevel     = r.u32();
    out.numCX            = r.u32();
    out.azmStart         = r.u32();
    out.azmStop          = r.u32();
    out.numLoseTotal     = r.u32();
    out.amplitudeAverage = r.u32();
    out.amplitudeCenter  = r.u32();
    out.rangeStart       = r.u32();
    out.doplerStart      = r.u32();
    return true;
}

/// Tách một gói tin quỹ đạo. False nếu không phải gói Track.
///
/// Phần trạng thái bộ lọc Kalman và vết lịch sử không có trong gói tin: quỹ đạo
/// dựng lại từ đây là để **hiển thị** đúng cái đã ghi, không phải để bám tiếp.
inline bool parseTrack(const char *data, int len, Track &out)
{
    if (!matches(data, len, kCategoryTrack, kSizeTrack))
        return false;

    Reader r(data, kWordsTrack);
    r.skip(3);
    out.serial          = r.u32();
    out.timeMs          = r.u32();
    out.type            = TrackType(r.u32());
    out.status          = TrackStatus(r.u32());
    out.id              = r.u32();
    out.top             = r.u32();
    out.azm             = r.u32();
    out.range           = r.u32();
    out.velocity        = r.u32();
    out.heading         = r.u32();
    out.iffReturnedMode = r.u32();
    out.iffCommander    = r.u32();
    out.iffFlightId     = r.u32();
    out.iffAltitude     = r.u32();
    out.iffFuelLevel    = r.u32();
    out.lat             = r.f32();
    out.lng             = r.f32();
    out.classify        = r.u32();
    out.altitudeManual  = r.u32();
    out.amplitude       = r.u32();
    r.skip(1);                       // reserved01
    out.windowAzm1      = r.u32();
    out.windowAzm2      = r.u32();
    out.windowRange1    = r.u32();
    out.windowRange2    = r.u32();
    return true;
}

} // namespace packetio
