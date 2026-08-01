#include "udplink.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QSet>
#include <QThread>
#include <QUdpSocket>
#include <QVariant>

namespace {

/// Đệm nhận của socket. Mặc định của hệ điều hành (vài trăm KB) chỉ chứa được
/// khoảng 50 gói RAW_V; một lần luồng mạng bị treo do đĩa hay do trình nền là
/// mất gói. 8 MB cho khoảng 2 giây dữ liệu, đủ để vượt qua mọi khựng ngắn.
constexpr int kSocketRecvBuffer = 8 * 1024 * 1024;

/// Số hiệu card mạng đang mang địa chỉ `ip`:
///   > 0  — đúng card đó
///     0  — ô để trống hoặc 0.0.0.0, nghĩa là nghe trên mọi card
///    -1  — địa chỉ không thuộc card nào trên máy này
int interfaceIndexFor(const QString &ip)
{
    const QString s = ip.trimmed();
    if (s.isEmpty() || s == QLatin1String("0.0.0.0"))
        return 0;

    const QHostAddress want(s);
    if (want.isNull())
        return -1;

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().isEqual(want, QHostAddress::TolerantConversion))
                return iface.index();
        }
    }
    return -1;
}

} // namespace

// ------------------------------------------------------------- bộ đệm 1 ----

