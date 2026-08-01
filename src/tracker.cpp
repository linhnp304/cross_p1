#include "tracker.h"

#include "geo.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>

namespace {

/// Chặn bước thời gian: gói tin đến lệch nhịp hoặc máy bị treo một lúc thì dt
/// có thể ra số vô lý, mà dt vào bình phương bậc bốn trong ma trận nhiễu.
constexpr double kMinDt = 0.05;
constexpr double kMaxDt = 120.0;

double normalizeDeg(double a)
{
    a = std::fmod(a, 360.0);
    return a < 0.0 ? a + 360.0 : a;
}

/// Chênh lệch góc theo đường ngắn nhất, trong (-180, 180].
double deltaDeg(double a)
{
    a = std::fmod(a + 180.0, 360.0);
    if (a < 0.0)
        a += 360.0;
    return a - 180.0;
}

int countBits(quint32 v)
{
    int n = 0;
    for (; v; v >>= 1)
        n += int(v & 1u);
    return n;
}

} // namespace

// ------------------------------------------------------- phép toán ma trận --
//
// Bốn trạng thái và hai phép đo — cỡ nhỏ tới mức viết thẳng ra rẻ hơn mọi thư
// viện ma trận, và không kéo thêm phụ thuộc nào vào dự án.

void Tracker::toXY(double azmDeg, double rangeM, double &x, double &y)
{
    // Phương vị tính từ hướng bắc theo chiều kim đồng hồ: x là đông, y là bắc.
    const double a = azmDeg * geo::kDeg2Rad;
    x = rangeM * std::sin(a);
    y = rangeM * std::cos(a);
}

void Tracker::predict(Track &t, double dt) const
{
    double *P = t.kp;

    // x' = F x
    t.kx[0] += t.kx[2] * dt;
    t.kx[1] += t.kx[3] * dt;

    // P' = F P Fᵀ. F chỉ khác ma trận đơn vị ở hai ô, nên phép nhân rút gọn
    // thành: cộng dt lần hàng 2 vào hàng 0 (và hàng 3 vào hàng 1), rồi làm
    // đúng như vậy với các cột.
    for (int j = 0; j < 4; ++j) {
        P[0 * 4 + j] += dt * P[2 * 4 + j];
        P[1 * 4 + j] += dt * P[3 * 4 + j];
    }
    for (int i = 0; i < 4; ++i) {
        P[i * 4 + 0] += dt * P[i * 4 + 2];
        P[i * 4 + 1] += dt * P[i * 4 + 3];
    }

    // Nhiễu quá trình kiểu gia tốc trắng từng đoạn.
    const double q   = m_params.sigmaAccel * m_params.sigmaAccel;
    const double dt2 = dt * dt;
    const double q11 = q * dt2 * dt2 / 4.0;   // vị trí
    const double q13 = q * dt2 * dt / 2.0;    // chéo vị trí - vận tốc
    const double q33 = q * dt2;               // vận tốc

    P[0 * 4 + 0] += q11;  P[1 * 4 + 1] += q11;
    P[2 * 4 + 2] += q33;  P[3 * 4 + 3] += q33;
    P[0 * 4 + 2] += q13;  P[2 * 4 + 0] += q13;
    P[1 * 4 + 3] += q13;  P[3 * 4 + 1] += q13;
}

void Tracker::measurementNoise(double azmDeg, double rangeM, double r[4]) const
{
    const double a  = azmDeg * geo::kDeg2Rad;
    const double sr = m_params.sigmaRangeM;
    // Sai số phương vị quy ra độ dài cung: cùng một góc, ở xa thì lệch nhiều
    // mét hơn ở gần.
    const double sa = m_params.sigmaAzmDeg * geo::kDeg2Rad * std::max(rangeM, 1.0);

    const double s = std::sin(a), c = std::cos(a);
    const double sr2 = sr * sr, sa2 = sa * sa;

    r[0] = sr2 * s * s + sa2 * c * c;
    r[1] = (sr2 - sa2) * s * c;
    r[2] = r[1];
    r[3] = sr2 * c * c + sa2 * s * s;
}

