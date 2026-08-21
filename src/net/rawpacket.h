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
inline constexpr int kSizeP  = kWordsP * 4;       ///< 72*4 = 288 byte

// Trường Length theo mô tả giao thức là **số byte**, tức kSizeV và kSizeP.
// Nhưng **cố ý không kiểm tra trường này** và cũng không dùng nó để cắt gói:
// nguồn phát mỗi nơi ghi một kiểu — đài thật ghi số từ (72 và 1031), có nguồn
// để thẳng 0 — mà không nguồn nào trong số đó là gói hỏng. Gói hợp lệ hay không
// xét bằng đủ số trường (cỡ datagram = kSizeV/kSizeP) cùng Header và Category;
// còn nội dung thì xét theo logic của từng trường, ví dụ 12 bit thấp của
// Azimuth luôn nằm trong 0..4095 nên chỉ việc che bit là xong.

/// Cả 64 ô Data_P đều là điểm dấu — xem chú thích của decodeRawP().
inline constexpr int kMaxPlots = kPlotsP;

/// Số ô cự ly trong một chu kỳ. Trùng với số điểm biên độ của RAW_V (10 bit
/// cự ly trong Data_P cũng cho đúng dải này), nên cùng một phép quy đổi dùng
/// được cho cả hai loại gói.
inline constexpr int kRangeCells = 1024;

/// Ô cự ly D (0..1023) đổi ra mét. Công thức tường minh của mô tả giao thức:
///
///     fb = (D+1)*Fs/2048            (MHz)
///     Td = (fb*Tc/B) * 10^-6        (giây)
///     R  = Td * 3*10^8 / 2          (mét)
///
/// Rút gọn lại còn **R = Rmax * (D+1) / 1024**, với Rmax là cự ly ứng với ô
/// cuối cùng D = 1023. Ô đánh số từ 0 nhưng cự ly tính từ ô **thứ nhất**, nên
/// cái +1 đó không bỏ được: bỏ đi thì ô cuối chỉ ra 1023/1024 cự ly tối đa và
/// mọi điểm dấu lùi vào trong đúng một ô.
inline double cellToMeters(double maxRangeM, double cell)
{
    return maxRangeM * (cell + 1.0) / kRangeCells;
}

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

/// Cách quy Data_V[] về thang hiển thị 0..255.
///
/// Hai công thức, chọn theo trường DataSend của lệnh CMD_COMMON:
///
///     DataSend = 1 (Fbeat)   Video[i] = Data_V[i]       / ZFbeat * 256 * Multi_V
///     DataSend = 2 (Doppler) Video[i] = (Data_V[i]>>16) / GainU  * 256 * Multi_V
///
/// ZFbeat lấy trong CMD_DSP_R, GainU trong CMD_DSP_S — cả hai là **số chia** nên
/// phải kiểm > 0 trước khi dùng. Kết quả vượt 255 thì bão hoà.
///
/// DataSend còn hai giá trị nữa: 0 là đài không truyền dữ liệu, 3 (Beam) mô tả
/// giai đoạn không nói công thức nào — cả hai đều dùng cách của Fbeat, đúng cách
/// phần mềm vẫn tính từ trước tới nay.
struct VideoScale {
    quint32 dataSend = 1;
    quint32 zfbeat   = 32768;
    quint32 gainU    = 32768;
    double  multiV   = 1.0;

    bool doppler() const { return dataSend == 2; }
};

/// Hệ số đã tính sẵn dạng dấu phẩy tĩnh 16 bit — thứ bộ giải mã thật sự dùng.
///
/// Tính một lần mỗi khi tham số đổi chứ không phải mỗi gói: phép nhân này chạy
/// 1024 lần cho mỗi gói, khoảng 400 gói mỗi giây.
struct VideoGain {
    quint64 factorQ16 = 512;   ///< 256*65536/32768, ứng với ZFbeat mặc định
    bool    doppler   = false;

