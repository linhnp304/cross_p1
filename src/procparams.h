#pragma once

// Tham số của hai thuật toán xử lý: tính tâm chùm xung và bám quỹ đạo.
//
// Cả hai nhóm đều sửa được lúc đang chạy (hai cửa sổ mở từ tab "Tham số") và
// đều lưu vào params.json. Để riêng ra khỏi AppParams để hai thuật toán chỉ
// phải nhận đúng phần của mình, không kéo theo cả danh sách cổng UDP.

#include <QtGlobal>

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
    double gateRangeM  = 40.0;   ///< cửa sổ cự ly cơ sở (mét)
    double gateSigma   = 2.0;    ///< số lần độ lệch chuẩn cộng thêm vào cửa sổ
    double gateMaxM    = 400.0;  ///< trần cửa sổ cự ly (mét)

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
