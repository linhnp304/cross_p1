#pragma once

// Tham số của hai thuật toán xử lý: tính tâm chùm xung và bám quỹ đạo.
//
// Cả hai nhóm đều sửa được lúc đang chạy (hai cửa sổ mở từ tab "Tham số") và
// đều lưu vào params.json. Để riêng ra khỏi AppParams để hai thuật toán chỉ
// phải nhận đúng phần của mình, không kéo theo cả danh sách cổng UDP.

#include <QVector>
#include <QtGlobal>

#include <cmath>

/// Chuẩn hoá một góc về 0..360.
inline double normalizeDeg360(double a)
{
    a = std::fmod(a, 360.0);
    return a < 0.0 ? a + 360.0 : a;
}

/// Một rẻ quạt xử lý.
///
/// Góc tính theo **chiều kim đồng hồ** từ `startDeg` tới `stopDeg`, nên
/// "bắt đầu > kết thúc" là rẻ quạt vắt qua hướng bắc chứ không phải nhập ngược.
struct Sector {
    bool   on       = false;
    double startDeg = 0.0;
    double stopDeg  = 360.0;

    bool contains(double deg) const
    {
        const double a = normalizeDeg360(deg);
        const double s = normalizeDeg360(startDeg);
        // 360 độ phải giữ nguyên là 360 chứ không chuẩn hoá về 0: rẻ quạt
        // 0..360 là cả vòng tròn, mà 0..0 thì không chứa gì ngoài đúng hướng bắc.
        const double e = stopDeg >= 360.0 ? 360.0 : normalizeDeg360(stopDeg);
        if (s <= e)
            return a >= s && a <= e;
        return a >= s || a <= e;
    }
};

/// Một vùng cấm khởi tạo quỹ đạo: mảnh hình quạt giữa hai phương vị và hai cự ly.
///
/// Bảng trên giao diện luôn giữ `azm1` là mép **đầu** theo chiều kim đồng hồ và
/// `range1 <= range2`, nên ở đây chỉ còn việc so.
struct NoInitZone {
    bool   on     = false;
    double azm1   = 0.0;    ///< phương vị đầu (độ)
    double azm2   = 0.0;    ///< phương vị cuối (độ), xuôi kim đồng hồ từ azm1
    double range1 = 0.0;    ///< cự ly gần (mét)
    double range2 = 0.0;    ///< cự ly xa (mét)

    bool contains(double azmDeg, double rangeM) const
    {
        if (rangeM < range1 || rangeM > range2)
            return false;
        return Sector{true, azm1, azm2}.contains(azmDeg);
    }
};

/// Hai rẻ quạt có chồng lấn nhau không.
///
/// Chạm nhau đúng ở một mép (30..90 và 90..120) **không** tính là chồng lấn —
/// nếu tính thì không thể chia vòng tròn thành các rẻ quạt liền nhau được.
inline bool sectorsOverlap(const Sector &a, const Sector &b)
{
    // Tách mỗi rẻ quạt thành các đoạn thẳng trên [0,360): rẻ quạt vắt qua hướng
    // bắc thành hai đoạn. So từng cặp đoạn là xong, khỏi phải lý sự vòng tròn.
    const auto split = [](const Sector &s, double lo[2], double hi[2]) {
        const double a0 = normalizeDeg360(s.startDeg);
        const double a1 = s.stopDeg >= 360.0 ? 360.0 : normalizeDeg360(s.stopDeg);
        if (a0 <= a1) {
            lo[0] = a0; hi[0] = a1;
            lo[1] = hi[1] = 0.0;
            return 1;
        }
        lo[0] = a0;  hi[0] = 360.0;
        lo[1] = 0.0; hi[1] = a1;
        return 2;
    };

    double al[2], ah[2], bl[2], bh[2];
    const int na = split(a, al, ah);
    const int nb = split(b, bl, bh);
    for (int i = 0; i < na; ++i) {
        for (int j = 0; j < nb; ++j) {
            if (al[i] < bh[j] && bl[j] < ah[i])
                return true;
        }
    }
    return false;
}

/// Rẻ quạt mặc định khi chưa có file tham số: một dòng cả vòng tròn, chưa bật.
inline QVector<Sector> defaultSectors()
{
    return {Sector{}};
}

/// True khi `deg` được phép xử lý: không rẻ quạt nào đang bật thì xử lý tất,
/// còn có thì phải nằm trong ít nhất một rẻ quạt đang bật.
inline bool sectorsAllow(const QVector<Sector> &list, double deg)
{
    bool anyOn = false;
    for (const Sector &s : list) {
        if (!s.on)
            continue;
        anyOn = true;
        if (s.contains(deg))
            return true;
    }
    return !anyOn;
}

