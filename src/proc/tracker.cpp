#include "proc/tracker.h"

#include "maps/geo.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>

namespace {

/// Chặn bước thời gian: gói tin đến lệch nhịp hoặc máy bị treo một lúc thì dt
/// có thể ra số vô lý, mà dt vào bình phương bậc bốn trong ma trận nhiễu.
constexpr double kMinDt = 0.05;
constexpr double kMaxDt = 120.0;

/// Ngưỡng coi là mục tiêu cơ động: điểm dấu rơi ra ngoài ngần này phần cửa sổ
/// dự đoán.
///
/// Đo theo **phần cửa sổ** chứ không theo số lần độ lệch chuẩn, dù cách sau mới
/// là cách chuẩn mực: cửa sổ chỉ rộng gateSigma lần độ lệch chuẩn (mặc định 2),
/// nên mọi ngưỡng đặt ở 3 lần độ lệch chuẩn đều nằm ngoài cửa sổ — điểm dấu
/// lệch tới mức ấy thì đã không ghép vào đâu để mà xét. Lấy theo phần cửa sổ
/// thì ngưỡng luôn nằm trong tầm với, và tự co giãn theo chính gateSigma mà
/// người dùng đặt.
constexpr double kManoeuvreEdge = 0.75;

/// Không có phương vị ăng-ten trong ngần này mili giây thì quay đường quét bằng
/// đồng hồ. Đặt hơn một chút so với nhịp gói thưa nhất còn coi là bình thường.
constexpr qint64 kSweepIdleMs = 1000;

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
    m_cand.clear();
    m_removed.clear();
    m_dirty = true;
}

