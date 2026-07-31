#include "lanstatus.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QColor kUp(88, 200, 96);
const QColor kDown(212, 92, 76);
const QColor kIdle(120, 140, 158);

/// Nhịp kiểm tra lại, chỉ chạy khi cửa sổ đang mở.
constexpr int kRefreshMs = 5000;

/// Cắt tiến trình ping treo lâu hơn mức này (một số hệ thống bỏ qua tham số
/// thời gian chờ của ping khi địa chỉ nằm ngoài dải định tuyến).
constexpr int kPingKillMs = 3000;

QStringList pingArgs(const QString &ip)
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("-n"), QStringLiteral("1"),
            QStringLiteral("-w"), QStringLiteral("1000"), ip};
#elif defined(Q_OS_MACOS)
    // macOS: -W tính bằng mili giây và không phải bản nào cũng có, -t (deadline
    // theo giây) thì bản nào cũng hiểu.
    return {QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-t"), QStringLiteral("1"), ip};
#else
    return {QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-W"), QStringLiteral("1"), ip};
#endif
}

QPixmap dotPixmap(const QColor &c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(1, 1, 10, 10));
    return pm;
}

} // namespace

// ------------------------------------------------------------- LanPopup ----

LanPopup::LanPopup(QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame { background: #131b24; border: 1px solid #2f4358; }"));

    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(10, 8, 14, 8);
    m_lay->setSpacing(5);

    m_empty = new QLabel(tr("Chưa có địa chỉ nào trong bảng kết nối"), this);
    m_empty->setStyleSheet(QStringLiteral("color: #4a5866; border: none;"));
    m_lay->addWidget(m_empty);

    m_refresh = new QTimer(this);
    m_refresh->setInterval(kRefreshMs);
    connect(m_refresh, &QTimer::timeout, this, &LanPopup::pingAll);

    m_hiddenAt.start();
}

LanPopup::~LanPopup()
{
    stopPings();
}

void LanPopup::setHosts(const QStringList &ips)
{
    if (m_hosts == ips)
        return;
    m_hosts = ips;
    rebuildRows();
    if (isVisible())
        pingAll();
}

void LanPopup::rebuildRows()
{
    stopPings();
    for (const Row &r : m_rows)
        delete r.widget;   // xoá hàng là xoá luôn nhãn và đèn bên trong
    m_rows.clear();
    m_upCount = 0;

    for (const QString &ip : m_hosts) {
        Row r;
        r.ip = ip;
        r.widget = new QWidget(this);
        r.widget->setStyleSheet(QStringLiteral("border: none;"));

        auto *lay = new QHBoxLayout(r.widget);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(10);

        auto *text = new QLabel(ip, r.widget);
        text->setStyleSheet(QStringLiteral("color: #c3ccd6; border: none;"));
        text->setMinimumWidth(110);

        r.dot = new QLabel(r.widget);
        r.dot->setPixmap(dotPixmap(kIdle));
        r.dot->setStyleSheet(QStringLiteral("border: none;"));

        lay->addWidget(text, 1);
        lay->addWidget(r.dot, 0);

        m_lay->addWidget(r.widget);
        m_rows.push_back(r);
    }

    m_empty->setVisible(m_rows.isEmpty());
    adjustSize();
    emit statusChanged();
}

void LanPopup::showEvent(QShowEvent *e)
{
    QFrame::showEvent(e);
    pingAll();
    m_refresh->start();
}

void LanPopup::hideEvent(QHideEvent *e)
{
    m_refresh->stop();
    stopPings();
    m_hiddenAt.restart();
    QFrame::hideEvent(e);
}

void LanPopup::stopPings()
{
    for (Row &r : m_rows) {
        if (!r.proc)
            continue;
        r.proc->disconnect(this);
        r.proc->kill();
        r.proc->deleteLater();
        r.proc = nullptr;
    }
}

void LanPopup::pingAll()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (r.proc)
            continue;   // lượt trước còn chạy, để yên cho nó xong

        r.proc = new QProcess(this);
        connect(r.proc, &QProcess::finished, this,
                [this, i](int code, QProcess::ExitStatus status) {
                    setRowState(i, status == QProcess::NormalExit && code == 0);
                });
        connect(r.proc, &QProcess::errorOccurred, this,
                [this, i] { setRowState(i, false); });

        // Chốt chặn cuối: ping treo thì tự cắt để hàng không đứng mãi ở màu xám.
        QTimer::singleShot(kPingKillMs, r.proc, [p = r.proc] {
            if (p->state() != QProcess::NotRunning)
                p->kill();
        });

        r.proc->start(QStringLiteral("ping"), pingArgs(r.ip));
    }
}

