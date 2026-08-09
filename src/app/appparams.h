#pragma once

#include "net/cmdproto.h"
#include "proc/procparams.h"
#include "net/rawpacket.h"

#include <QString>
#include <QStringList>
#include <QVector>

/// Loại dữ liệu của một dòng trong bảng cổng gửi.
enum class TxKind {
    Plot,     ///< điểm dấu tâm chùm (PlotTC)
    Track,    ///< quỹ đạo
    Command,  ///< lệnh điều khiển đài (tab "Điều khiển")
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
    /// thái không gửi, để mở phần mềm lên là không tự phát dòng dữ liệu nào ra
    /// mạng.
    ///
    /// Dòng Command thì ngược lại — **luôn bật**, và ô đánh dấu của nó bị khoá
    /// trên giao diện. Có dòng Command trong bảng nghĩa là đã khai đường gửi
    /// lệnh, không có trạng thái nào ở giữa: lệnh chỉ đi ra khi trắc thủ tự tay
    /// vặn một nút chứ không chảy liên tục, nên không có gì phải đề phòng.
    bool   enabled = false;
    TxKind kind    = TxKind::Plot;

    /// Dòng này có được mở socket không.
    bool sends() const { return enabled || kind == TxKind::Command; }

    /// Gửi tới địa chỉ quảng bá của dải chứa RemoteIP thay vì gửi đơn hướng.
    bool   broadcast = false;

    bool acceptsAnyHost() const;
    bool acceptsAnyPort() const { return remotePort == 0; }
};

/// Tham số kỹ thuật và danh sách cổng, lưu trong params.json cạnh file chạy.
///
/// Tách khỏi AppSettings (settings.json) vì hai nhóm thuộc về hai người khác nhau:
/// AppSettings là cấu hình hiển thị của trắc thủ, còn đây là tham số của người
/// lắp đặt hệ thống.
struct AppParams {
    // --- tham số kỹ thuật ---
    double fs = 1.0;      ///< tần số ADC (MHz), 0.1 .. 50
    int    b  = 154;      ///< giải thông điều tần (MHz), 1 .. 1000
    int    tc = 2500;     ///< chu kỳ kích (us), 1 .. 100000

    /// Hệ số nhân video — Multi_V. Nhân thêm vào Video[1024] sau khi đã chia
    /// cho ZFbeat (hay GainU), để chỉnh độ sáng nền tạp mà không phải đụng vào
    /// tham số của đài.
    double multiV = 1.0;

    /// Tự lấy fs/b/tc từ gói trạng thái lệnh điều khiển (giai đoạn sau).
    bool autoFromStatus = false;

    /// Tự tính cự ly tối đa từ fs/b/tc mỗi khi tham số đổi.
    bool autoRange = false;

    // --- xử lý theo rẻ quạt ---
    /// Chỉ xử lý RAW_P trong các rẻ quạt đang bật. Không dòng nào bật = xử lý
    /// cả vòng tròn.
    QVector<Sector> sectors = defaultSectors();

    // --- vùng cấm khởi tạo quỹ đạo ---
    /// Điểm dấu rơi vào một vùng đang bật thì vẫn ghép được vào quỹ đạo có sẵn,
    /// chỉ **không** được mở quỹ đạo mới. Mặc định bảng rỗng.
    QVector<NoInitZone> noInitZones;

    /// Vẽ các vùng cấm khởi tạo lên panel 1.
    bool showNoInitZones = false;

    // --- tham số hai thuật toán xử lý ---
    BeamParams  beam;
    TrackParams track;

    // --- giá trị các lệnh điều khiển (tab "Điều khiển") ---
    //
    // Lưu lại để mở phần mềm lên là thấy đúng thứ mình vặn hôm trước, và để
    // cách tính Video[1024] có ZFbeat / GainU / DataSend dùng ngay từ gói đầu
    // tiên chứ không phải chờ đài gửi trạng thái về.
    cmdproto::Values control;

    // --- danh sách cổng ---
    QVector<NetEndpoint> rx;   ///< cổng nhận
    QVector<NetEndpoint> tx;   ///< cổng gửi

    // --- giới hạn, dùng chung cho ô nhập và cho việc chặn giá trị từ file ---
    static constexpr double kFsMin = 0.1,   kFsMax = 50.0;
    static constexpr int    kBMin  = 1,     kBMax  = 1000;
    static constexpr int    kTcMin = 1,     kTcMax = 100000;

    /// Multi_V để 0 là màn hình đen, để quá lớn là trắng xoá — vẫn cho cả hai,
    /// chỉ chặn số âm và số vô lý.
    static constexpr double kMultiVMin = 0.0, kMultiVMax = 1000.0;

    /// Cách quy Data_V[] về thang 0..255, gom từ tham số và từ lệnh điều khiển.
    rawpkt::VideoScale videoScale() const;

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

    /// Dòng cổng gửi mặc định khi chưa có file tham số: một dòng "Command" —
    /// không có nó thì cú vặn nút đầu tiên trong tab "Điều khiển" chẳng đi tới đâu.
    static QVector<NetEndpoint> defaultTx();

    /// Nhãn ba loại dữ liệu của cột "Loại dữ liệu", đúng thứ tự trong ComboBox.
    static QStringList txKindNames();

    /// Bốn loại dữ liệu của cột "Tên" trong bảng cổng nhận, đúng thứ tự hiện
    /// trong ComboBox. Chỉ là gợi ý cho người dùng: việc giải mã phân loại theo
    /// nội dung gói chứ không theo tên, nên gõ tên khác vẫn chạy bình thường.
    static QStringList rxNames();

    /// Cổng nhận mặc định của loại dữ liệu chưa được tạo sẵn (Plot).
    static constexpr quint16 kDefaultPlotPort = 6004;

    /// File tham số nằm ngay cạnh file chạy, giống file cấu hình.
    static QString filePath();

    /// Nạp từ đĩa. Trả về false nếu chưa có file / file hỏng — khi đó các
    /// trường giữ nguyên giá trị mặc định.
    bool load();

    bool save() const;

    /// Kẹp mọi trường về dải hợp lệ. Gọi sau khi nạp file bị sửa tay.
    void clampToRange();
};
