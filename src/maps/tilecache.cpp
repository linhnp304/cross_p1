#include "maps/tilecache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {

/// Giữ khoảng 64 tile 512px trong bộ nhớ (chi phí tính bằng KB).
constexpr int kCacheCostKb = 64 * 1024;

/// Trần cho danh sách tile khuyết. Kéo bản đồ ra ngoài vùng đã tải sẽ liên tục
/// sinh thêm mục mới, nên phải xả định kỳ thay vì để phình mãi.
constexpr int kMaxMissing = 20000;

} // namespace

TileCache::TileCache()
    : m_cache(kCacheCostKb)
{
}

quint64 TileCache::packKey(int z, int x, int y)
{
    return (quint64(z & 0xFF) << 56) | (quint64(x & 0xFFFFFFF) << 28)
         | quint64(y & 0xFFFFFFF);
}

QVector<TileSetInfo> TileCache::listStyles(const QString &baseDir)
{
    QVector<TileSetInfo> out;
    const QDir base(baseDir);
    if (!base.exists())
        return out;

    const QStringList subs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                            QDir::Name);
    for (const QString &sub : subs) {
        QFile meta(base.filePath(sub) + QStringLiteral("/tileset.json"));
        if (!meta.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
        if (o.isEmpty())
            continue;
        out.push_back({sub, o.value(QStringLiteral("label")).toString(sub)});
    }

    std::sort(out.begin(), out.end(), [](const TileSetInfo &a, const TileSetInfo &b) {
        return a.label.localeAwareCompare(b.label) < 0;
    });
    return out;
}

bool TileCache::open(const QString &dir)
{
    m_valid = false;
    m_cache.clear();
    m_missing.clear();
    m_dir = QDir::cleanPath(dir);   // giữ cả khi mở hỏng, để nơi gọi so sánh được

    QFile meta(m_dir + QStringLiteral("/tileset.json"));
    if (!meta.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
    if (o.isEmpty())
        return false;

    m_format      = o.value(QStringLiteral("format")).toString(QStringLiteral("png"));
    m_tileSize    = o.value(QStringLiteral("tileSize")).toInt(256);
    m_minZoom     = o.value(QStringLiteral("minZoom")).toInt(0);
    m_maxZoom     = o.value(QStringLiteral("maxZoom")).toInt(0);
    m_attribution = o.value(QStringLiteral("attribution")).toString();

    if (m_tileSize <= 0 || m_maxZoom < m_minZoom)
        return false;

    m_valid = true;
    return true;
}

QPixmap TileCache::tile(int z, int x, int y)
{
    if (!m_valid || z < m_minZoom || z > m_maxZoom)
        return {};

    const quint64 key = packKey(z, x, y);
    if (QPixmap *hit = m_cache.object(key))
        return *hit;
    if (m_missing.contains(key))
        return {};

    const QString path = QStringLiteral("%1/%2/%3/%4.%5")
                             .arg(m_dir).arg(z).arg(x).arg(y).arg(m_format);

    QPixmap pm;
    if (!pm.load(path)) {
        if (m_missing.size() >= kMaxMissing)
            m_missing.clear();
        m_missing.insert(key);
        return {};
    }

    const int costKb = qMax(1, int(qint64(pm.width()) * pm.height() * 4 / 1024));
    m_cache.insert(key, new QPixmap(pm), costKb);
    return pm;
}