void LanPopup::setRowState(int index, bool up)
{
    if (index < 0 || index >= m_rows.size())
        return;

    Row &r = m_rows[index];
    if (r.proc) {
        r.proc->deleteLater();
        r.proc = nullptr;
    }
    r.up = up;
    r.dot->setPixmap(dotPixmap(up ? kUp : kDown));

    int up_ = 0;
    for (const Row &x : m_rows)
        up_ += x.up ? 1 : 0;
    m_upCount = up_;
    emit statusChanged();
}

// --------------------------------------------------------- LanIndicator ----

LanIndicator::LanIndicator(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Trạng thái kết nối mạng"));

    m_popup = new LanPopup(this);
    connect(m_popup, &LanPopup::statusChanged, this,
            QOverload<>::of(&QWidget::update));
}

void LanIndicator::setHosts(const QStringList &ips)
{
    m_popup->setHosts(ips);
    update();
}

QSize LanIndicator::sizeHint() const
{
    return QSize(26, 22);
}

void LanIndicator::paintEvent(QPaintEvent *)
{
    // Xám khi chưa có địa chỉ nào, xanh khi thông hết, vàng khi thông một
    // phần, đỏ khi không địa chỉ nào thông.
    const int hosts = m_popup->hostCount();
    const int up = m_popup->upCount();
    QColor c = kIdle;
    if (hosts > 0)
        c = (up == hosts) ? kUp : (up == 0 ? kDown : QColor(214, 178, 74));

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c, 1.3));
    p.setBrush(Qt::NoBrush);

    // Biểu tượng LAN tự vẽ: một máy chủ dưới, hai máy trạm trên, nối bằng bus.
    const double cx = width() / 2.0;
    const QRectF server(cx - 5.0, height() - 8.0, 10.0, 6.0);
    const QRectF nodeL(cx - 9.0, 3.0, 7.0, 5.0);
    const QRectF nodeR(cx + 2.0, 3.0, 7.0, 5.0);

    p.drawRect(server);
    p.drawRect(nodeL);
    p.drawRect(nodeR);

    const double busY = (server.top() + nodeL.bottom()) / 2.0;
    p.drawLine(QPointF(nodeL.center().x(), busY), QPointF(nodeR.center().x(), busY));
    p.drawLine(QPointF(nodeL.center().x(), nodeL.bottom()),
               QPointF(nodeL.center().x(), busY));
    p.drawLine(QPointF(nodeR.center().x(), nodeR.bottom()),
               QPointF(nodeR.center().x(), busY));
    p.drawLine(QPointF(cx, busY), QPointF(cx, server.top()));
}

void LanIndicator::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }

    // Qt::Popup tự đóng khi bấm ra ngoài, kể cả bấm vào chính biểu tượng này —
    // và cú bấm đó vẫn tới đây. Không có mốc thời gian thì cửa sổ vừa đóng đã
    // mở lại ngay, thành ra bấm lần nữa không đóng được.
    if (m_popup->hiddenAt().isValid() && m_popup->hiddenAt().elapsed() < 200) {
        e->accept();
        return;
    }

    if (m_popup->isVisible()) {
        m_popup->hide();
    } else {
        m_popup->adjustSize();
        // Góc dưới bên trái: nằm ngay trên thanh trạng thái, sát mép trái.
        const QPoint g = mapToGlobal(QPoint(0, 0));
        m_popup->move(g.x() - 8, g.y() - m_popup->height() - 6);
        m_popup->show();
    }
    e->accept();
}
