#include "ui/controltab.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTime>
#include <QVBoxLayout>

namespace {

using cmdproto::Field;
using cmdproto::Packet;
using cmdproto::Widget;

const QString kRedStyle   = QStringLiteral("color: #d47b6a;");
const QString kInfoStyle  = QStringLiteral("color: #7fa8c9;");

/// Chuỗi trong bảng trường đi qua đây để còn dịch được sau này. Bảng là dữ liệu
/// hằng nên không gọi tr() ngay trong đó được.
QString txt(const char *s)
{
    return QCoreApplication::translate("cmdproto", s);
}

/// Giá trị `raw` viết ra dạng người đọc, đúng theo mô tả của trường.
QString valueText(const Field &f, quint32 raw)
{
    if (f.format == cmdproto::Format::HwVersion) {
        // Dữ liệu phản hồi là bốn byte hex đọc như bốn số thập phân:
        // 0x26080915 nghĩa là 2026/08/09-15.
        if (raw == 0)
            return QStringLiteral("-");
        return QString::asprintf("20%02x/%02x/%02x-%02x", (raw >> 24) & 0xffu,
                                 (raw >> 16) & 0xffu, (raw >> 8) & 0xffu,
                                 raw & 0xffu);
    }

    // Lựa chọn có nhãn riêng thì hiện nhãn kèm số — chỉ số trần thì người đọc
    // phải tự tra ngược lại bảng.
    for (int i = 0; i < f.optionCount; ++i) {
        if (f.options[i].value == raw)
            return QStringLiteral("%1 (%2)").arg(txt(f.options[i].label)).arg(raw);
    }

    return QString::number(cmdproto::toUi(f, raw), 'f', f.decimals);
}

/// Ô nhập số cho một trường. Dùng QDoubleSpinBox cho cả trường số nguyên vì
/// QSpinBox (int 32 bit) không đủ dải unsigned int của Attn, ZFbeat, GainU...
QDoubleSpinBox *makeSpin(QWidget *parent, const Field &f)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(f.decimals);
    s->setRange(f.lo, f.hi);
    s->setSingleStep(f.decimals > 0 ? 0.1 : 1.0);

    // Không có nút "Gửi" nên mỗi lần valueChanged là một lệnh đi ra. Tắt
    // keyboardTracking để gõ "1234" không thành bốn lệnh 1, 12, 123, 1234 — đài
    // sẽ thật sự thi hành cả bốn.
    s->setKeyboardTracking(false);
    return s;
}

} // namespace

ControlTab::ControlTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    for (int g = 0; g < cmdproto::GroupCount; ++g)
        root->addWidget(buildGroup(g));

    // Kit tạo tín hiệu ADF4159 không nằm trong bốn gói lệnh trên: nó có bộ
    // thanh ghi riêng, nhiều tới mức phải một cửa sổ riêng mới đủ chỗ.
    auto *adf = new QPushButton(tr("Điều khiển ADF4159"), this);
    adf->setToolTip(tr("Mở cửa sổ điều khiển kit tạo tín hiệu ADF4159"));
    connect(adf, &QPushButton::clicked, this, &ControlTab::adf4159Requested);
    root->addWidget(adf);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(kInfoStyle);
    root->addWidget(m_status);
    root->addStretch(1);

    setValues(m_values);
}

