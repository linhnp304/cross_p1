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

// --- RAW_P: dữ liệu mục tiêu (giải mã ở giai đoạn sau) ----------------------

inline constexpr quint32 kHeaderP   = 0xc4c3c2c1u;
inline constexpr quint32 kCategoryP = 0x30180u;

inline constexpr int kPlotsP = 64;
inline constexpr int kWordsP = 7 + kPlotsP + 1;   ///< 72 từ
inline constexpr int kSizeP  = kWordsP * 4;

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

} // namespace rawpkt
