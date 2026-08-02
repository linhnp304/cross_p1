#pragma once

// Ghi lưu dữ liệu xuống đĩa.
//
// Toàn bộ việc mở file, xếp byte, ghi đĩa và cập nhật danh mục SQLite chạy trên
// **một luồng riêng**. Đĩa là thứ hay khựng nhất trong cả phần mềm: một lần hệ
// điều hành đẩy bộ nhớ đệm xuống ổ cứng có thể mất hàng trăm mili giây, mà luồng
// giao diện thì phải vẽ lại 25 lần mỗi giây và luồng mạng phải kịp vét ~400 gói
// mỗi giây ra khỏi socket. Hai bên nối với nhau qua RecordSpool, không bên nào
// phải chờ bên nào.
//
// Bên sinh dữ liệu hỏi wantsRaw()/wantsProc() trước rồi mới đóng gói, nên lúc
// không ghi lưu thì đường dữ liệu này không tốn gì cả.

#include "recordfile.h"
#include "recordindex.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

class QThread;
class RecorderWorker;

/// Bộ đếm dùng chung: luồng ghi lưu viết, luồng giao diện đọc.
struct RecorderCounters {
    std::atomic<quint64> rawRecords{0};
    std::atomic<quint64> procRecords{0};
    std::atomic<quint64> rawBytes{0};
    std::atomic<quint64> procBytes{0};
    std::atomic<quint32> files{0};        ///< số file đã mở trong phiên này

    void reset()
    {
        rawRecords.store(0);
        procRecords.store(0);
        rawBytes.store(0);
        procBytes.store(0);
        files.store(0);
    }
};

/// Thống kê hiện lên tab "Ghi lưu".
struct RecorderStats {
    bool    running     = false;
    bool    raw         = false;   ///< đang ghi dữ liệu gốc
    bool    proc        = false;   ///< đang ghi dữ liệu đã xử lý
    qint64  elapsedMs   = 0;
    quint64 rawRecords  = 0;
    quint64 procRecords = 0;
    quint64 rawBytes    = 0;
    quint64 procBytes   = 0;
    quint64 dropped     = 0;
    quint32 files       = 0;

    quint64 total() const { return rawRecords + procRecords; }
};

class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    /// Hàng đợi để các luồng khác đẩy bản ghi vào.
    rec::RecordSpool *spool() { return &m_spool; }

    /// Tiện đường cho nơi gọi: đẩy một bản ghi kèm mốc thời gian hiện tại.
    /// Không kiểm tra wantsRaw()/wantsProc() — nơi gọi phải hỏi trước, vì phần
    /// đắt nhất chính là việc dựng ra `payload`.
    void push(rec::RecType type, const QByteArray &payload);

    void start(bool raw, bool proc);
    void stop();

    bool isRunning() const { return m_running; }
    bool wantsRaw() const  { return m_spool.wantsRaw(); }
    bool wantsProc() const { return m_spool.wantsProc(); }

    RecorderStats stats() const;

    /// Duyệt lại ./records rồi phát sessionsChanged(). Gọi một lần lúc khởi
    /// động và mỗi khi cần làm mới danh sách.
    void refreshSessions();

signals:
    /// Lỗi mở thư mục, mở file hay ghi đĩa — nội dung sẵn sàng hiện cho người dùng.
    void failed(const QString &message);

    /// Danh mục phiên vừa đổi (quét lại thư mục, hoặc vừa đóng xong một file).
    void sessionsChanged(const QVector<RecSession> &all);

private:
    QThread        *m_thread = nullptr;
    RecorderWorker *m_worker = nullptr;

    rec::RecordSpool m_spool;
    RecorderCounters m_counters;

    bool   m_running   = false;
    bool   m_raw       = false;
    bool   m_proc      = false;
    qint64 m_startedMs = 0;
};
