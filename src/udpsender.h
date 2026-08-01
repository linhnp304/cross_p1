#pragma once

// Gửi PlotTC và Track đi các hệ thống khác.
//
// Để riêng khỏi UdpLink (phần nhận) và **chạy trên luồng giao diện**, không đẩy
// sang luồng mạng như phía nhận. Lý do: phía nhận là vài trăm gói mỗi giây, cỡ
// vài MB mỗi giây, đủ để làm nghẽn luồng giao diện; còn phía gửi chỉ vài gói
// mỗi vòng quét — đưa sang luồng khác thì phải thêm hàng đợi và khoá cho một
// việc chưa tới một phần nghìn tải của phía nhận.

#include "appparams.h"

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

    /// Gửi một datagram tới mọi dòng đang bật và đúng loại dữ liệu.
    void send(TxKind kind, const QByteArray &datagram);

    /// Số dòng đang thực sự gửi được, cho dòng trạng thái.
    int activeCount() const { return m_targets.size(); }

    quint64 sentPlots() const  { return m_sentPlots; }
    quint64 sentTracks() const { return m_sentTracks; }

signals:
    /// Lỗi mở cổng hoặc lỗi gửi — nội dung đã sẵn sàng hiện cho người dùng.
    /// Chỉ phát một lần cho mỗi lỗi giống nhau, không phát lại mỗi gói.
    void failed(const QString &message);

private:
    struct Target {
        QUdpSocket  *socket = nullptr;
        QHostAddress remote;
        quint16      remotePort = 0;
        TxKind       kind = TxKind::Plot;
        bool         reported = false;   ///< đã báo lỗi gửi cho dòng này rồi
    };

    void close();

    /// Địa chỉ đích thật sự của một dòng: chính RemoteIP, hoặc địa chỉ quảng bá
    /// của dải chứa nó khi bật ô Broadcast.
    QHostAddress resolveTarget(const NetEndpoint &e, QString &error) const;

    QVector<Target> m_targets;
    quint64 m_sentPlots  = 0;
    quint64 m_sentTracks = 0;
};