    /// Trần của hệ số. Trên mức này thì mẫu khác 0 nào cũng đã bão hoà rồi, mà
    /// giữ trần ở đây còn để phép nhân với mẫu 32 bit không tràn quint64.
    static constexpr quint64 kMaxFactor = quint64(1) << 32;

    static VideoGain from(const VideoScale &s)
    {
        const quint32 div = s.doppler() ? qMax(1u, s.gainU) : qMax(1u, s.zfbeat);
        const double  f   = 256.0 * qMax(0.0, s.multiV) * 65536.0 / double(div);

        VideoGain g;
        g.doppler   = s.doppler();
        g.factorQ16 = quint64(qBound(0.0, f, double(kMaxFactor)));
        return g;
    }
};

/// Giải mã RAW_V, quy biên độ về thang 0..255 theo hệ số đã tính sẵn.
///
/// Chế độ Doppler lấy **bit 16..31** của mỗi từ Data_V làm biên độ; đó cũng
/// chính là thứ đo được trên gói của đài thật.
inline void decodeRawV(const char *data, const VideoGain &gain, RawVSweep &out)
{
    out.serial  = word(data, 3);
    out.azimuth = word(data, 5) % kAzimuthSteps;

    for (int i = 0; i < kBinsV; ++i) {
        const quint32 sample = gain.doppler ? (word(data, 6 + i) >> 16)
                                            : word(data, 6 + i);
        const quint64 v = (quint64(sample) * gain.factorQ16 + 32768u) >> 16;
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
    quint32 azimuth = 0;    ///< phương vị encoder của chu kỳ, 0..4095
    qint32  count   = 0;    ///< số plot thực sự có trong gói, 0..64
    std::array<RawPlot, kMaxPlots> plots{};
};

/// Giải mã RAW_P.
///
///     Azimuth        bit 0..11  phương vị encoder
///                    bit 12..30 số đếm chu kỳ, tăng dần
///                    bit 31     =1, dấu đầu chu kỳ
///     Data_P[0..63]  bit 0..15  amplitude
///                    bit 16..25 ô cự ly
///                    bit 26..30 dopler
///                    bit 31     =0, dấu điểm dấu
///
/// Khác mô tả giao thức ở một điểm: **cả 64 ô Data_P đều là điểm dấu**, từ đầu
/// chu kỳ nằm ngay trong ô Azimuth chứ không phải ở Data_P[0]. Đo trên hai bản
/// ghi của đài thật — 165606 gói ngày 10/08/2026 và 12011 gói ngày 03/08 — bit
/// 31 của Azimuth bằng 1 ở **mọi** gói, còn trong cả 10,6 triệu ô Data_P thì
/// không ô nào có bit 31 bằng 1; số ô khác 0 luôn đúng bằng Num_P và luôn bắt
/// đầu từ Data_P[0]. Đọc plot từ Data_P[1] như mô tả cũ thì mất điểm dấu đầu
/// của mọi chu kỳ và sinh thêm một điểm dấu rỗng ở ô cự ly 0.
inline void decodeRawP(const char *data, RawPCycle &out)
{
    out.serial = word(data, 3);

    // Chỉ lấy 12 bit phương vị: phần trên của ô này là số đếm chu kỳ và dấu đầu
    // chu kỳ, không phải góc.
    out.azimuth = word(data, 5) & 0x0fffu;

    // Gói hỏng khai Num_P lớn hơn số ô thì cắt chứ không đọc lố mảng.
    out.count = qBound(0, int(word(data, 6)), kMaxPlots);

    for (int i = 0; i < out.count; ++i) {
        const quint32 w = word(data, 7 + i);   // Data_P[i]
        RawPlot &p = out.plots[size_t(i)];
        p.amplitude = quint16(w & 0xffffu);
        p.range     = quint16((w >> 16) & 0x03ffu);
        p.dopler    = quint8((w >> 26) & 0x1fu);
    }
}

} // namespace rawpkt
