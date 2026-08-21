#include "ui/adf4159window.h"

#include "net/adfproto.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTime>
#include <QVBoxLayout>

#include <iterator>

namespace {

const QString kRedStyle   = QStringLiteral("color: #d47b6a;");
const QString kInfoStyle  = QStringLiteral("color: #7fa8c9;");
const QString kValueStyle = QStringLiteral("color: #9ec5e4;");

/// Nền xanh lá của ô thanh ghi **chưa ghi xuống kit** — giữ đúng quy ước của
/// phần mềm gốc, chỉ đổi sang tông tối cho hợp phần mềm này.
const QString kDirtyStyle = QStringLiteral(
    "background: #10281a; color: #7fd6a0; border: 1px solid #2f6d4a;");

QString hex(quint32 v)
{
    return QString::asprintf("0x%08X", v);
}

/// Tên đơn vị viết liền số, để một dòng kết quả đọc lên thành câu.
QString num(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

} // namespace

Adf4159Window::Adf4159Window(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Điều khiển ADF4159"));
    // Tên đối tượng để công cụ kịch bản (tests/guidrv.cpp) chỉ đúng cửa sổ này:
    // chỉ theo tiêu đề thì trúng luôn cái nút mở nó, vì hai chỗ cùng một chữ.
    setObjectName(QStringLiteral("adf4159"));
    // Cửa sổ riêng, không chặn cửa sổ chính — trắc thủ còn phải nhìn màn hình
    // chính trong lúc vặn tần số.
    setWindowFlag(Qt::Window);
    setModal(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildMainTab(), tr("Điều khiển chính"));
    tabs->addTab(buildRampTab(), tr("Quét tần và dịch khoá"));
    root->addWidget(tabs, 1);

    root->addWidget(buildRegisterBar(), 0);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(kInfoStyle);
    root->addWidget(m_status);

    setSettings(m_settings);
    resize(1180, 860);
}

// --- các hàm dựng ô nhập -----------------------------------------------------

QComboBox *Adf4159Window::addCombo(QFormLayout *form, const QString &label,
                                   int *field, std::initializer_list<Opt> opts,
                                   const QString &tip)
{
    auto *c = new QComboBox(form->parentWidget());
    for (const Opt &o : opts)
        c->addItem(o.label, o.value);
    if (!tip.isEmpty())
        c->setToolTip(tip);

    m_load.append([this, c, field] {
        const int at = c->findData(*field);
        // Giá trị lạ (file sửa tay) thì lùi về mục đầu chứ không để hộp chọn
        // trỏ vào chỗ trống.
        c->setCurrentIndex(at >= 0 ? at : 0);
    });
    m_collect.append([c, field] {
        if (c->currentIndex() >= 0)
            *field = c->currentData().toInt();
    });

    connect(c, &QComboBox::currentIndexChanged, this, &Adf4159Window::onEdited);
    form->addRow(label, c);
    return c;
}

QComboBox *Adf4159Window::addCombo(QFormLayout *form, const QString &label,
                                   bool *field, std::initializer_list<Opt> opts,
                                   const QString &tip)
{
    auto *c = new QComboBox(form->parentWidget());
    for (const Opt &o : opts)
        c->addItem(o.label, o.value);
    if (!tip.isEmpty())
        c->setToolTip(tip);

    m_load.append([c, field] {
        const int at = c->findData(*field ? 1 : 0);
        c->setCurrentIndex(at >= 0 ? at : 0);
    });
    m_collect.append([c, field] {
        if (c->currentIndex() >= 0)
            *field = c->currentData().toInt() != 0;
    });

    connect(c, &QComboBox::currentIndexChanged, this, &Adf4159Window::onEdited);
    form->addRow(label, c);
    return c;
}

QComboBox *Adf4159Window::addRange(QFormLayout *form, const QString &label,
                                   int *field, int lo, int hi, const QString &tip)
{
    auto *c = new QComboBox(form->parentWidget());
    for (int v = lo; v <= hi; ++v)
        c->addItem(QString::number(v), v);
    if (!tip.isEmpty())
        c->setToolTip(tip);

    m_load.append([this, c, field] {
        const int at = c->findData(*field);
        c->setCurrentIndex(at >= 0 ? at : 0);
    });
    m_collect.append([c, field] {
        if (c->currentIndex() >= 0)
            *field = c->currentData().toInt();
    });

    connect(c, &QComboBox::currentIndexChanged, this, &Adf4159Window::onEdited);
    form->addRow(label, c);
    return c;
}

QCheckBox *Adf4159Window::addCheck(QFormLayout *form, const QString &label,
                                   bool *field, const QString &tip)
{
    auto *b = new QCheckBox(label, form->parentWidget());
    if (!tip.isEmpty())
        b->setToolTip(tip);

    m_load.append([b, field] { b->setChecked(*field); });
    m_collect.append([b, field] { *field = b->isChecked(); });

    connect(b, &QCheckBox::toggled, this, &Adf4159Window::onEdited);
    form->addRow(b);
    return b;
}

QSpinBox *Adf4159Window::addSpin(QFormLayout *form, const QString &label,
                                 int *field, int lo, int hi, const QString &tip)
{
    auto *s = new QSpinBox(form->parentWidget());
    s->setRange(lo, hi);
    // Gõ "1234" mà mỗi ký tự là một lần tính lại cả tám thanh ghi thì ô hex
    // nhấp nháy qua bốn giá trị vô nghĩa. Chỉ tính khi rời ô hay bấm Enter.
    s->setKeyboardTracking(false);
    if (!tip.isEmpty())
        s->setToolTip(tip);

    m_load.append([s, field] { s->setValue(*field); });
    m_collect.append([s, field] { *field = s->value(); });

    connect(s, &QSpinBox::valueChanged, this, &Adf4159Window::onEdited);
    form->addRow(label, s);
    return s;
}

QDoubleSpinBox *Adf4159Window::addDouble(QFormLayout *form, const QString &label,
                                         double *field, double lo, double hi,
                                         int decimals, const QString &tip)
{
    auto *s = new QDoubleSpinBox(form->parentWidget());
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSingleStep(1.0);
    s->setKeyboardTracking(false);
    if (!tip.isEmpty())
        s->setToolTip(tip);

    m_load.append([s, field] { s->setValue(*field); });
    m_collect.append([s, field] { *field = s->value(); });

    connect(s, &QDoubleSpinBox::valueChanged, this, &Adf4159Window::onEdited);
    form->addRow(label, s);
    return s;
}

QLabel *Adf4159Window::addReadout(QFormLayout *form, const QString &label,
                                  const QString &tip)
{
    auto *l = new QLabel(QStringLiteral("-"), form->parentWidget());
    l->setStyleSheet(kValueStyle);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!tip.isEmpty())
        l->setToolTip(tip);
    form->addRow(label, l);
    return l;
}