void SweepQueue::push(const rawpkt::RawVSweep &s)
{
    QMutexLocker lock(&m_mutex);
    if (int(m_queue.size()) >= m_capacity) {
        m_queue.pop_front();
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    m_queue.push_back(s);
    m_received.fetch_add(1, std::memory_order_relaxed);
}

void SweepQueue::drain(QVector<rawpkt::RawVSweep> &out)
{
    out.clear();
    QMutexLocker lock(&m_mutex);
    out.reserve(int(m_queue.size()));
    for (const rawpkt::RawVSweep &s : m_queue)
        out.push_back(s);
    m_queue.clear();
}

void SweepQueue::clear()
{
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
}

void PlotQueue::push(const rawpkt::RawPCycle &c)
{
    QMutexLocker lock(&m_mutex);
    if (int(m_queue.size()) >= m_capacity) {
        m_queue.pop_front();
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    m_queue.push_back(c);
}

void PlotQueue::drain(QVector<rawpkt::RawPCycle> &out)
{
    out.clear();
    QMutexLocker lock(&m_mutex);
    out.reserve(int(m_queue.size()));
    for (const rawpkt::RawPCycle &c : m_queue)
        out.push_back(c);
    m_queue.clear();
}

void PlotQueue::clear()
{
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
}

// ------------------------------------------------------------- bộ đệm 2 ----

void RecordQueue::push(const QByteArray &datagram)
{
    QMutexLocker lock(&m_mutex);
    m_queue.push_back(datagram);
    m_bytes += datagram.size();
    m_stored.fetch_add(1, std::memory_order_relaxed);

    while (m_bytes > m_capacityBytes && !m_queue.empty()) {
        m_bytes -= m_queue.front().size();
        m_queue.pop_front();
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordQueue::clear()
{
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
    m_bytes = 0;
}

// ------------------------------------------------- luồng nhận dữ liệu ------

/// Sống trọn đời trong luồng mạng. Mở socket, lọc theo địa chỉ remote, giải mã
/// RAW_V rồi đẩy vào hai bộ đệm.
class UdpWorker : public QObject
{
    Q_OBJECT

public:
    UdpWorker(SweepQueue *sweeps, PlotQueue *cycles, RecordQueue *records,
              LinkCounters *counters)
        : m_sweeps(sweeps), m_cycles(cycles), m_records(records),
          m_counters(counters) {}

    /// Gọi từ luồng mạng (qua invokeMethod), trừ setZfbeat — đó chỉ là một
    /// phép ghi atomic nên gọi thẳng từ luồng nào cũng được.
    void open(const QVector<NetEndpoint> &endpoints);
    void close();
    void setZfbeat(quint32 v) { m_zfbeat.store(v, std::memory_order_relaxed); }

signals:
    void failed(const QString &message);

private:
    /// Một cổng đã mở, kèm bộ lọc phía gửi và bộ lọc card mạng.
    struct Bound {
        QUdpSocket  *socket = nullptr;
        QHostAddress remote;        ///< rỗng = nhận từ mọi máy
        quint16      remotePort = 0;///< 0 = nhận từ mọi cổng
        int          ifIndex = 0;   ///< 0 = nhận trên mọi card
    };

    void read(const Bound &b);

    SweepQueue   *m_sweeps;
    PlotQueue    *m_cycles;
    RecordQueue  *m_records;
    LinkCounters *m_counters;

    QVector<Bound> m_bound;
    std::atomic<quint32> m_zfbeat{32768};

    /// Cấp phát một lần rồi dùng lại: 400 lần mỗi giây mà cấp phát 1 KB trên
    /// stack cho mỗi gói thì không sao, nhưng dùng lại thì rõ ý hơn.
    rawpkt::RawVSweep m_scratch;
    rawpkt::RawPCycle m_scratchP;
};

void UdpWorker::open(const QVector<NetEndpoint> &endpoints)
{
    close();

    QSet<quint16> taken;

    for (const NetEndpoint &ep : endpoints) {
        // Ô LocalIP nói **card nào**, chứ không phải địa chỉ đem đi bind. Đài
        // phát RAW_V/RAW_P tới địa chỉ quảng bá của mạng (192.168.1.255), mà
        // socket bind thẳng vào một địa chỉ đơn hướng thì hệ điều hành không
        // bao giờ giao gói quảng bá cho nó — cổng mở thành công, không báo lỗi
        // gì, nhưng màn hình trống trơn. Nên luôn bind mọi địa chỉ rồi lọc lại
        // theo card ở read().
        const int ifIndex = interfaceIndexFor(ep.localIp);
        if (ifIndex < 0) {
            emit failed(tr("%1: LocalIP %2 không phải địa chỉ của card mạng nào "
                           "trên máy — tạm nghe trên mọi card")
                            .arg(ep.name, ep.localIp));
        }

        // Hai dòng cùng một cổng thì dòng sau bind hỏng với thông báo khó hiểu
        // của hệ điều hành; nói thẳng ra đây cho dễ sửa. Trước đây hai dòng
        // khác LocalIP còn mở được cùng cổng, nay bind chung một địa chỉ nên
        // không còn.
        if (taken.contains(ep.localPort)) {
            emit failed(tr("%1: cổng %2 đã dùng cho một dòng phía trên — mỗi "
                           "cổng chỉ khai báo một lần")
                            .arg(ep.name).arg(ep.localPort));
            continue;
        }

        auto *socket = new QUdpSocket(this);
        if (!socket->bind(QHostAddress::AnyIPv4, ep.localPort)) {
            emit failed(tr("%1: không mở được cổng %2 — %3")
                            .arg(ep.name)
                            .arg(ep.localPort)
                            .arg(socket->errorString()));
            delete socket;
            continue;
        }
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                                QVariant(kSocketRecvBuffer));
        taken.insert(ep.localPort);

        Bound b;
        b.socket     = socket;
        b.remote     = ep.acceptsAnyHost() ? QHostAddress() : QHostAddress(ep.remoteIp);
        b.remotePort = ep.remotePort;
        b.ifIndex    = qMax(ifIndex, 0);

        if (!ep.acceptsAnyHost() && b.remote.isNull()) {
            emit failed(tr("%1: địa chỉ RemoteIP không hợp lệ (%2) — tạm nhận "
                           "từ mọi máy").arg(ep.name, ep.remoteIp));
        }

        connect(socket, &QUdpSocket::readyRead, this, [this, b] { read(b); });
        m_bound.push_back(b);
    }
}

void UdpWorker::close()
{
    for (const Bound &b : m_bound)
        delete b.socket;
    m_bound.clear();
}

void UdpWorker::read(const Bound &b)
{
    const quint32 zf = m_zfbeat.load(std::memory_order_relaxed);

    while (b.socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = b.socket->receiveDatagram();
        if (!dg.isValid())
            continue;

        // Lọc theo card mạng: socket nghe trên mọi địa chỉ nên gói của cùng
        // cổng đó từ card khác (wifi, VPN) cũng vào đây. interfaceIndex() bằng
        // 0 là nền tảng không cho biết gói vào từ card nào — khi ấy đành nhận,
        // thà thừa còn hơn câm.
        if (b.ifIndex != 0 && dg.interfaceIndex() != 0
            && dg.interfaceIndex() != b.ifIndex)
            continue;

        // Lọc phía gửi. TolerantConversion để địa chỉ IPv4 tới qua socket
        // IPv6 (dạng ::ffff:a.b.c.d) vẫn khớp với dòng người dùng nhập.
        if (!b.remote.isNull()
            && !dg.senderAddress().isEqual(b.remote, QHostAddress::TolerantConversion))
            continue;
        if (b.remotePort != 0 && dg.senderPort() != b.remotePort)
            continue;

        const QByteArray payload = dg.data();
        const char *raw = payload.constData();
        const int   len = payload.size();

        // Phân loại theo chính nội dung gói chứ không theo cổng nào nhận được:
        // cột "Tên" trong bảng kết nối chỉ là nhãn cho người đọc, đổi tên hay
        // gộp cổng cũng không làm hỏng việc giải mã.
        if (rawpkt::isRawV(raw, len)) {
            rawpkt::decodeRawV(raw, zf, m_scratch);
            m_sweeps->push(m_scratch);
            m_records->push(payload);
            m_counters->rawV.fetch_add(1, std::memory_order_relaxed);
        } else if (rawpkt::isRawP(raw, len)) {
            rawpkt::decodeRawP(raw, m_scratchP);
            m_cycles->push(m_scratchP);
            m_records->push(payload);
            m_counters->rawP.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_counters->other.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// ------------------------------------------------------------- UdpLink -----

UdpLink::UdpLink(QObject *parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("udp-rx"));

    m_worker = new UdpWorker(&m_sweeps, &m_cycles, &m_records, &m_counters);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &UdpWorker::failed, this, &UdpLink::failed);

    m_thread->start();
}

UdpLink::~UdpLink()
{
    m_thread->quit();
    m_thread->wait();
}

void UdpLink::start(const QVector<NetEndpoint> &endpoints)
{
    if (m_running)
        return;

    m_sweeps.clear();
    m_cycles.clear();
    m_records.clear();
    m_counters.reset();

    // Đẩy sang luồng mạng bằng functor thay vì tên slot: khỏi phải đăng ký
    // metatype cho QVector<NetEndpoint>, và sai kiểu là báo ngay lúc biên dịch.
    QMetaObject::invokeMethod(m_worker, [w = m_worker, endpoints] { w->open(endpoints); },
                              Qt::QueuedConnection);
    m_running = true;
    emit runningChanged(true);
}

void UdpLink::stop()
{
    if (!m_running)
        return;

    // Chờ luồng mạng đóng xong socket rồi mới báo đã dừng: bấm "Dừng" là dữ
    // liệu ngừng ngay, không còn gói nào rơi vào bộ đệm sau đó.
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->close(); },
                              Qt::BlockingQueuedConnection);
    m_sweeps.clear();
    m_cycles.clear();
    m_records.clear();

    m_running = false;
    emit runningChanged(false);
}

void UdpLink::setZfbeat(quint32 v)
{
    // Bên kia chỉ là một biến atomic nên gọi thẳng, không phải xếp hàng qua
    // luồng — đổi hệ số giữa chừng có hiệu lực ngay từ gói kế tiếp.
    m_worker->setZfbeat(v);
}

LinkStats UdpLink::stats() const
{
    LinkStats s;
    s.rawV       = m_counters.rawV.load(std::memory_order_relaxed);
    s.rawP       = m_counters.rawP.load(std::memory_order_relaxed);
    s.other      = m_counters.other.load(std::memory_order_relaxed);
    s.droppedV   = m_sweeps.dropped();
    s.droppedP   = m_cycles.dropped();
    s.droppedRec = m_records.dropped();
    return s;
}

#include "udplink.moc"
