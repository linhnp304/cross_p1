#pragma once

// Gói CTRL_SYNC — chiếm quyền điều khiển giữa nhiều máy tính cùng chạy phần mềm.
//
// Trong hệ thống có nhiều màn hình trắc thủ, nhưng tại một thời điểm chỉ **một**
// máy được ra lệnh cho đài; các máy còn lại chỉ theo dõi. Máy nào bấm "Mở khóa
// điều khiển" thì quảng bá một gói này, và mọi máy khác nghe được sẽ tự khóa
// lại.
//
// Chỉ có một loại gói và **không có gói trạng thái phản hồi**: người nhận không
// trả lời gì, nó chỉ khóa mình lại. Vì vậy ở đây không có bảng kPackets như
// cmdproto.h / filterproto.h, chỉ vài hằng số.

#include <QByteArray>
#include <QtEndian>
#include <QtGlobal>

namespace syncproto {

inline constexpr quint32 kHeader   = 0xcafe9113u;
inline constexpr quint32 kCategory = 0x9113u;

/// Header, Category, Length, Serial, Time, CtrlIP, CheckSum.
inline constexpr int kWords = 7;
inline constexpr int kSize  = kWords * 4;

/// Cổng mặc định cho cả hai chiều — cũng là con số nằm trong Header và Category.
inline constexpr quint16 kPort = 9113;

inline quint32 wordAt(const char *data, int index)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data) + index * 4);
}

/// Đóng gói. `ctrlIp` là địa chỉ IPv4 dạng số của máy đang chiếm quyền.
///
/// Dạng số ở đây là **QHostAddress::toIPv4Address()**: 192.168.11.22 thành
/// 0xC0A80B16, tức byte đầu của địa chỉ nằm ở byte cao. Mô tả giao thức chỉ nói
/// "unsigned int" nên chọn thế nào cũng được, miễn hai đầu đường truyền giống
/// nhau — mà hai đầu ở đây đều là phần mềm này. Chọn cách trên vì dump gói ra
/// hex là đọc được luôn địa chỉ.
inline QByteArray build(quint32 ctrlIp, quint32 serial, quint32 timeMs)
{
    QByteArray out(kSize, Qt::Uninitialized);
    auto *raw = reinterpret_cast<uchar *>(out.data());

    const auto put = [raw](int index, quint32 v) {
        qToLittleEndian(v, raw + index * 4);
    };

    put(0, kHeader);
    put(1, kCategory);
    put(2, quint32(kSize));
    put(3, serial);
    put(4, timeMs);
    put(5, ctrlIp);
    put(6, 0);   // CheckSum — chưa dùng

    return out;
}

inline bool isSync(const char *data, int len)
{
    return len >= kSize && wordAt(data, 0) == kHeader
        && wordAt(data, 1) == kCategory;
}

/// Tách một gói đã qua isSync().
inline void parse(const char *data, quint32 &serial, quint32 &timeMs,
                  quint32 &ctrlIp)
{
    serial = wordAt(data, 3);
    timeMs = wordAt(data, 4);
    ctrlIp = wordAt(data, 5);
}

} // namespace syncproto
