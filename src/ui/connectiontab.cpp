#include "ui/connectiontab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum Column { ColName, ColLocalIp, ColRemoteIp, ColLocalPort, ColRemotePort,
              ColCount };

/// Cột của bảng cổng gửi. Không có cột "Tên": mỗi dòng đã tự nói lên mình là
/// gì qua cột "Loại dữ liệu".
enum TxColumn { TxSend, TxKindCol, TxLocalIp, TxRemoteIp, TxLocalPort,
                TxRemotePort, TxBroadcast, TxColCount };

const QColor kBadCell(90, 30, 34);

/// Rỗng hoặc 0.0.0.0 đều hợp lệ — nghĩa là "mọi máy" / "mọi cổng".
bool ipLooksValid(const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return true;
    return QHostAddress(s).protocol() != QAbstractSocket::UnknownNetworkLayerProtocol;
}

bool portLooksValid(const QString &text, quint16 &out)
{
    const QString s = text.trimmed();
    if (s.isEmpty()) {
        out = 0;
        return true;
    }
    bool ok = false;
    const uint v = s.toUInt(&ok);
    if (!ok || v > 65535)
        return false;
    out = quint16(v);
    return true;
}

QTableWidgetItem *makeItem(const QString &text)
{
    auto *it = new QTableWidgetItem(text);
    it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return it;
}

/// Ô chỉ để chứa widget: khoá sửa và khoá chọn, nếu không thì kích vào ô là mở
/// ô nhập chữ chồng lên chính cái checkbox đang nằm đó.
QTableWidgetItem *makeHostItem()
{
    auto *it = new QTableWidgetItem;
    it->setFlags(Qt::ItemIsEnabled);
    return it;
}

/// Đặt một widget vào giữa ô.
QWidget *centred(QWidget *inner, QWidget *parent)
{
    auto *host = new QWidget(parent);
    auto *lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setAlignment(Qt::AlignCenter);
    lay->addWidget(inner);
    return host;
}

} // namespace

