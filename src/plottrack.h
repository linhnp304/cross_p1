#pragma once

// Hai đối tượng trung tâm của giai đoạn 4: điểm dấu tâm chùm (PlotTC) và quỹ
// đạo (Track).
//
// Tên và **đơn vị** của các trường lấy đúng theo mô tả gói tin PlotTC / Track
// trong tài liệu giai đoạn: phương vị 0.01 độ, cự ly 0.1 mét, vận tốc 0.1 m/s.
// Giữ nguyên đơn vị của giao thức ngay trong cấu trúc để khi làm chức năng gửi
// đi hệ thống khác (giai đoạn sau) chỉ còn việc xếp trường vào gói, không phải
// đi tìm xem chỗ nào đã đổi đơn vị.

#include <QVector>
#include <QtGlobal>

/// Trạng thái quỹ đạo — đúng mã của trường track_status.
///
/// Thuật toán hiện dùng 4 trong 6 mã: Init khi mới khởi tạo và chưa đủ tiêu
/// chuẩn, Tracking khi đang bám, Coasting khi ngoại suy, Deleted khi xoá.
/// Confirmed và Lost để sẵn cho các bước làm mịn về sau.
enum class TrackStatus : quint32 {
    Init      = 1,   ///< khởi tạo, chưa đủ tiêu chuẩn để coi là quỹ đạo thật
    Confirmed = 2,   ///< khẳng định
    Tracking  = 3,   ///< đang bám
    Lost      = 4,   ///< tạm mất
    Coasting  = 5,   ///< ngoại suy
    Deleted   = 6,   ///< xoá
};

/// Loại quỹ đạo — trường track_type.
enum class TrackType : quint32 {
    Radar    = 1,
    RadarIff = 2,   ///< hợp nhất với điểm dấu IFF (hệ thống hiện tại chưa có)
};

/// Điểm dấu tâm chùm.
///
/// Các trường IFF luôn bằng 0 ở hệ thống hiện tại nhưng vẫn giữ trong cấu trúc
/// để khỏi phải sửa lại nơi dùng khi lắp thêm IFF.
struct PlotTC {
    quint32 serial = 0;
    quint32 timeMs = 0;          ///< ms of day, gán khi tính xong tâm chùm

    quint32 azm    = 0;          ///< phương vị, đơn vị 0.01 độ
    quint32 range  = 0;          ///< cự ly, đơn vị 0.1 mét

    quint32 iffReturnedMode = 0;
    quint32 iffCommander    = 0;
    quint32 iffFlightId     = 0;
    quint32 iffAltitude     = 0;
    quint32 iffFuelLevel    = 0;

    quint32 numCX            = 0;  ///< tổng số xung trong chùm
    quint32 azmStart         = 0;  ///< phương vị encoder đầu chùm, 0..4095
    quint32 azmStop          = 0;  ///< phương vị encoder cuối chùm
    quint32 numLoseTotal     = 0;  ///< tổng số chu kỳ mất xung
    quint32 amplitudeAverage = 0;  ///< =0 khi không dùng trọng số
    quint32 amplitudeCenter  = 0;  ///< =0 khi không dùng trọng số
    quint32 rangeStart       = 0;  ///< ô cự ly đầu chùm
    quint32 doplerStart      = 0;  ///< dopler đầu chùm

    // --- phần không nằm trong gói tin, chỉ phục vụ hiển thị -----------------

    /// Toạ độ địa lý suy ra từ azm/range và tâm đài. Tính sẵn một lần lúc tạo:
    /// panel 1 vẽ lại 25 lần mỗi giây, tính đi tính lại là phí.
    double lat = 0.0;
    double lng = 0.0;

    /// Đồng hồ đơn điệu lúc tạo, để biết khi nào hết hạn hiển thị. Không dùng
    /// timeMs vì nó quay vòng lúc nửa đêm.
    qint64 bornMs = 0;

    double azmDeg()   const { return azm * 0.01; }
    double rangeM()   const { return range * 0.1; }
    double rangeKm()  const { return range * 0.0001; }
};

/// Một vết lịch sử của quỹ đạo.
struct TrackPoint {
    double      lat    = 0.0;
    double      lng    = 0.0;
    TrackStatus status = TrackStatus::Init;   ///< trạng thái lúc để lại vết
};

