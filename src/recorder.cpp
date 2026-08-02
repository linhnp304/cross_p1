#include "recorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

namespace {

/// Nhịp vét hàng đợi xuống đĩa. Thưa hơn nhịp vẽ nhiều lần: mỗi lượt gom được
/// một lô lớn rồi ghi bằng **một** lệnh write, thay vì hàng trăm lệnh write nhỏ.
constexpr int kFlushMs = 200;

/// Bao lâu thì ghi lại 64 byte header một lần. Header chỉ thực sự đúng lúc đóng
/// file; cập nhật giữa chừng để phiên bị mất điện vẫn còn đọc được mà không phải
/// duyệt lại cả file.
constexpr qint64 kHeaderRefreshMs = 2000;

/// Ngắt file khi dung lượng đạt 2 GB — cho cả hai loại dữ liệu.
constexpr qint64 kMaxFileBytes = 2LL * 1024 * 1024 * 1024;

/// Riêng dữ liệu đã xử lý còn ngắt file mỗi 2 tiếng, tuỳ điều kiện nào đến trước.
constexpr qint64 kMaxProcMs = 2LL * 60 * 60 * 1000;

/// Tên file theo thời gian bắt đầu ghi. Trùng tên (hai file cùng bắt đầu trong
/// một giây, xảy ra khi vừa ngắt file xong) thì thêm hậu tố.
QString uniquePath(const QString &dir, const QString &prefix, const QDateTime &at)
{
    const QString stamp = at.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString path = QStringLiteral("%1/%2%3.rec").arg(dir, prefix, stamp);
    for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n)
        path = QStringLiteral("%1/%2%3_%4.rec").arg(dir, prefix, stamp).arg(n);
    return path;
}

} // namespace

// ------------------------------------------------------ luồng ghi lưu ------

/// Sống trọn đời trong luồng ghi lưu. Mở file, xếp byte, ghi đĩa, cập nhật
/// danh mục SQLite.
class RecorderWorker : public QObject
{
    Q_OBJECT

public:
    RecorderWorker(rec::RecordSpool *spool, RecorderCounters *counters)
        : m_spool(spool), m_counters(counters)
    {
        // Bộ đếm giờ là con của worker nên theo worker sang luồng ghi lưu.
        // Chỉ được **khởi động** ở bên đó, nên start() nằm trong begin().
        m_timer = new QTimer(this);
        m_timer->setInterval(kFlushMs);
        connect(m_timer, &QTimer::timeout, this, &RecorderWorker::onTick);
    }

    /// Tất cả đều gọi từ luồng giao diện qua invokeMethod.
    void begin(bool raw, bool proc);
    void end();
    void rescan();

signals:
    void failed(const QString &message);
    void sessionsChanged(const QVector<RecSession> &all);

private:
    /// Một file đang mở.
    struct Stream {
        QFile           file;
        rec::FileHeader head;
        QString         path;
        qint64          openedMs   = 0;
        qint64          lastHeadMs = 0;
        bool            wanted     = false;
    };

    void onTick();
    void drainToDisk(bool finalRound);

    bool openStream(Stream &s, quint32 kind, const QDateTime &at);
    void closeStream(Stream &s);
    void flushHeader(Stream &s);

    /// Đến lúc ngắt sang file mới chưa.
    bool needsSplit(const Stream &s, qint64 nowMs) const;

    /// Mở danh mục SQLite nếu chưa mở. Lỗi chỉ báo một lần: thiếu trình điều
    /// khiển SQLite thì việc ghi file vẫn chạy bình thường, chỉ mất danh sách.
    void ensureIndex();

    rec::RecordSpool *m_spool;
    RecorderCounters *m_counters;

    RecordIndex m_index;
    bool        m_indexTried = false;
    bool        m_indexOk    = false;

    QTimer *m_timer = nullptr;
    Stream  m_raw;
    Stream  m_proc;

    std::deque<rec::RecItem> m_batch;
    QByteArray m_rawBuf;
    QByteArray m_procBuf;
};

void RecorderWorker::ensureIndex()
{
    if (m_indexTried)
        return;
    m_indexTried = true;

    QString error;
    m_indexOk = m_index.open(error);
    if (!m_indexOk)
        emit failed(error);
}

