#pragma once

#include "net/filterproto.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

/// Cửa sổ "Điều khiển các bộ lọc" — nạp hệ số bốn bộ lọc xuống đài.
///
/// Bảng 1024 dòng × chín cột: cột "STT", rồi mỗi bộ lọc hai cột — cột **S** là
/// giá trị đem gửi đi (sửa được), cột **R** là giá trị đài trả về (chỉ đọc, đổi
/// chữ đỏ ở dòng nào lệch). Bộ lọc nào ít hơn 1024 phần tử thì các dòng dưới của
/// hai cột ấy để trắng.
///
/// Dữ liệu **đọc lại từ đĩa mỗi lần mở cửa sổ** (xem reload()): thư mục ./filter
/// là chỗ người lắp đặt copy file vào, nên sửa file rồi mở lại cửa sổ là thấy
/// giá trị mới — không phải khởi động lại phần mềm. Thiếu file hay file sai thì
/// nút "Gửi lệnh" bị khoá và câu lỗi hiện ở cuối cửa sổ.
///
/// Giống cửa sổ ADF4159 và khác tab "Điều khiển": ở đây **phải bấm nút Gửi
/// lệnh**, vì một bộ lọc chỉ có nghĩa khi cả dãy hệ số xuống đài, không phải
/// từng phần tử một.
class FilterWindow : public QDialog
{
    Q_OBJECT

public:
    explicit FilterWindow(QWidget *parent = nullptr);

    /// Đọc lại cả bốn file dữ liệu và đổ lên bảng. Nơi gọi chạy hàm này **trước
    /// mỗi lần show()**.
    void reload();

    /// Một datagram vừa nhận được ở cổng "Status". Trả về true nếu đúng là trạng
    /// thái phản hồi của một trong bốn lệnh nạp bộ lọc.
    bool applyStatus(const QByteArray &datagram);

    /// Kết quả của lần gửi vừa rồi, do nơi nhận commandReady() báo lại.
    void setSendResult(int kind, bool ok);

    void setStatusText(const QString &text, bool isError = false);

signals:
    /// Một gói lệnh đã sẵn sàng. Nơi nhận **phải** gọi setSendResult() ngay
    /// trong lúc xử lý tín hiệu này: số Serial của lần gửi hỏng được trả lại chứ
    /// không tiêu mất, mà cặp serial trên nhãn cột cũng chỉ đổi khi gói thật sự
    /// ra khỏi máy.
    void commandReady(int kind, const QByteArray &datagram);

private:
    /// Cột của bảng. Bộ lọc thứ k chiếm hai cột liền nhau, nên chỉ số cột suy ra
    /// từ chỉ số bộ lọc chứ không liệt kê tay — thêm một bộ lọc vào bảng
    /// kPackets là bảng tự mọc thêm hai cột.
    static int sendColumn(int kind) { return 1 + kind * 2; }
    static int recvColumn(int kind) { return 2 + kind * 2; }
    static constexpr int kColumnCount = 1 + filterproto::KindCount * 2;

    struct KindUi {
        /// Giá trị đang có trên cột S. Luôn đủ count phần tử; thiếu file thì
        /// toàn 0 và cột để trắng.
        QVector<quint32> values;

        /// Giá trị của **lần gửi lệnh gần nhất** — cột R so với cái này chứ
        /// không so với ô đang hiện trên màn hình. Người dùng sửa một ô sau khi
        /// đã gửi thì đài vẫn đang dùng giá trị cũ, tô đỏ theo ô mới là nói sai.
        QVector<quint32> sent;

        QVector<quint32> status;

        QString error;          ///< câu lỗi đọc file, rỗng là đọc được
        bool    hasData   = false;
        bool    hasSent   = false;
        bool    hasStatus = false;
        quint32 cmdSerial    = 0;
        quint32 statusSerial = 0;
    };

    /// Dựng 1024 × 9 ô một lần rồi chỉ đổi chữ. Dựng lại mỗi lần mở cửa sổ thì
    /// là chín nghìn lần cấp phát cho một việc chỉ cần đổi chữ.
    void buildTable();

    /// Nhãn cột của một bộ lọc, kèm cặp serial.
    void refreshHeader(int kind);

    /// Đổ giá trị của một bộ lọc lên hai cột của nó.
    void refreshColumns(int kind);

    /// Tô lại phần lệch giữa lệnh đã gửi và trạng thái nhận về, cho một bộ lọc.
    void refreshMismatch(int kind);

    /// Gom lại các câu lỗi đọc file và khoá/mở nút "Gửi lệnh" theo đó.
    void refreshErrors();

    /// Người dùng vừa sửa một ô cột S.
    void onItemChanged(QTableWidgetItem *item);

    /// Bấm "Gửi lệnh": bắt đầu chuỗi bốn gói cách nhau 100 ms.
    void startSending();

    /// Đóng và phát một gói trong chuỗi.
    void sendOne(int kind);

    /// Giá trị `v` viết ra dạng 0x00000000.
    static QString hexText(quint32 v);

    QTableWidget *m_table  = nullptr;
    QPushButton  *m_send   = nullptr;
    QLabel       *m_errors = nullptr;
    QLabel       *m_status = nullptr;

    /// Nhịp 100 ms giữa bốn gói. Mô tả giao thức không nói vì sao, nhưng đài nạp
    /// bộ lọc mất thời gian thật — bắn liền bốn gói 4 KB thì gói sau tới lúc gói
    /// trước còn chưa nạp xong.
    QTimer *m_pace   = nullptr;
    int     m_nextTx = filterproto::KindCount;   ///< == KindCount là đang rỗi

    KindUi m_kinds[filterproto::KindCount];

    /// Câu trả lời của setSendResult() cho lần phát commandReady() đang dở.
    bool m_lastSendOk = false;

    bool m_loading = false;
};