void Tracker::clearCandidates()
{
    m_cand.clear();
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

void Tracker::applyExternal(const Track &in, qint64 nowMs)
{
    m_dirty = true;

    if (in.status == TrackStatus::Deleted) {
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i].id == in.id) {
                m_tracks.removeAt(i);
                return;
            }
        }
        return;
    }

    Track *t = find(in.id);
    if (!t) {
        if (m_tracks.size() >= m_params.maxTracks)
            return;
        m_tracks.push_back(Track{});
        t = &m_tracks.last();
    }

    // Giữ lại đúng hai thứ của bản đang có: vết lịch sử (file chỉ ghi vị trí
    // hiện thời của mỗi lần cập nhật, vết là do bên này gom lại) và ô "Theo dõi"
    // mà trắc thủ vừa tích trong lúc xem lại.
    QVector<TrackPoint> history = t->history;
    const bool watched = t->watched;

    *t = in;
    t->history = std::move(history);
    t->watched = watched;
    t->lastUpdateMs = nowMs;
    t->lastPlotMs   = nowMs;
    t->pendingSend  = false;   // phát lại thì gửi thẳng gói gốc, không dựng lại
    armRound(*t);              // để nếu bộ bám có chạy lại thì nó không chốt sổ ngay

    pushHistory(*t);
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
    // Trần động học: kể từ điểm dấu thật cuối cùng, mục tiêu nhanh nhất trong
    // dải quan tâm cũng chỉ đi được ngần này mét. Không cửa sổ nào cần rộng hơn
    // — rộng hơn thì chỉ tổ nuốt điểm dấu của mục tiêu khác.
    const double sinceLastPlot = dt + t.misses * m_scanSec;
    const double kinMax = m_params.gateRangeM + m_params.vMax * sinceLastPlot;

    // Vòng đầu tiên chưa biết gì về vận tốc nên chưa dự đoán được gì: cửa sổ
    // mở đúng bằng trần động học, tức cực đại. **Không** chặn ở gateMaxM: chặn
    // là mục tiêu nhanh không bao giờ khởi tạo được quỹ đạo. Với tham số mặc
    // định (vMax 120 m/s, vòng quét 10 s) trần đó là 1240 m còn gateMaxM chỉ
    // 400 m — mọi mục tiêu nhanh hơn ~36 m/s rơi ra ngoài cửa sổ ngay ở vòng
    // thứ hai, và mỗi vòng lại đẻ ra một cửa sổ mới không bao giờ chín.
    //
    // Đổi lại, người dùng phải khai vMax sát với loại mục tiêu đang quan tâm:
    // khai thừa thì cửa sổ khởi tạo trùm cả màn hình.
    if (!t.hasVelocity)
        return kinMax;

    // Độ bất định vị trí **sau khi dự đoán tới thời điểm của điểm dấu**, chứ
    // không phải độ bất định ngay lúc này. Hai cái lệch nhau cả chục lần: giữa
    // hai vòng quét cách nhau 10 giây, phần vận tốc chưa chắc chắn và phần
    // nhiễu quá trình mới là thứ quyết định, còn sai số vị trí hiện tại thì
    // nhỏ. Lấy nhầm cái kia thì cửa sổ hẹp hơn thực tế mấy lần, và mục tiêu
    // vừa ngoặt một cái là đứt quỹ đạo.
    //
    // Đây cũng chính là cơ chế **tự thu nhỏ**: mỗi lần ghép được điểm dấu, P co
    // lại, cửa sổ vòng sau hẹp đi. Và là cơ chế **tự nới ra** khi ngoại suy hay
    // khi vừa phát hiện cơ động — cả hai đều làm P phình.
    //
    // Khai triển đường chéo của P' = F P Fᵀ + Q, chỉ hai phần tử vị trí:
    //     P'₀₀ = P₀₀ + 2·dt·P₀₂ + dt²·P₂₂ + q·dt⁴/4
    const double q    = m_params.sigmaAccel * m_params.sigmaAccel;
    const double dt2  = dt * dt;
    const double qPos = q * dt2 * dt2 / 4.0;

    const double px = t.kp[0] + 2.0 * dt * t.kp[2] + dt2 * t.kp[10] + qPos;
    const double py = t.kp[5] + 2.0 * dt * t.kp[7] + dt2 * t.kp[15] + qPos;

    const double sigmaPos = std::sqrt(std::max(0.0, std::max(px, py)));
    const double gate = m_params.gateRangeM + m_params.gateSigma * sigmaPos;

    // Trần gateMaxM chỉ dành cho quỹ đạo đang bám đều — ở đó nó giữ cho một
    // quỹ đạo có P xấu khỏi quét sạch điểm dấu quanh nó. Đang ngoại suy hoặc
    // vừa bắt được mục tiêu ngoặt thì áp trần đó vào là tự tay bịt mất cơ hội
    // bắt lại: đúng hai lúc ấy là lúc cần cửa sổ rộng nhất. Cái chặn thật sự
    // vẫn còn đó — trần động học.
    const bool loose = t.misses > 0 || t.manoeuvre > 0;
    const double cap = loose ? kinMax : std::min(m_params.gateMaxM, kinMax);
    return std::min(gate, cap);
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
    double updatedScore = 0.0;
    bool   haveUpdated  = false;

    int    trackBest  = -1, candBest  = -1;
    double trackScore = 0.0, candScore = 0.0;

    const auto scan = [&](const QVector<Track> &list, int &best, double &bestScore) {
        for (int i = 0; i < list.size(); ++i) {
            const Track &t = list[i];

            const double dt = qBound(kMinDt, (nowMs - t.lastUpdateMs) / 1000.0, kMaxDt);

            // Dự đoán vị trí (không đụng tới trạng thái thật của bộ lọc: điểm
            // dấu này có thể rơi vào quỹ đạo khác).
            const double px = t.kx[0] + t.kx[2] * dt;
            const double py = t.kx[1] + t.kx[3] * dt;

            // Xét cửa sổ trong hệ Đề-các chứ không xét riêng cự ly và phương
            // vị: điều kiện thật là "mục tiêu không thể đi xa quá ngần này
            // mét". Xét theo phương vị thì ở gần tâm đài cùng một khoảng cách
            // lại thành một góc rất lớn, và cửa sổ nuốt trọn nửa màn hình.
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
            } else if (best < 0 || score < bestScore) {
                best      = i;
                bestScore = score;
            }
        }
    };

    scan(m_tracks, trackBest, trackScore);
    scan(m_cand,   candBest,  candScore);

    // Quỹ đạo đã khởi tạo được quyền nhận trước, kể cả khi một cửa sổ đang chờ
    // nằm sát hơn: điểm dấu của mục tiêu đang bám không nên bị một cửa sổ mới
    // chớm bên cạnh cướp mất — mất một vòng của quỹ đạo thật đắt hơn nhiều so
    // với việc một cửa sổ chờ phải đợi thêm vòng nữa.
    QVector<Track> *list = nullptr;
    int    best  = -1;
    double score = 0.0;
    if (trackBest >= 0) {
        list = &m_tracks; best = trackBest; score = trackScore;
    } else if (candBest >= 0) {
        list = &m_cand;   best = candBest;  score = candScore;
    }

    // Quỹ đạo đã cập nhật lại còn hợp hơn mọi quỹ đạo còn trống: điểm dấu này
    // là bản sao của chính mục tiêu đó, bỏ đi.
    if (haveUpdated && (best < 0 || updatedScore <= score))
        return;

    if (best < 0) {
        // Không quỹ đạo nào nhận — mở một cửa sổ dự đoán mới, nếu được phép.
        //
        // Hai chốt an toàn ở đây chứ không ở chỗ khác: cả hai chỉ chặn việc
        // **khởi tạo**, không đụng tới quỹ đạo đã có. Mục tiêu đang bám mà bay
        // qua vùng chết quanh tâm đài thì vẫn được cập nhật bình thường.
        if (m_tracks.size() + m_cand.size() >= m_params.maxTracks)
            return;
        if (rng < m_params.minInitRangeM)
            return;
        // Vùng cấm khởi tạo do người dùng khoanh — cùng tinh thần với hai chốt
        // trên: chỉ chặn mở quỹ đạo mới, không chặn cập nhật.
        if (inNoInitZone(plot.azmDeg(), rng))
            return;

        Track t;
        t.id     = m_nextId++;
        t.top    = t.id;
        // Chưa có trạng thái nào cả: cửa sổ này chưa phải quỹ đạo, chưa hiển
        // thị và chưa gửi đi đâu. Trạng thái 1 chỉ được gán đúng lúc nó đạt
        // tiêu chuẩn khởi tạo, ở closeCandidate().
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
        armRound(t, true);

        m_cand.push_back(t);
        return;
    }

    Track &t = (*list)[best];
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
        // Khoảng lệch giữa điểm dấu và vị trí dự đoán, lấy trước khi bộ lọc kéo
        // trạng thái về phía phép đo.
        const double dist = std::hypot(zx - t.kx[0], zy - t.kx[1]);
        correct(t, zx, zy, r);

        // --- phát hiện cơ động ---
        //
        // Điểm dấu rơi ra tận rìa cửa sổ: mục tiêu vừa làm điều bộ lọc không
        // ngờ tới — ngoặt, hoặc đổi tốc — chứ không phải đài đo lệch đi một
        // chút. Vòng sau nó rất có thể lại lệch thêm chừng ấy nữa, mà lần này
        // thì ra khỏi cửa sổ và đứt quỹ đạo.
        //
        // Bơm thêm vào bất định vận tốc đúng phần đã lệch quá nửa cửa sổ. Được
        // cùng lúc hai việc: cửa sổ vòng sau nở ra đón khúc cua tiếp theo, và
        // bộ lọc tin phép đo hơn nên bám kịp vòng ngoặt. Cộng vào đường chéo
        // nên P vẫn là ma trận nửa xác định dương — nhân hệ số vào cả khối thì
        // không chắc còn giữ được.
        if (score > kManoeuvreEdge) {
            const double gate = dist / score;   // bán kính cửa sổ vừa xét
            const double dv   = (dist - 0.5 * gate) / dt;
            t.kp[10] += dv * dv;
            t.kp[15] += dv * dv;
            t.manoeuvre = 2;
        }
    }

    t.lastUpdateMs = nowMs;
    t.lastPlotMs   = nowMs;
    t.updated      = true;
    t.misses       = 0;
    t.amplitude    = plot.amplitudeAverage;
    refresh(t);
    t.timeMs = plot.timeMs;
    if (list == &m_tracks)
        m_dirty = true;
}

