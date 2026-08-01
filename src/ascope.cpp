#include "ascope.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

namespace {

const QColor kTrace(120, 255, 120);
const QColor kGrid(35, 48, 61);
const QColor kAxisText(110, 125, 140);
const QColor kReadout(159, 180, 200);
const QColor kMarker(90, 120, 145);

/// Chừa lề cho nhãn cự ly ở đáy. Hai bên chỉ cần lề mỏng — thang biên độ
/// không ghi số nên không phải chừa chỗ bên trái.
constexpr int kLeft = 6, kBottom = 16, kTop = 6, kRight = 6;

} // namespace

AScope::AScope(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(90);
    setAutoFillBackground(false);
    setMouseTracking(true);   // đọc số theo con trỏ, không phải bấm mới hiện
}

QRectF AScope::plotRect() const
{
    return QRectF(kLeft, kTop,
                  qMax(1, width()  - kLeft - kRight),
                  qMax(1, height() - kTop  - kBottom));
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

void AScope::mouseMoveEvent(QMouseEvent *e)
{
    const int bin = binAt(e->position().x());
    if (bin != m_hoverBin) {
        m_hoverBin = bin;
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void AScope::leaveEvent(QEvent *e)
{
    if (m_hoverBin >= 0) {
        m_hoverBin = -1;
        update();
    }
    QWidget::leaveEvent(e);
}

int AScope::binAt(double x) const
{
    if (!m_hasTrace)
        return -1;

    const QRectF plot = plotRect();
    if (x < plot.left() || x > plot.right())
        return -1;

    // Gộp đúng như lúc vẽ rồi lấy ô cao nhất trong nhóm: khi widget hẹp hơn
    // 1024 điểm, mỗi cột màn hình là đỉnh của vài ô cự ly. Trỏ vào một đỉnh mà
    // đọc ra số của ô lân cận thấp hơn thì con số vô nghĩa.
    const int cols = qMin(rawpkt::kBinsV, int(plot.width()));
    if (cols < 1)
        return -1;

    const double t = (x - plot.left()) / plot.width();
    const int c    = qBound(0, int(t * cols), cols - 1);

    const int lo = int(qint64(c)     * rawpkt::kBinsV / cols);
    const int hi = qMin(int(qint64(c + 1) * rawpkt::kBinsV / cols),
                        rawpkt::kBinsV);

    int best = lo;
    for (int i = lo; i < qMax(hi, lo + 1) && i < rawpkt::kBinsV; ++i) {
        if (m_sweep.video[size_t(i)] > m_sweep.video[size_t(best)])
            best = i;
    }
    return best;
}

double AScope::rangeKmAt(int bin) const
{
    // Cùng công thức với cự ly tối đa, chỉ thay số ô: Rmax = 75·Fs·Tc/B ứng với
    // fb = 1024·Fs/2048, nên ô thứ n có fb = n·Fs/2048 và cự ly rút gọn thành
    // Rmax·n/1024. Ô đánh số từ 1 nên ô cuối cùng đúng bằng cự ly tối đa.
    return m_maxRangeKm * (bin + 1) / double(rawpkt::kBinsV);
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

    const QRectF plot = plotRect();

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

    // Số đọc theo con trỏ, đặt chính giữa hàng nhãn. Phải dựng trước khi vẽ hai
    // nhãn đầu trục, vì lúc panel bị kéo hẹp thì nó với hai nhãn kia tranh chỗ.
    QString readout;
    if (m_hoverBin >= 0) {
        readout = tr("Biên độ %1   Cự ly %2 km")
                      .arg(m_sweep.video[size_t(m_hoverBin)])
                      .arg(rangeKmAt(m_hoverBin), 0, 'f', 3);
    }

    const QFontMetrics fm = fontMetrics();
    const QString  rmaxText = tr("%1 km").arg(m_maxRangeKm, 0, 'g', 4);
    const QRectF   labelRow(plot.left(), plot.bottom() + 1, plot.width(), kBottom - 2);

    // Thang biên độ 0..255 là cố định nên không ghi số, để dành chỗ cho đường
    // biên độ; vạch lưới ngang vẫn đủ để ước lượng mức. Chỉ nhãn cự ly mới cần
    // ghi, vì nó đổi theo cự ly tối đa — và chỉ khi số đọc còn chừa đủ chỗ:
    // hai nhãn đầu trục là mốc tham chiếu, còn số đọc mới là cái đang được nhìn.
    const double freeSide = (plot.width() - fm.horizontalAdvance(readout)) / 2.0;
    const int    endWidth = qMax(fm.horizontalAdvance(QStringLiteral("0")),
                                 fm.horizontalAdvance(rmaxText));
    if (readout.isEmpty() || freeSide > endWidth + 10) {
        p.setPen(kAxisText);
        p.drawText(labelRow, Qt::AlignLeft  | Qt::AlignVCenter, QStringLiteral("0"));
        p.drawText(labelRow, Qt::AlignRight | Qt::AlignVCenter, rmaxText);
    }
    if (!readout.isEmpty()) {
        p.setPen(kReadout);
        p.drawText(labelRow, Qt::AlignHCenter | Qt::AlignVCenter, readout);
    }

    if (m_line.size() < 2) {
        p.setPen(QColor(74, 88, 102));
        p.drawText(plot, Qt::AlignCenter, tr("Chưa có dữ liệu RAW_V"));
        return;
    }

    // Vạch chỉ đúng ô đang đọc — chỗ chuột và đỉnh gần nhất có thể lệch vài
    // pixel do gộp ô. Vẽ trước đường biên độ để không che mất đỉnh.
    if (m_hoverBin >= 0) {
        const double x = plot.left()
                       + plot.width() * m_hoverBin / double(rawpkt::kBinsV - 1);
        p.setPen(kMarker);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    // Vẽ sau lưới để đường biên độ luôn nằm trên.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kTrace, 1.0));
    p.drawPolyline(m_line);
}
