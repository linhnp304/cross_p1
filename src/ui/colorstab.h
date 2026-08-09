#pragma once

#include "app/appcolors.h"

#include <QVector>
#include <QWidget>

class QPushButton;

/// Tab "Màu sắc" trong panel 2.1 — đổi màu các đối tượng đồ hoạ trên panel 1.
///
/// Danh sách các mục lấy thẳng từ AppColors::entries(), nên thêm một đối tượng
/// đồ hoạ mới chỉ phải khai báo màu trong AppColors, không phải sửa ở đây.
class ColorsTab : public QWidget
{
    Q_OBJECT

public:
    explicit ColorsTab(QWidget *parent = nullptr);

    /// Đổ giá trị vào các nút mà không phát tín hiệu.
    void setColors(const AppColors &c);

signals:
    void colorsChanged(const AppColors &c);

private:
    /// Mở hộp chọn màu cho mục thứ `index`.
    void pick(int index);

    /// Vẽ lại ô màu trên nút, kèm ô ca-rô để thấy được độ trong suốt.
    void refreshSwatch(int index);

    AppColors m_colors;
    QVector<QPushButton *> m_buttons;
};