bool RecorderWorker::openStream(Stream &s, quint32 kind, const QDateTime &at)
{
    QString error;
    const QString dir = RecordIndex::dayDir(at, error);
    if (dir.isEmpty()) {
        emit failed(error);
        return false;
    }

    s.path = uniquePath(dir, kind == rec::kKindRaw ? QStringLiteral("raw_")
                                                   : QStringLiteral("dat_"), at);
    s.file.setFileName(s.path);
    if (!s.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(QObject::tr("Không mở được file ghi lưu %1 — %2")
                        .arg(s.path, s.file.errorString()));
        return false;
    }

    s.head = rec::FileHeader{};
    s.head.kind     = kind;
    s.head.startSec = quint32(at.toSecsSinceEpoch());
    s.head.endSec   = s.head.startSec;
    if (s.file.write(rec::packHeader(s.head)) != rec::kHeaderBytes) {
        emit failed(QObject::tr("Không ghi được header của %1").arg(s.path));
        s.file.close();
        return false;
    }

    s.openedMs   = at.toMSecsSinceEpoch();
    s.lastHeadMs = s.openedMs;
    m_counters->files.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void RecorderWorker::flushHeader(Stream &s)
{
    if (!s.file.isOpen())
        return;
    const qint64 at = s.file.pos();
    if (!s.file.seek(0))
        return;
    s.file.write(rec::packHeader(s.head));
    s.file.seek(at);
}

void RecorderWorker::closeStream(Stream &s)
{
    if (!s.file.isOpen())
        return;

    flushHeader(s);
    s.file.flush();
    const qint64 bytes = s.file.size();
    s.file.close();

    ensureIndex();
    if (m_indexOk) {
        RecSession sess;
        sess.path     = QFileInfo(s.path).absoluteFilePath();
        sess.kind     = s.head.kind;
        sess.startSec = s.head.startSec;
        sess.endSec   = s.head.endSec;
        sess.total    = s.head.total;
        sess.rawV     = s.head.rawV;
        sess.rawP     = s.head.rawP;
        sess.video    = s.head.video;
        sess.plot     = s.head.plot;
        sess.track    = s.head.track;
        sess.other    = s.head.other;
        sess.bytes    = bytes;
        m_index.upsert(sess);
        emit sessionsChanged(m_index.sessions());
    }
    s.path.clear();
}

bool RecorderWorker::needsSplit(const Stream &s, qint64 nowMs) const
{
    if (!s.file.isOpen())
        return false;
    if (s.file.size() >= kMaxFileBytes)
        return true;
    // Điều kiện thời gian chỉ áp cho dữ liệu đã xử lý: dữ liệu gốc chảy nhanh
    // gấp hàng chục lần nên nó chạm trần dung lượng từ lâu trước 2 tiếng.
    return s.head.isProc() && nowMs - s.openedMs >= kMaxProcMs;
}

void RecorderWorker::begin(bool raw, bool proc)
{
    const QDateTime now = QDateTime::currentDateTime();

    m_raw.wanted  = raw;
    m_proc.wanted = proc;

    if (raw && !openStream(m_raw, rec::kKindRaw, now))
        m_raw.wanted = false;
    if (proc && !openStream(m_proc, rec::kKindProc, now))
        m_proc.wanted = false;

    // Loại nào mở file hỏng thì thôi không nhận nữa, khỏi chất đầy hàng đợi
    // bằng dữ liệu không có chỗ nào để ghi.
    m_spool->setWants(m_raw.wanted, m_proc.wanted);
    m_timer->start();
}

void RecorderWorker::end()
{
    m_timer->stop();
    drainToDisk(true);
    closeStream(m_raw);
    closeStream(m_proc);
    m_raw.wanted = m_proc.wanted = false;
    m_batch.clear();
}

void RecorderWorker::onTick()
{
    drainToDisk(false);
}

void RecorderWorker::drainToDisk(bool finalRound)
{
    m_spool->drain(m_batch);

    m_rawBuf.clear();
    m_procBuf.clear();

    for (const rec::RecItem &it : m_batch) {
        const bool isRaw = rec::isRawType(it.type);
        Stream    &s     = isRaw ? m_raw : m_proc;
        if (!s.file.isOpen())
            continue;

        QByteArray &buf = isRaw ? m_rawBuf : m_procBuf;
        char head[rec::kRecHeadBytes];
        rec::putU32(head,     quint32(it.type));
        rec::putU32(head + 4, quint32(it.payload.size()));
        rec::putU64(head + 8, quint64(it.timeMs));
        buf.append(head, rec::kRecHeadBytes);
        buf.append(it.payload);

        s.head.count(it.type);
        s.head.endSec = quint32(it.timeMs / 1000);
    }
    m_batch.clear();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    struct Job { Stream *s; QByteArray *buf; std::atomic<quint64> *records;
                 std::atomic<quint64> *bytes; quint32 kind; };
    const Job jobs[] = {
        {&m_raw,  &m_rawBuf,  &m_counters->rawRecords,  &m_counters->rawBytes,
         rec::kKindRaw},
        {&m_proc, &m_procBuf, &m_counters->procRecords, &m_counters->procBytes,
         rec::kKindProc},
    };

    for (const Job &j : jobs) {
        Stream &s = *j.s;
        if (!s.file.isOpen())
            continue;

        if (!j.buf->isEmpty()) {
            const qint64 want    = j.buf->size();
            const qint64 written = s.file.write(*j.buf);
            if (written != want) {
                emit failed(QObject::tr("Lỗi ghi %1 — %2. Dừng ghi lưu loại dữ "
                                        "liệu này.")
                                .arg(s.path, s.file.errorString()));
                closeStream(s);
                s.wanted = false;
                m_spool->setWants(m_raw.wanted, m_proc.wanted);
                continue;
            }
            j.bytes->store(quint64(s.file.size()), std::memory_order_relaxed);
            j.records->store(s.head.total, std::memory_order_relaxed);
        }

        if (finalRound)
            continue;

        if (nowMs - s.lastHeadMs >= kHeaderRefreshMs) {
            s.lastHeadMs = nowMs;
            flushHeader(s);
        }

        if (needsSplit(s, nowMs)) {
            closeStream(s);
            if (!openStream(s, j.kind, QDateTime::currentDateTime())) {
                s.wanted = false;
                m_spool->setWants(m_raw.wanted, m_proc.wanted);
            }
        }
    }
}

void RecorderWorker::rescan()
{
    ensureIndex();
    if (!m_indexOk)
        return;
    m_index.rescan();
    emit sessionsChanged(m_index.sessions());
}

// ------------------------------------------------------------- Recorder ----

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("rec-io"));

    m_worker = new RecorderWorker(&m_spool, &m_counters);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RecorderWorker::failed, this, &Recorder::failed);
    connect(m_worker, &RecorderWorker::sessionsChanged,
            this, &Recorder::sessionsChanged);

    m_thread->start();
}

