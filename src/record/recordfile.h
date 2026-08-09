#pragma once

// Định dạng file ghi lưu (.rec) và hàng đợi đưa bản ghi từ các luồng sinh dữ
// liệu sang luồng ghi đĩa.
//
// Một file gồm hai phần:
//
//   Header  64 byte cố định — 16 từ 4 byte, little-endian như mọi gói tin khác
//   Data    các bản ghi nối đuôi nhau, mỗi bản ghi có 16 byte tiêu đề riêng
//
// Tiêu đề mỗi bản ghi mang **độ dài** phần nội dung, nên đọc file là nhảy thẳng
// từ bản ghi này sang bản ghi kế tiếp mà không phải hiểu nội dung của nó. Nhờ
// vậy thêm một loại dữ liệu mới sau này không làm hỏng các bản đọc cũ: gặp mã
// loại lạ thì bỏ qua đúng số byte đã ghi rồi đi tiếp.
//
// Nội dung bản ghi giữ **nguyên dạng gói tin trên đường truyền** với RAW_V,
// RAW_P, PlotTC và Track. Ghi lại đúng cái đã nhận / đã gửi thì lúc phát lại
// không phải tin vào một phép chuyển đổi trung gian nào, và công cụ giải mã gói
// tin có sẵn dùng được luôn cho file ghi lưu.

#include <QByteArray>
#include <QMutex>
#include <QtEndian>

#include <atomic>
#include <deque>

namespace rec {

/// Định danh file do phần mềm này sinh ra.
inline constexpr quint32 kMagic = 0x6969cafeu;

/// Phân loại file — nằm ngay trong header đề phòng người dùng đổi tên file.
inline constexpr quint32 kKindRaw  = 0xcafe1122u;   ///< dữ liệu gốc
inline constexpr quint32 kKindProc = 0xcafe3344u;   ///< dữ liệu đã qua xử lý

inline constexpr int kHeaderWords = 16;
inline constexpr int kHeaderBytes = kHeaderWords * 4;   // 64

/// Số hiệu phiên bản định dạng, nằm ở một ô dự phòng của header. Đổi cấu trúc
/// phần Data sau này thì tăng số này lên để bản đọc cũ biết mà từ chối.
inline constexpr quint32 kFormatVersion = 1;

/// Loại bản ghi trong phần Data.
enum class RecType : quint32 {
    RawV  = 1,   ///< nguyên datagram RAW_V
    RawP  = 2,   ///< nguyên datagram RAW_P
    Video = 3,   ///< phương vị + nền tạp Video[1024] đã quy về 0..255
    Plot  = 4,   ///< nguyên gói tin PlotTC
    Track = 5,   ///< nguyên gói tin Track
    Other = 6,   ///< datagram không thuộc loại nào ở trên (trạng thái hệ thống,
                 ///< trạng thái lệnh điều khiển... — giai đoạn sau mới giải mã)
};

/// Bản ghi loại nào thì vào file nào. Dữ liệu gốc chỉ có RAW_V và RAW_P; mọi
/// thứ còn lại là dữ liệu đã qua xử lý.
inline bool isRawType(RecType t)
{
    return t == RecType::RawV || t == RecType::RawP;
}

/// Tiêu đề mỗi bản ghi: 4 byte loại, 4 byte độ dài nội dung, 8 byte thời điểm
/// (ms từ epoch, UTC). Thời điểm để 8 byte chứ không phải 4 như header file:
/// đây là mốc dùng để đặt nhịp phát lại nên phải có phần mili giây.
inline constexpr int kRecHeadBytes = 16;

/// Nội dung bản ghi Video: 4 byte phương vị + 1024 byte biên độ.
inline constexpr int kVideoBins  = 1024;
inline constexpr int kVideoBytes = 4 + kVideoBins;

/// Chặn trần độ dài một bản ghi lúc đọc, để file hỏng không làm phần mềm xin
/// cấp phát vài GB rồi chết.
inline constexpr int kMaxPayloadBytes = 1 << 20;   // 1 MB

// --- đọc/ghi số nguyên little-endian ---------------------------------------

inline void putU32(char *p, quint32 v)
{
    qToLittleEndian(v, reinterpret_cast<uchar *>(p));
}

inline void putU64(char *p, quint64 v)
{
    qToLittleEndian(v, reinterpret_cast<uchar *>(p));
}

inline quint32 getU32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

inline quint64 getU64(const char *p)
{
    return qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(p));
}

