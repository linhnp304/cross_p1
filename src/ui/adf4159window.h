#pragma once

#include "proc/adf4159.h"

#include <QDialog>
#include <QString>
#include <QVector>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

/// Cửa sổ điều khiển kit tạo tín hiệu ADF4159.
///
/// Dựng lại phần mềm gốc của Analog Devices (ADF4158/9 PLL Software) theo gam
/// màu tối của phần mềm này, và thay đường USB của nó bằng hai gói lệnh UDP:
/// CMD_ADF4159_REG8 gửi cả tám thanh ghi, CMD_ADF4159_REG gửi lẻ một thanh ghi.
///
/// Khác hẳn tab "Điều khiển": ở đó vặn một ô là lệnh đi ra ngay, còn ở đây
/// **phải bấm nút Ghi**. Lý do là của chính con chip: tám thanh ghi phải vào
/// theo thứ tự 7→0 vì ghi R0 mới là lúc chốt tần số, nên "đổi tới đâu gửi tới
/// đó" sẽ đẩy kit qua một loạt trạng thái nửa vời. Vặn xong cả bộ rồi mới ghi
/// một lượt là đúng cách phần mềm gốc làm.
///
/// Ô hex của thanh ghi nào **chưa ghi** kể từ lần đổi gần nhất thì đổi sang nền
/// xanh lá, giống hệt phần mềm gốc — nhìn là biết còn thanh ghi nào đang nằm
/// trên màn hình mà chưa xuống tới kit.
class Adf4159Window : public QDialog
{
    Q_OBJECT

public:
    /// Loại gói lệnh, cũng là chỉ số của bộ đếm Serial riêng cho từng loại.
    enum Kind { KindReg8 = 0, KindReg1, KindCount };

    explicit Adf4159Window(QWidget *parent = nullptr);

    /// Đổ giá trị đã lưu vào giao diện. **Không** phát lệnh nào.
    void setSettings(const adf4159::Settings &s);
    const adf4159::Settings &settings() const { return m_settings; }

    /// Một datagram vừa nhận được ở cổng "Status". Trả về true nếu đúng là
    /// trạng thái phản hồi của một trong hai lệnh ADF4159.
    bool applyStatus(const QByteArray &datagram);

    /// Kết quả của lần gửi vừa rồi, do nơi nhận commandReady() báo lại.
    void setSendResult(int kind, bool ok);

    void setStatusText(const QString &text, bool isError = false);

signals:
    /// Một gói lệnh đã sẵn sàng. Nơi nhận **phải** gọi setSendResult() ngay
    /// trong lúc xử lý tín hiệu này: số Serial của lần gửi hỏng được trả lại
    /// chứ không tiêu mất, mà ô hex cũng chỉ được coi là "đã ghi" khi gói thật
    /// sự ra khỏi máy — nên chỗ phát tín hiệu đứng chờ ngay tại đó.
    void commandReady(int kind, const QByteArray &datagram);

    /// Cấu hình vừa đổi — cần lưu xuống params.json.
    void settingsChanged();

private:
    /// Một lựa chọn của hộp chọn. Giá trị **không** nhất thiết bằng chỉ số:
    /// chế độ Σ-Δ nhảy 0 rồi 14, ngắt nhảy 0, 1, 3.
    struct Opt {
        int     value;
        QString label;
    };

    /// Phần giao diện của một thanh ghi trong thanh dưới cùng.
    struct RegCell {
        QLineEdit   *value  = nullptr;   ///< giá trị đang tính ra, dạng hex
        QLabel      *status = nullptr;   ///< giá trị kit trả về, đỏ khi lệch
        QPushButton *write  = nullptr;

        quint32 written     = 0;         ///< lần ghi gần nhất gửi đi số nào
        bool    hasWritten  = false;
        quint32 statusValue = 0;
        bool    hasStatus   = false;
    };

    /// Ba thanh ghi của nhánh quét thứ hai nằm ngay sau tám thanh ghi chính.
    enum { kDownR4 = adf4159::kRegCount, kDownR5, kDownR6, kCellCount };

    QWidget *buildMainTab();
    QWidget *buildRampTab();
    QWidget *buildRegisterBar();

    // --- các hàm dựng ô nhập ---
    //
    // Mỗi hàm gắn luôn hai việc vào hai danh sách closure: đổ giá trị từ
    // m_settings lên ô, và gom giá trị từ ô về m_settings. Nhờ vậy thêm một
    // trường mới chỉ phải viết **một** dòng, không phải nhớ sửa thêm hai chỗ
    // khác — mà quên một trong hai thì ô đó lặng lẽ không có tác dụng gì.

    QComboBox *addCombo(QFormLayout *form, const QString &label, int *field,
                        std::initializer_list<Opt> opts,
                        const QString &tip = QString());

