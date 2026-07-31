#pragma once

#include <QString>

/// Mật độ vòng tròn cự ly. Mỗi mức giữ nguyên các vòng của mức thưa hơn, và
/// mức càng dày thì nét càng mảnh.
enum class RingMode {
    Off,
    R5,     // vòng mỗi 5 km, nét đậm
    R1,     // thêm vòng mỗi 1 km
    R05,    // thêm vòng mỗi 0.5 km
    R01     // thêm vòng mỗi 0.1 km
};

/// Mật độ đường chia phương vị.
enum class AzimuthMode {
    Off,
    A30,    // đường mỗi 30 độ, nét đậm
    A10,    // thêm đường mỗi 10 độ
    A5      // thêm đường mỗi 5 độ
};

/// Mã kiểu nền của lớp bản đồ TC. Không phải tên thư mục con trong tilesDir như
/// các kiểu nền MapTiler, mà là giá trị dành riêng để chỉ sang dữ liệu vector.
inline constexpr char kTcStyleId[] = "tc";

/// Toàn bộ cấu hình người dùng, nạp/lưu ở dạng JSON.
struct AppSettings {
    /// Dải cự ly tối đa cho phép, dùng chung cho ô nhập và cho việc chặn giá
    /// trị từ file. Cận dưới thấp vì cự ly tính từ tham số có thể rất ngắn.
    static constexpr double kMinRangeKm = 0.1;
    static constexpr double kMaxRangeKm = 2000.0;

    bool        mapVisible    = true;
    int         mapBrightness = 60;         // 0..100
    double      siteLat       = 21.028;
    double      siteLng       = 105.852;
    double      maxRangeKm    = 20.0;
    RingMode    ringMode      = RingMode::R1;
    AzimuthMode azimuthMode   = AzimuthMode::A30;

    /// Số giây để một vệt nền tạp mờ hẳn. 0 = không làm mờ, giá trị mới đè lên
    /// giá trị cũ và ảnh giữ nguyên trên màn hình.
    int         videoFadeSec  = 3;         // 0..10

    /// Thư mục gốc chứa bản đồ; mỗi kiểu nền nằm trong một thư mục con mang
    /// tên style. Đường dẫn tuyệt đối thì dùng nguyên; tương đối thì tính từ
    /// thư mục chứa file chạy. Sửa trực tiếp trong JSON khi chuyển máy.
    QString     tilesDir      = QStringLiteral("maps/mt");

    /// Kiểu nền đang chọn — tên thư mục con trong tilesDir, hoặc kTcStyleId
    /// nếu đang dùng lớp bản đồ TC.
    QString     mapStyle      = QStringLiteral("basic-v2-dark");

    /// Thư mục dữ liệu shapefile của lớp bản đồ TC. Quy tắc đường dẫn giống
    /// tilesDir: tuyệt đối thì dùng nguyên, tương đối thì tính từ file chạy.
    QString     vectorDir     = QStringLiteral("maps/tc");

    /// Ẩn/hiện từng lớp của kiểu nền TC.
    bool        tcAirRoutes   = true;
    bool        tcAirports    = true;
    bool        tcRivers      = true;
    bool        tcPlaceNames  = true;
    bool        tcProvinces   = true;

    /// True khi kiểu nền đang chọn là lớp bản đồ TC (không phải tile MapTiler).
    bool isTcStyle() const { return mapStyle == QLatin1String(kTcStyleId); }

    /// File cấu hình nằm ngay cạnh file chạy, để cả bộ mang đi máy khác được.
    static QString filePath();

    /// Đường dẫn tuyệt đối tới thư mục gốc chứa bản đồ, đã áp dụng thứ tự ưu
    /// tiên: biến môi trường (appinfo::tilesDirEnvVar) > trường tilesDir.
    /// Thư mục trả về có thể chưa tồn tại — nơi gọi tự xử lý.
    QString resolvedTilesDir() const;

    /// Thư mục của kiểu nền đang chọn: resolvedTilesDir() + "/" + mapStyle.
    QString resolvedStyleDir() const;

    /// Đường dẫn tuyệt đối tới thư mục dữ liệu bản đồ TC, dò giống resolvedTilesDir().
    QString resolvedVectorDir() const;

    /// Nạp từ đĩa. Trả về false nếu chưa có file / file hỏng — khi đó
    /// các trường giữ nguyên giá trị mặc định.
    bool load();

    bool save() const;
};