/// Tham số tính tâm chùm xung. Giá trị mặc định lấy đúng theo PlotTC.c.
struct BeamParams {
    // --- tiêu chuẩn độ dài chùm ---
    int cxMin = 5;      ///< CX_MIN — dưới mức này không tạo điểm dấu
    int cxMax = 100;    ///< CX_MAX — trên mức này cũng không tạo điểm dấu

    // --- tiêu chuẩn dopler: quá nhỏ hoặc quá lớn thường là địa vật ---
    int doplerMin = 2;  ///< CX_DOPLER_MIN
    int doplerMax = 29; ///< CX_DOPLER_MAX

    // --- tiêu chuẩn xét duyệt một plot vào chùm đang mở ---
    int deltaRange  = 1;  ///< CX_DELTA_RANGE, tính bằng ô cự ly
    int deltaDopler = 1;  ///< CX_DELTA_DOPLER

    // --- tiêu chuẩn khởi tạo / kết thúc chùm ---
    int numWait = 2;    ///< CX_NUM_WAIT — số chu kỳ liên tiếp có plot thì mở chùm
    int numLose = 4;    ///< CX_NUM_LOSE — số chu kỳ liên tiếp mất plot thì đóng chùm

    /// CX_USE_AMPLITUDE — tính tâm chùm có trọng số biên độ phản xạ.
    bool useAmplitude = false;

    // --- hiệu chỉnh tâm chùm ---
    //
    // Cộng thêm vào **sau khi** đã tính xong tâm chùm, ngay trước lúc gán vào
    // gói PlotTC. Để bù sai lệch lắp đặt (gốc phương vị của encoder, trễ đường
    // truyền cao tần) mà không phải đụng vào chính thuật toán.

    /// Bù phương vị (độ). Kết quả quay vòng về 0..360.
    double azmOffsetDeg = 0.0;

    /// Bù cự ly (mét). Kết quả âm thì gán = 0.
    double rangeOffsetM = 0.0;

    /// Số giây giữ một điểm dấu trên màn hình trước khi xoá.
    int showSec = 8;

    /// Chặn mọi trường về dải hợp lệ. Gọi sau khi nạp file bị sửa tay.
    void clamp()
    {
        cxMin       = qBound(1, cxMin, 255);
        cxMax       = qBound(cxMin, cxMax, 255);
        doplerMin   = qBound(0, doplerMin, 31);
        doplerMax   = qBound(doplerMin, doplerMax, 31);
        deltaRange  = qBound(0, deltaRange, 64);
        deltaDopler = qBound(0, deltaDopler, 31);
        numWait     = qBound(1, numWait, 64);
        numLose     = qBound(1, numLose, 64);
        showSec     = qBound(1, showSec, 120);

        azmOffsetDeg = qBound(-360.0, azmOffsetDeg, 360.0);
        rangeOffsetM = qBound(-1000000.0, rangeOffsetM, 1000000.0);
    }
};

/// Tiêu chuẩn khởi tạo quỹ đạo — bao nhiêu lần phát hiện trong bao nhiêu vòng
/// quét liên tiếp thì một quỹ đạo mới được coi là thật.
enum class TrackInitRule {
    R2of2,   ///< 2 vòng liên tiếp
    R3of3,   ///< 3 vòng liên tiếp
    R2of3,   ///< 2 trong 3 vòng liên tiếp
};

/// Tham số bộ lọc Kalman và quản lý danh sách quỹ đạo.
///
/// Bộ lọc là loại vận tốc không đổi trong hệ Đề-các cục bộ (x đông, y bắc),
/// nên các tham số nhiễu cũng phát biểu theo cách đó: sai số đo ở dạng cực
/// (cự ly / phương vị, đúng cách ra đa đo), nhiễu quá trình ở dạng gia tốc.
struct TrackParams {
    // --- khởi tạo và kết thúc quỹ đạo ---
    TrackInitRule initRule  = TrackInitRule::R2of3;
    int           coastScans = 3;   ///< số vòng ngoại suy trước khi xoá

    // --- dải vận tốc quan tâm (m/s) ---
    // Mục tiêu của đài trải từ ~1 tới ~120 m/s. Thu hẹp dải lại là cách nhanh
    // nhất để loại bớt quỹ đạo rác khi chỉ đang quan tâm một loại mục tiêu.
    double vMin = 0.5;
    double vMax = 120.0;

