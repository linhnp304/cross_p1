#include "proc/beamcenter.h"

#include <cmath>

namespace {

/// Chênh lệch phương vị encoder theo đường ngắn nhất, kết quả trong (-2048, 2048].
/// Cần đến vì chùm xung có thể vắt qua mốc 0 độ.
int wrapDelta(int d)
{
    d %= rawpkt::kAzimuthSteps;
    if (d >  rawpkt::kAzimuthSteps / 2) d -= rawpkt::kAzimuthSteps;
    if (d < -rawpkt::kAzimuthSteps / 2) d += rawpkt::kAzimuthSteps;
    return d;
}

} // namespace

void BeamCenter::reset()
{
    for (Beam &b : m_beams) {
        b.state   = Beam::Free;
        b.numcx   = 0;
        b.added   = false;
        b.numlose = 0;
        b.numloseAll = 0;
        b.numwait = 0;
    }
}

bool BeamCenter::inSector(quint32 azimuth) const
{
    return sectorsAllow(m_sectors, rawpkt::azimuthToDeg(azimuth));
}

void BeamCenter::process(const rawpkt::RawPCycle &cycle, quint32 timeMs, qint64 nowMs,
                         QVector<PlotTC> &out)
{
    const quint32 azimuth = cycle.workAzimuth();
    const bool    usable  = cycle.count > 0 && inSector(azimuth);

    if (!usable) {
        // Chu kỳ không có plot (hoặc nằm ngoài rẻ quạt) — chỉ cập nhật trạng
        // thái các chùm đang mở.
        for (int j = 0; j < kMaxBeams; ++j) {
            if (m_beams[size_t(j)].state != Beam::Free)
                checkLose(j, timeMs, nowMs, out);
        }
        return;
    }

    for (int p = 0; p < cycle.count; ++p) {
        const rawpkt::RawPlot &plot = cycle.plots[size_t(p)];

        // Loại bỏ plot có dopler ngoài dải: quá nhỏ hoặc quá lớn thường là
        // địa vật chứ không phải mục tiêu bay.
        if (plot.dopler < m_params.doplerMin || plot.dopler > m_params.doplerMax)
            continue;

        bool added = false;
        for (int j = 0; j < kMaxBeams; ++j) {
            Beam &b = m_beams[size_t(j)];
            if (b.state == Beam::Free)
                continue;

            // Xét duyệt theo **xung đầu chùm**, không theo xung gần nhất: chùm
            // của một mục tiêu không được phép trôi dần sang ô cự ly khác.
            if (std::abs(int(plot.range) - int(b.range[0])) > m_params.deltaRange
                || std::abs(int(plot.dopler) - int(b.dopler[0])) > m_params.deltaDopler)
                continue;

            if (b.numcx < kMaxPulses - 1) {
                const size_t k = size_t(b.numcx);
                b.azm[k]       = quint16(azimuth);
                b.range[k]     = plot.range;
                b.amplitude[k] = plot.amplitude;
                b.dopler[k]    = plot.dopler;
                b.added        = true;
                ++b.numcx;

                if (b.state == Beam::Wait) {
                    ++b.numwait;
                    if (b.numwait >= m_params.numWait) {
                        b.numwait = 0;
                        b.state   = Beam::Pro;
                    }
                } else {
                    b.numlose = 0;
                    b.state   = Beam::Pro;
                }
                added = true;
            }
            // Chùm đã đầy thì plot bị bỏ, nhưng vẫn coi như đã có nơi nhận:
            // mở thêm chùm mới ở cùng ô cự ly chỉ sinh ra điểm dấu trùng.
            break;
        }

        if (added)
            continue;

        // Không vào chùm nào — mở chùm mới ở trạng thái chờ.
        for (Beam &b : m_beams) {
            if (b.state != Beam::Free)
                continue;
            b.azm[0]       = quint16(azimuth);
            b.range[0]     = plot.range;
            b.amplitude[0] = plot.amplitude;
            b.dopler[0]    = plot.dopler;
            b.added        = true;
            b.numcx        = 1;
            b.numwait      = 1;
            b.numlose      = 0;
            b.numloseAll   = 0;
            b.state        = Beam::Wait;
            break;
        }
    }

    // Cập nhật những chùm không nhận được plot nào trong chu kỳ này.
    for (int j = 0; j < kMaxBeams; ++j) {
        Beam &b = m_beams[size_t(j)];
        if (b.state == Beam::Free)
            continue;
        if (b.added)
            b.added = false;
        else
            checkLose(j, timeMs, nowMs, out);
    }
}

void BeamCenter::checkLose(int j, quint32 timeMs, qint64 nowMs, QVector<PlotTC> &out)
{
    Beam &b = m_beams[size_t(j)];

    switch (b.state) {
    case Beam::Free:
        break;

    case Beam::Wait:
        // Chùm còn đang chờ mà đã mất xung thì gần như chắc chắn là nhiễu đơn
        // lẻ — bỏ luôn, không tiếc.
        b.state = Beam::Free;
        b.numcx = 0;
        break;

    case Beam::Pro:
        b.numlose = 1;
        ++b.numloseAll;
        b.state = Beam::Lose;
        break;

    case Beam::Lose:
        ++b.numlose;
        ++b.numloseAll;
        if (b.numlose >= m_params.numLose) {
            // Đủ số chu kỳ mất xung: chùm kết thúc. Chỉ chùm có độ dài nằm
            // trong tiêu chuẩn mới sinh ra điểm dấu.
            if (b.numcx >= m_params.cxMin && b.numcx <= m_params.cxMax)
                emitPlot(j, timeMs, nowMs, out);
            b.state = Beam::Free;
            b.numcx = 0;
        }
        break;
    }
}

