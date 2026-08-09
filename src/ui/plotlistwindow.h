#pragma once

#include "proc/plottrack.h"

#include <QDialog>

class QCheckBox;
class QTableWidget;

/// Cửa sổ "Danh sách điểm dấu" — bảng cuộn theo dòng dữ liệu mới nhất.
///
/// Ngang và thấp, đặt ở mép trên màn hình: đây là cửa sổ để liếc mắt xem thuật
/// toán tâm chùm có ra điểm dấu hay không, không phải cửa sổ để ngồi đọc lâu,
/// nên không được che mất panel 1.
class PlotListWindow : public QDialog
{
    Q_OBJECT

public:
    explicit PlotListWindow(QWidget *parent = nullptr);

    /// Thêm một loạt điểm dấu mới vào cuối bảng.
    void addPlots(const QVector<PlotTC> &plots);

    void clearList();

private:
    /// Trần số dòng khi bật "Tự động xoá". Giữ nhiều hơn cũng không để làm gì:
    /// chức năng ghi lưu là việc của giai đoạn sau, không phải của bảng này.
    static constexpr int kAutoClearRows = 1000;

    QTableWidget *m_table    = nullptr;
    QCheckBox    *m_autoClear = nullptr;

    /// Số thứ tự dòng, tự tăng từ 1 và không reset khi bảng bị cắt bớt.
    int m_counter = 0;
};
