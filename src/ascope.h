#pragma once

#include "rawpacket.h"

#include <QPolygonF>
#include <QWidget>

/// Panel 2.2 — cửa sổ biên độ.
///
/// Trục ngang là 1024 ô cự ly, trục dọc là 256 mức biên độ. **Không phụ thuộc
/// cự ly tối đa**: đổi thang cự ly lúc đang chạy thì hình ở đây giữ nguyên, chỉ
/// nhãn cự ly ở hai đầu trục đổi theo.
class AScope : public QWidget
{
    Q_OBJECT

public:
    explicit AScope(QWidget *parent = nullptr);

    /// Nhận lượt quét mới nhất. Chỉ dựng lại đường biên độ, việc vẽ để
    /// paintEvent lo — hàm này được gọi theo nhịp vẽ nên phải rẻ.
    void setTrace(const rawpkt::RawVSweep &s);

    /// Xoá đường biên độ (khi dừng kết nối).
    void clearTrace();

    /// Cự ly tối đa hiện hành, chỉ dùng cho nhãn ở trục ngang.
    void setMaxRangeKm(double km);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    /// Vùng vẽ đường biên độ, đã trừ lề. Dùng chung cho cả việc dựng đường,
    /// vẽ lưới và quy vị trí chuột về ô cự ly — ba chỗ này phải khớp nhau.
    QRectF plotRect() const;

    /// Quy 1024 điểm biên độ về toạ độ pixel của widget.
    void rebuildPolyline();

    /// Ô cự ly (0..1023) ứng với hoành độ `x` trên màn hình, -1 nếu ở ngoài
    /// vùng vẽ hoặc chưa có dữ liệu.
    int binAt(double x) const;

    /// Cự ly (km) tại tâm ô thứ `bin`.
    double rangeKmAt(int bin) const;

    rawpkt::RawVSweep m_sweep;
    bool      m_hasTrace  = false;
    double    m_maxRangeKm = 20.0;
    QPolygonF m_line;

    /// Ô cự ly con trỏ đang chỉ vào, -1 khi chuột ra ngoài widget.
    int m_hoverBin = -1;
};
