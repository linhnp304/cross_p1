#pragma once

#include <QString>

/// Nhận dạng của phần mềm — tên hiển thị, tên file cấu hình, biến môi trường.
///
/// Gom về một chỗ để dùng bộ code này làm gốc cho dự án khác: tách dự án mới
/// chỉ phải sửa đúng file này và `project(...)` trong CMakeLists.txt, không
/// phải đi tìm tên rải rác trong code. Xem mục "Tách dự án mới" trong README.
namespace appinfo {

/// Tên hiện trên giao diện: tiêu đề cửa sổ, tiêu đề mặc định của hộp thoại.
/// Viết tiếng Việt có dấu được — ví dụ "Ra đa tầm gần X123".
inline QString displayName() { return QStringLiteral("AR01.01"); }

/// Tên đơn vị. Qt dùng khi phải tự chọn thư mục dữ liệu riêng cho phần mềm
/// (QStandardPaths). Phần mềm này để cấu hình cạnh file chạy nên ít dùng tới.
inline QString organizationName() { return QStringLiteral("MX"); }

/// Tên file cấu hình JSON, đặt ngay cạnh file chạy. Đây là tên file thật trên
/// đĩa nên **chỉ dùng chữ không dấu**.
inline QString configFileName() { return QStringLiteral("mx01.json"); }

/// Tên file tham số kỹ thuật và danh sách cổng UDP, cũng đặt cạnh file chạy.
/// Tách khỏi configFileName() vì hai nhóm này thuộc về hai người khác nhau:
/// cấu hình hiển thị là của trắc thủ, tham số là của người lắp đặt hệ thống.
inline QString paramsFileName() { return QStringLiteral("params.json"); }

/// Biến môi trường đè lên đường dẫn thư mục bản đồ. Cũng chỉ chữ không dấu.
inline const char *tilesDirEnvVar() { return "MX01_TILES_DIR"; }

} // namespace appinfo
