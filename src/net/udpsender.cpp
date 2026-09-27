#include "net/udpsender.h"

#include "net/adfproto.h"
#include "net/cmdproto.h"
#include "net/filterproto.h"
#include "net/netaddr.h"

#include <QNetworkDatagram>
#include <QUdpSocket>

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

    if (const QHostAddress b = netaddr::broadcastFor(remote); !b.isNull())
        return b;

    // Không card nào của máy nằm cùng dải với RemoteIP — gửi quảng bá kiểu gì
    // cũng không tới đúng chỗ, nói thẳng ra thay vì âm thầm gửi đơn hướng.
    error = tr("bật Broadcast nhưng không card mạng nào của máy cùng dải với "
               "RemoteIP %1").arg(e.remoteIp);
    return {};
}

void UdpSender::setEndpoints(const QVector<NetEndpoint> &tx)
{
    // Cổng nguồn của các dòng "Command" phải giữ nguyên qua mỗi lần dựng lại
    // bảng: đài trả trạng thái về đúng cổng nó vừa nhận lệnh, nên đổi cổng là
    // các gói trạng thái tiếp theo rơi vào hư không cho tới lệnh kế tiếp. Mà
    // hàm này chạy lại cả khi trắc thủ chỉ bấm "Bắt đầu gửi dữ liệu".
    //
    // Nhớ theo **thứ tự các dòng Command**, không theo chỉ số dòng trong bảng:
    // xoá một dòng Plot phía trên không được làm dòng Command đổi cổng.
    QVector<quint16> keep;
    for (const Target &t : m_targets) {
        if (t.kind == TxKind::Command)
            keep.push_back(t.socket->localPort());
    }
    int cmdSeen = 0;

    close();

    for (const NetEndpoint &e : tx) {
        // Dòng chưa bật ô "Gửi" thì không mở socket. Dòng lệnh điều khiển thì
        // ô đó luôn bật — xem NetEndpoint::sends().
        if (!e.sends())
            continue;

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
        //
        // Dòng "Command" là ngoại lệ: nó còn là nơi **nhận** trạng thái phản hồi
        // (xem statusReceived), nên phải bind ngay cả khi cả hai ô đều để trống.
        // Bind với cổng 0 vẫn để hệ điều hành tự chọn, chỉ khác là chọn ngay bây
        // giờ thay vì đợi gói lệnh đầu tiên — nhờ vậy commandLocalPort() có số
        // để hiện lên giao diện, và gói trạng thái nào về trước cả lệnh đầu tiên
        // cũng không rơi vào hư không.
        const QString localIp = e.localIp.trimmed();
        const bool anyIp = localIp.isEmpty() || localIp == QLatin1String("0.0.0.0");
        const bool wantBind = e.localPort != 0 || e.kind == TxKind::Command || !anyIp;

        // Dòng "Command" luôn bind mọi địa chỉ, kể cả khi ô LocalIP có ghi card.
        // Bind ghim vào một địa chỉ card thì hệ điều hành **không** giao gói có
        // địa chỉ đích là địa chỉ quảng bá cho socket đó — mà đài trả trạng thái
        // bằng gói quảng bá. Cổng vẫn mở, không lỗi gì, chỉ là không bao giờ có
        // gói nào về. Card đi ra vẫn giữ được: ghim vào từng gói lúc gửi bằng
        // QNetworkDatagram::setSender(), xem send().
        QHostAddress source;
        if (!anyIp && e.kind == TxKind::Command)
            source = QHostAddress(localIp);

        if (wantBind) {
            const QHostAddress local = (anyIp || e.kind == TxKind::Command)
                                           ? QHostAddress(QHostAddress::AnyIPv4)
                                           : QHostAddress(localIp);

            // Cổng của lần dựng trước, nếu lần này vẫn để hệ điều hành tự chọn.
            quint16 want = e.localPort;
            if (e.kind == TxKind::Command) {
                if (want == 0 && cmdSeen < keep.size())
                    want = keep.at(cmdSeen);
                ++cmdSeen;
            }

            // Thử lại với 0 khi cổng cũ đã bị chương trình khác chiếm mất: giữ
            // được cổng cũ là điều tốt, nhưng không gửi được lệnh nữa thì không.
            if (local.isNull()
                || (!socket->bind(local, want)
                    && (want == e.localPort || !socket->bind(local, e.localPort)))) {
                emit failed(tr("Cổng gửi: không bind được %1:%2 — %3")
                                .arg(localIp.isEmpty() ? QStringLiteral("0.0.0.0") : localIp)
                                .arg(e.localPort)
                                .arg(socket->errorString()));
                delete socket;
                continue;
            }
        }

        if (e.kind == TxKind::Command) {
            connect(socket, &QUdpSocket::readyRead, this,
                    [this, socket] { readStatus(socket); });
        }

        Target t;
        t.socket     = socket;
        t.remote     = remote;
        t.remotePort = e.remotePort;
        t.kind       = e.kind;
        t.source     = source;
        m_targets.push_back(t);
    }
}

void UdpSender::readStatus(QUdpSocket *socket)
{
    while (socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = socket->receiveDatagram();
        if (!dg.isValid())
            continue;

        // Vẫn đọc rồi bỏ khi đường này đang tắt — xem setReceiveStatus().
        if (!m_receiveStatus)
            continue;

        // Không lọc theo địa chỉ phía gửi: cổng này do hệ điều hành chọn nên
        // không ai khác biết mà gửi tới, còn chính đài thì trả lời từ cổng nào
        // là việc của nó. Lọc theo **nội dung gói** thay vì theo địa chỉ, giống
        // hệt cách bên nhận phân loại (xem UdpWorker::read).
        const QByteArray payload = dg.data();
        if (!cmdproto::isStatus(payload.constData(), payload.size())
            && !adfproto::isStatus(payload.constData(), payload.size())
            && !filterproto::isStatus(payload.constData(), payload.size()))
            continue;

        ++m_recvStatus;
        emit statusReceived(payload);
    }
}

quint16 UdpSender::commandLocalPort() const
{
    for (const Target &t : m_targets) {
        if (t.kind == TxKind::Command)
            return t.socket->localPort();
    }
    return 0;
}

int UdpSender::activeCount(TxKind kind) const
{
    int n = 0;
    for (const Target &t : m_targets) {
        if (t.kind == kind)
            ++n;
    }
    return n;
}

bool UdpSender::send(TxKind kind, const QByteArray &datagram)
{
    bool any = false;

    for (Target &t : m_targets) {
        if (t.kind != kind)
            continue;

        // Ghim card đi ra ở mức từng gói khi socket phải bind mọi địa chỉ —
        // xem chú thích chỗ bind trong rebuild().
        qint64 written;
        if (t.source.isNull()) {
            written = t.socket->writeDatagram(datagram, t.remote, t.remotePort);
        } else {
            QNetworkDatagram dg(datagram, t.remote, t.remotePort);
            dg.setSender(t.source, 0);
            written = t.socket->writeDatagram(dg);
        }

        if (written < 0) {
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
            any = true;
        }
    }

    if (!any)
        return false;

    switch (kind) {
    case TxKind::Plot:     ++m_sentPlots;    break;
    case TxKind::Track:    ++m_sentTracks;   break;
    case TxKind::Command:  ++m_sentCommands; break;
    case TxKind::CtrlSync: ++m_sentCtrlSync; break;
    }
    return true;
}
