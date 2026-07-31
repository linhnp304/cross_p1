#include "ascope.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

namespace {

const QColor kTrace(120, 255, 120);
const QColor kGrid(35, 48, 61);
const QColor kAxisText(110, 125, 140);

/// Chừa lề cho nhãn cự ly ở đáy và nhãn biên độ bên trái.
constexpr int kLeft = 30, kBottom = 16, kTop = 6, kRight = 6;

} // namespace

AScope::AScope(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(90);
    setAutoFillBackground(false);
}

void AScope::setTrace(const rawpkt::RawVSweep &s)
{
    m_sweep    = s;
    m_hasTrace = true;
    rebuildPolyline();
    update();
}

void AScope::clearTrace()
{
    m_hasTrace = false;
    m_line.clear();
    update();
}

void AScope::setMaxRangeKm(double km)
{
    if (qFuzzyCompare(m_maxRangeKm, km))
        return;
    m_maxRangeKm = km;
    update();   // chỉ nhãn trục đổi, đường biên độ giữ nguyên
}

void AScope::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    rebuildPolyline();
}

void AScope::rebuildPolyline()
{
    m_line.clear();
    if (!m_hasTrace)
        return;

    const double w = width()  - kLeft - kRight;
    const double h = height() - kTop  - kBottom;
    if (w <= 1.0 || h <= 1.0)
        return;

    // Nhiều điểm hơn số cột màn hình thì vẽ ra cũng chỉ là một mảng đặc, nên
    // gộp mỗi cột lấy mức lớn nhất — vừa nhanh vừa không mất đỉnh nhọn.
    const int cols = qMin(rawpkt::kBinsV, int(w));
    m_line.reserve(cols);
    for (int c = 0; c < cols; ++c) {
        const int lo = int(qint64(c)     * rawpkt::kBinsV / cols);
        const int hi = int(qint64(c + 1) * rawpkt::kBinsV / cols);
        int level = 0;
        for (int i = lo; i < qMax(hi, lo + 1) && i < rawpkt::kBinsV; ++i)
            level = qMax(level, int(m_sweep.video[size_t(i)]));

        const double x = kLeft + (cols > 1 ? w * c / (cols - 1) : 0.0);
        m_line << QPointF(x, kTop + h * (1.0 - level / 255.0));
    }
}

void AScope::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(7, 11, 15));

    const QRectF plot(kLeft, kTop,
                      qMax(1, width() - kLeft - kRight),
                      qMax(1, height() - kTop - kBottom));

    // Lưới: ngang theo mức biên độ 0/64/128/192/255, dọc chia 8 phần cự ly.
    p.setPen(kGrid);
    for (int level : {0, 64, 128, 192, 255}) {
        const double y = plot.top() + plot.height() * (1.0 - level / 255.0);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int i = 0; i <= 8; ++i) {
        const double x = plot.left() + plot.width() * i / 8.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    p.setPen(kAxisText);
    for (int level : {0, 128, 255}) {
        const double y = plot.top() + plot.height() * (1.0 - level / 255.0);
        p.drawText(QRectF(0, y - 8, kLeft - 4, 16),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(level));
    }
    p.drawText(QRectF(plot.left(), plot.bottom() + 1, 60, kBottom - 2),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("0"));
    p.drawText(QRectF(plot.right() - 90, plot.bottom() + 1, 90, kBottom - 2),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("%1 km").arg(m_maxRangeKm, 0, 'g', 4));

    if (m_line.size() < 2) {
        p.setPen(QColor(74, 88, 102));
        p.drawText(plot, Qt::AlignCenter, tr("Chưa có dữ liệu RAW_V"));
        return;
    }

    // Vẽ sau lưới để đường biên độ luôn nằm trên.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kTrace, 1.0));
    p.drawPolyline(m_line);
}