void Tracker::correct(Track &t, double zx, double zy, const double r[4]) const
{
    double *P = t.kp;

    // S = H P Hᵀ + R, với H lấy đúng hai trạng thái vị trí.
    const double s00 = P[0] + r[0];
    const double s01 = P[1] + r[1];
    const double s10 = P[4] + r[2];
    const double s11 = P[5] + r[3];

    const double det = s00 * s11 - s01 * s10;
    if (std::abs(det) < 1e-12)
        return;   // ma trận suy biến — bỏ qua lần cập nhật này còn hơn nổ số

    const double i00 =  s11 / det, i01 = -s01 / det;
    const double i10 = -s10 / det, i11 =  s00 / det;

    // K = P Hᵀ S⁻¹ (4x2). P Hᵀ chính là hai cột đầu của P.
    double k[8];
    for (int i = 0; i < 4; ++i) {
        const double a = P[i * 4 + 0], b = P[i * 4 + 1];
        k[i * 2 + 0] = a * i00 + b * i10;
        k[i * 2 + 1] = a * i01 + b * i11;
    }

    const double y0 = zx - t.kx[0];
    const double y1 = zy - t.kx[1];
    for (int i = 0; i < 4; ++i)
        t.kx[i] += k[i * 2 + 0] * y0 + k[i * 2 + 1] * y1;

    // P = (I - K H) P. H P là hai hàng đầu của P, phải giữ lại bản gốc vì
    // chính hai hàng đó cũng bị sửa trong vòng lặp.
    double hp[8];
    for (int j = 0; j < 4; ++j) {
        hp[0 * 4 + j] = P[0 * 4 + j];
        hp[1 * 4 + j] = P[1 * 4 + j];
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j)
            P[i * 4 + j] -= k[i * 2 + 0] * hp[0 * 4 + j] + k[i * 2 + 1] * hp[1 * 4 + j];
    }

    // Ép lại đối xứng: sai số làm tròn tích luỹ dần sẽ làm P lệch khỏi đối
    // xứng và bộ lọc phân kỳ sau vài trăm vòng.
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            const double m = 0.5 * (P[i * 4 + j] + P[j * 4 + i]);
            P[i * 4 + j] = P[j * 4 + i] = m;
        }
    }
}

// ------------------------------------------------------------ quản lý ------

void Tracker::clear()
{
    m_tracks.clear();
    m_removed.clear();
    m_dirty = true;
}

Track *Tracker::find(quint32 id)
{
    for (Track &t : m_tracks) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

const Track *Tracker::find(quint32 id) const
{
    return const_cast<Tracker *>(this)->find(id);
}

bool Tracker::remove(quint32 id)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id != id)
            continue;
        Track t = m_tracks[i];
        t.status = TrackStatus::Deleted;
        t.serial = ++m_serial;
        t.timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
        m_removed.push_back(t);
        m_tracks.removeAt(i);
        m_dirty = true;
        return true;
    }
    return false;
}

bool Tracker::setTop(quint32 id, quint32 top)
{
    for (const Track &t : m_tracks) {
        if (t.top == top && t.id != id)
            return false;
    }
    Track *t = find(id);
    if (!t)
        return false;
    t->top = top;
    m_dirty = true;
    return true;
}

QVector<Track> Tracker::takeRemoved()
{
    QVector<Track> out;
    out.swap(m_removed);
    return out;
}

bool Tracker::takeDirty()
{
    const bool d = m_dirty;
    m_dirty = false;
    return d;
}

// ---------------------------------------------------- liên kết điểm dấu ----

