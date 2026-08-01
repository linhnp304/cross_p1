#include "trackinfopopup.h"

#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

#include <cmath>

TrackInfoPopup::TrackInfoPopup(QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameShape(QFrame::Box);
    setStyleSheet(QStringLiteral(
        "QFrame { background: #0f151c; border: 1px solid #4e9ad4; }"
        "QLabel { border: none; color: #d7e3ef; }"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 12, 8);
    m_text = new QLabel(this);
    m_text->setTextFormat(Qt::RichText);
    lay->addWidget(m_text);
}

void TrackInfoPopup::showTrack(const Track &t, const AppSettings &s,
                               const QPoint &globalPos)
{
    const QString unknown = tr("Chưa xác định");

    const QString classify = t.classify == 0 ? unknown
                                             : s.classifyName(t.classify);
    const QString altitude = t.altitude() == 0
                                 ? unknown
                                 : tr("%1 m").arg(t.altitude());

    // Dòng kinh/vĩ độ thụt vào một chút cho thấy nó là cách viết khác của dòng
    // vị trí ngay trên, chứ không phải một thông tin riêng.
    m_text->setText(QStringLiteral(
        "<b>%1</b> %2<br>"
        "<b>%3</b> %4° - %5 m<br>"
        "&nbsp;&nbsp;&nbsp;&nbsp;<span style='color:#8fa6bb'>%6, %7</span><br>"
        "<b>%8</b> %9 m/s (%10 km/h)<br>"
        "<b>%11</b> %12°<br>"
        "<b>%13</b> %14<br>"
        "<b>%15</b> %16<br>"
        "<b>%17</b> %18")
        .arg(tr("Tốp:")).arg(t.top)
        .arg(tr("Vị trí:"))
        .arg(t.azmDeg(), 0, 'f', 2).arg(t.rangeM(), 0, 'f', 1)
        .arg(double(t.lat), 0, 'f', 6).arg(double(t.lng), 0, 'f', 6)
        .arg(tr("Vận tốc:"))
        .arg(t.speedMs(), 0, 'f', 1).arg(t.speedMs() * 3.6, 0, 'f', 1)
        .arg(tr("Hướng:")).arg(t.headingDeg(), 0, 'f', 2)
        .arg(tr("Độ cao:"), altitude)
        .arg(tr("Phân loại:"), classify)
        .arg(tr("NL:")).arg(t.amplitude));

    adjustSize();

    // Popup phải nằm gọn trong màn hình: bật lên ở mép phải mà tràn ra ngoài
    // thì mất luôn nửa nội dung.
    QPoint at = globalPos + QPoint(14, 10);
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        const QRect avail = screen->availableGeometry();
        at.setX(qBound(avail.left(), at.x(), avail.right() - width()));
        at.setY(qBound(avail.top(), at.y(), avail.bottom() - height()));
    }
    move(at);
    show();
}
