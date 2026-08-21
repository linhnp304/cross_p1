#pragma once

// Hai gói lệnh điều khiển kit tạo tín hiệu ADF4159 và trạng thái phản hồi.
//
// Cùng lối với cmdproto.h: mỗi lệnh và trạng thái phản hồi của nó dùng **chung
// một bố cục gói tin**, chỉ khác trường Category. Khác chỗ này: thân gói không
// phải một bảng trường có nhãn có dải giá trị, mà chỉ là những từ 32 bit thô —
// nội dung của chúng do mô hình thanh ghi trong proc/adf4159.h quyết định. Vì
// vậy ở đây không cần bảng Field như cmdproto.h.
//
//   CMD_ADF4159_REG8  gửi cả tám thanh ghi trong một gói
//   CMD_ADF4159_REG   gửi đúng một thanh ghi
//
// Lệnh một thanh ghi **không** có trường "số hiệu thanh ghi": ba bit thấp nhất
// của chính từ đó đã là số hiệu (xem chú thích đầu proc/adf4159.h).

#include <QByteArray>
#include <QtEndian>
#include <QtGlobal>

namespace adfproto {

/// Mô tả một loại gói: đủ để đóng lệnh, nhận ra trạng thái và tách nó ra.
struct Packet {
    quint32 header;
    quint32 cmdCategory;
    quint32 statusCategory;
    int     regCount;    ///< số thanh ghi trong thân gói

    /// Năm từ đầu (Header, Category, Length, Serial, Time) + thân + CheckSum.
    constexpr int words() const { return 5 + regCount + 1; }
    constexpr int size() const { return words() * 4; }
};

inline constexpr Packet kReg8{0xadf4159au, 0x5018u, 0x50180u, 8};
inline constexpr Packet kReg1{0xadf4159bu, 0x6018u, 0x60180u, 1};

// Mô tả giai đoạn ghi Length của CMD_ADF4159_REG8 là 13*4. Đếm lại đúng những
// trường mà chính nó liệt kê thì ra 14 từ: 5 từ đầu + 8 thanh ghi + CheckSum —
// và CMD_ADF4159_REG với 7*4 = 5 + 1 + 1 thì khớp, tức là quy ước "Length đếm
// cả CheckSum" đúng cho cả bốn gói lệnh cũ (xem static_assert ở cmdproto.h).
// Nên 13 là thiếu một, ở đây gửi đi 14*4 = 56.
static_assert(kReg8.size() == 14 * 4, "CMD_ADF4159_REG8: 5 + 8 + 1 từ");
static_assert(kReg1.size() == 7 * 4,  "CMD_ADF4159_REG: Length = 7*4");

inline quint32 wordAt(const char *data, int index)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data) + index * 4);
}

/// Đóng một gói lệnh. `regs` có đúng p.regCount phần tử.
inline QByteArray buildCommand(const Packet &p, const quint32 *regs,
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
    for (int i = 0; i < p.regCount; ++i)
        put(5 + i, regs[i]);
    put(p.words() - 1, 0);   // CheckSum — chưa dùng

    return out;
}

/// Lệnh gửi một thanh ghi.
inline QByteArray buildOne(quint32 reg, quint32 serial, quint32 timeMs)
{
    return buildCommand(kReg1, &reg, serial, timeMs);
}

/// Gói này là trạng thái phản hồi của lệnh nào? nullptr nếu không phải.
///
/// Bám vào Header cho giống cách phân loại của cmdproto.h — và ở đây Header còn
/// là thứ duy nhất phân biệt được hai loại gói khi độ dài bị cắt bớt.
inline const Packet *statusPacket(const char *data, int len)
{
    if (len < 12)
        return nullptr;
    const quint32 header   = wordAt(data, 0);
    const quint32 category = wordAt(data, 1);

    for (const Packet *p : {&kReg8, &kReg1}) {
        if (p->header == header && p->statusCategory == category
            && len >= p->size())
            return p;
    }
    return nullptr;
}

inline bool isStatus(const char *data, int len)
{
    return statusPacket(data, len) != nullptr;
}

/// Tách phần thân một gói trạng thái. `regs` có đúng p.regCount phần tử.
inline void parseStatus(const Packet &p, const char *data, quint32 &serial,
                        quint32 &timeMs, quint32 *regs)
{
    serial = wordAt(data, 3);
    timeMs = wordAt(data, 4);
    for (int i = 0; i < p.regCount; ++i)
        regs[i] = wordAt(data, 5 + i);
}

/// Số hiệu thanh ghi nằm trong chính từ 32 bit đó.
inline int regIndexOf(quint32 word) { return int(word & 7u); }

} // namespace adfproto
