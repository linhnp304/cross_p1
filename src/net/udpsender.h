#pragma once

// Gửi PlotTC và Track đi các hệ thống khác.
//
// Để riêng khỏi UdpLink (phần nhận) và **chạy trên luồng giao diện**, không đẩy
// sang luồng mạng như phía nhận. Lý do: phía nhận là vài trăm gói mỗi giây, cỡ
// vài MB mỗi giây, đủ để làm nghẽn luồng giao diện; còn phía gửi chỉ vài gói
// mỗi vòng quét — đưa sang luồng khác thì phải thêm hàng đợi và khoá cho một
// việc chưa tới một phần nghìn tải của phía nhận.

#include "app/appparams.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QVector>

class QUdpSocket;

class UdpSender : public QObject
{
    Q_OBJECT

public:
    explicit UdpSender(QObject *parent = nullptr);
    ~UdpSender() override;

    /// Mở lại toàn bộ socket theo bảng cổng gửi. Dòng nào không bật ô "Gửi"
    /// thì bỏ qua, không mở socket. Gọi lại mỗi khi bảng đổi.
    void setEndpoints(const QVector<NetEndpoint> &tx);

    /// Gửi một datagram tới mọi dòng đang bật và đúng loại dữ liệu. Trả về
    /// true khi có ít nhất một dòng nhận được — nơi gửi lệnh điều khiển cần
    /// biết để còn báo cho người dùng.
    bool send(TxKind kind, const QByteArray &datagram);

    /// Bật/tắt đường nhận trạng thái ngay trên socket gửi lệnh — ô "Tự động cấu
    /// hình cổng nhận Status..." của tab "Kết nối". Tắt thì gói về vẫn được đọc
    /// (để không đọng trong đệm của hệ điều hành) nhưng bỏ đi, vì khi ấy dòng
    /// "Status" trong bảng cổng nhận mới là nơi nghe.
    void setReceiveStatus(bool on) { m_receiveStatus = on; }

    /// Số dòng đang thực sự gửi được, cho dòng trạng thái.
    int activeCount() const { return m_targets.size(); }

    /// Riêng số dòng của một loại dữ liệu.
    int activeCount(TxKind kind) const;

    /// Cổng nguồn thật sự của dòng "Command" đầu tiên đang mở — chính là cổng
    /// hệ thống thật sẽ trả trạng thái về. 0 = chưa mở được dòng nào.
    quint16 commandLocalPort() const;

    quint64 sentPlots() const    { return m_sentPlots; }
    quint64 sentTracks() const   { return m_sentTracks; }
    quint64 sentCommands() const { return m_sentCommands; }
    quint64 recvStatus() const   { return m_recvStatus; }

signals:
    /// Lỗi mở cổng hoặc lỗi gửi — nội dung đã sẵn sàng hiện cho người dùng.
    /// Chỉ phát một lần cho mỗi lỗi giống nhau, không phát lại mỗi gói.
    void failed(const QString &message);

    /// Một gói trạng thái vừa về **trên chính socket gửi lệnh**.
    ///
    /// Hệ thống thật trả lời về đúng cổng nguồn của gói lệnh vừa nhận được, mà
    /// cổng đó thường do hệ điều hành tự chọn nên không khai trước được trong
    /// bảng cổng nhận. Hai socket không cùng bind được một cổng, nên nơi duy
    /// nhất nghe được câu trả lời là chính socket đã gửi câu hỏi.
    void statusReceived(const QByteArray &datagram);

private:
    struct Target {
        QUdpSocket  *socket = nullptr;
        QHostAddress remote;
        quint16      remotePort = 0;
        TxKind       kind = TxKind::Plot;
        bool         reported = false;   ///< đã báo lỗi gửi cho dòng này rồi

        /// Card đi ra, ghim vào từng gói lúc gửi thay vì bind vào socket. Rỗng
        /// là để hệ điều hành chọn theo bảng định tuyến. Xem chỗ bind trong
        /// rebuild(): dòng "Command" phải bind mọi địa chỉ mới nhận được gói
        /// trạng thái quảng bá, nên không bind ghim card được nữa.
        QHostAddress source;
    };

    void close();

    /// Địa chỉ đích thật sự của một dòng: chính RemoteIP, hoặc địa chỉ quảng bá
    /// của dải chứa nó khi bật ô Broadcast.
    QHostAddress resolveTarget(const NetEndpoint &e, QString &error) const;

    /// Vét các gói vừa về trên một socket của dòng "Command".
    void readStatus(QUdpSocket *socket);

    QVector<Target> m_targets;
    bool    m_receiveStatus = true;
    quint64 m_sentPlots    = 0;
    quint64 m_sentTracks   = 0;
    quint64 m_sentCommands = 0;
    quint64 m_recvStatus   = 0;
};
