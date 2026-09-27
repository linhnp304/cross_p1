#pragma once

#include "net/cmdproto.h"

#include <QVector>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

/// Tab "Điều khiển" trong panel 2.1 — bốn gói lệnh điều khiển đài.
///
/// Không có nút "Gửi": mỗi lần người dùng đổi một ô nhập hay một lựa chọn là
/// **cả gói** của group đó được gửi đi ngay. Ô nhập tắt keyboardTracking nên gõ
/// dở một con số chưa phát lệnh — chỉ khi rời ô, bấm Enter, hay bấm mũi tên.
///
/// Giao diện dựng thẳng từ bảng trường trong cmdproto.h, không viết tay từng ô:
/// thứ tự và cách quy đổi của chúng phải khớp với gói tin, mà giữ khớp bằng mắt
/// giữa hai danh sách 23 dòng là việc không ai làm nổi lâu dài.
///
/// Tab tự bọc phần thân của mình trong vùng cuộn, không để nơi gọi bọc như các
/// tab khác: ba nút "Mở khóa điều khiển", "Điều khiển ADF4159", "Điều khiển các
/// bộ lọc" và nhãn CtrlIP phải luôn nhìn thấy, không được trôi theo bốn group
/// lệnh ở dưới.
class ControlTab : public QWidget
{
    Q_OBJECT

public:
    explicit ControlTab(QWidget *parent = nullptr);

    /// Đổ giá trị đã lưu vào giao diện. **Không** phát lệnh nào.
    void setValues(const cmdproto::Values &v);
    const cmdproto::Values &values() const { return m_values; }

    /// Một datagram vừa nhận được ở cổng "Status". Trả về true nếu đúng là
    /// trạng thái phản hồi của một trong bốn lệnh.
    bool applyStatus(const QByteArray &datagram);

    /// Kết quả của lần gửi vừa rồi, do nơi nhận commandReady() báo lại.
    void setSendResult(int group, bool ok);

    /// Dòng trạng thái dưới cùng của tab.
    void setStatusText(const QString &text, bool isError = false);

    /// Giá trị **trạng thái** mới nhất của một trường, dạng gói tin. False khi
    /// group đó chưa nhận được gói trạng thái nào, hoặc không có trường đó.
    bool statusValue(int group, const char *name, quint32 &out) const;

    /// Khóa (hay mở khóa) toàn bộ điều khiển trong tab.
    ///
    /// Khóa là trạng thái mặc định lúc mở phần mềm, và là trạng thái bình
    /// thường của cả ca trực: không ô nào nhận được thao tác, các ô đi theo giá
    /// trị trong gói trạng thái đài trả về, nên tab chỉ còn là chỗ **đọc** xem
    /// đài đang đặt ở đâu. Mở khóa mới là lúc mỗi lần vặn một ô là một lệnh
    /// thật đi ra đài — xem sendGroup().
    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }

    /// Nhãn "CtrlIP: ..." — máy nào trong hệ thống đang giữ quyền điều khiển.
    ///
    /// Chữ do nơi gọi đặt, không phải do tab tự tính: địa chỉ ấy đến từ gói
    /// CTRL_SYNC và từ bảng cổng gửi, cả hai đều nằm ngoài tab này.
    void setCtrlIpText(const QString &text, bool isLocal);

