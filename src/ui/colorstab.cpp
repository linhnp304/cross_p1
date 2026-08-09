#include "ui/colorstab.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/// Ô màu vẽ lên nút. Nền ca-rô để nhìn ra ngay màu nào đang trong suốt — nền ô
/// text theo dõi bắt buộc phải trong suốt, mà màu đặc và màu trong suốt đặt
/// cạnh nhau trên nền tối thì gần như không phân biệt được.
QPixmap swatch(const QColor &c, int w, int h, qreal dpr)
{
    QPixmap pm(int(w * dpr), int(h * dpr));
    pm.setDevicePixelRatio(dpr);

    QPainter p(&pm);
    constexpr int kCell = 5;
    for (int y = 0; y < h; y += kCell) {
        for (int x = 0; x < w; x += kCell) {
            const bool odd = ((x / kCell) + (y / kCell)) % 2;
            p.fillRect(QRect(x, y, kCell, kCell),
                       odd ? QColor(70, 78, 88) : QColor(48, 55, 63));
        }
    }
    p.fillRect(QRect(0, 0, w, h), c);
    p.setPen(QColor(120, 135, 150));
    p.drawRect(QRect(0, 0, w - 1, h - 1));
    return pm;
}

constexpr int kSwatchW = 44;
constexpr int kSwatchH = 14;

} // namespace

ColorsTab::ColorsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    auto *box  = new QGroupBox(tr("Màu các đối tượng đồ hoạ"), this);
    auto *form = new QFormLayout(box);
    form->setLabelAlignment(Qt::AlignLeft);

    const auto entries = m_colors.entries();
    m_buttons.reserve(int(entries.size()));

    for (int i = 0; i < int(entries.size()); ++i) {
        auto *b = new QPushButton(box);
        b->setFlat(false);
        b->setFixedWidth(kSwatchW + 22);
        b->setToolTip(tr("Bấm để chọn màu"));
        connect(b, &QPushButton::clicked, this, [this, i] { pick(i); });
        m_buttons.push_back(b);
        form->addRow(entries[size_t(i)].label, b);
    }

    auto *reset = new QPushButton(tr("Khôi phục màu mặc định"), this);
    connect(reset, &QPushButton::clicked, this, [this] {
        setColors(AppColors{});
        emit colorsChanged(m_colors);
    });

    root->addWidget(box);
    root->addWidget(reset, 0, Qt::AlignLeft);
    root->addStretch(1);

    setColors(m_colors);
}

void ColorsTab::refreshSwatch(int index)
{
    const auto entries = m_colors.entries();
    QPushButton *b = m_buttons.at(index);
    b->setIcon(QIcon(swatch(*entries[size_t(index)].value, kSwatchW, kSwatchH,
                            devicePixelRatioF())));
    b->setIconSize(QSize(kSwatchW, kSwatchH));
}

void ColorsTab::setColors(const AppColors &c)
{
    m_colors = c;
    for (int i = 0; i < m_buttons.size(); ++i)
        refreshSwatch(i);
}

void ColorsTab::pick(int index)
{
    const auto entries = m_colors.entries();
    QColor *slot = entries[size_t(index)].value;

    // ShowAlphaChannel: nền ô text theo dõi phải chỉnh được độ trong suốt, mà
    // đã mở được cho một màu thì mở cho tất cả cho nhất quán.
    const QColor c = QColorDialog::getColor(
        *slot, this, tr("Chọn màu — %1").arg(entries[size_t(index)].label),
        QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return;   // người dùng bấm Huỷ

    *slot = c;
    refreshSwatch(index);
    emit colorsChanged(m_colors);
}