    /// Cùng hộp chọn ấy, cho những bit chỉ có hai trạng thái. Vẫn là hộp chọn
    /// chứ không phải ô đánh dấu: nhãn "0. Tắt" / "1. Bật" cho biết luôn bit đó
    /// đang mang giá trị nào, nên đối chiếu được với ô hex bên dưới bằng mắt —
    /// đúng lối phần mềm gốc đặt nhãn.
    QComboBox *addCombo(QFormLayout *form, const QString &label, bool *field,
                        std::initializer_list<Opt> opts,
                        const QString &tip = QString());

    /// Hộp chọn liệt kê sẵn các số nguyên từ lo tới hi.
    QComboBox *addRange(QFormLayout *form, const QString &label, int *field,
                        int lo, int hi, const QString &tip = QString());

    QCheckBox *addCheck(QFormLayout *form, const QString &label, bool *field,
                        const QString &tip = QString());

    QSpinBox *addSpin(QFormLayout *form, const QString &label, int *field,
                      int lo, int hi, const QString &tip = QString());

    QDoubleSpinBox *addDouble(QFormLayout *form, const QString &label,
                              double *field, double lo, double hi, int decimals,
                              const QString &tip = QString());

    /// Ô chỉ hiện giá trị tính ra, không nhập được.
    QLabel *addReadout(QFormLayout *form, const QString &label,
                       const QString &tip = QString());

    /// Một nhánh quét: bốn ô nhập và bốn dòng kết quả.
    QGroupBox *buildRampGroup(const QString &title, adf4159::Ramp *ramp,
                              QLabel **devLabel, QLabel **totalLabel,
                              QLabel **stepLabel, QLabel **rampLabel);

    /// Giá trị hiện tại của một ô trong thanh thanh ghi.
    quint32 cellValue(int cell) const;

    /// Gom giá trị từ giao diện về m_settings rồi tính lại mọi thứ suy ra.
    void onEdited();

    /// Cập nhật phần kết quả, thanh thanh ghi và các ô bị khoá theo phụ thuộc.
    void refresh();

    /// Gửi một thanh ghi lẻ (CMD_ADF4159_REG).
    void writeOne(int cell);

    /// Gửi cả tám thanh ghi trong một gói (CMD_ADF4159_REG8).
    void writeAll();

    /// Gửi lần lượt từng thanh ghi theo thứ tự 7, 6, 6, 5, 5, 4, 4, 3, 2, 1, 0
    /// — đúng thứ tự phần mềm gốc dùng, và là thứ tự **duy nhất** đúng: R0 phải
    /// vào cuối cùng vì ghi nó mới là lúc kit chốt tần số.
    void writeSequence();

    /// Bật/tắt quét tần: lật bit RAMP ON của R0 rồi ghi đúng R0.
    void toggleRamp();

    /// Số Serial cho gói sắp đóng — lấy **trước** khi đóng gói.
    quint32 nextSerial(int kind);

    /// Phát commandReady() rồi trả về việc gói có thật sự đi ra khỏi máy không.
    /// Trả lời tới từ setSendResult(), gọi ngay trong lúc xử lý tín hiệu.
    bool send(int kind, const QByteArray &datagram);

    adf4159::Settings m_settings;

    QVector<std::function<void()>> m_load;      ///< m_settings -> giao diện
    QVector<std::function<void()>> m_collect;   ///< giao diện -> m_settings

    RegCell m_cells[kCellCount];

    // Ô nhập cần đụng tới sau khi dựng: hoặc bị khoá theo trường khác, hoặc bị
    // đổi giá trị sau lưng người dùng (dòng rò âm khi bật "tự chọn").
    QComboBox *m_negBleedCombo = nullptr;
    QWidget   *m_downRampBox   = nullptr;
    QPushButton *m_rampButton  = nullptr;

    // --- các dòng kết quả ---
    QLabel *m_pfd     = nullptr;
    QLabel *m_spacing = nullptr;
    QLabel *m_int     = nullptr;
    QLabel *m_frac    = nullptr;
    QLabel *m_rfout   = nullptr;
    QLabel *m_rfout2  = nullptr;
    QLabel *m_n       = nullptr;
    QLabel *m_bleed   = nullptr;
    QLabel *m_warn    = nullptr;
    QLabel *m_delay   = nullptr;
    QLabel *m_status  = nullptr;

    QLabel *m_upDev = nullptr, *m_upTotal = nullptr;
    QLabel *m_upStep = nullptr, *m_upRamp = nullptr;
    QLabel *m_downDev = nullptr, *m_downTotal = nullptr;
    QLabel *m_downStep = nullptr, *m_downRamp = nullptr;

    quint32 m_serial[KindCount] = {};
    quint32 m_statusSerial[KindCount] = {};

    /// Câu trả lời của setSendResult() cho lần send() đang dở.
    bool m_lastSendOk = false;

    bool m_loading = false;
};