/// Quỹ đạo.
///
/// Ngoài các trường của gói tin Track, cấu trúc này còn mang cả trạng thái bộ
/// lọc Kalman. Gộp làm một chứ không tách vì mọi nơi cần quỹ đạo đều cần cả
/// hai, mà tách ra thì phải giữ hai mảng song song luôn khớp chỉ số.
struct Track {
    // --- các trường của gói tin -------------------------------------------
    quint32   serial = 0;
    quint32   timeMs = 0;        ///< ms of day, gán mỗi khi cập nhật
    TrackType type   = TrackType::Radar;
    TrackStatus status = TrackStatus::Init;

    quint32 id       = 0;        ///< định danh, tăng dần, không trùng lặp
    quint32 top      = 0;        ///< số đầu tốp, mặc định = id
    quint32 azm      = 0;        ///< phương vị, 0.01 độ
    quint32 range    = 0;        ///< cự ly, 0.1 mét
    quint32 velocity = 0;        ///< vận tốc, 0.1 m/s
    quint32 heading  = 0;        ///< hướng chuyển động, 0.01 độ

    quint32 iffReturnedMode = 0;
    quint32 iffCommander    = 0;
    quint32 iffFlightId     = 0;
    quint32 iffAltitude     = 0;  ///< độ cao (mét), cũng là chỗ gán độ cao IFF
    quint32 iffFuelLevel    = 0;

    float   lat = 0.0f;
    float   lng = 0.0f;

    quint32 classify       = 0;   ///< 0 = chưa xác định
    quint32 altitudeManual = 0;   ///< độ cao người dùng nhập
    quint32 amplitude      = 0;   ///< mức năng lượng phản xạ

    /// Cửa sổ dự đoán cho vị trí tiếp theo, gán mỗi khi cập nhật.
    quint32 windowAzm1   = 0;     ///< 0.01 độ
    quint32 windowAzm2   = 0;
    quint32 windowRange1 = 0;     ///< 0.1 mét
    quint32 windowRange2 = 0;

    // --- phần chỉ dùng trong phần mềm --------------------------------------

    bool watched = false;         ///< "Theo dõi" trong bảng danh sách

    /// Vừa được cập nhật, chưa gửi đi hệ thống khác. Đặt mỗi lần các trường
    /// hiển thị đổi, xoá khi đã gửi.
    bool pendingSend = false;

    /// Vết lịch sử, cũ nhất đứng đầu. Cắt ở mức tối đa của thanh trượt.
    QVector<TrackPoint> history;

    // --- trạng thái bộ lọc Kalman ------------------------------------------
    // Hệ toạ độ Đề-các cục bộ quanh tâm đài: x hướng đông, y hướng bắc (mét).
    // Vec-tơ trạng thái [x, y, vx, vy], P là ma trận hiệp phương sai 4x4.

    double kx[4]  = {0, 0, 0, 0};
    double kp[16] = {0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0};

    /// Đồng hồ đơn điệu (ms) mà trạng thái bộ lọc đang ứng với — dùng để tính
    /// dt. Vòng ngoại suy cũng đẩy mốc này lên, nên **không** dùng nó để đo
    /// xem quỹ đạo lâu rồi không có dữ liệu thật.
    qint64 lastUpdateMs = 0;

    /// Đồng hồ đơn điệu (ms) của lần cuối cùng thực sự có điểm dấu ghép vào.
    qint64 lastPlotMs = 0;

    /// Lịch sử phát hiện theo vòng quét, bit 0 là vòng gần nhất. Dùng cho tiêu
    /// chuẩn khởi tạo kiểu "2 trong 3 vòng".
    quint32 hitMask = 0;

    int  scans      = 0;      ///< số vòng quét kể từ lúc khởi tạo
    int  misses     = 0;      ///< số vòng liên tiếp không có điểm dấu
    int  outOfBand  = 0;      ///< số vòng liên tiếp vận tốc nằm ngoài dải
    bool updated    = false;  ///< đã nhận điểm dấu trong vòng quét đang chạy
    bool hasVelocity = false; ///< đã có đủ hai điểm để ước lượng vận tốc

    double azmDeg()  const { return azm * 0.01; }
    double rangeM()  const { return range * 0.1; }
    double rangeKm() const { return range * 0.0001; }
    double speedMs() const { return velocity * 0.1; }
    double headingDeg() const { return heading * 0.01; }

    /// Độ cao hiển thị: ưu tiên giá trị người dùng nhập, sau đó tới IFF.
    quint32 altitude() const { return altitudeManual != 0 ? altitudeManual : iffAltitude; }

    bool alive() const { return status != TrackStatus::Deleted; }
};