QGroupBox *ControlTab::buildGroup(int group)
{
    const Packet &p = cmdproto::kPackets[group];
    GroupUi &ui = m_groups[group];

    ui.box = new QGroupBox(txt(p.label), this);
    auto *form = new QFormLayout(ui.box);
    form->setLabelAlignment(Qt::AlignLeft);
    // Panel bên phải chỉ rộng chừng 30% màn hình, mà có hàng năm lựa chọn nằm
    // ngang — hẹp quá thì cho nhãn xuống dòng riêng chứ đừng ép nhau.
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    ui.cells.resize(p.fieldCount);

    for (int i = 0; i < p.fieldCount; ++i) {
        const Field &f = p.fields[i];
        if (f.widget == Widget::None)
            continue;   // trường chưa dùng đến: có chỗ trong gói, không có giao diện

        Cell &cell = ui.cells[i];
        const QString label = txt(f.label);
        const QString tip   = *f.tip ? txt(f.tip) : QString();

        // Nhãn đỏ bên phải: giá trị trạng thái nhận về khi nó khác giá trị đang
        // điều khiển. Dựng sẵn cho mọi loại ô (trừ nhóm nút chọn, loại đó báo
        // lệch bằng cách đổi màu chính lựa chọn tương ứng).
        auto *row = new QWidget(ui.box);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(8);

        switch (f.widget) {
        case Widget::Radio: {
            cell.radios = new QButtonGroup(this);
            for (int k = 0; k < f.optionCount; ++k) {
                auto *b = new QRadioButton(txt(f.options[k].label), row);
                if (!tip.isEmpty())
                    b->setToolTip(tip);
                cell.radios->addButton(b, k);
                rowLay->addWidget(b);
            }
            rowLay->addStretch(1);
            connect(cell.radios, &QButtonGroup::idToggled, this,
                    [this, group](int, bool checked) {
                        if (checked)
                            sendGroup(group);
                    });
            break;
        }

        case Widget::Combo: {
            cell.combo = new QComboBox(row);
            if (f.optionCount > 0) {
                for (int k = 0; k < f.optionCount; ++k)
                    cell.combo->addItem(txt(f.options[k].label), f.options[k].value);
            } else {
                // Hộp chọn liệt kê số: dải đã khai trong bảng trường.
                for (int v = int(f.lo); v <= int(f.hi); ++v)
                    cell.combo->addItem(QString::number(v), quint32(v));
            }
            rowLay->addWidget(cell.combo);
            connect(cell.combo, &QComboBox::currentIndexChanged, this,
                    [this, group] { sendGroup(group); });
            break;
        }

        case Widget::Spin:
        case Widget::SpinF: {
            cell.spin = makeSpin(row, f);
            rowLay->addWidget(cell.spin);
            connect(cell.spin, &QDoubleSpinBox::valueChanged, this,
                    [this, group] { sendGroup(group); });
            break;
        }

        case Widget::ReadOnly: {
            cell.readout = new QLineEdit(row);
            cell.readout->setReadOnly(true);
            cell.readout->setEnabled(false);
            cell.readout->setText(QStringLiteral("-"));
            rowLay->addWidget(cell.readout);
            break;
        }

        case Widget::None:
            break;
        }

        if (f.widget != Widget::Radio) {
            cell.mismatch = new QLabel(row);
            cell.mismatch->setStyleSheet(kRedStyle);
            cell.mismatch->setToolTip(
                tr("Giá trị đài đang thực sự dùng — khác với giá trị đang điều "
                   "khiển, nghĩa là đài chưa đáp ứng lệnh"));
            rowLay->addWidget(cell.mismatch);
            rowLay->addStretch(1);
        }

        if (!tip.isEmpty())
            row->setToolTip(tip);

        auto *labelWidget = new QLabel(label, ui.box);
        labelWidget->setWordWrap(true);
        if (!tip.isEmpty())
            labelWidget->setToolTip(tip);
        form->addRow(labelWidget, row);
    }

    return ui.box;
}

void ControlTab::setValues(const cmdproto::Values &v)
{
    m_values = v;
    for (int g = 0; g < cmdproto::GroupCount; ++g) {
        loadGroup(g);
        refreshMismatch(g);
        refreshTitle(g);
    }
}

void ControlTab::loadGroup(int group)
{
    const Packet &p = cmdproto::kPackets[group];
    GroupUi &ui = m_groups[group];

    m_loading = true;
    for (int i = 0; i < p.fieldCount; ++i) {
        const Field &f = p.fields[i];
        const Cell  &c = ui.cells[i];
        const quint32 raw = m_values.v[group][i];

        if (c.spin) {
            c.spin->setValue(cmdproto::toUi(f, raw));
        } else if (c.combo) {
            const int at = c.combo->findData(raw);
            // Giá trị lạ (file sửa tay) thì lùi về mục đầu chứ không để hộp
            // chọn trỏ vào chỗ trống.
            c.combo->setCurrentIndex(at >= 0 ? at : 0);
        } else if (c.radios) {
            for (int k = 0; k < f.optionCount; ++k) {
                if (f.options[k].value != raw)
                    continue;
                if (QAbstractButton *b = c.radios->button(k))
                    b->setChecked(true);
                break;
            }
        }
    }
    m_loading = false;
}

void ControlTab::sendGroup(int group)
{
    if (m_loading)
        return;

    const Packet &p = cmdproto::kPackets[group];
    GroupUi &ui = m_groups[group];

    // Gom **cả group** chứ không chỉ ô vừa đổi: một lệnh mang trọn bộ trường,
    // nên giá trị của các ô còn lại cũng phải là giá trị đang hiện trên giao diện.
    for (int i = 0; i < p.fieldCount; ++i) {
        const Field &f = p.fields[i];
        const Cell  &c = ui.cells[i];

        // Trường soi giá trị gói khác (FixEncoder lấy theo AzmOffset của
        // CMD_COMMON): ô của nó bị khoá và chỉ hiện trạng thái trả về, nên giá
        // trị gửi đi lấy từ **lần gửi lệnh gần nhất** của gói kia, không phải từ
        // ô đang hiện trên giao diện. Chưa gửi lệnh nào thì đó là giá trị mặc
        // định — xem m_lastSent.
        if (f.mirrors()) {
            m_values.v[group][i] = m_lastSent.v[f.mirrorGroup][f.mirrorIndex];
            continue;
        }

        if (c.spin)
            m_values.v[group][i] = cmdproto::fromUi(f, c.spin->value());
        else if (c.combo && c.combo->currentIndex() >= 0)
            m_values.v[group][i] = c.combo->currentData().toUInt();
        else if (c.radios && c.radios->checkedId() >= 0)
            m_values.v[group][i] = f.options[c.radios->checkedId()].value;
    }

    // Serial tăng trước khi đóng gói; gửi hỏng thì setSendResult() trả lại.
    ++ui.cmdSerial;
    const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
    const QByteArray dg =
        cmdproto::buildCommand(p, m_values.v[group], ui.cmdSerial, timeMs);

    // Chốt lại "đã ra lệnh cho đài những gì" — trường soi giá trị gói khác đọc
    // ở đây chứ không đọc ô trên giao diện.
    for (int i = 0; i < p.fieldCount; ++i)
        m_lastSent.v[group][i] = m_values.v[group][i];

    emit commandReady(group, dg);
    emit valuesChanged();
}

