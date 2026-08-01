#pragma once

// Giao thức gói tin UDP của đài: RAW_V (nền tạp) và RAW_P (điểm dấu).
//
// Mọi trường đều là unsigned int 4 byte, xếp liền nhau, **little-endian** —
// cùng thứ tự byte với x86/ARM nên trên mọi máy đang dùng là đọc thẳng được.
// Vẫn đọc qua qFromLittleEndian để mã nguồn còn đúng nếu sau này chạy trên
// máy big-endian.

#include <QtEndian>
#include <QtGlobal>

#include <array>

namespace rawpkt {

// --- RAW_V: dữ liệu nền tạp -------------------------------------------------

inline constexpr quint32 kHeaderV   = 0xb4b3b2b1u;
inline constexpr quint32 kCategoryV = 0x20180u;

inline constexpr int kBinsV  = 1024;              ///< số điểm biên độ mỗi gói
inline constexpr int kWordsV = 6 + kBinsV + 1;    ///< 6 trường đầu + Data_V + CheckSum
inline constexpr int kSizeV  = kWordsV * 4;       ///< 1031*4 = 4124 byte

// --- RAW_P: dữ liệu điểm dấu đơn xung ---------------------------------------

inline constexpr quint32 kHeaderP   = 0xc4c3c2c1u;
inline constexpr quint32 kCategoryP = 0x30180u;

inline constexpr int kPlotsP = 64;                ///< Data_P[64]
inline constexpr int kWordsP = 7 + kPlotsP + 1;   ///< 72 từ
inline constexpr int kSizeP  = kWordsP * 4;

/// Data_P[0] là từ tiêu đề chu kỳ, nên tối đa còn 63 điểm dấu.
inline constexpr int kMaxPlots = kPlotsP - 1;

/// Số ô cự ly trong một chu kỳ. Trùng với số điểm biên độ của RAW_V (10 bit
/// cự ly trong Data_P cũng cho đúng dải này), nên cùng một phép quy đổi
/// `R = Rmax * ô / 1024` dùng được cho cả hai loại gói.
inline constexpr int kRangeCells = 1024;

/// Số nấc encoder trong một vòng quay. Góc tia = Azimuth * 360 / 4096.
inline constexpr int kAzimuthSteps = 4096;

inline double azimuthToDeg(quint32 azimuth)
{
    return azimuth * 360.0 / kAzimuthSteps;
}

/// Đọc một từ 4 byte tại vị trí từ thứ `word` trong gói.
inline quint32 word(const char *data, int index)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data) + index * 4);
}

/// Một lượt quét RAW_V đã giải mã và đã chia theo ZFbeat.
///
/// 1 KB mỗi lượt — cố ý để dạng POD cỡ cố định để đẩy qua bộ đệm giữa hai
/// luồng mà không phải cấp phát động cho từng gói.
struct RawVSweep {
    quint32 serial  = 0;
    quint32 azimuth = 0;    ///< 0..4095, nấc encoder
    std::array<quint8, kBinsV> video{};   ///< biên độ đã quy về 0..255
};

/// Kiểm tra một datagram có phải RAW_V hợp lệ không (header + category + cỡ).
/// CheckSum chưa kiểm theo đúng mô tả giao thức.
inline bool isRawV(const char *data, int size)
{
    return size >= kSizeV
        && word(data, 0) == kHeaderV
        && word(data, 1) == kCategoryV;
}

inline bool isRawP(const char *data, int size)
{
    return size >= kSizeP
        && word(data, 0) == kHeaderP
        && word(data, 1) == kCategoryP;
}

