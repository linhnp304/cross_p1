#include "player.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QTimer>

namespace {

/// Nhịp của luồng đọc. Đủ mịn để nhịp phát lại không giật ở tốc độ 1x (một nhịp
/// ứng với ~2 gói RAW_V), mà vẫn xa mức làm luồng đó bận rộn vô ích.
constexpr int kStepMs = 5;

/// Trần số bản ghi đẩy sang trong một nhịp. Sau một lúc bị nghẽn, đồng hồ ảo có
/// thể tụt lại khá xa; không có trần này thì nhịp kế tiếp phụt cả chục nghìn bản
/// ghi vào hàng đợi và luồng giao diện khựng nguyên một lượt vẽ.
constexpr int kMaxPerStep = 2000;

/// Khoảng trống trong mốc thời gian mà quá mức này thì nhảy thẳng đồng hồ ảo
/// tới bản ghi kế tiếp. Xảy ra khi trắc thủ bật ghi lưu rồi dừng nhận dữ liệu
/// một lúc: không có nó thì phát lại ngồi im đúng bằng khoảng thời gian đó.
constexpr qint64 kMaxGapMs = 2000;

} // namespace

// ------------------------------------------------------------ hàng đợi -----

bool PlayQueue::push(const rec::RecItem &it)
{
    QMutexLocker lock(&m_mutex);
    if (m_bytes >= m_capacityBytes)
        return false;
    m_queue.push_back(it);
    m_bytes += it.payload.size() + rec::kRecHeadBytes;
    return true;
}

void PlayQueue::drain(std::deque<rec::RecItem> &out)
{
    QMutexLocker lock(&m_mutex);
    out.swap(m_queue);
    m_queue.clear();
    m_bytes = 0;
}

void PlayQueue::clear()
{
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
    m_bytes = 0;
}

bool PlayQueue::isEmpty() const
{
    QMutexLocker lock(&m_mutex);
    return m_queue.empty();
}

// -------------------------------------------------------- luồng đọc đĩa ----

/// Sống trọn đời trong luồng phát lại: mở file, đọc từng bản ghi, giữ đồng hồ
/// ảo và đẩy sang hàng đợi khi tới hạn.
class PlayerWorker : public QObject
{
    Q_OBJECT

public:
    PlayerWorker(PlayQueue *queue, std::atomic<qint64> *virtualMs,
                 std::atomic<quint64> *played)
        : m_queue(queue), m_virtualMs(virtualMs), m_played(played)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(kStepMs);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &PlayerWorker::onStep);
    }

    void begin(const QString &path);
    void end();

    /// Chỉ là một phép ghi atomic nên gọi thẳng từ luồng nào cũng được.
    void setSpeed(double v) { m_speed.store(v, std::memory_order_relaxed); }

signals:
    void failed(const QString &message);
    void reachedEnd();

private:
    void onStep();

    /// Đọc bản ghi kế tiếp vào `m_next`. False khi hết file hoặc file cụt.
    bool readNext();

    PlayQueue            *m_queue;
    std::atomic<qint64>  *m_virtualMs;
    std::atomic<quint64> *m_played;

    QTimer *m_timer = nullptr;
    QFile   m_file;

    rec::RecItem m_next;      ///< bản ghi đã đọc nhưng chưa tới hạn phát
    bool         m_hasNext = false;
    bool         m_eof     = false;

    std::atomic<double> m_speed{1.0};

    /// Đồng hồ thật, để biết đồng hồ ảo phải tiến bao nhiêu mỗi nhịp.
    QElapsedTimer m_wall;
    qint64        m_lastWallMs = 0;
    qint64        m_clockMs    = 0;   ///< đồng hồ ảo
};

bool PlayerWorker::readNext()
{
    char head[rec::kRecHeadBytes];
    if (m_file.read(head, rec::kRecHeadBytes) != rec::kRecHeadBytes)
        return false;

    const quint32 len = rec::getU32(head + 4);
    if (len > quint32(rec::kMaxPayloadBytes))
        return false;   // độ dài vô lý: file hỏng, dừng ở đây

    m_next.payload = m_file.read(qint64(len));
    if (m_next.payload.size() != int(len))
        return false;   // file cụt giữa một bản ghi

    m_next.type   = rec::RecType(rec::getU32(head));
    m_next.timeMs = qint64(rec::getU64(head + 8));
    return true;
}

