#include "connectiontab.h"

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
    m_rx->setToolTip(tr("RemoteIP để trống hoặc 0.0.0.0, RemotePort để 0: "
                        "nhận từ mọi máy / mọi cổng"));
    rxLay->addWidget(m_rx);

    m_add    = new QPushButton(tr("Thêm dòng"), rxBox);
    m_remove = new QPushButton(tr("Xoá dòng"), rxBox);
    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_add);
    btnRow->addWidget(m_remove);
    btnRow->addStretch(1);
    rxLay->addLayout(btnRow);

    // --- Bảng cổng gửi (giai đoạn sau) ------------------------------------
    auto *txBox = new QGroupBox(tr("Cổng UDP gửi dữ liệu"), this);
    auto *txLay = new QVBoxLayout(txBox);
    auto *txNote = new QLabel(tr("Phần gửi dữ liệu — làm ở giai đoạn sau"), txBox);
    txNote->setStyleSheet(QStringLiteral("color: #4a5866;"));
    txLay->addWidget(txNote);

    // --- Nút kết nối ------------------------------------------------------
    m_toggle = new QPushButton(tr("Kết nối"), this);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #7fa8c9;"));

    root->addWidget(rxBox);
    root->addWidget(txBox);
    root->addWidget(m_toggle, 0, Qt::AlignLeft);
    root->addWidget(m_status);
    root->addStretch(1);

    connect(m_rx, &QTableWidget::itemChanged, this, &ConnectionTab::onCellChanged);
    connect(m_add, &QPushButton::clicked, this, &ConnectionTab::addRow);
    connect(m_remove, &QPushButton::clicked, this, &ConnectionTab::removeSelectedRow);
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (m_running)
            emit disconnectRequested();
        else
            emit connectRequested();
    });
}

void ConnectionTab::setParams(const AppParams &p)
{
    m_params = p;
    rebuildTable();
}

void ConnectionTab::setRunning(bool on)
{
    m_running = on;
    m_toggle->setText(on ? tr("Dừng kết nối") : tr("Kết nối"));

    // Sửa cổng lúc đang chạy chỉ gây hiểu nhầm là đã có hiệu lực.
    m_rx->setEnabled(!on);
    m_add->setEnabled(!on);
    m_remove->setEnabled(!on);
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
    m_rx->setRowCount(m_params.rx.size());
    for (int r = 0; r < m_params.rx.size(); ++r) {
        const NetEndpoint &e = m_params.rx[r];
        m_rx->setItem(r, ColName,       makeItem(e.name));
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
    e.name = tr("UDP-%1").arg(m_params.rx.size() + 1);
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

        e.name     = text(ColName);
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