// -------------------------------------------------------- chốt vòng quét ---

void Tracker::setScanPeriod(qint64 periodMs)
{
    if (periodMs > 0)
        m_scanSec = qBound(0.5, periodMs / 1000.0, 120.0);
}

void Tracker::onSweep(double beamAzmDeg, qint64 nowMs)
{
    const double a = normalizeDeg(beamAzmDeg);
    if (m_lastBeamDeg >= 0.0) {
        // Cộng dồn theo bước ngắn nhất: qua mốc bắc là bước dương nhỏ, còn
        // encoder rung lùi lại vài nấc thì bỏ qua chứ không trừ đi — trừ đi là
        // mốc chốt sổ bị lùi theo và có quỹ đạo chốt sổ hai lần một vòng.
        const double d = deltaDeg(a - m_lastBeamDeg);
        if (d > 0.0)
            m_sweepDeg += d;
    }
    m_lastBeamDeg = a;
    m_lastSweepMs = nowMs;

    closeDueRounds(nowMs);
}

void Tracker::tickClock(qint64 nowMs)
{
    if (m_lastSweepMs != 0 && nowMs - m_lastSweepMs < kSweepIdleMs) {
        m_virtualSweepMs = 0;   // nguồn thật đang quay đường quét, không xen vào
        return;
    }

    if (m_virtualSweepMs == 0) {
        m_virtualSweepMs = nowMs;
        return;
    }

    // Quay tiếp bằng đồng hồ theo chu kỳ đo được lần cuối. Không có phương vị
    // ăng-ten thì đây là ước lượng duy nhất còn lại — và nó phải có, nếu không
    // quỹ đạo cũ nằm lại mãi trên màn hình: đài ngừng phát thì không còn ai
    // chốt sổ, mà không chốt sổ thì không bao giờ ngoại suy rồi xoá.
    const double dt = (nowMs - m_virtualSweepMs) / 1000.0;
    m_virtualSweepMs = nowMs;
    m_sweepDeg += 360.0 * dt / m_scanSec;

    closeDueRounds(nowMs);
}