void ControlTab::setSendResult(int group, bool ok)
{
    if (group < 0 || group >= cmdproto::GroupCount)
        return;

    GroupUi &ui = m_groups[group];
    if (!ok && ui.cmdSerial > 0) {
        // Lệnh không ra khỏi máy thì số Serial đó chưa dùng — trả lại để dãy
        // Serial bên đài vẫn liền mạch.
        --ui.cmdSerial;
    }

    // Phần báo lệch tính lại kể cả khi gửi hỏng: giá trị đang điều khiển đã đổi
    // rồi, để nguyên vệt đỏ cũ là nó chỉ vào một phép so sánh không còn nữa.
    refreshTitle(group);
    refreshMismatch(group);
}

bool ControlTab::applyStatus(const QByteArray &datagram)
{
    const Packet *p = cmdproto::statusPacket(datagram.constData(), datagram.size());
    if (!p)
        return false;

    const int group = int(p - cmdproto::kPackets);
    GroupUi &ui = m_groups[group];

    quint32 timeMs = 0;
    cmdproto::parseStatus(*p, datagram.constData(), ui.statusSerial, timeMs,
                          ui.status);
    ui.hasStatus = true;

    refreshMismatch(group);
    refreshTitle(group);
    emit statusReceived(group);
    return true;
}

bool ControlTab::statusValue(int group, const char *name, quint32 &out) const
{
    if (group < 0 || group >= cmdproto::GroupCount || !m_groups[group].hasStatus)
        return false;
    const int i = cmdproto::indexOf(cmdproto::kPackets[group], name);
    if (i < 0)
        return false;
    out = m_groups[group].status[i];
    return true;
}

void ControlTab::refreshMismatch(int group)
{
    const Packet &p = cmdproto::kPackets[group];
    GroupUi &ui = m_groups[group];

    for (int i = 0; i < p.fieldCount; ++i) {
        const Field &f = p.fields[i];
        const Cell  &c = ui.cells[i];
        const quint32 status = ui.status[i];

        // Trường chỉ nhận trạng thái: không có gì mà so, giá trị về là giá trị
        // duy nhất có — hiện thẳng trong ô của nó.
        if (c.readout) {
            c.readout->setText(ui.hasStatus ? valueText(f, status)
                                            : QStringLiteral("-"));
            continue;
        }

        const bool differs = ui.hasStatus && status != m_values.v[group][i];

        if (c.radios) {
            // Nhóm nút chọn: tô đỏ chính lựa chọn ứng với **giá trị trạng
            // thái**, để nhìn là thấy đài đang ở đâu so với chỗ mình vặn tới.
            for (int k = 0; k < f.optionCount; ++k) {
                QAbstractButton *b = c.radios->button(k);
                if (!b)
                    continue;
                const bool mark = differs && f.options[k].value == status;
                b->setStyleSheet(mark ? kRedStyle : QString());
            }
            continue;
        }

        if (c.mismatch)
            c.mismatch->setText(differs ? valueText(f, status) : QString());
    }
}

void ControlTab::refreshTitle(int group)
{
    const GroupUi &ui = m_groups[group];
    if (!ui.box)
        return;

    const QString label = txt(cmdproto::kPackets[group].label);
    if (ui.cmdSerial == 0 && !ui.hasStatus) {
        ui.box->setTitle(label);
        return;
    }

    const auto num = [](quint32 v, bool has) {
        return has ? QString::number(v) : QStringLiteral("-");
    };
    ui.box->setTitle(QStringLiteral("%1 (%2 - %3)")
                         .arg(label, num(ui.cmdSerial, ui.cmdSerial > 0),
                              num(ui.statusSerial, ui.hasStatus)));
}

void ControlTab::setStatusText(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError ? kRedStyle : kInfoStyle);
}