ConnectionTab::ConnectionTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // --- Bảng cổng nhận ---------------------------------------------------
    auto *rxBox = new QGroupBox(tr("Cổng UDP nhận dữ liệu"), this);
    auto *rxLay = new QVBoxLayout(rxBox);

    m_rx = new QTableWidget(0, ColCount, rxBox);
    m_rx->setHorizontalHeaderLabels({tr("Tên"), tr("LocalIP"), tr("RemoteIP"),
                                     tr("LocalPort"), tr("RemotePort")});
    m_rx->verticalHeader()->setVisible(false);
    m_rx->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rx->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rx->setEditTriggers(QAbstractItemView::DoubleClicked
                          | QAbstractItemView::SelectedClicked
                          | QAbstractItemView::EditKeyPressed);
    m_rx->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_rx->horizontalHeader()->setStretchLastSection(true);
    m_rx->setMinimumHeight(130);
    m_rx->setToolTip(tr("LocalIP: địa chỉ của card mạng nối với đài — để trống "
                        "hoặc 0.0.0.0 là nghe trên mọi card.\n"
                        "RemoteIP để trống hoặc 0.0.0.0, RemotePort để 0: "
                        "nhận từ mọi máy / mọi cổng"));
    rxLay->addWidget(m_rx);

    m_add    = new QPushButton(tr("Thêm dòng"), rxBox);
    m_remove = new QPushButton(tr("Xoá dòng"), rxBox);
    // Nút bật/tắt nằm trong chính group của nó: hai chiều dữ liệu bật tắt độc
    // lập với nhau, để một nút chung ở ngoài thì không nhìn ra điều đó.
    m_toggle = new QPushButton(tr("Bắt đầu nhận dữ liệu"), rxBox);
    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_add);
    btnRow->addWidget(m_remove);
    btnRow->addStretch(1);
    btnRow->addWidget(m_toggle);
    rxLay->addLayout(btnRow);

    // --- Bảng cổng gửi ----------------------------------------------------
    auto *txBox = new QGroupBox(tr("Cổng UDP gửi dữ liệu"), this);
    auto *txLay = new QVBoxLayout(txBox);

    m_tx = new QTableWidget(0, TxColCount, txBox);
    m_tx->setHorizontalHeaderLabels({tr("Gửi"), tr("Loại dữ liệu"), tr("LocalIP"),
                                     tr("RemoteIP"), tr("LocalPort"),
                                     tr("RemotePort"), tr("Broadcast")});
    m_tx->verticalHeader()->setVisible(false);
    m_tx->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tx->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tx->setEditTriggers(QAbstractItemView::DoubleClicked
                          | QAbstractItemView::SelectedClicked
                          | QAbstractItemView::EditKeyPressed);
    m_tx->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tx->horizontalHeader()->setStretchLastSection(true);
    m_tx->setMinimumHeight(110);
    m_tx->setToolTip(tr("Mỗi khi có điểm dấu mới hoặc quỹ đạo được cập nhật thì "
                        "gói tin được gửi tới mọi dòng đang bật ô Gửi và đúng "
                        "loại dữ liệu — với điều kiện nút \"Bắt đầu gửi dữ "
                        "liệu\" bên dưới đang bật.\n"
                        "LocalIP ở đây là card mạng đi ra; LocalPort để 0 là "
                        "để hệ điều hành tự chọn cổng nguồn.\n"
                        "Broadcast: gửi tới địa chỉ quảng bá của dải chứa "
                        "RemoteIP thay vì gửi thẳng cho một máy.\n"
                        "Ô Gửi của dòng Plot/Track luôn bắt đầu ở trạng thái tắt "
                        "mỗi lần chạy.\n"
                        "Dòng \"Command\" là nơi các lệnh của tab \"Điều khiển\" "
                        "đi ra. Nó luôn gửi (ô Gửi bị khoá) và không phụ thuộc "
                        "nút \"Bắt đầu gửi dữ liệu\"."));
    txLay->addWidget(m_tx);

    m_txAdd    = new QPushButton(tr("Thêm dòng"), txBox);
    m_txRemove = new QPushButton(tr("Xoá dòng"), txBox);
    m_txToggle = new QPushButton(tr("Bắt đầu gửi dữ liệu"), txBox);
    m_txToggle->setToolTip(tr("Cổng chỉ thực sự mở khi nút này bật và dòng đó "
                              "cũng đã đánh dấu ô Gửi."));
    auto *txBtnRow = new QHBoxLayout;
    txBtnRow->addWidget(m_txAdd);
    txBtnRow->addWidget(m_txRemove);
    txBtnRow->addStretch(1);
    txBtnRow->addWidget(m_txToggle);
    txLay->addLayout(txBtnRow);

    // --- Dòng trạng thái --------------------------------------------------
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #7fa8c9;"));

    root->addWidget(rxBox);
    root->addWidget(txBox);
    root->addWidget(m_status);
    root->addStretch(1);

    connect(m_rx, &QTableWidget::itemChanged, this, &ConnectionTab::onCellChanged);
    connect(m_add, &QPushButton::clicked, this, &ConnectionTab::addRow);
    connect(m_remove, &QPushButton::clicked, this, &ConnectionTab::removeSelectedRow);

    connect(m_tx, &QTableWidget::itemChanged, this, &ConnectionTab::onTxCellChanged);
    connect(m_txAdd, &QPushButton::clicked, this, &ConnectionTab::addTxRow);
    connect(m_txRemove, &QPushButton::clicked, this,
            &ConnectionTab::removeSelectedTxRow);
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (m_running)
            emit disconnectRequested();
        else
            emit connectRequested();
    });
    connect(m_txToggle, &QPushButton::clicked, this, [this] {
        if (m_txRunning)
            emit sendStopRequested();
        else
            emit sendStartRequested();
    });
}

void ConnectionTab::setParams(const AppParams &p)
{
    m_params = p;
    // Ô "Gửi" của dòng dữ liệu luôn bắt đầu ở trạng thái tắt: mở phần mềm lên mà
    // tự phát gói ra mạng là chuyện không ai muốn, nhất là khi cấu hình còn của
    // lần lắp đặt trước. Dòng lệnh điều khiển thì luôn bật — xem chú thích ở
    // NetEndpoint::enabled.
    for (NetEndpoint &e : m_params.tx)
        e.enabled = e.kind == TxKind::Command;
    rebuildTable();
    rebuildTxTable();
}

void ConnectionTab::setRunning(bool on)
{
    m_running = on;
    m_toggle->setText(on ? tr("Dừng nhận dữ liệu") : tr("Bắt đầu nhận dữ liệu"));

    // Sửa cổng **nhận** lúc đang chạy chỉ gây hiểu nhầm là đã có hiệu lực.
    // Bảng gửi thì ngược lại: ô "Gửi" sinh ra là để bật/tắt giữa chừng, và mọi
    // thay đổi trong bảng đó đều mở lại socket ngay nên vẫn để sửa được.
    m_rx->setEnabled(!on);
    m_add->setEnabled(!on);
    m_remove->setEnabled(!on);
}