void Tracker::armRound(Track &t, bool firstRound) const
{
    // Mốc chốt sổ là mép sau (theo chiều quét) của cửa sổ dự đoán.
    const double edge = t.windowAzm2 * 0.01;
    const double adj  = m_lastBeamDeg < 0.0 ? 0.0 : deltaDeg(edge - m_lastBeamDeg);

    if (firstRound) {
        // Vòng đầu của một cửa sổ vừa mở: đường quét đang đứng ngay chỗ điểm
        // dấu vừa sinh ra nó, chỉ còn phải đi nốt phần cửa sổ phía trước là
        // chốt được sổ vòng này. **Không** cộng cả vòng vào — cộng thì điểm dấu
        // của vòng sau lại rơi vào cùng một vòng sổ với điểm dấu đã sinh ra cửa
        // sổ, hai vòng bị đếm thành một, và tiêu chuẩn khởi tạo lúc nào cũng
        // chậm mất đúng một vòng.
        t.closeAtDeg = m_sweepDeg + std::max(0.0, adj);
        return;
    }

    // Các vòng sau: đường quét vừa đi qua mục tiêu nên phải quay đủ một vòng
    // mới gặp lại. Phần cộng thêm chỉ là chỗ cửa sổ đã dịch đi vì mục tiêu
    // chuyển động và vì cửa sổ co giãn.
    //
    // Chặn phần cộng thêm ở ±90°: cửa sổ rộng tới mức chắn nửa vòng trời (mục
    // tiêu ở rất gần tâm đài) thì mép sau của nó không còn nói lên điều gì về
    // nhịp quét nữa, mà nhịp mới là thứ đang cần.
    t.closeAtDeg = m_sweepDeg + 360.0 + qBound(-90.0, adj, 90.0);
}