signals:
    /// Một gói lệnh đã sẵn sàng. Nơi nhận **phải** gọi setSendResult() sau đó,
    /// vì số Serial của lần gửi hỏng được trả lại chứ không tiêu mất.
    void commandReady(int group, const QByteArray &datagram);

    /// Giá trị điều khiển vừa đổi — cần lưu xuống params.json, và cách tính
    /// Video[1024] cũng lấy ba trường từ đây (DataSend, ZFbeat, GainU).
    void valuesChanged();

    /// Vừa nhận được gói trạng thái của một group. Nơi nhận tự tra trường mình
    /// cần bằng statusValue().
    void statusReceived(int group);

    /// Bấm nút "Điều khiển ADF4159" — mở cửa sổ điều khiển kit tạo tín hiệu.
    void adf4159Requested();

    /// Bấm nút "Điều khiển các bộ lọc" — mở cửa sổ nạp hệ số bốn bộ lọc.
    void filterRequested();

    /// Vừa khóa hay mở khóa điều khiển — **chỉ phát khi trạng thái thật sự
    /// đổi**. Nơi nhận đóng hai cửa sổ "Điều khiển ADF4159" và "Điều khiển các
    /// bộ lọc" khi khóa lại (chúng cũng là chỗ ra lệnh cho đài, để mở tiếp thì
    /// khóa tab này chẳng khóa được gì), và quảng bá một gói CTRL_SYNC khi mở
    /// khóa — xem mục đồng bộ điều khiển trong README.
    ///
    /// Chỉ phát khi đổi chứ không phát mỗi lần gọi, vì nơi nhận **gửi gói tin**
    /// theo tín hiệu này: gọi setLocked(false) hai lần liền là hai gói chiếm
    /// quyền đi ra cho một cú bấm nút.
    void lockChanged(bool locked);

private:
    /// Phần giao diện của một trường. Trường chưa dùng đến để trống hết.
    struct Cell {
        QDoubleSpinBox *spin     = nullptr;
        QComboBox      *combo    = nullptr;
        QButtonGroup   *radios   = nullptr;
        QLineEdit      *readout  = nullptr;   ///< trường chỉ nhận trạng thái
        QLabel         *mismatch = nullptr;   ///< giá trị trạng thái, chữ đỏ
    };

    struct GroupUi {
        QGroupBox     *box          = nullptr;
        QVector<Cell>  cells;
        quint32        cmdSerial    = 0;
        quint32        statusSerial = 0;
        bool           hasStatus    = false;
        quint32        status[cmdproto::kMaxFields] = {};
    };

    QGroupBox *buildGroup(int group);

    /// Gom giá trị của group từ giao diện, đóng gói rồi phát commandReady().
    void sendGroup(int group);

    /// Đổ lại giá trị của một group lên giao diện.
    void loadGroup(int group);

    /// Lấy giá trị trong gói trạng thái mới nhất của một group làm giá trị của
    /// các ô điều khiển được, rồi đổ lên giao diện. Trả về true nếu có ô đổi
    /// giá trị. Chỉ dùng lúc đang khóa điều khiển — lúc mở khóa thì giá trị
    /// trên ô là của người dùng, đè lên là mất chỗ họ vừa vặn tới.
    bool adoptStatus(int group);

    /// Tô lại phần lệch giữa giá trị đang điều khiển và trạng thái nhận về.
    void refreshMismatch(int group);

    /// Nhãn group kèm hai số Serial.
    void refreshTitle(int group);

    cmdproto::Values m_values;

    /// Giá trị của lần **gửi lệnh** gần nhất, cho các trường soi giá trị gói
    /// khác (FixEncoder lấy theo AzmOffset). Không phải m_values: m_values theo
    /// ô trên giao diện và được nạp lại từ settings.json lúc mở phần mềm, còn
    /// cái này là "đã ra lệnh cho đài những gì" nên mở phần mềm là quay về giá
    /// trị mặc định — chưa gửi lệnh nào thì chưa biết đài đang ở đâu.
    cmdproto::Values m_lastSent;

    GroupUi          m_groups[cmdproto::GroupCount];
    QLabel          *m_status  = nullptr;
    bool             m_loading = false;

    /// Ba nút và một nhãn ghim ở đầu tab, ngoài vùng cuộn.
    QPushButton     *m_lockBtn   = nullptr;
    QPushButton     *m_adfBtn    = nullptr;
    QPushButton     *m_filterBtn = nullptr;
    QLabel          *m_ctrlIp    = nullptr;

    bool             m_locked  = true;
};
