#pragma once

#include <QCache>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QVector>

/// Một kiểu nền bản đồ đã có sẵn trên đĩa.
struct TileSetInfo {
    QString id;      ///< tên thư mục con, cũng là mã style của MapTiler
    QString label;   ///< tên hiển thị cho người dùng
};

/// Đọc tile bản đồ đã tải sẵn trên đĩa (cấu trúc XYZ: <thư mục>/<z>/<x>/<y>.png)
/// và giữ lại một ít trong bộ nhớ để khỏi đọc đĩa liên tục khi vẽ.
///
/// Bộ tile do tools/download_tiles.py sinh ra, kèm file tileset.json mô tả
/// cỡ ảnh và dải mức phóng — nhờ vậy phần vẽ không phải đoán.
class TileCache
{
public:
    TileCache();

    /// Liệt kê các kiểu nền có trong thư mục gốc — mỗi thư mục con có
    /// tileset.json là một kiểu. Nhờ vậy tải thêm style là phần mềm tự có thêm
    /// lựa chọn, không phải sửa code. Kết quả sắp theo tên hiển thị.
    static QVector<TileSetInfo> listStyles(const QString &baseDir);

    /// Mở một thư mục tile. Trả về false nếu thư mục không có tileset.json —
    /// khi đó directory() vẫn giữ đường dẫn vừa thử, để báo lỗi cho đúng chỗ.
    /// Đường dẫn lấy từ AppSettings::resolvedStyleDir().
    bool open(const QString &dir);

    bool isValid() const { return m_valid; }
    int  tileSize() const { return m_tileSize; }
    int  minZoom() const { return m_minZoom; }
    int  maxZoom() const { return m_maxZoom; }
    QString attribution() const { return m_attribution; }
    QString directory() const { return m_dir; }

    /// Trả về tile, hoặc QPixmap rỗng nếu không có trên đĩa.
    QPixmap tile(int z, int x, int y);

private:
    static quint64 packKey(int z, int x, int y);

    QString m_dir;
    QString m_format = QStringLiteral("png");
    QString m_attribution;
    int     m_tileSize = 256;
    int     m_minZoom = 0;
    int     m_maxZoom = 0;
    bool    m_valid = false;

    QCache<quint64, QPixmap> m_cache;
    QSet<quint64>            m_missing;   ///< tránh hỏi lại đĩa về tile không có
};