void Tracker::closeCommon(Track &t, qint64 nowMs)
{
    t.hitMask = (t.hitMask << 1) | (t.updated ? 1u : 0u);
    ++t.scans;
    if (t.manoeuvre > 0)
        --t.manoeuvre;

    if (!t.updated) {
        // Không có điểm dấu: đẩy trạng thái tới thời điểm hiện tại. Từ đây cửa
        // sổ dự đoán cũng tự mở rộng ra, vì P vừa phình thêm một bước nhiễu
        // quá trình mà không có phép đo nào kéo lại.
        const double dt = qBound(kMinDt, (nowMs - t.lastUpdateMs) / 1000.0, kMaxDt);
        predict(t, dt);
        t.lastUpdateMs = nowMs;
        ++t.misses;
        refresh(t);
    }
    t.updated = false;
}

Tracker::CandOutcome Tracker::closeCandidate(Track &t, qint64 nowMs)
{
    closeCommon(t, nowMs);

    const int window = m_params.initWindow();
    const int hits   = countBits(t.hitMask & ((1u << window) - 1u));

    if (hits >= m_params.initHits()) {
        // Đủ tiêu chuẩn. Chỉ nhận quỹ đạo có vận tốc nằm trong dải đang quan
        // tâm — đây là cái van chính để lọc bớt quỹ đạo rác. Không nằm trong
        // dải thì bỏ hẳn chứ không giữ lại chờ tiếp: giữ lại là nó ngồi đó mãi,
        // vòng nào cũng đủ tiêu chuẩn, vòng nào cũng bị loại, mà vẫn ăn điểm
        // dấu như một quỹ đạo thật.
        const double v = t.speedMs();
        if (v < m_params.vMin || v > m_params.vMax)
            return CandOutcome::Drop;

        t.status = TrackStatus::Init;
        t.misses = 0;
        return CandOutcome::Promote;
    }

    // Chưa đủ. Còn (window - scans) vòng nữa trong cửa sổ tiêu chuẩn; nếu có
    // phát hiện hết ngần ấy vòng mà vẫn không đủ số lần thì huỷ ngay, không
    // nuôi thêm vòng nào — cửa sổ chờ cũng ăn điểm dấu như quỹ đạo thật, để nó
    // sống thừa là cướp điểm dấu của quỹ đạo khác.
    //
    // Một phép tính lo cả ba tiêu chuẩn: 2/2 và 3/3 hụt một vòng là chết ngay,
    // còn 2/3 thì vòng giữa được phép trượt.
    if (hits + (window - t.scans) < m_params.initHits())
        return CandOutcome::Drop;

    return CandOutcome::Wait;
}