/// Giải mã RAW_V, quy biên độ về thang 0..255 theo hệ số căn chỉnh ZFbeat.
///
///     ZFbeat_Scale = ZFbeat / 256
///     Video[i]     = round(Data_V[i] / ZFbeat_Scale)
///
/// Rút gọn thành phép chia số nguyên `(Data_V[i]*256 + ZFbeat/2) / ZFbeat`
/// để khỏi đụng dấu phẩy động 1024 lần mỗi gói, 400 gói mỗi giây.
/// Dùng quint64 vì Data_V toàn dải, nhân 256 là tràn 32 bit.
inline void decodeRawV(const char *data, quint32 zfbeat, RawVSweep &out)
{
    out.serial  = word(data, 3);
    out.azimuth = word(data, 5) % kAzimuthSteps;

    const quint64 div  = zfbeat > 0 ? zfbeat : 1;
    const quint64 half = div / 2;
    for (int i = 0; i < kBinsV; ++i) {
        const quint64 v = (quint64(word(data, 6 + i)) * 256u + half) / div;
        out.video[size_t(i)] = quint8(qMin<quint64>(v, 255));
    }
}

/// Một điểm dấu đơn xung (plot) đã tách bit khỏi Data_P.
struct RawPlot {
    quint16 range     = 0;   ///< ô cự ly, 0..1023
    quint16 amplitude = 0;   ///< biên độ phản xạ xung đơn, 0..65535
    quint8  dopler    = 0;   ///< 0..31
};

/// Một chu kỳ RAW_P đã giải mã.
///
/// Cũng để dạng POD cỡ cố định như RawVSweep, vì cũng đi qua bộ đệm giữa luồng
/// mạng và luồng giao diện. 400 chu kỳ mỗi giây mà cấp phát động từng cái thì
/// riêng việc cấp phát đã tốn hơn cả việc xử lý.
struct RawPCycle {
    quint32 serial  = 0;
    quint32 azimuth = 0;    ///< trường Azimuth của gói, 0..4095
    quint32 azm     = 0;    ///< phương vị trong Data_P[0], bit 0..11
    bool    azmValid = false; ///< Data_P[0] có bit 31 = 1 như mô tả giao thức
    qint32  count   = 0;    ///< số plot thực sự có trong gói, 0..63
    std::array<RawPlot, kMaxPlots> plots{};

    /// Phương vị dùng để xử lý. Ưu tiên Data_P[0] (thuật toán tâm chùm đọc ở
    /// đó), nhưng nếu từ đó không mang dấu đầu chu kỳ thì lùi về trường
    /// Azimuth của gói — thà lấy trường kia còn hơn gom chùm quanh phương vị 0.
    quint32 workAzimuth() const { return azmValid ? azm : azimuth; }
};

/// Giải mã RAW_P. Cách tách bit theo đúng mô tả giao thức:
///
///     Data_P[0]      bit 0..11  phương vị encoder
///                    bit 31     =1, dấu đầu chu kỳ
///     Data_P[1..63]  bit 0..15  amplitude
///                    bit 16..25 ô cự ly
///                    bit 26..30 dopler
///                    bit 31     =0, dấu điểm dấu
inline void decodeRawP(const char *data, RawPCycle &out)
{
    out.serial  = word(data, 3);
    out.azimuth = word(data, 5) % kAzimuthSteps;

    const quint32 head = word(data, 7);        // Data_P[0]
    out.azm      = head & 0x0fffu;
    out.azmValid = (head & 0x80000000u) != 0;

    // Num_P là số plot; Data_P[0] không phải plot nên trần thật sự là 63. Gói
    // hỏng khai Num_P lớn hơn thì cắt chứ không đọc lố mảng.
    out.count = qBound(0, int(word(data, 6)), kMaxPlots);

    for (int i = 0; i < out.count; ++i) {
        const quint32 w = word(data, 8 + i);   // Data_P[1 + i]
        RawPlot &p = out.plots[size_t(i)];
        p.amplitude = quint16(w & 0xffffu);
        p.range     = quint16((w >> 16) & 0x03ffu);
        p.dopler    = quint8((w >> 26) & 0x1fu);
    }
}

} // namespace rawpkt
