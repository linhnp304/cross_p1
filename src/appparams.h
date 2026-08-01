#pragma once

#include "procparams.h"

#include <QString>
#include <QVector>

/// Loại dữ liệu của một dòng trong bảng cổng gửi.
enum class TxKind {
    Plot,    ///< điểm dấu tâm chùm (PlotTC)
    Track,   ///< quỹ đạo
};

/// Một điểm kết nối UDP trong bảng "Kết nối".
///
/// Dùng chung cho cả bảng nhận và bảng gửi; mỗi bảng chỉ đọc phần trường của
/// mình. Để trống (hoặc 0.0.0.0 / cổng 0) ở phía remote nghĩa là **nhận từ bất
/// kỳ** máy nào, cổng nào — tiện lúc thử với công cụ tạo giả dữ liệu, vì công cụ
/// đó gửi đi từ một cổng nguồn ngẫu nhiên.
struct NetEndpoint {
    QString name;                                   ///< nhãn, ví dụ UDP-RAW_V
    QString localIp   = QStringLiteral("127.0.0.1");
    QString remoteIp  = QStringLiteral("127.0.0.1");
    quint16 localPort  = 0;
    quint16 remotePort = 0;

    // --- chỉ dùng cho bảng gửi ---

    /// Ô "Gửi". **Cố ý không lưu xuống file**: mỗi lần chạy đều bắt đầu ở trạng
    /// thái không gửi, để mở phần mềm lên là không tự phát gói ra mạng.
    bool   enabled = false;
    TxKind kind    = TxKind::Plot;

    /// Gửi tới địa chỉ quảng bá của dải chứa RemoteIP thay vì gửi đơn hướng.
    bool   broadcast = false;

    bool acceptsAnyHost() const;
    bool acceptsAnyPort() const { return remotePort == 0; }
};

/// Tham số kỹ thuật và danh sách cổng, lưu trong params.json cạnh file chạy.
///
/// Tách khỏi AppSettings (mx01.json) vì hai nhóm thuộc về hai người khác nhau:
/// AppSettings là cấu hình hiển thị của trắc thủ, còn đây là tham số của người
/// lắp đặt hệ thống.
struct AppParams {
    // --- tham số kỹ thuật ---
    double  fs     = 1.0;      ///< tần số ADC (MHz), 0.1 .. 50
    int     b      = 154;      ///< giải thông điều tần (MHz), 1 .. 1000
    int     tc     = 2500;     ///< chu kỳ kích (us), 1 .. 100000
    quint32 zfbeat = 32768;    ///< hệ số căn chỉnh biên độ, > 0

    /// Tự lấy fs/b/tc/zfbeat từ gói trạng thái lệnh điều khiển (giai đoạn sau).
    bool autoFromStatus = false;

    /// Tự tính cự ly tối đa từ fs/b/tc mỗi khi tham số đổi.
    bool autoRange = false;

    // --- xử lý theo rẻ quạt ---
    /// Chỉ xử lý RAW_P trong một góc rẻ quạt. Tính theo chiều kim đồng hồ từ
    /// góc bắt đầu tới góc kết thúc, nên "bắt đầu > kết thúc" là rẻ quạt vắt
    /// qua hướng bắc chứ không phải nhập ngược.
    bool   sectorOn    = false;
    double sectorStart = 0.0;     ///< độ
    double sectorStop  = 360.0;   ///< độ

    // --- tham số hai thuật toán xử lý ---
    BeamParams  beam;
    TrackParams track;

    // --- danh sách cổng ---
    QVector<NetEndpoint> rx;   ///< cổng nhận
    QVector<NetEndpoint> tx;   ///< cổng gửi — dùng ở giai đoạn sau

    // --- giới hạn, dùng chung cho ô nhập và cho việc chặn giá trị từ file ---
    static constexpr double kFsMin = 0.1,   kFsMax = 50.0;
    static constexpr int    kBMin  = 1,     kBMax  = 1000;
    static constexpr int    kTcMin = 1,     kTcMax = 100000;

    /// Cự ly tối đa (mét) suy ra từ tham số. Công thức tường minh:
    ///
    ///     fb   = 1024*Fs/2048        (MHz)
    ///     Td   = (fb*Tc/B) * 10^-6   (giây)
    ///     Rmax = Td * 3*10^8 / 2     (mét)
    ///
    /// Rút gọn lại còn **Rmax = 75 * Fs * Tc / B**. Ví dụ Fs=1, B=154,
    /// Tc=2500 cho 1217.53 m.
    static double rmaxMeters(double fs, int b, int tc);
    double rmaxMeters() const { return rmaxMeters(fs, b, tc); }

    /// Cự ly tối đa (km) đã làm tròn tới mét, đúng độ chính xác của ô nhập.
    double rmaxKm() const;

    /// Ba dòng cổng nhận mặc định khi chưa có file tham số.
    static QVector<NetEndpoint> defaultRx();

    /// File tham số nằm ngay cạnh file chạy, giống file cấu hình.
    static QString filePath();

    /// Nạp từ đĩa. Trả về false nếu chưa có file / file hỏng — khi đó các
    /// trường giữ nguyên giá trị mặc định.
    bool load();

    bool save() const;

    /// Kẹp mọi trường về dải hợp lệ. Gọi sau khi nạp file bị sửa tay.
    void clampToRange();
};