Recorder::~Recorder()
{
    stop();
    m_thread->quit();
    m_thread->wait();
}

void Recorder::push(rec::RecType type, const QByteArray &payload)
{
    m_spool.push(type, QDateTime::currentMSecsSinceEpoch(), payload);
}

void Recorder::start(bool raw, bool proc)
{
    if (m_running || (!raw && !proc))
        return;

    m_spool.clear();
    m_counters.reset();

    // Hai cờ "muốn ghi loại nào" **không** bật ở đây mà để chính luồng ghi lưu
    // bật sau khi đã mở xong file. Bật trước là bên sinh dữ liệu bắt đầu đóng
    // gói ngay, trong khi chưa có file nào để ghi vào — mấy mili giây đầu tiên
    // sẽ rơi vào hàng đợi rồi bị bỏ đi lặng lẽ.
    QMetaObject::invokeMethod(m_worker, [w = m_worker, raw, proc] {
        w->begin(raw, proc);
    }, Qt::QueuedConnection);

    m_running   = true;
    m_raw       = raw;
    m_proc      = proc;
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
}

void Recorder::stop()
{
    if (!m_running)
        return;

    // Ngừng nhận thêm ngay tại đây, trước cả khi luồng ghi lưu kịp phản ứng:
    // bấm "Dừng ghi lưu" là bên sinh dữ liệu thôi đóng gói ngay lập tức.
    m_spool.setWants(false, false);

    // Chờ luồng ghi lưu đóng file xong rồi mới trả về: bấm dừng là file đã
    // hoàn chỉnh trên đĩa và phiên đã có trong danh sách, không phải "sắp có".
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->end(); },
                              Qt::BlockingQueuedConnection);

    m_running = false;
    m_raw = m_proc = false;
}

void Recorder::refreshSessions()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->rescan(); },
                              Qt::QueuedConnection);
}

RecorderStats Recorder::stats() const
{
    RecorderStats s;
    s.running     = m_running;
    s.raw         = m_raw;
    s.proc        = m_proc;
    s.elapsedMs   = m_running
                      ? QDateTime::currentMSecsSinceEpoch() - m_startedMs : 0;
    s.rawRecords  = m_counters.rawRecords.load(std::memory_order_relaxed);
    s.procRecords = m_counters.procRecords.load(std::memory_order_relaxed);
    s.rawBytes    = m_counters.rawBytes.load(std::memory_order_relaxed);
    s.procBytes   = m_counters.procBytes.load(std::memory_order_relaxed);
    s.files       = m_counters.files.load(std::memory_order_relaxed);
    s.dropped     = m_spool.dropped();
    return s;
}

#include "recorder.moc"
