#pragma once

// Bốn gói lệnh nạp bộ lọc cho đài và trạng thái phản hồi của chúng.
//
// Cùng lối với adfproto.h: mỗi lệnh và trạng thái phản hồi của nó dùng **chung
// một bố cục gói tin**, chỉ khác trường Category, và thân gói chỉ là những từ
// 32 bit thô — không có bảng trường có nhãn có dải giá trị như cmdproto.h. Ở
// đây thân gói còn dài tới 1024 từ, nên "bảng trường" cũng chẳng có nghĩa gì:
// nội dung của nó là hệ số bộ lọc, đọc từ file văn bản chứ không vặn trên giao
// diện.
//
//   FILTER_FIR   Filter[33]    lấy từ ./filter/FIR.txt
//   FILTER_WFC   Filter[1024]  lấy từ ./filter/WFC.txt
//   FILTER_STF   Filter[512]   lấy từ ./filter/STF.txt
//   FILTER_MTK   Filter[16]    lấy từ ./filter/MTK.txt
//
// `lines` và `linesPerValue` mô tả **file dữ liệu** chứ không mô tả gói tin —
// nhưng chúng nằm ở đây vì chúng là hệ quả của gói tin: FILTER_WFC có 1024 từ
// mà mỗi từ ghép từ hai dòng file, nên file phải có 2048 dòng. Tách hai con số
// ấy sang chỗ khác là tách một quan hệ ra làm hai danh sách phải giữ khớp nhau
// bằng mắt (xem chú thích cùng ý ở đầu cmdproto.h).

#include <QByteArray>
#include <QtEndian>
#include <QtGlobal>

#include <iterator>

namespace filterproto {

/// Mô tả một loại gói: đủ để đóng lệnh, nhận ra trạng thái, tách nó ra, và biết
/// phải đọc file nào với bao nhiêu dòng.
struct Packet {
    const char *name;      ///< tên bộ lọc, cũng là nhãn cột trên giao diện
    const char *file;      ///< tên file dữ liệu trong thư mục ./filter
    quint32     header;
    quint32     cmdCategory;
    quint32     statusCategory;
    int         count;     ///< số phần tử Filter[] trong gói

    /// Bao nhiêu dòng file cho một phần tử Filter[]. Một dòng thì lấy thẳng;
    /// hai dòng thì ghép **dòng trước thành nửa cao**:
    ///
    ///     Filter[0] = ((dòng1 & 0xffff) << 16) + dòng2
    int linesPerValue;

    /// Số dòng đầu tiên của file phải đọc được thành số hex. Dòng sau đó không
    /// quan tâm — các file mẫu đều có một dòng chú thích ở cuối.
    constexpr int lines() const { return count * linesPerValue; }

    /// Năm từ đầu (Header, Category, Length, Serial, Time) + thân + CheckSum.
    constexpr int words() const { return 5 + count + 1; }
    constexpr int size() const { return words() * 4; }
};

/// Số hiệu loại gói, cũng là chỉ số trong bảng kPackets và thứ tự cột trên giao
/// diện.
enum Kind { FIR = 0, WFC, STF, MTK, KindCount };

inline constexpr Packet kPackets[KindCount] = {
    {"FIR", "FIR.txt", 0xd5d4d3d2u, 0x8019u, 0x80190u,   33, 1},
    {"WFC", "WFC.txt", 0xd6d5d4d3u, 0x8021u, 0x80210u, 1024, 2},
    {"STF", "STF.txt", 0xd7d6d5d4u, 0x8022u, 0x80220u,  512, 2},
    {"MTK", "MTK.txt", 0xd8d7d6d5u, 0x9019u, 0x90190u,   16, 2},
};

// Độ dài gói tính ra từ bảng phải khớp trường Length của mô tả giao thức. Sai
// một từ thì đài nhận một gói dài sai mà không có gì báo — bắt ngay lúc biên
// dịch, đúng như bốn gói lệnh của cmdproto.h.
static_assert(kPackets[FIR].size() == (6 +   33) * 4, "FILTER_FIR: (6+33)*4");
static_assert(kPackets[WFC].size() == (6 + 1024) * 4, "FILTER_WFC: (6+1024)*4");
static_assert(kPackets[STF].size() == (6 +  512) * 4, "FILTER_STF: (6+512)*4");
static_assert(kPackets[MTK].size() == (6 +   16) * 4, "FILTER_MTK: (6+16)*4");

/// Số phần tử Filter[] nhiều nhất của một loại — đủ chỗ cho một bộ đệm tĩnh và
/// cũng là số dòng của bảng trên giao diện.
inline constexpr int kMaxCount = 1024;
static_assert(kPackets[WFC].count == kMaxCount, "kMaxCount phải là cỡ của WFC");

/// Số dòng file nhiều nhất phải đọc.
inline constexpr int kMaxLines = 2048;
static_assert(kPackets[WFC].lines() == kMaxLines, "kMaxLines phải là cỡ của WFC");

inline quint32 wordAt(const char *data, int index)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data) + index * 4);
}

/// Đóng một gói lệnh. `values` có đúng p.count phần tử.
inline QByteArray buildCommand(const Packet &p, const quint32 *values,
                               quint32 serial, quint32 timeMs)
{
    QByteArray out(p.size(), Qt::Uninitialized);
    auto *raw = reinterpret_cast<uchar *>(out.data());

    const auto put = [raw](int index, quint32 v) {
        qToLittleEndian(v, raw + index * 4);
    };

    put(0, p.header);
    put(1, p.cmdCategory);
    put(2, quint32(p.size()));
    put(3, serial);
    put(4, timeMs);
    for (int i = 0; i < p.count; ++i)
        put(5 + i, values[i]);
    put(p.words() - 1, 0);   // CheckSum — chưa dùng

    return out;
}

/// Gói này là trạng thái phản hồi của lệnh nào? nullptr nếu không phải.
///
/// Xét cả Header lẫn Category, giống cmdproto::statusPacket() — hai khoá thì mô
/// tả giao thức có đổi lại cách phân biệt cũng không phải sửa hàm này.
inline const Packet *statusPacket(const char *data, int len)
{
    if (len < 12)
        return nullptr;
    const quint32 header   = wordAt(data, 0);
    const quint32 category = wordAt(data, 1);

    for (const Packet &p : kPackets) {
        if (p.header == header && p.statusCategory == category
            && len >= p.size())
            return &p;
    }
    return nullptr;
}

inline bool isStatus(const char *data, int len)
{
    return statusPacket(data, len) != nullptr;
}

/// Tách phần thân một gói trạng thái. `values` có đúng p.count phần tử.
inline void parseStatus(const Packet &p, const char *data, quint32 &serial,
                        quint32 &timeMs, quint32 *values)
{
    serial = wordAt(data, 3);
    timeMs = wordAt(data, 4);
    for (int i = 0; i < p.count; ++i)
        values[i] = wordAt(data, 5 + i);
}

} // namespace filterproto
