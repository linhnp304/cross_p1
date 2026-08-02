#include "recordindex.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

/// Tên kết nối SQLite. Chỉ một kết nối trong cả phần mềm, và nó nằm trên luồng
/// ghi lưu — QSqlDatabase gắn kết nối với luồng đã tạo ra nó.
const char kConnName[] = "recindex";

/// File .rec mà header khai tổng số bản ghi bằng 0 là file của một phiên chết
/// giữa chừng (mất điện, phần mềm bị giết). Đếm lại bằng cách duyệt phần Data —
/// nhưng chỉ với file nhỏ, vì duyệt một file 2 GB lúc khởi động thì phần mềm
/// đứng hình trước khi kịp hiện cửa sổ.
constexpr qint64 kMaxRecoverBytes = 64 * 1024 * 1024;

/// Đếm lại các bản ghi thực sự có trong file, bắt đầu từ ngay sau header.
void recoverCounts(QFile &f, rec::FileHeader &h)
{
    if (!f.seek(rec::kHeaderBytes))
        return;

    qint64 lastMs = 0;
    QByteArray head;
    forever {
        head = f.read(rec::kRecHeadBytes);
        if (head.size() < rec::kRecHeadBytes)
            break;

        const quint32 type = rec::getU32(head.constData());
        const quint32 len  = rec::getU32(head.constData() + 4);
        if (len > quint32(rec::kMaxPayloadBytes))
            break;   // độ dài vô lý: file cụt ngay giữa một bản ghi

        // Bản ghi cuối cùng có thể chỉ ghi được một nửa — không tính nó.
        if (f.pos() + qint64(len) > f.size())
            break;
        if (!f.seek(f.pos() + qint64(len)))
            break;

        lastMs = qint64(rec::getU64(head.constData() + 8));
        h.count(rec::RecType(type));
    }

    if (lastMs > 0)
        h.endSec = quint32(lastMs / 1000);
}

} // namespace

QString RecSession::label() const
{
    return QDateTime::fromSecsSinceEpoch(startSec)
        .toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
}

// ------------------------------------------------------------ thư mục ------

QString RecordIndex::rootDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/records");
}

QString RecordIndex::dayDir(const QDateTime &at, QString &error)
{
    const QString path = rootDir() + QLatin1Char('/')
                       + at.toString(QStringLiteral("yyyy/MM/dd"));
    if (!QDir().mkpath(path)) {
        error = QObject::tr("Không tạo được thư mục ghi lưu %1").arg(path);
        return {};
    }
    return path;
}

// -------------------------------------------------------------- danh mục ---

RecordIndex::~RecordIndex()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (!m_conn.isEmpty())
        QSqlDatabase::removeDatabase(m_conn);
}

bool RecordIndex::open(QString &error)
{
    if (m_db.isOpen())
        return true;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        error = QObject::tr("Thiếu trình điều khiển SQLite của Qt (QSQLITE) — "
                            "danh sách phiên sẽ không dùng được. Trên Ubuntu "
                            "cài thêm gói libqt6sql6-sqlite.");
        return false;
    }

    const QString dir = rootDir();
    if (!QDir().mkpath(dir)) {
        error = QObject::tr("Không tạo được thư mục %1").arg(dir);
        return false;
    }

    m_conn = QString::fromLatin1(kConnName);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dir + QStringLiteral("/index.db"));
    if (!m_db.open()) {
        error = QObject::tr("Không mở được danh mục phiên: %1")
                    .arg(m_db.lastError().text());
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_conn);
        m_conn.clear();
        return false;
    }

    QSqlQuery q(m_db);
    // Đường dẫn làm khoá chính: một file là một phiên, và đó cũng chính là thứ
    // duy nhất phải khớp giữa bảng và đĩa.
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sessions ("
            " path TEXT PRIMARY KEY,"
            " kind INTEGER NOT NULL,"
            " startSec INTEGER NOT NULL,"
            " endSec INTEGER NOT NULL,"
            " total INTEGER NOT NULL,"
            " rawV INTEGER NOT NULL, rawP INTEGER NOT NULL,"
            " video INTEGER NOT NULL, plot INTEGER NOT NULL,"
            " track INTEGER NOT NULL, other INTEGER NOT NULL,"
            " bytes INTEGER NOT NULL)"))) {
        error = QObject::tr("Không dựng được bảng danh mục: %1")
                    .arg(q.lastError().text());
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_kind_start ON sessions(kind, startSec)"));
    return true;
}