// --- tab "Điều khiển chính" --------------------------------------------------

QWidget *Adf4159Window::buildMainTab()
{
    auto *page = new QWidget(this);
    auto *cols = new QHBoxLayout(page);
    cols->setContentsMargins(10, 10, 10, 10);
    cols->setSpacing(10);

    const auto column = [&cols](QWidget *parent) {
        auto *w = new QWidget(parent);
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(10);
        cols->addWidget(w, 1);
        return v;
    };
    const auto group = [](QWidget *parent, const QString &title) {
        auto *box = new QGroupBox(title, parent);
        auto *form = new QFormLayout(box);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setLabelAlignment(Qt::AlignLeft);
        return form;
    };

    // --- cột 1: tham số RF ---
    auto *col1 = column(page);

    auto *rf = group(page, tr("Tham số RF"));
    addDouble(rf, tr("Tần số ra VCO (MHz)"), &m_settings.vcoMHz, 0.5, 13000.0, 6,
              tr("Tần số cần có ở đầu ra VCO. Bo mạch mẫu của Analog Devices "
                 "hồi tiếp VCO/2 về ADF4159, khi đó phải nhập một nửa tần số "
                 "thật của VCO."));
    addDouble(rf, tr("Tần số chuẩn (MHz)"), &m_settings.refMHz, 1.0, 300.0, 6,
              tr("Tần số thật đang cấp vào chân REFIN"));
    addRange(rf, tr("Bộ chia R"), &m_settings.rCounter, 1, 32,
             tr("Chia tần số chuẩn xuống trước khi vào bộ so pha"));
    addCheck(rf, tr("Nhân đôi tần số chuẩn (Ref Doubler)"), &m_settings.refDoubler);
    addCheck(rf, tr("Chia đôi tần số chuẩn (Ref /2)"), &m_settings.refDiv2,
             tr("Chèn một mạch chia đôi để tín hiệu vào bộ so pha có độ rỗng 50%"));
    addCombo(rf, tr("Bộ chia trước (Prescaler)"), &m_settings.prescaler,
             {{0, tr("4/5")}, {1, tr("8/9")}},
             tr("Trên 8 GHz bắt buộc dùng 8/9. Bộ chia trước cũng quyết định "
                "trị INT nhỏ nhất: 4/5 thì 23, 8/9 thì 75."));

    m_pfd     = addReadout(rf, tr("Tần số so pha (MHz)"));
    m_spacing = addReadout(rf, tr("Bước tần số (kHz)"),
                           tr("fPFD / 2^25 — bước nhỏ nhất mà phần lẻ FRAC đi được"));
    m_int     = addReadout(rf, tr("INT"));
    m_frac    = addReadout(rf, tr("FRAC / MOD"));
    m_n       = addReadout(rf, tr("N = INT + FRAC/MOD"));
    m_rfout   = addReadout(rf, tr("Tần số ra thật (MHz)"),
                           tr("Tần số đạt được sau khi INT và FRAC đã làm tròn"));
    m_rfout2  = addReadout(rf, tr("Tần số ra × 2 (MHz)"));

    m_warn = new QLabel(page);
    m_warn->setWordWrap(true);
    m_warn->setStyleSheet(kRedStyle);
    rf->addRow(m_warn);

    col1->addWidget(rf->parentWidget());
    col1->addStretch(1);

    // --- cột 2: thanh ghi 0..3 ---
    auto *col2 = column(page);

    auto *r0 = group(page, tr("Thanh ghi 0"));
    addCombo(r0, tr("Muxout"), &m_settings.muxout,
             {{0, tr("0. Ba trạng thái")},
              {1, tr("1. DVDD")},
              {2, tr("2. DGND")},
              {3, tr("3. Đầu ra bộ chia R")},
              {4, tr("4. Đầu ra bộ chia N")},
              {5, tr("5. Dự trữ")},
              {6, tr("6. Dò đồng bộ số")},
              {7, tr("7. Dữ liệu nối tiếp ra")},
              {8, tr("8. Dự trữ")},
              {9, tr("9. Dự trữ")},
              {10, tr("10. Đầu ra bộ chia CLK")},
              {11, tr("11. Dự trữ")},
              {12, tr("12. Dự trữ")},
              {13, tr("13. Bộ chia R / 2")},
              {14, tr("14. Bộ chia N / 2")},
              {15, tr("15. Đọc ngược ra Muxout")}},
             tr("Tín hiệu đưa ra chân MUXOUT của chip"));
    col2->addWidget(r0->parentWidget());

    auto *r1 = group(page, tr("Thanh ghi 1"));
    addCombo(r1, tr("Chỉnh pha"), &m_settings.phaseAdjust,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addSpin(r1, tr("Pha"), &m_settings.phase, 0, 4095);
    col2->addWidget(r1->parentWidget());

    auto *r2 = group(page, tr("Thanh ghi 2"));
    addCombo(r2, tr("CSR (giảm trượt chu kỳ)"), &m_settings.csr,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Chỉ dùng được khi cực tính bộ so pha là dương và dòng bơm "
                "điện tích để mức nhỏ nhất"));
    addCombo(r2, tr("Dòng bơm điện tích (mA)"), &m_settings.cpCurrent,
             {{0, QStringLiteral("0.31")},  {1, QStringLiteral("0.63")},
              {2, QStringLiteral("0.94")},  {3, QStringLiteral("1.25")},
              {4, QStringLiteral("1.57")},  {5, QStringLiteral("1.88")},
              {6, QStringLiteral("2.19")},  {7, QStringLiteral("2.50")},
              {8, QStringLiteral("2.81")},  {9, QStringLiteral("3.13")},
              {10, QStringLiteral("3.44")}, {11, QStringLiteral("3.75")},
              {12, QStringLiteral("4.06")}, {13, QStringLiteral("4.38")},
              {14, QStringLiteral("4.69")}, {15, QStringLiteral("5.00")}},
             tr("Đặt đúng dòng mà mạch lọc vòng được thiết kế theo"));
    col2->addWidget(r2->parentWidget());

    auto *r3 = group(page, tr("Thanh ghi 3"));
    addCombo(r3, tr("Báo mất đồng bộ (LOL)"), &m_settings.lol,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(r3, tr("N SEL"), &m_settings.nSel,
             {{0, tr("0. Nạp theo nhịp Σ-Δ")}, {1, tr("1. Chậm 4 chu kỳ")}},
             tr("Nạp chậm để INT và FRAC vào cùng lúc, tránh vọt tần số"));
    addCombo(r3, tr("Xoá bộ điều chế Σ-Δ"), &m_settings.sdReset,
             {{0, tr("0. Bật")}, {1, tr("1. Tắt")}},
             tr("Bật thì mỗi lần ghi R0 là bộ điều chế Σ-Δ được xoá"));
    addCombo(r3, tr("Độ chính xác dò đồng bộ (LDP)"), &m_settings.ldp,
             {{0, tr("0. 14 ns")}, {1, tr("1. 6 ns")}});
    addCombo(r3, tr("Cực tính bộ so pha"), &m_settings.pdPolarity,
             {{0, tr("0. Âm")}, {1, tr("1. Dương")}},
             tr("Mạch lọc vòng tích cực đảo pha thì chọn âm"));
    addCombo(r3, tr("Bơm điện tích ba trạng thái"), &m_settings.cpThreeState,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(r3, tr("Tắt nguồn"), &m_settings.powerDown,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(r3, tr("Giữ bộ đếm ở trạng thái xoá"), &m_settings.counterReset,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    col2->addWidget(r3->parentWidget());
    col2->addStretch(1);

    // --- cột 3: thanh ghi 4 và dòng rò âm ---
    auto *col3 = column(page);

    auto *r4 = group(page, tr("Thanh ghi 4"));
    addCombo(r4, tr("Chế độ bộ chia CLK"), &m_settings.clkDivMode,
             {{0, tr("0. Tắt")},
              {1, tr("1. Bộ chia khoá nhanh")},
              {3, tr("3. Bộ chia quét tần")}},
             tr("Quét tần thì bắt buộc chọn \"bộ chia quét tần\""));
    addCombo(r4, tr("LE SEL"), &m_settings.leSel,
             {{0, tr("0. LE lấy từ chân")}, {1, tr("1. LE đồng bộ với REFIN")}});
    addCombo(r4, tr("Chế độ bộ điều chế Σ-Δ"), &m_settings.sdMode,
             {{0, tr("0. Bình thường")}, {14, tr("14. Tắt khi FRAC = 0")}},
             tr("Tắt hẳn Σ-Δ là chuyển sang chế độ chia nguyên: mất quét tần, "
                "mất PSK/FSK và mất cả phần chỉnh pha"));
    addCombo(r4, tr("Trạng thái quét (Ramp status)"), &m_settings.rampStatus,
             {{0, tr("0. Bình thường")},
              {2, tr("2. Đọc ngược ra Muxout")},
              {3, tr("3. Báo hết nhánh quét ra Muxout")},
              {16, tr("16. Bơm điện tích lên")},
              {17, tr("17. Bơm điện tích xuống")}},
             tr("Hai mục dùng chân Muxout đòi Muxout ở R0 phải để \"đọc ngược\""));
    col3->addWidget(r4->parentWidget());

    auto *nb = group(page, tr("Dòng rò âm (Negative Bleed)"));
    addCheck(nb, tr("Tự chọn theo công thức 4 × Icp / N"),
             &m_settings.autoNegBleed,
             tr("Bỏ tích thì tự chọn lấy trong danh sách bên dưới"));
    m_bleed = addReadout(nb, tr("Theo công thức (µA)"));
    m_negBleedCombo = addCombo(nb, tr("Dòng rò âm (µA)"), &m_settings.negBleed,
                               {{0, QStringLiteral("3.73")},
                                {1, QStringLiteral("11.03")},
                                {2, QStringLiteral("25.25")},
                                {3, QStringLiteral("53.1")},
                                {4, QStringLiteral("109.7")},
                                {5, QStringLiteral("224.7")},
                                {6, QStringLiteral("454.7")},
                                {7, QStringLiteral("916.4")}});
    addCombo(nb, tr("Bật dòng rò âm"), &m_settings.negBleedEn,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Bật dòng rò âm thì phần báo mất đồng bộ không còn đúng nữa"));
    col3->addWidget(nb->parentWidget());
    col3->addStretch(1);

    return page;
}

// --- tab "Quét tần và dịch khoá" ---------------------------------------------

QWidget *Adf4159Window::buildRampTab()
{
    auto *page = new QWidget(this);
    auto *cols = new QHBoxLayout(page);
    cols->setContentsMargins(10, 10, 10, 10);
    cols->setSpacing(10);

    const auto column = [&cols](QWidget *parent) {
        auto *w = new QWidget(parent);
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(10);
        cols->addWidget(w, 1);
        return v;
    };
    const auto group = [](QWidget *parent, const QString &title) {
        auto *box = new QGroupBox(title, parent);
        auto *form = new QFormLayout(box);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setLabelAlignment(Qt::AlignLeft);
        return form;
    };

    // --- cột 1: kiểu điều chế ---
    auto *col1 = column(page);

    auto *mod = group(page, tr("Kiểu điều chế"));
    addCombo(mod, tr("Quét tần"), &m_settings.rampOn,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Bit RAMP ON của R0 — ghi R0 xong là kit bắt đầu quét"));
    addCombo(mod, tr("Dạng sóng quét"), &m_settings.rampMode,
             {{0, tr("0. Răng cưa liên tục")},
              {1, tr("1. Tam giác liên tục")},
              {2, tr("2. Một nhịp răng cưa")},
              {3, tr("3. Một nhịp quét")}});
    addCombo(mod, tr("Quét nhanh (Fast ramp)"), &m_settings.fastRamp,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Về tần số đầu bằng nhiều bước nhỏ thay vì một bước lớn"));
    addCombo(mod, tr("Nhánh quét thứ hai"), &m_settings.dualRamp,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Bật thì nhóm \"nhánh xuống\" có tác dụng và ba thanh ghi "
                "nhánh 2 mới ghi được"));
    addCombo(mod, tr("Quét FSK"), &m_settings.fskRamp,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(mod, tr("FSK"), &m_settings.fsk,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(mod, tr("PSK"), &m_settings.psk,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(mod, tr("Một tam giác đầy đủ"), &m_settings.singleFullTri,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Dùng được khi dạng sóng quét để \"một nhịp quét\""));
    addCombo(mod, tr("Quét parabol"), &m_settings.parabolic,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addSpin(mod, tr("CLK1"), &m_settings.clk1, 0, 4095,
            tr("Cùng với CLK2 quyết định thời gian một bước quét. CLK1 và CLK2 "
               "không được cùng bằng 1."));
    col1->addWidget(mod->parentWidget());

    auto *other = group(page, tr("Khác"));
    addCombo(other, tr("Ngắt (Interrupt)"), &m_settings.interrupt,
             {{0, tr("0. Tắt")},
              {1, tr("1. Nạp kênh, quét tiếp")},
              {3, tr("3. Nạp kênh, dừng quét")}});
    m_rampButton = new QPushButton(tr("Bật/tắt quét tần"), page);
    m_rampButton->setToolTip(tr("Lật bit RAMP ON rồi ghi ngay R0 — không đụng "
                                "tới bảy thanh ghi còn lại"));
    connect(m_rampButton, &QPushButton::clicked, this, &Adf4159Window::toggleRamp);
    other->addRow(m_rampButton);
    col1->addWidget(other->parentWidget());
    col1->addStretch(1);

    // --- cột 2: nhánh lên ---
    auto *col2 = column(page);
    col2->addWidget(buildRampGroup(tr("Nhánh lên"), &m_settings.up, &m_upDev,
                                   &m_upTotal, &m_upStep, &m_upRamp));

    auto *tx = group(page, tr("TXdata"));
    addCombo(tx, tr("Nhịp quét lấy từ"), &m_settings.txRampClk,
             {{0, tr("0. Bộ chia CLK")}, {1, tr("1. TXDATA")}});
    addCombo(tx, tr("TXdata kích quét"), &m_settings.txDataTrigger,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(tx, tr("Đảo TXdata"), &m_settings.txDataInvert,
             {{0, tr("0. Không đảo")}, {1, tr("1. Đảo")}},
             tr("Không đảo thì sự kiện xảy ra ở sườn lên của xung TXDATA"));
    col2->addWidget(tx->parentWidget());
    col2->addStretch(1);

    // --- cột 3: nhánh xuống ---
    auto *col3 = column(page);
    m_downRampBox = buildRampGroup(tr("Nhánh xuống (nhánh 2)"), &m_settings.down,
                                   &m_downDev, &m_downTotal, &m_downStep,
                                   &m_downRamp);
    col3->addWidget(m_downRampBox);
    col3->addStretch(1);

    // --- cột 4: giữ chậm ---
    auto *col4 = column(page);

    auto *del = group(page, tr("Giữ chậm"));
    addSpin(del, tr("Từ giữ chậm"), &m_settings.delayWord, 0, 4095);
    addCombo(del, tr("Nhịp giữ chậm"), &m_settings.delClkSel,
             {{0, tr("0. Nhịp PFD")}, {1, tr("1. Nhịp PFD × CLK1")}},
             tr("Cần khoảng chậm dài thì nhân thêm CLK1"));
    m_delay = addReadout(del, tr("Thời gian giữ chậm (µs)"));
    addCombo(del, tr("Giữ chậm lúc khởi động"), &m_settings.delStartEn,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(del, tr("Giữ chậm giữa hai nhánh"), &m_settings.rampDelay,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(del, tr("Khoá nhanh trong lúc giữ chậm"), &m_settings.rampDelayFl,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}});
    addCombo(del, tr("Giữ chậm khi TXdata kích"), &m_settings.txTriggerDelay,
             {{0, tr("0. Không giữ chậm")}, {1, tr("1. Giữ chậm")}});
    addCombo(del, tr("Giữ chậm giữa hai vế tam giác"), &m_settings.triDelay,
             {{0, tr("0. Tắt")}, {1, tr("1. Bật")}},
             tr("Chỉ có tác dụng với dạng sóng tam giác và khi đã bật giữ chậm"));
    col4->addWidget(del->parentWidget());
    col4->addStretch(1);

    return page;
}

QGroupBox *Adf4159Window::buildRampGroup(const QString &title,
                                         adf4159::Ramp *ramp, QLabel **devLabel,
                                         QLabel **totalLabel, QLabel **stepLabel,
                                         QLabel **rampLabel)
{
    auto *box  = new QGroupBox(title, this);
    auto *form = new QFormLayout(box);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setLabelAlignment(Qt::AlignLeft);

    addSpin(form, tr("CLK2"), &ramp->clk2, 0, 4095,
            tr("Cùng với CLK1 quyết định thời gian một bước quét"));
    addSpin(form, tr("Từ độ lệch tần (DEV)"), &ramp->dev, -32768, 32767,
            tr("Số có dấu: âm là nhánh đi xuống"));
    addSpin(form, tr("Số mũ độ lệch tần (DEVoff)"), &ramp->devOffset, 0, 15,
            tr("Độ lệch tần mỗi bước = (fPFD / 2^25) × DEV × 2^DEVoff"));
    addSpin(form, tr("Số bước (Steps)"), &ramp->steps, 0, 1048575);

    *devLabel   = addReadout(form, tr("Độ lệch tần mỗi bước (kHz)"));
    *totalLabel = addReadout(form, tr("Cả nhánh quét (kHz)"));
    *stepLabel  = addReadout(form, tr("Thời gian một bước (µs)"));
    *rampLabel  = addReadout(form, tr("Thời gian cả nhánh (µs)"));

    return box;
}

// --- thanh thanh ghi ---------------------------------------------------------

QWidget *Adf4159Window::buildRegisterBar()
{
    auto *box  = new QGroupBox(tr("Thanh ghi"), this);
    auto *grid = new QGridLayout(box);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // Một ô thanh ghi: tên, giá trị hex, nút ghi, giá trị trạng thái trả về.
    const auto cell = [&](int index, const QString &name, int row, int col) {
        RegCell &c = m_cells[index];

        auto *title = new QLabel(name, box);
        title->setStyleSheet(kInfoStyle);
        grid->addWidget(title, row, col);

        c.value = new QLineEdit(box);
        c.value->setReadOnly(true);
        c.value->setFont(mono);
        c.value->setAlignment(Qt::AlignRight);
        grid->addWidget(c.value, row + 1, col);

        c.write = new QPushButton(tr("Ghi %1").arg(name), box);
        connect(c.write, &QPushButton::clicked, this,
                [this, index] { writeOne(index); });
        grid->addWidget(c.write, row + 2, col);

        c.status = new QLabel(box);
        c.status->setFont(mono);
        c.status->setAlignment(Qt::AlignRight);
        c.status->setToolTip(tr("Giá trị kit trả về trong gói trạng thái. Chữ "
                                "đỏ nghĩa là khác với giá trị vừa ghi xuống."));
        grid->addWidget(c.status, row + 3, col);
    };

    for (int i = 0; i < adf4159::kRegCount; ++i)
        cell(i, QStringLiteral("R%1").arg(i), 0, i);

    auto *line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("color: #23303d;"));
    grid->addWidget(line, 4, 0, 1, 8);

    cell(kDownR4, tr("R4 nhánh 2"), 5, 0);
    cell(kDownR5, tr("R5 nhánh 2"), 5, 1);
    cell(kDownR6, tr("R6 nhánh 2"), 5, 2);

    // --- các nút gửi cả bộ, xếp về bên phải như phần mềm gốc ---
    auto *actions = new QWidget(box);
    auto *av = new QVBoxLayout(actions);
    av->setContentsMargins(0, 0, 0, 0);
    av->setSpacing(4);

    auto *all = new QPushButton(tr("Ghi cả 8 thanh ghi (một gói)"), actions);
    all->setToolTip(tr("Gói CMD_ADF4159_REG8 — cả tám thanh ghi đi trong một "
                       "datagram, kit tự ghi xuống chip theo thứ tự của nó"));
    connect(all, &QPushButton::clicked, this, &Adf4159Window::writeAll);
    av->addWidget(all);

    auto *seq = new QPushButton(tr("Ghi lần lượt 7, 6, 6, 5, 5, 4, 4, 3, 2, 1, 0"),
                                actions);
    seq->setToolTip(tr("Mười một lệnh CMD_ADF4159_REG nối nhau, đúng thứ tự "
                       "phần mềm gốc dùng — R0 vào cuối cùng vì ghi nó mới là "
                       "lúc chip chốt tần số"));
    connect(seq, &QPushButton::clicked, this, &Adf4159Window::writeSequence);
    av->addWidget(seq);

    auto *reset = new QPushButton(tr("Về giá trị mặc định"), actions);
    reset->setToolTip(tr("Chỉ đổi các ô trên màn hình, không gửi lệnh nào"));
    connect(reset, &QPushButton::clicked, this, [this] {
        setSettings(adf4159::Settings{});
        emit settingsChanged();
        setStatusText(tr("Đã về giá trị mặc định — chưa gửi lệnh nào"));
    });
    av->addWidget(reset);

    grid->addWidget(actions, 5, 5, 4, 3);

    for (int i = 0; i < 8; ++i)
        grid->setColumnStretch(i, 1);

    return box;
}

// --- nạp, gom, tính lại ------------------------------------------------------

void Adf4159Window::setSettings(const adf4159::Settings &s)
{
    m_settings = s;
    m_settings.clamp();

    m_loading = true;
    for (const auto &load : m_load)
        load();
    m_loading = false;

    refresh();
}

void Adf4159Window::onEdited()
{
    if (m_loading)
        return;

    for (const auto &collect : m_collect)
        collect();
    m_settings.clamp();

    // Dòng rò âm "tự chọn" là ô duy nhất đổi sau lưng người dùng, nên cũng là ô
    // duy nhất phải đổ ngược lại lên giao diện ở đây.
    if (m_settings.autoNegBleed) {
        m_settings.negBleed = m_settings.bestNegBleed();
        const QSignalBlocker block(m_negBleedCombo);
        m_negBleedCombo->setCurrentIndex(
            m_negBleedCombo->findData(m_settings.negBleed));
    }

    refresh();
    emit settingsChanged();
}

quint32 Adf4159Window::cellValue(int cell) const
{
    switch (cell) {
    case kDownR4: return m_settings.reg4Down();
    case kDownR5: return m_settings.reg5Down();
    case kDownR6: return m_settings.reg6Down();
    default:      return m_settings.registers()[cell];
    }
}

void Adf4159Window::refresh()
{
    const adf4159::Settings &s = m_settings;

    // --- phần tham số RF ---
    m_pfd->setText(num(s.pfdMHz(), 6));
    m_spacing->setText(num(s.channelSpacingKHz(), 7));
    m_int->setText(QString::number(s.intValue()));
    m_frac->setText(QStringLiteral("%1 / %2")
                        .arg(s.fracValue())
                        .arg(quint32(adf4159::kMod)));
    m_n->setText(num(s.nValue(), 6));
    m_rfout->setText(num(s.actualVcoMHz(), 6));
    m_rfout2->setText(num(s.actualVcoMHz() * 2.0, 6));
    m_bleed->setText(num(s.wantedBleedUa(), 1));
    m_negBleedCombo->setEnabled(!s.autoNegBleed);

    // Ba điều kiện dưới đây tài liệu nói thẳng ra; vi phạm thì kit vẫn nhận
    // lệnh nhưng không khoá được tần số, mà nhìn vào giao diện không thấy gì lạ.
    QStringList warn;
    if (s.intValue() < adf4159::minInt(s.prescaler)) {
        warn << tr("INT = %1 nhỏ hơn mức bộ chia trước cho phép (%2)")
                    .arg(s.intValue())
                    .arg(adf4159::minInt(s.prescaler));
    }
    if (s.prescaler == 0 && s.vcoMHz > adf4159::kPrescaler45MaxMHz)
        warn << tr("Trên 8 GHz phải chọn bộ chia trước 8/9");
    if (s.clk1 == 1 && s.up.clk2 == 1)
        warn << tr("CLK1 và CLK2 không được cùng bằng 1");
    m_warn->setText(warn.join(QStringLiteral("\n")));
    m_warn->setVisible(!warn.isEmpty());

    // --- phần quét tần ---
    const auto showRamp = [&s](const adf4159::Ramp &r, QLabel *dev,
                               QLabel *total, QLabel *step, QLabel *ramp) {
        dev->setText(num(s.devKHz(r), 6));
        total->setText(num(s.totalKHz(r), 3));
        step->setText(num(s.stepUs(r), 6));
        ramp->setText(num(s.rampUs(r), 3));
    };
    showRamp(s.up, m_upDev, m_upTotal, m_upStep, m_upRamp);
    showRamp(s.down, m_downDev, m_downTotal, m_downStep, m_downRamp);
    m_downRampBox->setEnabled(s.dualRamp);
    m_delay->setText(num(s.delayUs(), 3));

    // --- thanh thanh ghi ---
    for (int i = 0; i < kCellCount; ++i) {
        RegCell &c = m_cells[i];
        const quint32 now = cellValue(i);

        c.value->setText(hex(now));
        // Nền xanh lá: giá trị trên màn hình chưa xuống tới kit.
        const bool dirty = !c.hasWritten || c.written != now;
        c.value->setStyleSheet(dirty ? kDirtyStyle : QString());

        if (!c.hasStatus) {
            c.status->setText(QString());
        } else {
            c.status->setText(hex(c.statusValue));
            // So với **giá trị đã ghi**, không phải giá trị đang hiện: sau khi
            // vặn thêm vài ô mà chưa ghi thì kit khác màn hình là chuyện đương
            // nhiên, tô đỏ chỗ đó chỉ làm nhiễu mắt.
            const bool differs = c.hasWritten && c.statusValue != c.written;
            c.status->setStyleSheet(differs ? kRedStyle : kInfoStyle);
        }
    }

    if (m_rampButton) {
        m_rampButton->setText(s.rampOn ? tr("Tắt quét tần (ghi R0)")
                                       : tr("Bật quét tần (ghi R0)"));
    }
}

// --- gửi lệnh ----------------------------------------------------------------

quint32 Adf4159Window::nextSerial(int kind)
{
    // Tăng **trước** khi đóng gói, nếu không gói đầu tiên mang Serial 0 mà mô
    // tả giao thức nói rõ là đếm từ 1. Gửi hỏng thì setSendResult() trả lại.
    return ++m_serial[kind];
}

bool Adf4159Window::send(int kind, const QByteArray &datagram)
{
    m_lastSendOk = false;
    emit commandReady(kind, datagram);
    return m_lastSendOk;
}

void Adf4159Window::setSendResult(int kind, bool ok)
{
    if (kind < 0 || kind >= KindCount)
        return;
    m_lastSendOk = ok;
    if (!ok && m_serial[kind] > 0) {
        // Lệnh không ra khỏi máy thì số Serial đó chưa dùng — trả lại để dãy
        // Serial bên kit vẫn liền mạch.
        --m_serial[kind];
    }
}

void Adf4159Window::writeOne(int cell)
{
    const quint32 value = cellValue(cell);
    const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());

    if (!send(KindReg1, adfproto::buildOne(value, nextSerial(KindReg1), timeMs)))
        return;

    m_cells[cell].written    = value;
    m_cells[cell].hasWritten = true;
    refresh();
    setStatusText(tr("Đã ghi %1 = %2")
                      .arg(cell < adf4159::kRegCount
                               ? QStringLiteral("R%1").arg(cell)
                               : tr("R%1 nhánh 2").arg(cell - kDownR4 + 4))
                      .arg(hex(value)));
}

void Adf4159Window::writeAll()
{
    const adf4159::Registers regs = m_settings.registers();
    const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());

    if (!send(KindReg8, adfproto::buildCommand(adfproto::kReg8, regs.data(),
                                               nextSerial(KindReg8), timeMs)))
        return;

    for (int i = 0; i < adf4159::kRegCount; ++i) {
        m_cells[i].written    = regs[i];
        m_cells[i].hasWritten = true;
    }
    refresh();
    setStatusText(tr("Đã gửi cả 8 thanh ghi trong một gói"));
}

void Adf4159Window::writeSequence()
{
    // Đúng dãy của phần mềm gốc. Ba thanh ghi nhánh 2 vẫn ghi kể cả khi chưa
    // bật nhánh quét thứ hai — chúng chỉ nạp vào bộ nhớ thứ hai của chip, không
    // có tác dụng gì cho tới khi bit DUAL RAMP bật lên.
    const int order[] = {7, 6, kDownR6, 5, kDownR5, 4, kDownR4, 3, 2, 1, 0};

    int sent = 0;
    for (int cell : order) {
        const quint32 value  = cellValue(cell);
        const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
        if (!send(KindReg1, adfproto::buildOne(value, nextSerial(KindReg1), timeMs)))
            break;
        m_cells[cell].written    = value;
        m_cells[cell].hasWritten = true;
        ++sent;
    }

    refresh();
    if (sent == int(std::size(order)))
        setStatusText(tr("Đã ghi lần lượt %1 thanh ghi").arg(sent));
}

void Adf4159Window::toggleRamp()
{
    m_settings.rampOn = !m_settings.rampOn;

    // Đổi thẳng trong m_settings nên phải đổ ngược lên hộp chọn tương ứng, rồi
    // mới ghi R0 — nếu không thì lần onEdited() sau lấy lại giá trị cũ của ô.
    m_loading = true;
    for (const auto &load : m_load)
        load();
    m_loading = false;

    refresh();
    emit settingsChanged();
    writeOne(0);
}

// --- trạng thái phản hồi -----------------------------------------------------

bool Adf4159Window::applyStatus(const QByteArray &datagram)
{
    const adfproto::Packet *p =
        adfproto::statusPacket(datagram.constData(), datagram.size());
    if (!p)
        return false;

    const int kind = (p == &adfproto::kReg8) ? KindReg8 : KindReg1;

    quint32 regs[adf4159::kRegCount] = {};
    quint32 timeMs = 0;
    adfproto::parseStatus(*p, datagram.constData(), m_statusSerial[kind], timeMs,
                          regs);

    if (kind == KindReg8) {
        for (int i = 0; i < adf4159::kRegCount; ++i) {
            m_cells[i].statusValue = regs[i];
            m_cells[i].hasStatus   = true;
        }
    } else {
        // Gói một thanh ghi không mang số hiệu riêng: ba bit thấp nhất của
        // chính từ đó cho biết là thanh ghi nào. R4/R5/R6 còn có bit chọn nhánh
        // để biết đó là bộ nhánh 1 hay nhánh 2.
        const int reg = adfproto::regIndexOf(regs[0]);
        int cell = reg;
        if (reg == 4 && (regs[0] & (1u << 6)))
            cell = kDownR4;
        else if (reg == 5 && (regs[0] & (1u << 23)))
            cell = kDownR5;
        else if (reg == 6 && (regs[0] & (1u << 23)))
            cell = kDownR6;

        m_cells[cell].statusValue = regs[0];
        m_cells[cell].hasStatus   = true;
    }

    refresh();
    setStatusText(tr("Trạng thái: lệnh %1 gói, kit trả lời %2 gói")
                      .arg(m_serial[kind])
                      .arg(m_statusSerial[kind]));
    return true;
}

void Adf4159Window::setStatusText(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError ? kRedStyle : kInfoStyle);
}
