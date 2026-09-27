#pragma once

#include "app/appparams.h"
#include "proc/plottrack.h"
#include "net/rawpacket.h"
#include "record/recordfile.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QVector>

#include <atomic>
#include <deque>

class QThread;
class UdpWorker;

/// Bộ đệm lượt quét dùng chung giữa luồng mạng và luồng giao diện.
///
/// Đây là "bộ đệm 1" trong thiết kế: giải mã và hiển thị. Đầy thì **bỏ gói cũ
/// nhất** chứ không chặn luồng mạng — thà mất một vệt quét cũ còn hơn để hàng
/// đợi của hệ điều hành tràn và mất cả chùm gói mới.
class SweepQueue
{
public:
    explicit SweepQueue(int capacity = 2048) : m_capacity(capacity) {}

    void push(const rawpkt::RawVSweep &s);

    /// Lấy hết những gì đang có. `out` bị ghi đè.
    void drain(QVector<rawpkt::RawVSweep> &out);

    void clear();

    quint64 received() const { return m_received.load(std::memory_order_relaxed); }
    quint64 dropped() const  { return m_dropped.load(std::memory_order_relaxed); }

private:
    mutable QMutex m_mutex;
    std::deque<rawpkt::RawVSweep> m_queue;
    int m_capacity;

    std::atomic<quint64> m_received{0};
    std::atomic<quint64> m_dropped{0};
};

/// Bộ đệm chu kỳ RAW_P, cùng nguyên tắc với SweepQueue.
///
/// Để riêng khỏi SweepQueue chứ không gộp thành một hàng đợi chung: hai loại
/// gói có cỡ khác nhau hàng chục lần, mà trần của hàng đợi thì tính theo số
/// phần tử — gộp lại là một chu kỳ RAW_P 300 byte lại chiếm chỗ như một lượt
/// quét RAW_V 1 KB.
class PlotQueue
{
public:
    explicit PlotQueue(int capacity = 4096) : m_capacity(capacity) {}

    void push(const rawpkt::RawPCycle &c);
    void drain(QVector<rawpkt::RawPCycle> &out);
    void clear();

    quint64 dropped() const { return m_dropped.load(std::memory_order_relaxed); }

private:
    mutable QMutex m_mutex;
    std::deque<rawpkt::RawPCycle> m_queue;
    int m_capacity;

    std::atomic<quint64> m_dropped{0};
};

/// Bộ đệm điểm dấu tâm chùm nhận **từ ngoài** — gói PlotTC do hệ thống khác
/// (hoặc công cụ tạo giả dữ liệu) gửi tới, không phải do bộ tách chùm ở đây
/// tính ra. Cùng nguyên tắc bỏ phần tử cũ nhất với hai hàng đợi trên.
class PlotTcQueue
{
public:
    explicit PlotTcQueue(int capacity = 4096) : m_capacity(capacity) {}

    void push(const PlotTC &p);
    void drain(QVector<PlotTC> &out);
    void clear();

    quint64 dropped() const { return m_dropped.load(std::memory_order_relaxed); }

private:
    mutable QMutex m_mutex;
    std::deque<PlotTC> m_queue;
    int m_capacity;

    std::atomic<quint64> m_dropped{0};
};

/// Bộ đệm gói trạng thái lệnh điều khiển nhận ở cổng "Status", và gói chiếm
/// quyền điều khiển CTRL_SYNC nhận ở cổng "CtrlSync_R".
///
/// Giữ nguyên dạng datagram: việc tách trường thuộc về tab "Điều khiển", cửa sổ
/// ADF4159, cửa sổ bộ lọc — mà luồng mạng thì không nên biết gì về giao diện.
/// Hai loại đi chung một hàng đợi vì cả hai đều là "một datagram cần tới luồng
/// giao diện nguyên vẹn", và cả hai đều chỉ vài gói mỗi giây nên trần để nhỏ.
class StatusQueue
{
public:
    explicit StatusQueue(int capacity = 256) : m_capacity(capacity) {}

    void push(const QByteArray &datagram);
    void drain(QVector<QByteArray> &out);
    void clear();

private:
    mutable QMutex m_mutex;
    std::deque<QByteArray> m_queue;
    int m_capacity;
};