double Tracker::gateMeters(const Track &t, double dt) const
{
    // Quỹ đạo chưa biết vận tốc thì chưa dự đoán được gì: cửa sổ phải phủ được
    // quãng đường mục tiêu nhanh nhất đi được trong một vòng quét. Vẫn chặn ở
    // gateMaxM, vì với đài tầm gần thì vMax nhân chu kỳ vòng quét có thể vượt
    // cả cự ly tối đa — khi đó cửa sổ ôm trọn màn hình và mọi điểm dấu đều rơi
    // vào quỹ đạo đầu tiên gặp được.
    if (!t.hasVelocity) {
        return std::min(m_params.gateMaxM,
                        m_params.gateRangeM + m_params.vMax * m_scanSec);
    }

    // Độ bất định vị trí **sau khi dự đoán tới thời điểm của điểm dấu**, chứ
    // không phải độ bất định ngay lúc này. Hai cái lệch nhau cả chục lần: giữa
    // hai vòng quét cách nhau 10 giây, phần vận tốc chưa chắc chắn và phần
    // nhiễu quá trình mới là thứ quyết định, còn sai số vị trí hiện tại thì
    // nhỏ. Lấy nhầm cái kia thì cửa sổ hẹp hơn thực tế mấy lần, và mục tiêu
    // vừa ngoặt một cái là đứt quỹ đạo.
    //
    // Khai triển đường chéo của P' = F P Fᵀ + Q, chỉ hai phần tử vị trí:
    //     P'₀₀ = P₀₀ + 2·dt·P₀₂ + dt²·P₂₂ + q·dt⁴/4
    const double q    = m_params.sigmaAccel * m_params.sigmaAccel;
    const double dt2  = dt * dt;
    const double qPos = q * dt2 * dt2 / 4.0;

    const double px = t.kp[0] + 2.0 * dt * t.kp[2] + dt2 * t.kp[10] + qPos;
    const double py = t.kp[5] + 2.0 * dt * t.kp[7] + dt2 * t.kp[15] + qPos;

    const double sigmaPos = std::sqrt(std::max(0.0, std::max(px, py)));
    return std::min(m_params.gateMaxM,
                    m_params.gateRangeM + m_params.gateSigma * sigmaPos);
}

double Tracker::gateHalfAngleDeg(double gateM, double rangeM) const
{
    // Nửa góc mà một hình tròn bán kính gateM ở cự ly rangeM chắn được. Cửa sổ
    // trùm cả tâm đài thì góc là cả vòng tròn.
    if (gateM >= rangeM)
        return 180.0;
    return m_params.gateAzmDeg + std::asin(gateM / rangeM) * geo::kRad2Deg;
}

