#include "ui/compacttabs.h"

#include <QEvent>
#include <QWidget>

namespace {

/// Đệm ngang mỗi bên một tab: rộng nhất là mức của bảng màu, hẹp nhất là mức
/// vẫn còn tách được chữ của hai tab cạnh nhau.
constexpr int kMaxPad = 16;
constexpr int kMinPad = 4;

/// Chừa lại chút ít cho viền khung tab và sai số làm tròn, để tab cuối không
/// thừa ra đúng một điểm ảnh rồi kéo theo cả hai nút mũi tên.
constexpr int kEdgeMargin = 4;

} // namespace

CompactTabBar::CompactTabBar(QWidget *parent)
    : QTabBar(parent)
    , m_pad(kMaxPad)
{
    // Khai mức đệm ngay từ đầu bằng bảng màu của chính widget này: refit() trừ
    // ngược mức ấy ra khỏi bề rộng tab để lấy phần không đổi (chữ, viền, lề),
    // nên nó phải biết chắc mức đang có là bao nhiêu chứ không thể đoán theo
    // bảng màu chung.
    applyPadding();
}

void CompactTabBar::applyPadding()
{
    setStyleSheet(QStringLiteral(
                      "QTabBar::tab { padding-left: %1px; padding-right: %1px; }")
                      .arg(m_pad));
}

int CompactTabBar::availableWidth() const
{
    const QWidget *p = parentWidget();
    return (p ? p->width() : width()) - kEdgeMargin;
}

void CompactTabBar::refit()
{
    const int n = count();
    if (n <= 0 || m_fitting)
        return;

    int total = 0;
    for (int i = 0; i < n; ++i)
        total += tabSizeHint(i).width();

    // Phần không đổi theo đệm: chữ, viền và lề của cả dãy tab.
    const int fixed = total - n * 2 * m_pad;
    const int room  = availableWidth() - fixed;

    const int pad = qBound(kMinPad, room / (2 * n), kMaxPad);
    if (pad == m_pad)
        return;

    m_pad = pad;

    // Đặt bảng màu kéo theo một lượt dựng hình mới, tức là có thể quay lại đúng
    // hàm này. Không sao về kết quả — phép tính chỉ phụ thuộc bề rộng của
    // widget cha, mà bề rộng ấy chưa đổi — nhưng chặn lại cho khỏi lồng nhau.
    m_fitting = true;
    applyPadding();
    m_fitting = false;
}

bool CompactTabBar::event(QEvent *e)
{
    // Bám theo bề rộng của widget cha — đó mới là con số refit() đọc. Bề rộng
    // của chính thanh tab không dùng được làm mốc: khi các tab đã nằm vừa, nó
    // đúng bằng tổng bề rộng các tab, nên panel có nới rộng thêm nữa thanh tab
    // cũng không hề thay đổi kích thước, và đệm sẽ kẹt mãi ở mức hẹp.
    if (e->type() == QEvent::ParentChange) {
        if (m_watched)
            m_watched->removeEventFilter(this);
        m_watched = parentWidget();
        if (m_watched)
            m_watched->installEventFilter(this);
    }
    return QTabBar::event(e);
}

bool CompactTabBar::eventFilter(QObject *watched, QEvent *e)
{
    if (watched == m_watched && e->type() == QEvent::Resize)
        refit();
    return QTabBar::eventFilter(watched, e);
}

void CompactTabBar::showEvent(QShowEvent *e)
{
    QTabBar::showEvent(e);

    // Lần đầu hiện ra: các tab có thể vừa được thêm vào sau lần đổi cỡ gần nhất
    // của widget cha, tức là refit() chưa từng thấy tab nào.
    refit();
}

CompactTabWidget::CompactTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBar(new CompactTabBar);
}