void ConnectionTab::setTxRunning(bool on)
{
    m_txRunning = on;
    m_txToggle->setText(on ? tr("Dừng gửi dữ liệu") : tr("Bắt đầu gửi dữ liệu"));
}

void ConnectionTab::setStatusText(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError ? QStringLiteral("color: #d47b6a;")
                                    : QStringLiteral("color: #7fa8c9;"));
}

void ConnectionTab::rebuildTable()
{
    m_loading = true;
    // setRowCount về 0 trước: đặt lại số dòng mà không xoá thì các ComboBox của
    // lần dựng trước còn nằm nguyên trong ô và chồng lên cái mới.
    m_rx->setRowCount(0);
    m_rx->setRowCount(m_params.rx.size());

    for (int r = 0; r < m_params.rx.size(); ++r) {
        const NetEndpoint &e = m_params.rx[r];

        // ComboBox cho sẵn bốn loại dữ liệu, nhưng vẫn để sửa được: tên chỉ là
        // nhãn cho người đọc, việc giải mã phân loại theo nội dung gói.
        auto *name = new QComboBox(m_rx);
        name->setEditable(true);
        name->addItems(AppParams::rxNames());
        name->setCurrentText(e.name);
        m_rx->setItem(r, ColName, makeHostItem());
        m_rx->setCellWidget(r, ColName, name);
        connect(name, &QComboBox::currentTextChanged,
                this, &ConnectionTab::onCellChanged);

        m_rx->setItem(r, ColLocalIp,    makeItem(e.localIp));
        m_rx->setItem(r, ColRemoteIp,   makeItem(e.remoteIp));
        m_rx->setItem(r, ColLocalPort,  makeItem(QString::number(e.localPort)));
        m_rx->setItem(r, ColRemotePort, makeItem(QString::number(e.remotePort)));
    }
    m_rx->resizeColumnsToContents();
    m_loading = false;
}

void ConnectionTab::addRow()
{
    NetEndpoint e;

    // Ba loại kia đã có sẵn từ lần chạy đầu, nên dòng người dùng tự thêm gần
    // như luôn là "Plot" — điền sẵn cả tên lẫn cổng mặc định của nó.
    const QStringList names = AppParams::rxNames();
    e.name      = names.last();
    e.localPort = AppParams::kDefaultPlotPort;

    m_params.rx.push_back(e);
    rebuildTable();
    emit endpointsChanged(m_params.rx);
}

void ConnectionTab::removeSelectedRow()
{
    const int r = m_rx->currentRow();
    if (r < 0 || r >= m_params.rx.size())
        return;
    m_params.rx.remove(r);
    rebuildTable();
    emit endpointsChanged(m_params.rx);
}

void ConnectionTab::onCellChanged()
{
    if (m_loading)
        return;
    if (readTable())
        emit endpointsChanged(m_params.rx);
}

// ------------------------------------------------------- bảng cổng gửi ----

void ConnectionTab::fillTxWidgets(int row, const NetEndpoint &e)
{
    // Ô rỗng có cờ chỉ-đọc nằm dưới widget: không có nó thì kích vào phần nền
    // quanh ô đánh dấu là bật ra một ô nhập chữ chồng lên chính widget đó.
    auto *send = new QCheckBox(m_tx);
    send->setChecked(e.enabled);
    m_tx->setItem(row, TxSend, makeHostItem());
    m_tx->setCellWidget(row, TxSend, centred(send, m_tx));
    connect(send, &QCheckBox::toggled, this, &ConnectionTab::onTxCellChanged);

    auto *kind = new QComboBox(m_tx);
    const QStringList kinds = AppParams::txKindNames();
    for (int i = 0; i < kinds.size(); ++i)
        kind->addItem(kinds.at(i), i);
    kind->setCurrentIndex(int(e.kind));
    m_tx->setItem(row, TxKindCol, makeHostItem());
    m_tx->setCellWidget(row, TxKindCol, kind);
    connect(kind, &QComboBox::currentIndexChanged,
            this, &ConnectionTab::onTxCellChanged);

    auto *bcast = new QCheckBox(m_tx);
    bcast->setChecked(e.broadcast);
    m_tx->setItem(row, TxBroadcast, makeHostItem());
    m_tx->setCellWidget(row, TxBroadcast, centred(bcast, m_tx));
    connect(bcast, &QCheckBox::toggled, this, &ConnectionTab::onTxCellChanged);
}

