#pragma once

#include "app/appparams.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

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

    /// Đặt riêng ô Fs, dùng cho giá trị nhận về từ trạng thái lệnh điều khiển.
    /// Không phát tín hiệu và không dựng lại hai bảng bên dưới — trắc thủ có
    /// thể đang gõ dở một dòng rẻ quạt lúc gói trạng thái tới.
    void setFs(double fs);

    /// Thêm một vùng cấm khởi tạo do người dùng vừa vẽ trên bản đồ.
    void addZoneFromMap(const NoInitZone &z);

    /// Đổi chữ trên nút "Vẽ trên bản đồ" khi chế độ vẽ bật/tắt.
    void setDrawingZone(bool on);

signals:
    /// Phát khi có bộ tham số mới cần áp dụng (và lưu xuống params.json).
    /// Danh sách cổng trong `p` giữ nguyên như lúc setParams.
    void paramsApplied(const AppParams &p);

    /// Người dùng bấm nút mở một trong hai cửa sổ tham số thuật toán.
    void beamParamsRequested();
    void trackParamsRequested();

    /// Bấm "Vẽ trên bản đồ" — panel 1 chuyển sang chế độ vẽ vùng cấm.
    void drawZoneRequested();

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
    QDoubleSpinBox *m_multiV = nullptr;

    QCheckBox   *m_autoStatus = nullptr;
    QCheckBox   *m_autoRange  = nullptr;
    QPushButton *m_apply      = nullptr;
    QLabel      *m_rmax       = nullptr;

    // --- bảng rẻ quạt xử lý ---
    QTableWidget *m_sectors      = nullptr;
    QPushButton  *m_sectorAdd    = nullptr;
    QPushButton  *m_sectorRemove = nullptr;
    QLabel       *m_sectorWarn   = nullptr;

    void rebuildSectorTable();

    /// Đọc bảng rẻ quạt vào m_params. False khi có hai rẻ quạt chồng lấn —
    /// khi đó các dòng phạm lỗi bị tô đỏ và m_params giữ nguyên giá trị cũ.
    bool readSectorTable();

    // --- bảng vùng cấm khởi tạo ---
    QCheckBox    *m_showZones  = nullptr;
    QTableWidget *m_zones      = nullptr;
    QPushButton  *m_zoneAdd    = nullptr;
    QPushButton  *m_zoneRemove = nullptr;
    QPushButton  *m_zoneDraw   = nullptr;

    void rebuildZoneTable();
    void readZoneTable();
};
