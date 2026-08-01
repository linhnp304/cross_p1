#include "udpsender.h"

#include <QNetworkInterface>
#include <QUdpSocket>

namespace {

/// Địa chỉ quảng bá của dải chứa `want`, tra trong các card mạng của máy.
///
/// Không tự thay số cuối bằng 255: cách đó chỉ đúng với dải /24. Hỏi card mạng
/// thì được đúng địa chỉ quảng bá của dải thật, kể cả /16 hay /25.
QHostAddress broadcastFor(const QHostAddress &want)
{
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
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

} // namespace

UdpSender::UdpSender(QObject *parent)
    : QObject(parent)
{
}

UdpSender::~UdpSender()
{
    close();
}

void UdpSender::close()
{
    for (const Target &t : m_targets)
        delete t.socket;
    m_targets.clear();
}

QHostAddress UdpSender::resolveTarget(const NetEndpoint &e, QString &error) const
{
    const QHostAddress remote(e.remoteIp.trimmed());
    if (remote.isNull()) {
        error = tr("RemoteIP không hợp lệ (%1)").arg(e.remoteIp);
        return {};
    }

    if (!e.broadcast)
        return remote;

    if (const QHostAddress b = broadcastFor(remote); !b.isNull())
        return b;

    // Không card nào của máy nằm cùng dải với RemoteIP — gửi quảng bá kiểu gì
    // cũng không tới đúng chỗ, nói thẳng ra thay vì âm thầm gửi đơn hướng.
    error = tr("bật Broadcast nhưng không card mạng nào của máy cùng dải với "
               "RemoteIP %1").arg(e.remoteIp);
    return {};
}

void UdpSender::setEndpoints(const QVector<NetEndpoint> &tx)
{
    close();

    for (const NetEndpoint &e : tx) {
        if (!e.enabled)
            continue;   // dòng chưa bật ô "Gửi" thì không mở socket

        QString error;
        const QHostAddress remote = resolveTarget(e, error);
        if (remote.isNull()) {
            emit failed(tr("Cổng gửi: %1").arg(error));
            continue;
        }
        if (e.remotePort == 0) {
            emit failed(tr("Cổng gửi tới %1: RemotePort phải khác 0")
                            .arg(e.remoteIp));
            continue;
        }

        auto *socket = new QUdpSocket(this);

        // LocalPort = 0 và LocalIP để trống thì khỏi bind: hệ điều hành tự chọn
        // cổng nguồn và card ra. Có LocalIP thì bind vào đó, vì với phía gửi
        // LocalIP là **card đi ra**, khác hẳn ý nghĩa của nó ở bảng nhận.
        const QString localIp = e.localIp.trimmed();
        const bool wantBind = e.localPort != 0
                           || (!localIp.isEmpty() && localIp != QLatin1String("0.0.0.0"));
        if (wantBind) {
            const QHostAddress local = localIp.isEmpty()
                                           ? QHostAddress(QHostAddress::AnyIPv4)
                                           : QHostAddress(localIp);
            if (local.isNull() || !socket->bind(local, e.localPort)) {
                emit failed(tr("Cổng gửi: không bind được %1:%2 — %3")
                                .arg(localIp.isEmpty() ? QStringLiteral("0.0.0.0") : localIp)
                                .arg(e.localPort)
                                .arg(socket->errorString()));
                delete socket;
                continue;
            }
        }

        Target t;
        t.socket     = socket;
        t.remote     = remote;
        t.remotePort = e.remotePort;
        t.kind       = e.kind;
        m_targets.push_back(t);
    }
}

void UdpSender::send(TxKind kind, const QByteArray &datagram)
{
    bool any = false;

    for (Target &t : m_targets) {
        if (t.kind != kind)
            continue;
        any = true;

        if (t.socket->writeDatagram(datagram, t.remote, t.remotePort) < 0) {
            // Báo một lần cho mỗi dòng: gửi hỏng thì gói nào cũng hỏng, mà mỗi
            // vòng quét có hàng chục gói — báo từng gói là ngập dòng trạng thái.
            if (!t.reported) {
                t.reported = true;
                emit failed(tr("Cổng gửi tới %1:%2 — %3")
                                .arg(t.remote.toString())
                                .arg(t.remotePort)
                                .arg(t.socket->errorString()));
            }
        } else {
            t.reported = false;
        }
    }

    if (!any)
        return;
    if (kind == TxKind::Plot)
        ++m_sentPlots;
    else
        ++m_sentTracks;
}