bool RecordIndex::probe(const QString &path, RecSession &out, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Không mở được %1").arg(path);
        return false;
    }

    const QByteArray head = f.read(rec::kHeaderBytes);
    rec::FileHeader h;
    if (!rec::parseHeader(head.constData(), head.size(), h)) {
        error = QObject::tr("%1 không phải file ghi lưu của phần mềm này")
                    .arg(QFileInfo(path).fileName());
        return false;
    }
    if (h.version > rec::kFormatVersion) {
        error = QObject::tr("%1 ghi bằng phiên bản định dạng mới hơn (%2) — "
                            "phần mềm này chưa đọc được")
                    .arg(QFileInfo(path).fileName()).arg(h.version);
        return false;
    }

    if (h.total == 0 && f.size() > rec::kHeaderBytes
        && f.size() <= kMaxRecoverBytes) {
        recoverCounts(f, h);
    }

    out.path     = QFileInfo(path).absoluteFilePath();
    out.kind     = h.kind;
    out.startSec = h.startSec;
    out.endSec   = h.endSec;
    out.total    = h.total;
    out.rawV     = h.rawV;
    out.rawP     = h.rawP;
    out.video    = h.video;
    out.plot     = h.plot;
    out.track    = h.track;
    out.other    = h.other;
    out.bytes    = f.size();
    return true;
}

void RecordIndex::rescan()
{
    if (!m_db.isOpen())
        return;

    // Kích thước đang lưu trong bảng: file nào không đổi cỡ thì bỏ qua, khỏi mở
    // ra đọc header. Với vài nghìn phiên thì đây là khác biệt giữa "mở phần mềm
    // là thấy danh sách" và "chờ vài giây".
    QHash<QString, qint64> known;
    QSqlQuery sel(m_db);
    if (sel.exec(QStringLiteral("SELECT path, bytes FROM sessions"))) {
        while (sel.next())
            known.insert(sel.value(0).toString(), sel.value(1).toLongLong());
    }

    QSet<QString> found;
    m_db.transaction();

    QDirIterator it(rootDir(), {QStringLiteral("*.rec")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = QFileInfo(it.next()).absoluteFilePath();
        found.insert(path);

        const auto seen = known.constFind(path);
        if (seen != known.constEnd() && *seen == QFileInfo(path).size())
            continue;

        RecSession s;
        QString error;
        if (probe(path, s, error))
            upsert(s);
    }

    // Hàng của file đã bị xoá tay thì bỏ đi — đĩa mới là nguồn đúng.
    for (auto k = known.constBegin(); k != known.constEnd(); ++k) {
        if (found.contains(k.key()))
            continue;
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM sessions WHERE path = ?"));
        del.addBindValue(k.key());
        del.exec();
    }

    m_db.commit();
}

void RecordIndex::upsert(const RecSession &s)
{
    if (!m_db.isOpen())
        return;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions"
        " (path, kind, startSec, endSec, total, rawV, rawP, video, plot,"
        "  track, other, bytes)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET"
        "  kind=excluded.kind, startSec=excluded.startSec,"
        "  endSec=excluded.endSec, total=excluded.total,"
        "  rawV=excluded.rawV, rawP=excluded.rawP, video=excluded.video,"
        "  plot=excluded.plot, track=excluded.track, other=excluded.other,"
        "  bytes=excluded.bytes"));
    q.addBindValue(s.path);
    q.addBindValue(qlonglong(s.kind));
    q.addBindValue(qlonglong(s.startSec));
    q.addBindValue(qlonglong(s.endSec));
    q.addBindValue(qlonglong(s.total));
    q.addBindValue(qlonglong(s.rawV));
    q.addBindValue(qlonglong(s.rawP));
    q.addBindValue(qlonglong(s.video));
    q.addBindValue(qlonglong(s.plot));
    q.addBindValue(qlonglong(s.track));
    q.addBindValue(qlonglong(s.other));
    q.addBindValue(qlonglong(s.bytes));
    q.exec();
}

QVector<RecSession> RecordIndex::sessions()
{
    QVector<RecSession> out;
    if (!m_db.isOpen())
        return out;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT path, kind, startSec, endSec, total, rawV, rawP, video,"
            " plot, track, other, bytes FROM sessions ORDER BY startSec DESC")))
        return out;

    while (q.next()) {
        RecSession s;
        s.path     = q.value(0).toString();
        s.kind     = quint32(q.value(1).toULongLong());
        s.startSec = q.value(2).toLongLong();
        s.endSec   = q.value(3).toLongLong();
        s.total    = quint32(q.value(4).toULongLong());
        s.rawV     = quint32(q.value(5).toULongLong());
        s.rawP     = quint32(q.value(6).toULongLong());
        s.video    = quint32(q.value(7).toULongLong());
        s.plot     = quint32(q.value(8).toULongLong());
        s.track    = quint32(q.value(9).toULongLong());
        s.other    = quint32(q.value(10).toULongLong());
        s.bytes    = q.value(11).toLongLong();
        out.push_back(s);
    }
    return out;
}
