#pragma once

#include "app/appparams.h"

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// Tab "Kết nối" trong panel 2.1 — danh sách cổng UDP và nút bật/tắt nhận dữ liệu.
///
/// Cột "Tên" chỉ là nhãn cho người đọc: gói tin được phân loại theo header của
/// chính nó, nên đổi tên hay gộp cổng cũng không làm hỏng việc giải mã.
class ConnectionTab : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectionTab(QWidget *parent = nullptr);

    void setParams(const AppParams &p);

    /// Đổi chữ trên nút và khoá bảng khi đang nhận dữ liệu — bảng cổng nhận chỉ
    /// sửa được lúc đã dừng, tránh cảnh sửa cổng giữa chừng rồi tưởng là đã có
    /// hiệu lực.
    void setRunning(bool on);

    /// Tương tự cho nút bật/tắt việc gửi dữ liệu. Bảng cổng gửi thì **không**
    /// khoá: ô "Gửi" của từng dòng sinh ra là để bật/tắt giữa chừng.
    void setTxRunning(bool on);

    /// Dòng trạng thái dưới bảng (số gói nhận được, lỗi mở cổng...).
    void setStatusText(const QString &text, bool isError = false);

    /// Các chức năng đang bật mà phải tắt trước khi thoát phần mềm. Rỗng là
    /// thoát được: nút "Thoát phần mềm" mở, ngược lại nó bị khoá và chú giải
    /// của nó kể ra đang vướng những gì.
    void setExitBlockers(const QStringList &busy);

signals:
    /// Danh sách cổng nhận vừa đổi — cần lưu xuống params.json.
    ///
    /// Chỉ phát đúng phần của mình chứ không phát cả AppParams: bản sao tham
    /// số kỹ thuật ở tab này có thể đã cũ, phát đi là đè mất giá trị vừa được
    /// áp dụng bên tab "Tham số".
    void endpointsChanged(const QVector<NetEndpoint> &rx);

    /// Danh sách cổng gửi vừa đổi. Phát cả khi chỉ bật/tắt ô "Gửi" — nơi nhận
    /// mở lại socket theo danh sách mới.
    void txEndpointsChanged(const QVector<NetEndpoint> &tx);

    /// Ô "Tự động cấu hình cổng nhận Status..." vừa được bật/tắt.
    void statusFollowsCommandChanged(bool on);

    /// Người dùng bấm nút Bắt đầu / Dừng nhận dữ liệu.
    void connectRequested();
    void disconnectRequested();

    /// Người dùng bấm nút Bắt đầu / Dừng gửi dữ liệu.
    void sendStartRequested();
    void sendStopRequested();

    /// Người dùng bấm nút "Thoát phần mềm".
    void exitRequested();

private:
    void rebuildTable();
    void addRow();
    void removeSelectedRow();

    /// Đọc bảng vào m_params.rx. Ô sai định dạng được tô đỏ; trả về false nếu
    /// có ô sai.
    bool readTable();

    void onCellChanged();

    // --- bảng cổng gửi ---
    void rebuildTxTable();
    void addTxRow();
    void removeSelectedTxRow();
    bool readTxTable();
    void onTxCellChanged();

    /// Dựng các widget trong ô của một dòng gửi (hai ô đánh dấu và ComboBox).
    void fillTxWidgets(int row, const NetEndpoint &e);

    /// Ép ô "Gửi" của dòng Command tích sẵn và khoá lại.
    void syncTxSendBoxes();

    /// Làm mờ dòng "Status" khi ô tự động đang bật — dòng vẫn sửa được, chỉ là
    /// không còn hiệu lực. Không có dấu hiệu này thì bảng hiện một dòng cấu hình
    /// đúng đắn mà chẳng làm gì, đúng kiểu lỗi không nhìn ra được.
    void markStatusRow();

    AppParams m_params;
    bool      m_loading   = false;
    bool      m_running   = false;
    bool      m_txRunning = false;

    QTableWidget *m_rx         = nullptr;
    QCheckBox    *m_autoStatus = nullptr;
    QPushButton  *m_add        = nullptr;
    QPushButton  *m_remove     = nullptr;
    QPushButton  *m_toggle     = nullptr;
    QLabel       *m_status     = nullptr;

    QTableWidget *m_tx       = nullptr;
    QPushButton  *m_txAdd    = nullptr;
    QPushButton  *m_txRemove = nullptr;
    QPushButton  *m_txToggle = nullptr;

    QPushButton  *m_exit     = nullptr;
};
