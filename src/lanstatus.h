#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QProcess;
class QTimer;
class QVBoxLayout;

/// Cửa sổ nhỏ liệt kê các địa chỉ IP kèm đèn xanh (thông) / đỏ (không thông).
///
/// Kiểm tra bằng lệnh `ping` của hệ điều hành thay vì tự mở socket ICMP: gửi
/// gói ICMP trực tiếp đòi quyền quản trị trên cả ba nền tảng, còn gọi `ping`
/// thì ở đâu cũng chạy được với quyền người dùng thường.
class LanPopup : public QFrame
{
    Q_OBJECT

public:
    explicit LanPopup(QWidget *parent = nullptr);
    ~LanPopup() override;

    /// Danh sách địa chỉ cần theo dõi (đã bỏ trùng lặp).
    void setHosts(const QStringList &ips);

    /// Số địa chỉ đang thông, để tô màu biểu tượng ngoài thanh trạng thái.
    int upCount() const { return m_upCount; }
    int hostCount() const { return m_rows.size(); }

    /// Thời điểm cửa sổ vừa bị đóng — để bấm lần nữa vào biểu tượng thì không
    /// bị mở lại ngay (Qt::Popup nuốt cú bấm đó để tự đóng).
    const QElapsedTimer &hiddenAt() const { return m_hiddenAt; }

signals:
    void statusChanged();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    struct Row {
        QString   ip;
        QWidget  *widget = nullptr;   ///< hàng chứa nhãn và đèn
        QLabel   *dot    = nullptr;
        QProcess *proc   = nullptr;
        bool      up     = false;
    };

    void rebuildRows();
    void pingAll();
    void stopPings();
    void setRowState(int index, bool up);

    QVBoxLayout  *m_lay     = nullptr;
    QLabel       *m_empty   = nullptr;
    QTimer       *m_refresh = nullptr;
    QVector<Row>  m_rows;
    QStringList   m_hosts;
    int           m_upCount = 0;
    QElapsedTimer m_hiddenAt;
};

/// Biểu tượng mạng LAN ở góc trái thanh trạng thái. Bấm để mở/đóng LanPopup.
class LanIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit LanIndicator(QWidget *parent = nullptr);

    /// Danh sách địa chỉ lấy từ bảng cổng nhận/gửi bên tab "Kết nối".
    void setHosts(const QStringList &ips);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    QSize sizeHint() const override;

private:
    LanPopup *m_popup = nullptr;
};