/// Bộ đếm gói: luồng mạng ghi, luồng giao diện đọc.
struct LinkCounters {
    std::atomic<quint64> rawV{0};
    std::atomic<quint64> rawP{0};
    std::atomic<quint64> plotTc{0};
    std::atomic<quint64> status{0};
    std::atomic<quint64> ctrlSync{0};
    std::atomic<quint64> other{0};

    void reset()
    {
        rawV.store(0);
        rawP.store(0);
        plotTc.store(0);
        status.store(0);
        ctrlSync.store(0);
        other.store(0);
    }
};

/// Thống kê hiện lên giao diện.
struct LinkStats {
    quint64 rawV      = 0;   ///< số gói RAW_V nhận được
    quint64 rawP      = 0;   ///< số gói RAW_P (giai đoạn sau mới giải mã)
    quint64 plotTc    = 0;   ///< số gói PlotTC nhận từ ngoài
    quint64 status    = 0;   ///< số gói trạng thái lệnh điều khiển
    quint64 ctrlSync  = 0;   ///< số gói CTRL_SYNC (kể cả gói của chính máy mình)
    quint64 other     = 0;   ///< datagram không khớp giao thức nào
    quint64 droppedV  = 0;   ///< lượt quét bị bỏ vì bộ đệm hiển thị đầy
    quint64 droppedP  = 0;   ///< chu kỳ RAW_P bị bỏ vì bộ đệm đầy
    quint64 droppedPlotTc = 0;  ///< điểm dấu ngoài bị bỏ vì bộ đệm đầy
    quint64 droppedRec = 0;  ///< gói bị bỏ vì bộ đệm ghi lưu đầy
};

/// Quản lý việc nhận dữ liệu UDP.
///
/// Toàn bộ việc đọc socket và giải mã chạy trên một luồng riêng: ~400 gói mỗi
/// giây, mỗi gói 4 KB, nếu để trên luồng giao diện thì mỗi lần vẽ lại panel là
/// một lần nguy cơ mất gói. Luồng giao diện chỉ việc gọi drain() theo nhịp vẽ.
class UdpLink : public QObject
{
    Q_OBJECT

public:
    explicit UdpLink(QObject *parent = nullptr);
    ~UdpLink() override;

    /// Mở các cổng trong danh sách. Cổng nào mở hỏng thì báo qua failed(),
    /// các cổng còn lại vẫn chạy.
    void start(const QVector<NetEndpoint> &endpoints);
    void stop();
    bool isRunning() const { return m_running; }

    /// Cách quy Data_V[] về thang 0..255, đổi được cả khi đang chạy.
    void setVideoScale(const rawpkt::VideoScale &s);

    /// "Bộ đệm 2" của thiết kế: nơi đẩy dữ liệu sang luồng ghi lưu. Con trỏ
    /// mượn, phải sống lâu hơn đối tượng này. Đặt một lần lúc khởi tạo; lúc
    /// không ghi lưu thì hai cờ trong hàng đợi đều tắt và đường này không tốn gì.
    void setSpool(rec::RecordSpool *spool);

    /// Lấy các lượt quét nhận được từ lần gọi trước. Gọi từ luồng giao diện.
    void drain(QVector<rawpkt::RawVSweep> &out) { m_sweeps.drain(out); }

    /// Tương tự cho các chu kỳ RAW_P.
    void drainPlots(QVector<rawpkt::RawPCycle> &out) { m_cycles.drain(out); }

    /// Điểm dấu tâm chùm nhận từ ngoài dưới dạng gói PlotTC.
    void drainPlotTc(QVector<PlotTC> &out) { m_plotTc.drain(out); }

    /// Gói trạng thái lệnh điều khiển và gói CTRL_SYNC, còn nguyên dạng datagram.
    void drainStatus(QVector<QByteArray> &out) { m_status.drain(out); }

    LinkStats stats() const;

signals:
    void runningChanged(bool on);

    /// Lỗi mở cổng — nội dung đã sẵn sàng để hiện cho người dùng.
    void failed(const QString &message);

private:
    QThread   *m_thread = nullptr;
    UdpWorker *m_worker = nullptr;
    bool       m_running = false;

    SweepQueue        m_sweeps;
    PlotQueue         m_cycles;
    PlotTcQueue       m_plotTc;
    StatusQueue       m_status;
    rec::RecordSpool *m_spool = nullptr;
    LinkCounters      m_counters;
};