void Tracker::addPlot(const PlotTC &plot, qint64 nowMs)
{
    const double azm = plot.azmDeg();
    const double rng = plot.rangeM();

    double zx = 0.0, zy = 0.0;
    toXY(azm, rng, zx, zy);

    // --- tìm quỹ đạo phù hợp nhất ---
    //
    // Xét cả quỹ đạo đã nhận điểm dấu trong vòng quét này. Lý do: thuật toán
    // tâm chùm có thể cắt một mục tiêu thành hai chùm (biên độ tụt giữa chừng,
    // ô cự ly nhích quá tiêu chuẩn xét duyệt) và sinh ra hai điểm dấu sát nhau.
    // Nếu chỉ xét quỹ đạo còn trống thì điểm dấu thứ hai sẽ mở ra một quỹ đạo
    // mới, và một mục tiêu thành hai tốp trên màn hình.
    int    freeBest      = -1;
    double freeScore     = 0.0;
    double updatedScore  = 0.0;
    bool   haveUpdated   = false;

    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &t = m_tracks[i];

        const double dt = qBound(kMinDt, (nowMs - t.lastUpdateMs) / 1000.0, kMaxDt);

        // Dự đoán vị trí (không đụng tới trạng thái thật của bộ lọc: điểm dấu
        // này có thể rơi vào quỹ đạo khác).
        const double px = t.kx[0] + t.kx[2] * dt;
        const double py = t.kx[1] + t.kx[3] * dt;

        // Xét cửa sổ trong hệ Đề-các chứ không xét riêng cự ly và phương vị:
        // điều kiện thật là "mục tiêu không thể đi xa quá ngần này mét". Xét
        // theo phương vị thì ở gần tâm đài cùng một khoảng cách lại thành một
        // góc rất lớn, và cửa sổ nuốt trọn nửa màn hình.
        const double gateM = gateMeters(t, dt);
        const double dist  = std::hypot(zx - px, zy - py);
        if (dist > gateM)
            continue;

        const double score = dist / gateM;

        if (t.updated) {
            if (!haveUpdated || score < updatedScore) {
                haveUpdated  = true;
                updatedScore = score;
            }
        } else if (freeBest < 0 || score < freeScore) {
            freeBest  = i;
            freeScore = score;
        }
    }

    // Quỹ đạo đã cập nhật lại còn hợp hơn mọi quỹ đạo còn trống: điểm dấu này
    // là bản sao của chính mục tiêu đó, bỏ đi.
    if (haveUpdated && (freeBest < 0 || updatedScore <= freeScore))
        return;

    const int best = freeBest;
    if (best < 0) {
        // Không quỹ đạo nào nhận — mở quỹ đạo mới, nếu được phép.
        //
        // Hai chốt an toàn ở đây chứ không ở chỗ khác: cả hai chỉ chặn việc
        // **khởi tạo**, không đụng tới quỹ đạo đã có. Mục tiêu đang bám mà bay
        // qua vùng chết quanh tâm đài thì vẫn được cập nhật bình thường.
        if (m_tracks.size() >= m_params.maxTracks)
            return;
        if (rng < m_params.minInitRangeM)
            return;

        Track t;
        t.id     = m_nextId++;
        t.top    = t.id;
        t.status = TrackStatus::Init;
        t.kx[0] = zx; t.kx[1] = zy; t.kx[2] = 0.0; t.kx[3] = 0.0;

        double r[4];
        measurementNoise(azm, rng, r);
        t.kp[0] = r[0]; t.kp[1] = r[1];
        t.kp[4] = r[2]; t.kp[5] = r[3];
        // Chưa biết gì về vận tốc: đặt độ bất định bằng chính dải vận tốc quan
        // tâm, để lần cập nhật thứ hai phép đo được tin gần như tuyệt đối.
        t.kp[10] = t.kp[15] = m_params.vMax * m_params.vMax;

        t.lastUpdateMs = nowMs;
        t.lastPlotMs   = nowMs;
        t.updated      = true;
        t.amplitude    = plot.amplitudeAverage;
        refresh(t);
        t.timeMs = plot.timeMs;

        m_tracks.push_back(t);
        m_dirty = true;
        return;
    }

    Track &t = m_tracks[best];
    const double dt = qBound(kMinDt, (nowMs - t.lastUpdateMs) / 1000.0, kMaxDt);

    double r[4];
    measurementNoise(azm, rng, r);

    if (!t.hasVelocity) {
        // Khởi tạo hai điểm: vận tốc suy thẳng từ hiệu hai vị trí, chính xác
        // hơn nhiều so với để bộ lọc tự mò ra từ một điểm đứng yên.
        t.kx[2] = (zx - t.kx[0]) / dt;
        t.kx[3] = (zy - t.kx[1]) / dt;
        t.kx[0] = zx;
        t.kx[1] = zy;

        for (double &v : t.kp)
            v = 0.0;
        t.kp[0] = r[0]; t.kp[1] = r[1];
        t.kp[4] = r[2]; t.kp[5] = r[3];
        // Sai số vận tốc của phép trừ hai vị trí: gấp đôi sai số vị trí chia dt.
        t.kp[10] = 2.0 * r[0] / (dt * dt);
        t.kp[15] = 2.0 * r[3] / (dt * dt);
        t.hasVelocity = true;
    } else {
        predict(t, dt);
        correct(t, zx, zy, r);
    }

    t.lastUpdateMs = nowMs;
    t.lastPlotMs   = nowMs;
    t.updated      = true;
    t.misses       = 0;
    t.amplitude    = plot.amplitudeAverage;
    refresh(t);
    t.timeMs = plot.timeMs;
    m_dirty  = true;
}

