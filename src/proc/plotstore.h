#pragma once

// Các điểm dấu đang còn hiển thị trên màn hình: điểm dấu tâm chùm (PlotTC) và
// điểm dấu đơn xung (dữ liệu thô trong RAW_P, trước khi gom chùm).
//
// Điểm dấu chỉ sống vài giây rồi biến mất, nên đây là một hàng đợi theo thời
// gian chứ không phải một danh sách quản lý: thêm vào đuôi, cắt ở đầu. Điểm dấu
// sinh ra theo thứ tự thời gian nên đầu hàng đợi luôn là cái cũ nhất — chỉ phải
// so hạn cho tới khi gặp cái đầu tiên còn hạn.
//
// Hai loại điểm dấu dùng chung một khuôn: cùng quy tắc hết hạn, mà chép quy tắc
// đó ra hai chỗ thì sửa một chỗ quên chỗ kia là hai lớp lệch hạn nhau.

#include "proc/plottrack.h"

#include <QVector>

/// Một điểm dấu đơn xung đã sẵn sàng để vẽ.
///
/// Chỉ giữ đúng những gì lớp hiển thị cần, không giữ cả RawPlot: một mục tiêu
/// để lại vài chục chấm mỗi vòng quét, mà cả trăm mục tiêu thì hàng đợi này lên
/// tới hàng vạn phần tử.
struct RawPlotDot {
    double lat = 0.0;
    double lng = 0.0;
    qint64 bornMs = 0;   ///< đồng hồ đơn điệu lúc nhận, xem PlotTC::bornMs
};

/// Hàng đợi điểm dấu theo thời gian. `T` phải có trường `bornMs`.
template <class T>
class TimedPlotStore
{
public:
    void add(const T &p) { m_plots.push_back(p); }

    /// Xoá các điểm dấu quá `showSec` giây. `nowMs` là đồng hồ đơn điệu.
    void expire(qint64 nowMs, int showSec)
    {
        const qint64 limit = qint64(showSec) * 1000;
        int drop = 0;
        while (drop < m_plots.size() && nowMs - m_plots.at(drop).bornMs > limit)
            ++drop;
        if (drop > 0)
            m_plots.remove(0, drop);
    }

    void clear() { m_plots.clear(); }

    const QVector<T> &plots() const { return m_plots; }

private:
    QVector<T> m_plots;
};

using PlotStore    = TimedPlotStore<PlotTC>;
using RawPlotStore = TimedPlotStore<RawPlotDot>;
