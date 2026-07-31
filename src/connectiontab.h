#pragma once

#include "appparams.h"

#include <QWidget>

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

    /// Đổi chữ trên nút và khoá bảng khi đang kết nối — bảng chỉ sửa được lúc
    /// đã dừng, tránh cảnh sửa cổng giữa chừng rồi tưởng là đã có hiệu lực.
    void setRunning(bool on);

    /// Dòng trạng thái dưới bảng (số gói nhận được, lỗi mở cổng...).
    void setStatusText(const QString &text, bool isError = false);

signals:
    /// Danh sách cổng nhận vừa đổi — cần lưu xuống params.json.
    ///
    /// Chỉ phát đúng phần của mình chứ không phát cả AppParams: bản sao tham
    /// số kỹ thuật ở tab này có thể đã cũ, phát đi là đè mất giá trị vừa được
    /// áp dụng bên tab "Tham số".
    void endpointsChanged(const QVector<NetEndpoint> &rx);

    /// Người dùng bấm nút Kết nối / Dừng kết nối.
    void connectRequested();
    void disconnectRequested();

private:
    void rebuildTable();
    void addRow();
    void removeSelectedRow();

    /// Đọc bảng vào m_params.rx. Ô sai định dạng được tô đỏ; trả về false nếu
    /// có ô sai.
    bool readTable();

    void onCellChanged();

    AppParams m_params;
    bool      m_loading = false;
    bool      m_running = false;

    QTableWidget *m_rx     = nullptr;
    QPushButton  *m_add    = nullptr;
    QPushButton  *m_remove = nullptr;
    QPushButton  *m_toggle = nullptr;
    QLabel       *m_status = nullptr;
};