void ConnectionTab::syncTxSendBoxes()
{
    const bool wasLoading = m_loading;
    m_loading = true;   // ép tích cũng phát toggled, mà toggled lại gọi về đây

    for (int r = 0; r < m_tx->rowCount(); ++r) {
        auto *kind = qobject_cast<QComboBox *>(m_tx->cellWidget(r, TxKindCol));
        QWidget *host = m_tx->cellWidget(r, TxSend);
        QCheckBox *send = host ? host->findChild<QCheckBox *>() : nullptr;
        if (!kind || !send)
            continue;

        // Dòng lệnh điều khiển luôn gửi: tích sẵn rồi khoá ô lại, để không có
        // trạng thái "có dòng Command mà lệnh vẫn không đi đâu" — trạng thái đó
        // nhìn giao diện không phân biệt được với cấu hình đúng.
        const bool cmd = TxKind(kind->currentData().toInt()) == TxKind::Command;
        if (cmd && !send->isChecked())
            send->setChecked(true);
        send->setEnabled(!cmd);
        send->setToolTip(cmd ? tr("Dòng lệnh điều khiển luôn gửi") : QString());
    }

    m_loading = wasLoading;
}

void ConnectionTab::rebuildTxTable()
{
    m_loading = true;
    m_tx->setRowCount(0);
    m_tx->setRowCount(m_params.tx.size());

    for (int r = 0; r < m_params.tx.size(); ++r) {
        const NetEndpoint &e = m_params.tx[r];
        m_tx->setItem(r, TxLocalIp,    makeItem(e.localIp));
        m_tx->setItem(r, TxRemoteIp,   makeItem(e.remoteIp));
        m_tx->setItem(r, TxLocalPort,  makeItem(QString::number(e.localPort)));
        m_tx->setItem(r, TxRemotePort, makeItem(QString::number(e.remotePort)));
        fillTxWidgets(r, e);
    }
    m_tx->resizeColumnsToContents();
    m_loading = false;
    syncTxSendBoxes();
}

void ConnectionTab::addTxRow()
{
    NetEndpoint e;
    // Card ra để trống = theo bảng định tuyến của hệ điều hành, hợp với đa số
    // trường hợp hơn là chốt cứng một địa chỉ.
    e.localIp   = QString();
    e.localPort = 0;
    // Xen kẽ Plot / Track, vì gần như lần nào cũng cần cả hai. Dòng Command
    // không tính vào phép xen kẽ này: nó đã có sẵn từ cấu hình mặc định.
    int dataRows = 0;
    for (const NetEndpoint &row : m_params.tx) {
        if (row.kind != TxKind::Command)
            ++dataRows;
    }
    const bool wantTrack = dataRows % 2 == 1;
    e.kind       = wantTrack ? TxKind::Track : TxKind::Plot;
    e.remotePort = wantTrack ? 6102 : 6101;

    m_params.tx.push_back(e);
    rebuildTxTable();
    emit txEndpointsChanged(m_params.tx);
}

void ConnectionTab::removeSelectedTxRow()
{
    const int r = m_tx->currentRow();
    if (r < 0 || r >= m_params.tx.size())
        return;
    m_params.tx.remove(r);
    rebuildTxTable();
    emit txEndpointsChanged(m_params.tx);
}

void ConnectionTab::onTxCellChanged()
{
    if (m_loading)
        return;
    // Đổi loại dữ liệu sang / khỏi Command thì ô "Gửi" phải theo ngay, trước
    // khi đọc bảng — nếu không thì dòng vừa thành Command vẫn còn ô bỏ trống.
    syncTxSendBoxes();
    if (readTxTable())
        emit txEndpointsChanged(m_params.tx);
}

