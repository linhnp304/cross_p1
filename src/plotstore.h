#pragma once

// Các điểm dấu tâm chùm đang còn hiển thị trên màn hình.
//
// Điểm dấu chỉ sống vài giây rồi biến mất, nên đây là một hàng đợi theo thời
// gian chứ không phải một danh sách quản lý: thêm vào đuôi, cắt ở đầu. Điểm dấu
// sinh ra theo thứ tự thời gian nên đầu hàng đợi luôn là cái cũ nhất — chỉ phải
// so hạn cho tới khi gặp cái đầu tiên còn hạn.

#include "plottrack.h"

#include <QVector>

class PlotStore
{
public:
    void add(const PlotTC &p) { m_plots.push_back(p); }

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

    const QVector<PlotTC> &plots() const { return m_plots; }

private:
    QVector<PlotTC> m_plots;
};
