#pragma once

#include <QPointer>
#include <QTabBar>
#include <QTabWidget>

/// Thanh tab tự bớt đệm hai bên để cả bảy tab cùng nằm vừa panel.
///
/// Bảng màu cho tab đệm 16 điểm ảnh mỗi bên — rộng rãi và dễ bấm, nhưng bảy tab
/// của panel 2 cộng lại vượt quá bề rộng panel, nên tab cuối ("Cài đặt") bị đẩy
/// ra ngoài ngay khi mở phần mềm. Thay vì cắt cứng phần đệm cho mọi trường hợp,
/// lớp này chỉ bớt **đúng bằng lượng còn thiếu**: panel rộng thì tab vẫn rộng
/// rãi như cũ, panel hẹp thì đệm co dần lại.
///
/// Phần chữ không bao giờ bị cắt: đệm chỉ co tới kMinPad. Hẹp hơn nữa thì
/// QTabBar tự hiện lại hai nút mũi tên như trước — đúng hành vi mong muốn khi
/// người dùng kéo hẹp panel.
///
/// Đệm đổi bằng chính bảng màu của widget chứ không bằng tabSizeHint(): với
/// bảng màu, bề rộng ô chữ tính từ đệm đã khai, nên thu hẹp ô tab mà không thu
/// hẹp đệm chỉ làm chữ bị cắt cụt hai đầu.
class CompactTabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit CompactTabBar(QWidget *parent = nullptr);

protected:
    bool event(QEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    /// Tính lại mức đệm vừa với bề rộng đang có rồi áp vào nếu khác mức cũ.
    void refit();

    void applyPadding();

    /// Bề rộng dùng để quyết định co bao nhiêu.
    ///
    /// Lấy theo **widget cha** (QTabWidget) chứ không theo width() của chính
    /// thanh tab: bề rộng thanh tab lại do sizeHint() của nó quyết định, mà
    /// sizeHint() cộng từ bề rộng các tab — tự soi mình thì mỗi lần co lại là
    /// "đã vừa", lần sau nở ra, thành dao động không dứt. Bề rộng của cha không
    /// phụ thuộc gì vào đệm nên phép tính này chỉ chạy một chiều.
    int availableWidth() const;

    /// Widget cha đang được theo dõi bề rộng. Giữ con trỏ riêng chứ không hỏi
    /// lại parentWidget() lúc gỡ: khi cha đổi rồi thì không còn tìm ra cha cũ
    /// để gỡ bộ lọc sự kiện nữa.
    QPointer<QWidget> m_watched;

    int  m_pad     = 0;
    bool m_fitting = false;
};

/// QTabWidget dùng CompactTabBar. Phải là một lớp riêng vì setTabBar() của
/// QTabWidget là hàm protected — chỉ lớp kế thừa mới gọi được.
class CompactTabWidget : public QTabWidget
{
    Q_OBJECT

public:
    explicit CompactTabWidget(QWidget *parent = nullptr);
};