bool ConnectionTab::readTxTable()
{
    const bool wasLoading = m_loading;
    m_loading = true;

    bool allOk = true;
    QVector<NetEndpoint> rows;
    rows.reserve(m_tx->rowCount());

    for (int r = 0; r < m_tx->rowCount(); ++r) {
        NetEndpoint e;

        const auto text = [this, r](int c) {
            const QTableWidgetItem *it = m_tx->item(r, c);
            return it ? it->text().trimmed() : QString();
        };
        const auto mark = [this, r](int c, bool ok) {
            if (QTableWidgetItem *it = m_tx->item(r, c))
                it->setBackground(ok ? QBrush() : QBrush(kBadCell));
        };
        const auto boxAt = [this, r](int c) -> QCheckBox * {
            QWidget *host = m_tx->cellWidget(r, c);
            return host ? host->findChild<QCheckBox *>() : nullptr;
        };

        if (const QCheckBox *b = boxAt(TxSend))
            e.enabled = b->isChecked();
        if (auto *c = qobject_cast<QComboBox *>(m_tx->cellWidget(r, TxKindCol)))
            e.kind = TxKind(c->currentData().toInt());
        if (e.kind == TxKind::Command)
            e.enabled = true;
        if (const QCheckBox *b = boxAt(TxBroadcast))
            e.broadcast = b->isChecked();

        e.localIp  = text(TxLocalIp);
        e.remoteIp = text(TxRemoteIp);

        // Địa chỉ và cổng đích chỉ bắt buộc khi dòng đã bật gửi — còn đang gõ
        // dở mà đã tô đỏ cả bảng thì rất khó chịu.
        const bool localIpOk = ipLooksValid(e.localIp);
        const bool remoteIpOk = ipLooksValid(e.remoteIp)
                             && (!e.enabled || !e.remoteIp.isEmpty());
        mark(TxLocalIp, localIpOk);
        mark(TxRemoteIp, remoteIpOk);

        const bool localPortOk  = portLooksValid(text(TxLocalPort),  e.localPort);
        bool       remotePortOk = portLooksValid(text(TxRemotePort), e.remotePort);
        if (e.enabled && e.remotePort == 0)
            remotePortOk = false;
        mark(TxLocalPort, localPortOk);
        mark(TxRemotePort, remotePortOk);

        allOk = allOk && localIpOk && remoteIpOk && localPortOk && remotePortOk;
        rows.push_back(e);
    }

    if (allOk) {
        m_params.tx = rows;
        setStatusText(QString());
    } else {
        setStatusText(tr("Bảng cổng gửi có ô chưa hợp lệ (ô tô đỏ) — dòng đã "
                         "bật Gửi phải có RemoteIP và RemotePort khác 0"), true);
    }

    m_loading = wasLoading;
    return allOk;
}

bool ConnectionTab::readTable()
{
    // Tô nền ô cũng phát itemChanged, mà itemChanged lại gọi về đây — không
    // chặn thì thành đệ quy vô hạn ngay ở ô sai đầu tiên.
    const bool wasLoading = m_loading;
    m_loading = true;

    bool allOk = true;
    QVector<NetEndpoint> rows;
    rows.reserve(m_rx->rowCount());

    // Tô đỏ ô sai ngay tại chỗ thay vì hiện hộp thoại: người dùng còn đang gõ
    // dở cả bảng, cắt ngang từng ô một thì rất khó chịu.
    for (int r = 0; r < m_rx->rowCount(); ++r) {
        NetEndpoint e;
        bool rowOk = true;

        const auto text = [this, r](int c) {
            const QTableWidgetItem *it = m_rx->item(r, c);
            return it ? it->text().trimmed() : QString();
        };
        const auto mark = [this, r](int c, bool ok) {
            if (QTableWidgetItem *it = m_rx->item(r, c))
                it->setBackground(ok ? QBrush() : QBrush(kBadCell));
        };

        if (auto *c = qobject_cast<QComboBox *>(m_rx->cellWidget(r, ColName)))
            e.name = c->currentText().trimmed();
        e.localIp  = text(ColLocalIp);
        e.remoteIp = text(ColRemoteIp);

        const bool localIpOk  = ipLooksValid(e.localIp);
        const bool remoteIpOk = ipLooksValid(e.remoteIp);
        mark(ColLocalIp, localIpOk);
        mark(ColRemoteIp, remoteIpOk);
        rowOk = localIpOk && remoteIpOk;

        const bool localPortOk  = portLooksValid(text(ColLocalPort),  e.localPort);
        const bool remotePortOk = portLooksValid(text(ColRemotePort), e.remotePort);
        mark(ColLocalPort, localPortOk);
        mark(ColRemotePort, remotePortOk);
        rowOk = rowOk && localPortOk && remotePortOk;

        allOk = allOk && rowOk;
        rows.push_back(e);
    }

    if (allOk) {
        m_params.rx = rows;
        setStatusText(QString());
    } else {
        setStatusText(tr("Có ô địa chỉ hoặc cổng chưa hợp lệ (ô tô đỏ) — "
                         "sửa lại trước khi kết nối"), true);
    }

    m_loading = wasLoading;
    return allOk;
}