bool Tracker::closeTrack(Track &t, qint64 nowMs)
{
    closeCommon(t, nowMs);

    // Trạng thái 1 (khởi tạo) chỉ sống đúng vòng vừa đạt tiêu chuẩn; từ vòng
    // này trở đi là 3 (đang bám) hoặc 5 (ngoại suy).
    t.status = t.misses == 0 ? TrackStatus::Tracking : TrackStatus::Coasting;

    // Ngoại suy đủ số vòng cho phép mà vẫn không có điểm dấu thì thôi.
    if (t.misses > m_params.coastScans)
        t.status = TrackStatus::Deleted;

    // Dải vận tốc xét cả sau khi đã khởi tạo, không chỉ một lần lúc đó: quỹ đạo
    // bắt nhầm điểm dấu của mục tiêu khác thì vận tốc chạy loạn, mà nếu chỉ xét
    // lúc khởi tạo thì nó sống tới lúc bay ra khỏi vùng phủ. Đòi hai vòng liên
    // tiếp mới xoá, để một lần cập nhật xấu đơn lẻ không giết mất quỹ đạo đang
    // bám tốt.
    if (t.hasVelocity) {
        const double v = t.speedMs();
        if (v < m_params.vMin || v > m_params.vMax) {
            if (++t.outOfBand >= 2)
                t.status = TrackStatus::Deleted;
        } else {
            t.outOfBand = 0;
        }
    }

    // Ra khỏi vùng phủ của đài thì xoá luôn, không chờ hết số vòng ngoại suy:
    // ngoài đó không còn điểm dấu nào để cập nhật. Chừa 10% lề để quỹ đạo đang
    // bám sát vòng cự ly tối đa không bị xoá oan vì sai số đo.
    if (t.rangeM() > m_maxRangeM * 1.1)
        t.status = TrackStatus::Deleted;

    if (t.status == TrackStatus::Deleted)
        return true;

    // Vết lịch sử ghi mỗi vòng quét một điểm, kể cả vòng ngoại suy — trắc thủ
    // cần thấy chỗ nào bám thật, chỗ nào là thuật toán tự đoán.
    pushHistory(t);
    armRound(t);
    return false;
}

void Tracker::closeDueRounds(qint64 nowMs)
{
    for (int i = m_cand.size() - 1; i >= 0; --i) {
        if (m_sweepDeg < m_cand[i].closeAtDeg)
            continue;

        Track &t = m_cand[i];
        switch (closeCandidate(t, nowMs)) {
        case CandOutcome::Promote:
            // Đạt tiêu chuẩn: từ giờ mới là quỹ đạo — mới hiển thị, mới có mặt
            // trong bảng danh sách, mới được gửi đi.
            pushHistory(t);
            armRound(t);
            m_tracks.push_back(t);
            m_cand.removeAt(i);
            m_dirty = true;
            break;
        case CandOutcome::Drop:
            // Huỷ cửa sổ dự đoán. Lặng lẽ: nó chưa từng được báo ra ngoài nên
            // không có gói trạng thái "xoá" nào phải gửi.
            m_cand.removeAt(i);
            break;
        case CandOutcome::Wait:
            // Vẫn ghi vết: cửa sổ này mà chín thành quỹ đạo thì trắc thủ thấy
            // được cả quãng đường dẫn tới lúc khởi tạo, không phải một hình tam
            // giác hiện ra từ hư không.
            pushHistory(t);
            armRound(t);
            break;
        }
    }

    for (int i = m_tracks.size() - 1; i >= 0; --i) {
        if (m_sweepDeg < m_tracks[i].closeAtDeg)
            continue;

        Track &t = m_tracks[i];
        if (closeTrack(t, nowMs)) {
            t.serial = ++m_serial;
            t.timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
            m_removed.push_back(t);
            m_tracks.removeAt(i);
        }
        m_dirty = true;
    }
}

void Tracker::expireStale(qint64 nowMs)
{
    const qint64 limit = qint64(m_params.staleSec) * 1000;

    // Cửa sổ chờ cũng phải quá hạn được: đường quét ngừng quay thì không có gì
    // chốt sổ cho chúng, mà mỗi cái vẫn chiếm một chỗ trong hạn maxTracks.
    for (int i = m_cand.size() - 1; i >= 0; --i) {
        if (nowMs - m_cand[i].lastPlotMs > limit)
            m_cand.removeAt(i);
    }

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