// -------------------------------------------------------- chốt vòng quét ---

void Tracker::endScan(qint64 nowMs, qint64 periodMs)
{
    if (periodMs > 0)
        m_scanSec = qBound(0.5, periodMs / 1000.0, 120.0);

    const int window = m_params.initWindow();
    const quint32 mask = (1u << window) - 1u;

    for (int i = m_tracks.size() - 1; i >= 0; --i) {
        Track &t = m_tracks[i];

        t.hitMask = (t.hitMask << 1) | (t.updated ? 1u : 0u);
        ++t.scans;

        if (!t.updated) {
            // Không có điểm dấu: đẩy trạng thái tới thời điểm hiện tại rồi
            // đánh dấu là ngoại suy.
            const double dt = qBound(kMinDt, (nowMs - t.lastUpdateMs) / 1000.0, kMaxDt);
            predict(t, dt);
            t.lastUpdateMs = nowMs;
            ++t.misses;
            if (t.status != TrackStatus::Init)
                t.status = TrackStatus::Coasting;
            refresh(t);
        }
        t.updated = false;

        // --- tiêu chuẩn khởi tạo ---
        if (t.status == TrackStatus::Init) {
            if (countBits(t.hitMask & mask) >= m_params.initHits()) {
                // Chỉ nhận quỹ đạo có vận tốc nằm trong dải đang quan tâm —
                // đây là cái van chính để lọc bớt quỹ đạo rác.
                const double v = t.speedMs();
                if (v >= m_params.vMin && v <= m_params.vMax) {
                    t.status = TrackStatus::Tracking;
                    t.misses = 0;
                } else {
                    t.status = TrackStatus::Deleted;
                }
            } else if (t.scans >= window) {
                t.status = TrackStatus::Deleted;
            }
        } else if (t.misses == 0) {
            t.status = TrackStatus::Tracking;
        } else if (t.misses > m_params.coastScans) {
            t.status = TrackStatus::Deleted;
        }

        // Dải vận tốc xét cả sau khi đã khẳng định, không chỉ một lần lúc khởi
        // tạo: quỹ đạo bắt nhầm điểm dấu của mục tiêu khác thì vận tốc chạy
        // loạn, mà nếu chỉ xét lúc khởi tạo thì nó sống tới lúc bay ra khỏi
        // vùng phủ. Đòi hai vòng liên tiếp mới xoá, để một lần cập nhật xấu
        // đơn lẻ không giết mất quỹ đạo đang bám tốt.
        if (t.status != TrackStatus::Init && t.hasVelocity) {
            const double v = t.speedMs();
            if (v < m_params.vMin || v > m_params.vMax) {
                if (++t.outOfBand >= 2)
                    t.status = TrackStatus::Deleted;
            } else {
                t.outOfBand = 0;
            }
        }

        // Ra khỏi vùng phủ của đài thì xoá luôn, không chờ hết số vòng ngoại
        // suy: ngoài đó không còn điểm dấu nào để cập nhật. Chừa 10% lề để quỹ
        // đạo đang bám sát vòng cự ly tối đa không bị xoá oan vì sai số đo.
        if (t.rangeM() > m_maxRangeM * 1.1)
            t.status = TrackStatus::Deleted;

        if (t.status == TrackStatus::Deleted) {
            t.serial = ++m_serial;
            t.timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
            m_removed.push_back(t);
            m_tracks.removeAt(i);
            m_dirty = true;
            continue;
        }

        // Vết lịch sử ghi mỗi vòng quét một điểm, kể cả vòng ngoại suy — trắc
        // thủ cần thấy chỗ nào bám thật, chỗ nào là thuật toán tự đoán.
        pushHistory(t);
    }
    m_dirty = true;
}