void PlayerWorker::begin(const QString &path)
{
    // Dọn phần còn lại của lần phát trước. Bình thường end() đã lo, nhưng lần
    // phát chạy tới hết file thì không ai gọi end() cả — mà QFile đang mở thì
    // setFileName() không đổi được tên, và lần phát sau sẽ hỏng câm lặng.
    end();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        emit failed(QObject::tr("Không mở được %1 — %2")
                        .arg(path, m_file.errorString()));
        emit reachedEnd();
        return;
    }

    // Bỏ qua 64 byte header: nơi gọi đã đọc và kiểm tra nó trước khi tới đây.
    if (!m_file.seek(rec::kHeaderBytes)) {
        emit failed(QObject::tr("%1 không đủ dài để là file ghi lưu").arg(path));
        m_file.close();
        emit reachedEnd();
        return;
    }

    m_eof     = false;
    m_hasNext = readNext();
    if (!m_hasNext) {
        emit failed(QObject::tr("%1 không có bản ghi nào").arg(path));
        m_file.close();
        emit reachedEnd();
        return;
    }

    // Đồng hồ ảo bắt đầu ngay tại bản ghi đầu tiên, nên bấm phát là thấy dữ
    // liệu ngay chứ không phải chờ.
    m_clockMs = m_next.timeMs;
    m_virtualMs->store(m_clockMs, std::memory_order_relaxed);
    m_played->store(0, std::memory_order_relaxed);

    m_wall.start();
    m_lastWallMs = 0;
    m_timer->start();
}

void PlayerWorker::end()
{
    m_timer->stop();
    if (m_file.isOpen())
        m_file.close();
    m_hasNext = false;
    m_eof     = false;
}

void PlayerWorker::onStep()
{
    const qint64 wall = m_wall.elapsed();
    const qint64 dt   = wall - m_lastWallMs;
    m_lastWallMs = wall;

    // Hết file thì còn phải chờ luồng giao diện tiêu hoá nốt hàng đợi — lúc ấy
    // mới thực sự là "đã tái hiện xong".
    if (m_eof) {
        if (m_queue->isEmpty()) {
            m_timer->stop();
            if (m_file.isOpen())
                m_file.close();
            m_eof = false;
            emit reachedEnd();
        }
        return;
    }

    const double speed = m_speed.load(std::memory_order_relaxed);
    m_clockMs += qint64(double(dt) * speed);

    // Khoảng trống dài giữa hai bản ghi thì tua thẳng tới bản ghi kế tiếp.
    if (m_hasNext && m_next.timeMs - m_clockMs > kMaxGapMs)
        m_clockMs = m_next.timeMs;

    int pushed = 0;
    while (m_hasNext && m_next.timeMs <= m_clockMs && pushed < kMaxPerStep) {
        if (!m_queue->push(m_next)) {
            // Hàng đợi đầy: lùi đồng hồ ảo về đúng chỗ đang đứng để nhịp sau
            // không phụt ra một lô bù. Phát lại chậm lại, nhưng không thủng.
            m_clockMs = m_next.timeMs;
            break;
        }
        ++pushed;
        m_hasNext = readNext();
    }

    if (pushed > 0) {
        m_played->fetch_add(quint64(pushed), std::memory_order_relaxed);
        m_virtualMs->store(m_clockMs, std::memory_order_relaxed);
    }

    if (!m_hasNext)
        m_eof = true;
}

// --------------------------------------------------------------- Player ----

Player::Player(QObject *parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("rec-play"));

    m_worker = new PlayerWorker(&m_queue, &m_virtualMs, &m_played);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &PlayerWorker::failed, this, &Player::failed);
    connect(m_worker, &PlayerWorker::reachedEnd, this, [this] {
        m_running = false;
        emit reachedEnd();
    });

    m_thread->start();
}

Player::~Player()
{
    stop();
    m_thread->quit();
    m_thread->wait();
}

void Player::start(const QString &path, quint32 total, double speed)
{
    if (m_running)
        return;

    m_queue.clear();
    m_virtualMs.store(0);
    m_played.store(0);
    m_total   = total;
    m_running = true;

    m_worker->setSpeed(speed > 0.0 ? speed : 1.0);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, path] { w->begin(path); },
                              Qt::QueuedConnection);
}

void Player::setSpeed(double speed)
{
    m_worker->setSpeed(speed > 0.0 ? speed : 1.0);
}

void Player::stop()
{
    if (!m_running)
        return;

    // Chờ luồng đọc đóng file xong rồi mới trả về, giống phía nhận dữ liệu:
    // bấm "Dừng phát lại" là không còn bản ghi nào rơi vào hàng đợi sau đó.
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->end(); },
                              Qt::BlockingQueuedConnection);
    m_queue.clear();
    m_running = false;
}

PlayerStats Player::stats() const
{
    PlayerStats s;
    s.running   = m_running;
    s.virtualMs = m_virtualMs.load(std::memory_order_relaxed);
    s.played    = m_played.load(std::memory_order_relaxed);
    s.total     = m_total;
    return s;
}

#include "player.moc"