void BeamCenter::emitPlot(int j, quint32 timeMs, qint64 nowMs, QVector<PlotTC> &out)
{
    const Beam &b = m_beams[size_t(j)];
    const int   n = b.numcx;
    if (n <= 0)
        return;

    double cellCentre = 0.0;   ///< ô cự ly của tâm chùm
    double azEnc      = 0.0;   ///< phương vị encoder của tâm chùm (có thể lẻ)
    quint32 ampAvg    = 0;
    quint32 ampCentre = 0;

    // Tổng trọng số bằng 0 (biên độ toàn 0) thì phép chia có trọng số vô nghĩa
    // — lùi về phương án đơn giản chứ không sinh ra một điểm dấu vô lý.
    double wSum = 0.0;
    if (m_params.useAmplitude) {
        for (int i = 0; i < n; ++i)
            wSum += b.amplitude[size_t(i)];
    }

    if (m_params.useAmplitude && wSum > 0.0) {
        // --- phương án có trọng số biên độ phản xạ ---
        //
        // Xung nào phản xạ mạnh thì kéo tâm chùm về phía nó. Phương vị phải
        // cộng dồn theo **độ lệch so với xung đầu chùm** chứ không cộng thẳng
        // giá trị encoder: chùm vắt qua mốc 0 thì cộng thẳng sẽ ra tâm nằm ở
        // phía đối diện của vòng tròn.
        double rSum = 0.0, aSum = 0.0;
        const int a0 = b.azm[0];
        for (int i = 0; i < n; ++i) {
            const double w = b.amplitude[size_t(i)];
            rSum += w * b.range[size_t(i)];
            aSum += w * wrapDelta(int(b.azm[size_t(i)]) - a0);
        }
        cellCentre = rSum / wSum;
        azEnc      = a0 + aSum / wSum;
        ampAvg     = quint32(std::llround(wSum / n));

        // Năng lượng tại tâm chùm: lấy của xung nằm gần tâm nhất. Nội suy giữa
        // hai xung kề cũng được, nhưng biên độ xung đơn vốn đã dao động mạnh
        // nên nội suy chỉ thêm phép tính chứ không thêm thông tin.
        int best = 0;
        double bestDist = 1e18;
        for (int i = 0; i < n; ++i) {
            const double d = std::abs(a0 + wrapDelta(int(b.azm[size_t(i)]) - a0) - azEnc);
            if (d < bestDist) {
                bestDist = d;
                best     = i;
            }
        }
        ampCentre = b.amplitude[size_t(best)];
    } else {
        // --- phương án đơn giản (mặc định) ---
        // Cự ly lấy trung bình cộng, phương vị lấy điểm giữa của xung đầu và
        // xung cuối chùm.
        double rSum = 0.0;
        for (int i = 0; i < n; ++i)
            rSum += b.range[size_t(i)];
        cellCentre = rSum / n;

        const int a0 = b.azm[0];
        const int a1 = b.azm[size_t(n - 1)];
        azEnc = a0 < a1 ? (a0 + a1) / 2.0
                        : (rawpkt::kAzimuthSteps + a0 + a1) / 2.0;
        // amplitudeAverage / amplitudeCenter để 0 khi không dùng trọng số,
        // đúng như mô tả gói tin.
    }

    // Hiệu chỉnh lắp đặt cộng vào ở **cuối cùng**, ngay trước lúc gán vào gói
    // tin: mọi phép tính tâm chùm phía trên vẫn làm việc trên số liệu thô của
    // đài, nên chỉnh lại giá trị bù không kéo theo hiệu ứng phụ nào.
    const double azDeg = normalizeDeg360(azEnc * 360.0 / rawpkt::kAzimuthSteps
                                         + m_params.azmOffsetDeg);
    const double rangeM = qMax(0.0, cellToMeters(cellCentre)
                                        + m_params.rangeOffsetM);

    PlotTC p;
    p.serial = ++m_serial;
    p.timeMs = timeMs;
    p.bornMs = nowMs;
    p.azm    = quint32(std::llround(azDeg * 100.0)) % 36000u;
    p.range  = quint32(std::llround(rangeM * 10.0));

    p.numCX            = quint32(n);
    p.azmStart         = b.azm[0];
    p.azmStop          = b.azm[size_t(n - 1)];
    p.numLoseTotal     = quint32(b.numloseAll);
    p.amplitudeAverage = ampAvg;
    p.amplitudeCenter  = ampCentre;
    p.rangeStart       = b.range[0];
    p.doplerStart      = b.dopler[0];

    out.push_back(p);
}
