#pragma once

// Vài phép tính địa chỉ IPv4 dùng ở hơn một nơi.
//
// Trước đây broadcastFor() nằm trong udpsender.cpp, nhưng bảng cổng gửi của tab
// "Kết nối" cũng cần đúng phép tính ấy để điền sẵn RemoteIP cho dòng
// "CtrlSync_S" — hai chỗ tính hai kiểu thì giao diện hiện một địa chỉ mà gói tin
// lại đi tới địa chỉ khác.

#include <QHostAddress>
#include <QNetworkInterface>

namespace netaddr {

/// Địa chỉ quảng bá của dải chứa `want`, tra trong các card mạng của máy. Rỗng
/// nếu không card nào của máy nằm cùng dải.
///
/// Không tự thay số cuối bằng 255: cách đó chỉ đúng với dải /24. Hỏi card mạng
/// thì được đúng địa chỉ quảng bá của dải thật, kể cả /16 hay /25.
inline QHostAddress broadcastFor(const QHostAddress &want)
{
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (entry.netmask().isNull() || entry.broadcast().isNull())
                continue;
            if (want.isInSubnet(entry.ip(), entry.prefixLength()))
                return entry.broadcast();
        }
    }
    return {};
}

/// Địa chỉ quảng bá **để điền sẵn vào một ô trên giao diện**, luôn trả về một
/// địa chỉ dùng được.
///
/// Khác broadcastFor() ở hai chỗ, cả hai đều là vì đây là giá trị gợi ý chứ
/// không phải giá trị đem đi gửi ngay:
///
///   * địa chỉ nội bộ (127.x) trả về **chính nó** — dải loopback là /8 nên địa
///     chỉ quảng bá thật của nó là 127.255.255.255, mà lúc chạy thử trên một
///     máy thì gửi thẳng tới 127.0.0.1 mới là điều người dùng muốn;
///   * không tra ra card nào cùng dải (địa chỉ của mạng chưa cắm dây) thì đoán
///     theo dải /24 — đúng với hầu hết mạng nội bộ, và người dùng sửa được.
inline QHostAddress broadcastGuess(const QHostAddress &want)
{
    if (want.isNull())
        return {};
    if (want.isLoopback())
        return want;
    if (const QHostAddress b = broadcastFor(want); !b.isNull())
        return b;

    bool ok = false;
    const quint32 v = want.toIPv4Address(&ok);
    return ok ? QHostAddress(v | 0xffu) : want;
}

} // namespace netaddr
