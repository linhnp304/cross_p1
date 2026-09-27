#pragma once

// Đọc hệ số bộ lọc từ các file văn bản trong thư mục ./filter.
//
// Để ở tầng proc chứ không ở tầng ui, cùng lối với proc/adf4159.h: cửa sổ
// "Điều khiển các bộ lọc" chỉ hiện bảng và bấm nút, còn việc biến một file văn
// bản thành dãy Filter[] là thuật toán thuần — không đụng Qt Widgets, nên kiểm
// thử riêng được.
//
// Bố cục file và số dòng phải đọc thì lấy từ net/filterproto.h: chúng là hệ quả
// của gói tin, xem chú thích ở đầu file ấy.

#include <QString>
#include <QVector>

namespace filterdata {

/// Thư mục dữ liệu bộ lọc: ./filter cạnh file chạy.
QString dirPath();

/// Đường dẫn đầy đủ của một file dữ liệu.
QString filePath(const char *fileName);

/// Tạo thư mục ./filter nếu chưa có. Gọi một lần lúc phần mềm khởi động: người
/// dùng phải tự copy file dữ liệu vào đó, mà thư mục chưa tồn tại thì không có
/// chỗ nào chỉ ra rằng phải copy vào đâu.
bool ensureDir();

/// Kết quả đọc một file.
struct Result {
    /// Giá trị Filter[], đúng `count` phần tử khi đọc được. Rỗng khi có lỗi.
    QVector<quint32> values;

    /// Rỗng là đọc được. Khác rỗng là câu thông báo đã sẵn sàng hiện cho người
    /// dùng — thiếu file, thiếu dòng, hay một dòng không đọc ra số hex.
    QString error;

    bool ok() const { return error.isEmpty(); }
};

/// Đọc `lines` dòng đầu của file, ghép `linesPerValue` dòng thành một phần tử
/// Filter[].
///
/// Mỗi dòng là một số **hex** (cho phép cả tiền tố 0x, cho phép khoảng trắng hai
/// đầu — file mẫu căn phải bằng dấu cách). Dòng thứ `lines + 1` trở đi **không
/// đọc tới**: các file mẫu đều có một dòng chú thích ở cuối ("hamming",
/// "kaiser 0.5"...), đó không phải lỗi.
///
/// Ghép hai dòng thì dòng trước thành nửa cao, đúng mô tả giao thức:
///
///     Filter[0] = ((dòng1 & 0xffff) << 16) + dòng2
Result load(const QString &path, int count, int linesPerValue);

} // namespace filterdata
