#pragma once

// Tính điểm dấu tâm chùm (PlotTC) từ dòng gói RAW_P.
//
// Chuyển thể từ PlotTC.c. Ý tưởng giữ nguyên: mỗi mục tiêu khi chùm tia quét
// qua sẽ để lại một loạt plot đơn xung ở gần cùng một ô cự ly và cùng một mức
// dopler, trong nhiều chu kỳ liên tiếp. Gom loạt đó lại thành một "chùm xung"
// rồi lấy tâm của nó làm điểm dấu.

#include "proc/plottrack.h"
#include "proc/procparams.h"
#include "net/rawpacket.h"

#include <QVector>

#include <array>

/// Bộ gom chùm xung.
///
/// Chạy trên luồng giao diện theo nhịp vẽ: mỗi nhịp lấy ra vài chục chu kỳ đã
/// nhận được rồi xử lý một lượt. Đặt ở đây chứ không ở luồng mạng vì tham số
/// đổi được lúc đang chạy và kết quả đi thẳng sang bộ bám quỹ đạo — để chung
/// một luồng thì không cần khoá nào cả.
class BeamCenter
{
public:
    /// Số chùm xử lý đồng thời — CX_PRO trong bản C.
    static constexpr int kMaxBeams = 64;

    /// Số xung tối đa giữ trong một chùm — CX_NUM.
    static constexpr int kMaxPulses = 256;

    void setParams(const BeamParams &p) { m_params = p; }
    const BeamParams &params() const { return m_params; }

    /// Danh sách rẻ quạt xử lý. Ngoài mọi rẻ quạt đang bật thì chu kỳ coi như
    /// không có plot — chùm đang mở vẫn được đóng đúng quy trình chứ không bị
    /// treo lại. Danh sách rỗng hoặc không dòng nào bật = xử lý cả vòng tròn.
    void setSectors(const QVector<Sector> &sectors) { m_sectors = sectors; }

    /// Cự ly tối đa (mét) để quy ô cự ly ra mét. Đổi được lúc đang chạy.
    void setMaxRangeMeters(double m) { m_maxRangeM = m; }

    /// Xoá sạch trạng thái các chùm đang mở (lúc bắt đầu một phiên nhận mới).
    void reset();

    /// Xử lý một chu kỳ RAW_P. Điểm dấu sinh ra được **thêm vào** `out` —
    /// nơi gọi tự quyết định khi nào xoá, để dùng lại một vec-tơ cho cả loạt.
    void process(const rawpkt::RawPCycle &cycle, quint32 timeMs, qint64 nowMs,
                 QVector<PlotTC> &out);

private:
    /// Một chùm xung đang gom — CX_T trong bản C.
    struct Beam {
        std::array<quint16, kMaxPulses> azm{};
        std::array<quint16, kMaxPulses> range{};
        std::array<quint16, kMaxPulses> amplitude{};
        std::array<quint8,  kMaxPulses> dopler{};

        int  numcx      = 0;   ///< số xung trong chùm
        int  numlose    = 0;   ///< số chu kỳ liên tiếp không có plot
        int  numloseAll = 0;   ///< tổng số chu kỳ không có plot
        int  numwait    = 0;   ///< số chu kỳ đang chờ đủ điều kiện mở chùm
        bool added      = false; ///< vừa được thêm xung trong chu kỳ đang xử lý

        enum State { Free, Wait, Pro, Lose } state = Free;
    };

    /// Cập nhật một chùm không nhận được plot nào trong chu kỳ — CheckLose().
    void checkLose(int j, quint32 timeMs, qint64 nowMs, QVector<PlotTC> &out);

    /// Chùm đã đủ điều kiện: tính tâm rồi sinh điểm dấu — Have_PlotTC().
    void emitPlot(int j, quint32 timeMs, qint64 nowMs, QVector<PlotTC> &out);

    /// Ô cự ly -> mét. Công thức nằm ở rawpkt::cellToMeters, dùng chung với lớp
    /// điểm dấu đơn xung — hai chỗ lệch nhau thì chấm đơn xung không nằm trên
    /// điểm dấu tâm chùm mà chính nó sinh ra.
    double cellToMeters(double cell) const
    {
        return rawpkt::cellToMeters(m_maxRangeM, cell);
    }

    bool inSector(quint32 azimuth) const;

    BeamParams m_params;
    std::array<Beam, kMaxBeams> m_beams;

    double m_maxRangeM = 1218.0;

    QVector<Sector> m_sectors;

    quint32 m_serial = 0;   ///< số đếm điểm dấu, tăng dần từ 1
};
