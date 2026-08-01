#pragma once

#include "appparams.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

/// Tab "Tham số" trong panel 2.1 — tham số kỹ thuật của đài.
///
/// Khác tab "Cài đặt" ở chỗ giá trị **không có hiệu lực ngay**: phải bấm "Áp
/// dụng". Trừ khi bật "Tự động nhận từ trạng thái lệnh điều khiển", lúc đó nút
/// bấm bị khoá và mọi thay đổi vào thẳng.
class ParamsTab : public QWidget
{
    Q_OBJECT

public:
    explicit ParamsTab(QWidget *parent = nullptr);

    /// Đổ giá trị vào các ô nhập mà không phát tín hiệu.
    void setParams(const AppParams &p);

signals:
    /// Phát khi có bộ tham số mới cần áp dụng (và lưu xuống params.json).
    /// Danh sách cổng trong `p` giữ nguyên như lúc setParams.
    void paramsApplied(const AppParams &p);

    /// Người dùng bấm nút mở một trong hai cửa sổ tham số thuật toán.
    void beamParamsRequested();
    void trackParamsRequested();

private:
    /// Gom giá trị trên giao diện vào m_params rồi phát tín hiệu.
    void apply();

    /// Cập nhật dòng cự ly tối đa tính được và trạng thái bật/tắt của các ô.
    void refreshDerived();

    /// Người dùng vừa sửa một ô nhập.
    void onEdited();

    /// Cự ly tối đa suy ra từ giá trị đang hiện trên giao diện.
    double previewRmaxKm() const;

    AppParams m_params;
    bool      m_loading = false;

    QDoubleSpinBox *m_fs     = nullptr;
    QSpinBox       *m_b      = nullptr;
    QSpinBox       *m_tc     = nullptr;
    QDoubleSpinBox *m_zfbeat = nullptr;   ///< QSpinBox không đủ dải unsigned int

    QCheckBox   *m_autoStatus = nullptr;
    QCheckBox   *m_autoRange  = nullptr;
    QPushButton *m_apply      = nullptr;
    QLabel      *m_rmax       = nullptr;

    QCheckBox      *m_sectorOn    = nullptr;
    QDoubleSpinBox *m_sectorStart = nullptr;
    QDoubleSpinBox *m_sectorStop  = nullptr;
};
