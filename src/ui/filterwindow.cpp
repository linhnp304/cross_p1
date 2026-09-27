#include "ui/filterwindow.h"

#include "proc/filterdata.h"

#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStringList>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {

using filterproto::Packet;

const QString kRedStyle  = QStringLiteral("color: #d47b6a;");
const QString kInfoStyle = QStringLiteral("color: #7fa8c9;");

/// Chữ của cột S (giá trị đem gửi) và cột R (giá trị đài trả về). Hai màu khác
/// nhau để nhìn một cái là biết đang đọc chiều nào — mô tả giai đoạn nêu thẳng
/// yêu cầu này.
const QColor kSendText(0xd7, 0xe3, 0xef);   ///< sáng, như ô nhập được
const QColor kRecvText(0x7f, 0xa8, 0xc9);   ///< xanh, như dòng thông tin
const QColor kBadText(0xd4, 0x7b, 0x6a);    ///< đỏ, dòng lệch giá trị
const QColor kIndexText(0x8b, 0x98, 0xa5);  ///< cột STT, mờ hơn cả hai

/// Nền hai dòng chẵn lẻ. Chênh nhau rất ít — đủ để mắt lần theo một dòng ngang
/// qua chín cột, không đủ để thành hai màu.
const QColor kRowEven(0x0d, 0x11, 0x17);
const QColor kRowOdd(0x12, 0x1a, 0x23);

/// Bề rộng cột. Đặt tay chứ không resizeColumnsToContents(): bảng có 1024 dòng,
/// mà hàm ấy đo **mọi** dòng của mọi cột — một phần giây đứng máy mỗi lần mở
/// cửa sổ, để đo ra đúng những con số dưới đây.
constexpr int kIndexWidth = 62;
constexpr int kHexWidth   = 94;

} // namespace