void Tracker::expireStale(qint64 nowMs)
{
    const qint64 limit = qint64(m_params.staleSec) * 1000;

    for (int i = m_tracks.size() - 1; i >= 0; --i) {
        Track &t = m_tracks[i];
        // lastPlotMs chứ không phải lastUpdateMs: mốc kia bị vòng ngoại suy đẩy
        // lên nên không bao giờ quá hạn.
        if (nowMs - t.lastPlotMs <= limit)
            continue;

        t.status = TrackStatus::Deleted;
        t.serial = ++m_serial;
        t.timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
        m_removed.push_back(t);
        m_tracks.removeAt(i);
        m_dirty = true;
    }
}

void Tracker::pushHistory(Track &t)
{
    if (m_historyLimit <= 0) {
        t.history.clear();
        return;
    }

    t.history.push_back({double(t.lat), double(t.lng), t.status});
    if (t.history.size() > m_historyLimit)
        t.history.remove(0, t.history.size() - m_historyLimit);
}

void Tracker::refresh(Track &t)
{
    // Mọi trường hiển thị đổi ở đây, nên cũng chính ở đây đánh dấu là có cái
    // mới để gửi đi — khỏi phải nhớ đặt cờ ở từng nơi gọi.
    t.pendingSend = true;

    const double x = t.kx[0], y = t.kx[1];
    const double vx = t.kx[2], vy = t.kx[3];

    const double rangeM = std::hypot(x, y);
    const double azmDeg = normalizeDeg(std::atan2(x, y) * geo::kRad2Deg);
    const double speed  = std::hypot(vx, vy);

    t.serial   = ++m_serial;
    t.timeMs   = quint32(QTime::currentTime().msecsSinceStartOfDay());
    t.azm      = quint32(std::llround(azmDeg * 100.0)) % 36000u;
    t.range    = quint32(std::llround(std::max(0.0, rangeM) * 10.0));
    t.velocity = quint32(std::llround(speed * 10.0));
    // Đứng yên thì atan2(0,0) trả về 0 chứ không phải "không xác định" — giữ
    // nguyên hướng cũ để hình tam giác trên màn hình khỏi giật về hướng bắc.
    if (speed > 1e-3)
        t.heading = quint32(std::llround(normalizeDeg(std::atan2(vx, vy) * geo::kRad2Deg) * 100.0)) % 36000u;

    double lat = 0.0, lng = 0.0;
    geo::destination(m_siteLat, m_siteLng, azmDeg, rangeM / 1000.0, lat, lng);
    t.lat = float(lat);
    t.lng = float(lng);

    // --- cửa sổ dự đoán cho vòng quét kế tiếp ---
    const double px = x + vx * m_scanSec;
    const double py = y + vy * m_scanSec;
    const double rP = std::hypot(px, py);
    const double aP = normalizeDeg(std::atan2(px, py) * geo::kRad2Deg);

    const double gateM   = gateMeters(t, m_scanSec);
    const double gateDeg = gateHalfAngleDeg(gateM, rP);

    t.windowRange1 = quint32(std::llround(std::max(0.0, rP - gateM) * 10.0));
    t.windowRange2 = quint32(std::llround(std::max(0.0, rP + gateM) * 10.0));
    // Cửa sổ vắt qua mốc bắc thì azm1 > azm2 — nơi vẽ tự hiểu theo chiều kim
    // đồng hồ từ azm1 tới azm2.
    t.windowAzm1 = quint32(std::llround(normalizeDeg(aP - gateDeg) * 100.0)) % 36000u;
    t.windowAzm2 = quint32(std::llround(normalizeDeg(aP + gateDeg) * 100.0)) % 36000u;
}