    // --- cửa sổ liên kết điểm dấu với quỹ đạo ---
    //
    // Bán kính cửa sổ = gateRangeM + gateSigma × (độ lệch chuẩn vị trí dự đoán).
    // Số hạng đầu là phần sàn không phụ thuộc bộ lọc, số hạng sau tự co giãn
    // theo mức chắc chắn của quỹ đạo.
    double gateRangeM  = 40.0;   ///< cửa sổ cự ly cơ sở (mét)
    double gateSigma   = 2.0;    ///< số lần độ lệch chuẩn cộng thêm vào cửa sổ

    /// Trần cửa sổ cự ly (mét) — chỉ áp cho quỹ đạo **đang bám đều**.
    ///
    /// Ba lúc khác thì trần này không áp, vì áp vào là mất quỹ đạo: vòng đầu
    /// tiên (chưa biết vận tốc nên cửa sổ phải mở cực đại), lúc ngoại suy, và
    /// lúc vừa phát hiện mục tiêu cơ động. Cả ba lúc đó cửa sổ chỉ còn bị chặn
    /// bởi **trần động học** vMax × thời gian trôi qua — quãng đường xa nhất mà
    /// mục tiêu nhanh nhất trong dải quan tâm có thể đi được.
    double gateMaxM    = 400.0;

    /// Cửa sổ phương vị cơ sở (độ). **Không** tham gia việc ghép điểm dấu —
    /// cửa sổ liên kết xét theo khoảng cách Đề-các. Chỉ nới thêm hình cửa sổ
    /// dự đoán vẽ trên màn hình và các trường window trong gói tin Track.
    double gateAzmDeg  = 3.0;

    // --- sai số phép đo của đài ---
    double sigmaRangeM = 8.0;    ///< độ lệch chuẩn cự ly (mét)
    double sigmaAzmDeg = 0.6;    ///< độ lệch chuẩn phương vị (độ)

    // --- nhiễu quá trình ---
    double sigmaAccel = 1.0;     ///< gia tốc ngẫu nhiên của mục tiêu (m/s²)

    /// Bao lâu không có điểm dấu nào ghép vào thì tự xoá quỹ đạo (giây).
    ///
    /// Chặn theo **thời gian**, độc lập với số vòng ngoại suy: ăng-ten quay
    /// chậm lại hay ngừng quay thì số vòng đếm mãi không tới, mà quỹ đạo thì
    /// vẫn nằm đó trên màn hình.
    int staleSec = 40;

    /// Vẽ cửa sổ dự đoán lên panel 1.
    bool drawWindow = false;

    // --- hai chốt an toàn, chỉ sửa trong params.json, không đưa lên giao diện ---

    /// Trần số quỹ đạo quản lý cùng lúc. Gặp nhiễu dày hoặc địa vật động thì
    /// danh sách phình ra vô hạn, mà mỗi điểm dấu phải quét qua toàn bộ danh
    /// sách — càng phình càng chậm, càng chậm càng phình.
    int maxTracks = 200;

    /// Cự ly nhỏ nhất (mét) cho phép **khởi tạo** quỹ đạo mới. Quanh tâm đài
    /// là chỗ địa vật mạnh nhất. Không chặn việc cập nhật quỹ đạo đã có: mục
    /// tiêu bay qua tâm đài vẫn được bám tiếp.
    double minInitRangeM = 50.0;

    void clamp()
    {
        coastScans  = qBound(1, coastScans, 20);
        vMin        = qBound(0.0, vMin, 1000.0);
        vMax        = qBound(vMin, vMax, 1000.0);
        gateRangeM  = qBound(1.0, gateRangeM, 5000.0);
        gateAzmDeg  = qBound(0.1, gateAzmDeg, 90.0);
        gateSigma   = qBound(0.0, gateSigma, 20.0);
        gateMaxM    = qBound(gateRangeM, gateMaxM, 10000.0);
        sigmaRangeM = qBound(0.1, sigmaRangeM, 1000.0);
        sigmaAzmDeg = qBound(0.01, sigmaAzmDeg, 30.0);
        sigmaAccel  = qBound(0.01, sigmaAccel, 100.0);
        staleSec    = qBound(1, staleSec, 3600);

        maxTracks     = qBound(1, maxTracks, 5000);
        minInitRangeM = qBound(0.0, minInitRangeM, 100000.0);
    }

    /// Số vòng quét của cửa sổ tiêu chuẩn khởi tạo.
    int initWindow() const { return initRule == TrackInitRule::R2of2 ? 2 : 3; }

    /// Số lần phát hiện cần có trong cửa sổ đó.
    int initHits() const { return initRule == TrackInitRule::R3of3 ? 3 : 2; }
};