FilterWindow::FilterWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Điều khiển các bộ lọc"));
    // Tên đối tượng để công cụ điều khiển giao diện bằng kịch bản chỉ đúng được
    // vào đây: chữ "Điều khiển các bộ lọc" vừa là tiêu đề cửa sổ vừa là chữ trên
    // nút mở nó, nên chỉ theo chữ thì không phân biệt được hai thứ.
    setObjectName(QStringLiteral("filterWindow"));
    setWindowFlag(Qt::Window);
    setModal(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *intro = new QLabel(
        tr("Hệ số đọc từ thư mục %1 — mỗi bộ lọc một file, mỗi dòng một số hex. "
           "Cột S là giá trị gửi đi (sửa được), cột R là giá trị đài trả về.")
            .arg(QDir::toNativeSeparators(filterdata::dirPath())), this);
    intro->setWordWrap(true);
    intro->setStyleSheet(kInfoStyle);
    root->addWidget(intro);

    buildTable();
    root->addWidget(m_table, 1);

    m_errors = new QLabel(this);
    m_errors->setObjectName(QStringLiteral("filterErrors"));
    m_errors->setWordWrap(true);
    m_errors->setStyleSheet(kRedStyle);
    m_errors->setVisible(false);
    root->addWidget(m_errors);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("filterStatus"));
    m_status->setWordWrap(true);
    m_status->setStyleSheet(kInfoStyle);
    root->addWidget(m_status);

    m_send = new QPushButton(tr("Gửi lệnh"), this);
    m_send->setObjectName(QStringLiteral("filterSend"));
    m_send->setToolTip(tr("Gửi bốn gói nạp bộ lọc (FIR, WFC, STF, MTK) cách nhau "
                          "100 ms.\n"
                          "Khoá khi còn file dữ liệu thiếu hoặc đọc không được."));
    connect(m_send, &QPushButton::clicked, this, &FilterWindow::startSending);

    auto *closeBtn = new QPushButton(tr("Đóng"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    auto *row = new QHBoxLayout;
    row->addWidget(m_send, 0);
    row->addStretch(1);
    row->addWidget(closeBtn, 0);
    root->addLayout(row);

    // Nhịp 100 ms giữa bốn gói. Một QTimer chạy lại chứ không bốn singleShot xếp
    // trước: có một chỗ duy nhất trả lời câu "đang gửi dở hay không"
    // (isActive()), nhờ vậy bấm "Gửi lệnh" lần nữa giữa chừng không xếp thêm bốn
    // gói nữa, và nút khoá/mở đúng theo trạng thái đó.
    //
    // Chuỗi đã bắt đầu thì **chạy cho hết**, kể cả khi cửa sổ vừa bị đóng (khoá
    // điều khiển cũng đóng nó). Bốn bộ lọc là một bộ: dừng giữa chừng để đài
    // chạy với hai bộ mới và hai bộ cũ còn tệ hơn là để nốt 300 ms cho xong.
    m_pace = new QTimer(this);
    m_pace->setInterval(100);
    connect(m_pace, &QTimer::timeout, this, [this] {
        if (m_nextTx >= filterproto::KindCount) {
            m_pace->stop();
            m_send->setEnabled(m_errors->text().isEmpty());
            return;
        }
        sendOne(m_nextTx++);
    });

    // Cửa sổ cao và hẹp: chín cột vừa đủ một bề rộng biết trước, còn 1024 dòng
    // thì càng cao càng đỡ phải cuộn.
    const QRect avail = QGuiApplication::primaryScreen()
                            ? QGuiApplication::primaryScreen()->availableGeometry()
                            : QRect(0, 0, 1280, 800);
    const int w = qMin(kIndexWidth + kHexWidth * filterproto::KindCount * 2 + 60,
                       avail.width() - 40);
    resize(w, avail.height() * 4 / 5);
    move(avail.x() + 40, avail.y() + 20);
}

void FilterWindow::buildTable()
{
    m_table = new QTableWidget(filterproto::kMaxCount, kColumnCount, this);
    m_table->setObjectName(QStringLiteral("filterTable"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(0, kIndexWidth);
    for (int c = 1; c < kColumnCount; ++c)
        m_table->setColumnWidth(c, kHexWidth);

    // Dòng đều nhau và biết trước chiều cao: QTableWidget khỏi phải đo 1024 dòng
    // mỗi lần dàn trang.
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(20);

    m_loading = true;
    for (int r = 0; r < filterproto::kMaxCount; ++r) {
        const QBrush back(r % 2 == 0 ? kRowEven : kRowOdd);

        auto *idx = new QTableWidgetItem(QString::number(r + 1));
        idx->setFlags(Qt::ItemIsEnabled);
        idx->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        idx->setForeground(QBrush(kIndexText));
        idx->setBackground(back);
        m_table->setItem(r, 0, idx);

        for (int k = 0; k < filterproto::KindCount; ++k) {
            const bool within = r < filterproto::kPackets[k].count;

            auto *send = new QTableWidgetItem;
            // Ngoài dải của bộ lọc thì ô để trắng **và** không sửa được: cho sửa
            // một ô không có chỗ nào trong gói tin là mời người dùng gõ vào hư
            // không.
            send->setFlags(within ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                     | Qt::ItemIsEditable)
                                  : Qt::ItemFlags(Qt::NoItemFlags));
            send->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            send->setForeground(QBrush(kSendText));
            send->setBackground(back);
            m_table->setItem(r, sendColumn(k), send);

            auto *recv = new QTableWidgetItem;
            recv->setFlags(within ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable)
                                  : Qt::ItemFlags(Qt::NoItemFlags));
            recv->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            recv->setForeground(QBrush(kRecvText));
            recv->setBackground(back);
            m_table->setItem(r, recvColumn(k), recv);
        }
    }
    m_loading = false;

    for (int k = 0; k < filterproto::KindCount; ++k)
        refreshHeader(k);
    m_table->setHorizontalHeaderItem(0, new QTableWidgetItem(tr("STT")));

    connect(m_table, &QTableWidget::itemChanged, this,
            &FilterWindow::onItemChanged);
}

QString FilterWindow::hexText(quint32 v)
{
    return QString::asprintf("0x%08X", v);
}

void FilterWindow::refreshHeader(int kind)
{
    const Packet &p = filterproto::kPackets[kind];
    const KindUi &u = m_kinds[kind];

    // Chưa có số thì bỏ hẳn cả dấu gạch, không hiện "FIR (S--)": nhãn cột hẹp,
    // mà hai gạch liền nhau đọc như một con số âm.
    const auto title = [&p](QLatin1Char side, quint32 serial, bool has) {
        return has ? QStringLiteral("%1 (%2-%3)")
                         .arg(QLatin1String(p.name))
                         .arg(side)
                         .arg(serial)
                   : QStringLiteral("%1 (%2)")
                         .arg(QLatin1String(p.name))
                         .arg(side);
    };

    m_table->setHorizontalHeaderItem(
        sendColumn(kind),
        new QTableWidgetItem(title(QLatin1Char('S'), u.cmdSerial,
                                   u.cmdSerial > 0)));
    m_table->setHorizontalHeaderItem(
        recvColumn(kind),
        new QTableWidgetItem(title(QLatin1Char('R'), u.statusSerial,
                                   u.hasStatus)));
}

void FilterWindow::reload()
{
    for (int k = 0; k < filterproto::KindCount; ++k) {
        const Packet &p = filterproto::kPackets[k];
        KindUi &u = m_kinds[k];

        const filterdata::Result r =
            filterdata::load(filterdata::filePath(p.file), p.count,
                             p.linesPerValue);
        u.error   = r.error;
        u.hasData = r.ok();
        // Đọc hỏng thì xoá luôn giá trị cũ chứ không giữ lại: nút "Gửi lệnh" lúc
        // ấy bị khoá, mà bảng vẫn hiện dãy số của lần đọc trước thì nó nói rằng
        // đang có dữ liệu sẵn sàng gửi.
        u.values = r.ok() ? r.values : QVector<quint32>(p.count, 0);

        refreshColumns(k);
    }
    refreshErrors();
}

void FilterWindow::refreshColumns(int kind)
{
    const Packet &p = filterproto::kPackets[kind];
    const KindUi &u = m_kinds[kind];

    m_loading = true;
    for (int r = 0; r < p.count; ++r) {
        if (QTableWidgetItem *it = m_table->item(r, sendColumn(kind)))
            it->setText(u.hasData ? hexText(u.values.at(r)) : QString());
        if (QTableWidgetItem *it = m_table->item(r, recvColumn(kind))) {
            // Chưa có gói phản hồi thì cột R là 0 ở đúng những dòng bộ lọc có —
            // mô tả giai đoạn nêu thẳng, và nó phân biệt được "chưa trả lời" với
            // "dòng ngoài dải" (để trắng).
            it->setText(hexText(u.hasStatus ? u.status.at(r) : 0u));
        }
    }
    m_loading = false;

    refreshMismatch(kind);
}

void FilterWindow::refreshMismatch(int kind)
{
    const Packet &p = filterproto::kPackets[kind];
    const KindUi &u = m_kinds[kind];

    // So với giá trị của **lần gửi gần nhất**, không phải với ô đang hiện: sửa
    // một ô sau khi đã gửi thì đài vẫn đang dùng giá trị cũ. Chưa gửi lần nào
    // thì lấy giá trị đang có trên bảng — đó là tất cả những gì biết được.
    const QVector<quint32> &want = u.hasSent ? u.sent : u.values;

    m_loading = true;
    for (int r = 0; r < p.count; ++r) {
        QTableWidgetItem *it = m_table->item(r, recvColumn(kind));
        if (!it)
            continue;
        const bool differs = u.hasStatus && r < want.size()
                             && u.status.at(r) != want.at(r);
        it->setForeground(QBrush(differs ? kBadText : kRecvText));
    }
    m_loading = false;
}

void FilterWindow::refreshErrors()
{
    QStringList msgs;
    for (const KindUi &u : m_kinds) {
        if (!u.error.isEmpty())
            msgs << u.error;
    }

    m_errors->setText(msgs.join(QLatin1Char('\n')));
    m_errors->setVisible(!msgs.isEmpty());

    // Nút khoá khi còn lỗi: bốn gói là một bộ, gửi ba gói đúng với một gói toàn
    // số 0 thì đài chạy với một bộ lọc chắp vá.
    m_send->setEnabled(msgs.isEmpty() && !m_pace->isActive());
}

void FilterWindow::onItemChanged(QTableWidgetItem *item)
{
    if (m_loading || !item)
        return;

    // Chỉ cột S sửa được, nhưng tín hiệu này cũng phát khi chính hàm refresh đổi
    // chữ cột R — cờ m_loading chặn phần đó, còn đây chặn phần còn lại.
    const int col = item->column();
    if (col <= 0 || (col - 1) % 2 != 0)
        return;

    const int kind = (col - 1) / 2;
    const int r    = item->row();
    KindUi &u = m_kinds[kind];
    if (r >= u.values.size())
        return;

    const QString typed = item->text().trimmed();
    QString s = typed;
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        s = s.mid(2);

    bool ok = false;
    const qulonglong v = s.toULongLong(&ok, 16);
    if (!ok || v > 0xffffffffull) {
        // Trả ô về giá trị cũ thay vì để một chuỗi vô nghĩa nằm đó: giá trị trên
        // bảng là thứ sẽ đi vào gói tin, không được có ô nào "đang gõ dở".
        m_loading = true;
        item->setText(hexText(u.values.at(r)));
        m_loading = false;
        setStatusText(tr("Dòng %1 cột %2: \"%3\" không phải số hex 32 bit")
                          .arg(r + 1)
                          .arg(QLatin1String(filterproto::kPackets[kind].name),
                               typed),
                      true);
        return;
    }

    u.values[r] = quint32(v);
    // Gõ "1f" thì hiện lại thành 0x0000001F: mọi ô cùng một dạng thì so sánh
    // bằng mắt giữa cột S và cột R mới nhanh được.
    m_loading = true;
    item->setText(hexText(u.values.at(r)));
    m_loading = false;

    refreshMismatch(kind);
    setStatusText(QString());
}

void FilterWindow::startSending()
{
    if (m_pace->isActive())
        return;

    m_send->setEnabled(false);
    setStatusText(tr("Đang gửi bốn gói nạp bộ lọc..."));

    // Gói đầu đi ngay, ba gói sau theo nhịp 100 ms.
    sendOne(filterproto::FIR);
    m_nextTx = filterproto::FIR + 1;
    m_pace->start();
}

void FilterWindow::sendOne(int kind)
{
    const Packet &p = filterproto::kPackets[kind];
    KindUi &u = m_kinds[kind];
    // Nút "Gửi lệnh" đã khoá khi còn file lỗi, nhưng chuỗi bốn gói chạy trong
    // 300 ms và cửa sổ có thể được mở lại (đọc lại đĩa) giữa chừng — chặn thêm
    // ở đây để không gói nào mang một dãy toàn số 0 đi ra đài.
    if (!u.hasData || u.values.size() != p.count)
        return;

    ++u.cmdSerial;
    const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
    const QByteArray dg = filterproto::buildCommand(p, u.values.constData(),
                                                    u.cmdSerial, timeMs);

    m_lastSendOk = false;
    emit commandReady(kind, dg);

    if (!m_lastSendOk) {
        // Gói không ra khỏi máy thì số Serial đó chưa dùng — trả lại để dãy
        // Serial bên đài vẫn liền mạch.
        --u.cmdSerial;
    } else {
        u.sent    = u.values;
        u.hasSent = true;
        setStatusText(tr("Đã gửi %1 (serial %2)")
                          .arg(QLatin1String(p.name))
                          .arg(u.cmdSerial));
    }

    refreshHeader(kind);
    refreshMismatch(kind);
}

void FilterWindow::setSendResult(int kind, bool ok)
{
    Q_UNUSED(kind);
    m_lastSendOk = ok;
}

bool FilterWindow::applyStatus(const QByteArray &datagram)
{
    const Packet *p =
        filterproto::statusPacket(datagram.constData(), datagram.size());
    if (!p)
        return false;

    const int kind = int(p - filterproto::kPackets);
    KindUi &u = m_kinds[kind];

    u.status.resize(p->count);
    quint32 timeMs = 0;
    filterproto::parseStatus(*p, datagram.constData(), u.statusSerial, timeMs,
                             u.status.data());
    u.hasStatus = true;

    refreshColumns(kind);
    refreshHeader(kind);
    return true;
}

void FilterWindow::setStatusText(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError ? kRedStyle : kInfoStyle);
}