/// Header của file ghi lưu.
///
/// Thứ tự trường đúng theo bảng mô tả giai đoạn. Hai ô `video` và `version` lấy
/// từ phần dự phòng: số bản ghi nền tạp phải đếm riêng, nếu không thì tổng số
/// bản ghi của file dữ liệu đã xử lý lớn gấp hàng nghìn lần tổng ba loại còn
/// lại mà không có gì giải thích.
struct FileHeader {
    quint32 kind     = 0;
    quint32 startSec = 0;   ///< giây từ epoch, UTC
    quint32 endSec   = 0;
    quint32 total    = 0;

    quint32 rawV  = 0;
    quint32 rawP  = 0;
    quint32 plot  = 0;
    quint32 track = 0;
    quint32 other = 0;
    quint32 video = 0;

    quint32 version = kFormatVersion;

    bool isRaw()  const { return kind == kKindRaw; }
    bool isProc() const { return kind == kKindProc; }

    /// Đếm thêm một bản ghi vào đúng ô của nó.
    void count(RecType t);
};

/// 64 byte header đã xếp sẵn, ghi thẳng ra đầu file.
QByteArray packHeader(const FileHeader &h);

/// Đọc header. False khi không đủ 64 byte, sai định danh, hoặc phân loại lạ.
bool parseHeader(const char *data, int size, FileHeader &out);

// --- hàng đợi bản ghi -------------------------------------------------------

/// Một bản ghi đang chờ xuống đĩa.
struct RecItem {
    RecType    type   = RecType::Other;
    qint64     timeMs = 0;      ///< ms từ epoch, UTC
    QByteArray payload;
};

/// "Bộ đệm 2" của thiết kế: giữ dữ liệu để ghi lưu, tách hẳn khỏi bộ đệm hiển
/// thị.
///
/// Bên đẩy vào là luồng mạng (RAW_V/RAW_P/nền tạp) và luồng giao diện (điểm dấu,
/// quỹ đạo); bên lấy ra là luồng ghi đĩa. Đầy thì **bỏ bản ghi cũ nhất** chứ
/// không chặn: đĩa chậm một nhịp mà làm nghẽn luồng mạng thì mất luôn cả dữ liệu
/// đang hiển thị, hỏng nặng hơn nhiều so với một lỗ hổng trong file ghi lưu.
class RecordSpool
{
public:
    explicit RecordSpool(qint64 capacityBytes = 64 * 1024 * 1024)
        : m_capacityBytes(capacityBytes) {}

    /// Hai ô đánh dấu trên giao diện, đọc được từ mọi luồng. Bên sinh dữ liệu
    /// hỏi trước rồi mới đóng gói, nên loại nào không ghi thì không tốn gì cả.
    bool wantsRaw() const  { return m_wantRaw.load(std::memory_order_relaxed); }
    bool wantsProc() const { return m_wantProc.load(std::memory_order_relaxed); }
    void setWants(bool raw, bool proc);

    /// True khi ít nhất một trong hai loại đang được ghi.
    bool active() const { return wantsRaw() || wantsProc(); }

    void push(RecType type, qint64 timeMs, const QByteArray &payload);

    /// Lấy hết những gì đang có. Nội dung cũ của `out` bị bỏ đi.
    void drain(std::deque<RecItem> &out);

    void clear();

    quint64 dropped() const { return m_dropped.load(std::memory_order_relaxed); }

private:
    mutable QMutex m_mutex;
    std::deque<RecItem> m_queue;
    qint64 m_bytes = 0;
    qint64 m_capacityBytes;

    std::atomic<bool>    m_wantRaw{false};
    std::atomic<bool>    m_wantProc{false};
    std::atomic<quint64> m_dropped{0};
};

/// Đóng gói một lượt quét thành nội dung bản ghi Video.
QByteArray packVideo(quint32 azimuth, const quint8 *video);

/// Tách bản ghi Video ra lại. False nếu độ dài không đúng.
bool unpackVideo(const QByteArray &payload, quint32 &azimuth, quint8 *video);

} // namespace rec
