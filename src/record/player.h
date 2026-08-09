#pragma once

// Phát lại (tái hiện) một file ghi lưu.
//
// Cũng như phần ghi, việc đọc đĩa chạy trên **một luồng riêng**. Luồng đó giữ
// một "đồng hồ ảo" chạy nhanh/chậm theo tốc độ tái hiện đang chọn, và chỉ đẩy
// bản ghi sang luồng giao diện khi mốc thời gian của bản ghi đã tới hạn. Nhờ
// vậy nhịp phát lại bám theo đúng mốc thời gian ghi trong file chứ không theo
// tốc độ đọc đĩa, và luồng giao diện chỉ việc vét hàng đợi theo nhịp vẽ y như
// khi nhận dữ liệu thật.
//
// Hàng đợi có trần. Đầy thì luồng đọc **dừng cả đồng hồ ảo** chứ không bỏ bản
// ghi: phát lại mà mất dữ liệu thì không còn là tái hiện nữa. Hệ quả là ở tốc
// độ cao, nếu máy không kịp xử lý thì nhịp phát lại tự chậm lại đúng bằng mức
// máy chịu được — chậm nhưng đủ, hơn là nhanh nhưng thủng.

#include "record/recordfile.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <deque>

class QThread;
class PlayerWorker;

/// Hàng đợi bản ghi đã tới hạn phát: luồng đọc đĩa đẩy vào, luồng giao diện vét
/// ra theo nhịp vẽ.
class PlayQueue
{
public:
    explicit PlayQueue(qint64 capacityBytes = 16 * 1024 * 1024)
        : m_capacityBytes(capacityBytes) {}

    /// Đẩy vào nếu còn chỗ. False nghĩa là đầy — bên gọi phải dừng đồng hồ ảo
    /// lại chứ không được bỏ bản ghi đi.
    bool push(const rec::RecItem &it);

    void drain(std::deque<rec::RecItem> &out);
    void clear();
    bool isEmpty() const;

private:
    mutable QMutex m_mutex;
    std::deque<rec::RecItem> m_queue;
    qint64 m_bytes = 0;
    qint64 m_capacityBytes;
};

/// Thống kê hiện lên tab "Ghi lưu" lúc đang phát lại.
struct PlayerStats {
    bool    running   = false;
    qint64  virtualMs = 0;   ///< mốc thời gian đang tái hiện (ms từ epoch)
    quint64 played    = 0;   ///< số bản ghi đã đẩy sang luồng giao diện
    quint32 total     = 0;   ///< tổng số bản ghi của file, lấy từ header
};

class Player : public QObject
{
    Q_OBJECT

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    /// Bắt đầu phát lại. `total` chỉ để hiện tiến độ, lấy từ header của file.
    void start(const QString &path, quint32 total, double speed);

    /// Đổi tốc độ giữa chừng.
    void setSpeed(double speed);

    void stop();

    bool isRunning() const { return m_running; }

    /// Lấy các bản ghi đã tới hạn phát. Gọi từ luồng giao diện theo nhịp vẽ.
    void drain(std::deque<rec::RecItem> &out) { m_queue.drain(out); }

    PlayerStats stats() const;

signals:
    void failed(const QString &message);

    /// Hết file **và** hàng đợi đã cạn — lúc này màn hình đã hiện hết mọi thứ
    /// có trong file, nút bấm mới nên trở về "Bắt đầu phát lại".
    void reachedEnd();

private:
    QThread      *m_thread = nullptr;
    PlayerWorker *m_worker = nullptr;

    PlayQueue            m_queue;
    std::atomic<qint64>  m_virtualMs{0};
    std::atomic<quint64> m_played{0};

    bool    m_running = false;
    quint32 m_total   = 0;
};
