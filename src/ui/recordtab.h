#pragma once

#include "record/player.h"
#include "record/recorder.h"
#include "record/recordindex.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;

/// Tab "Ghi lưu" trong panel 2.1 — ghi dữ liệu xuống đĩa và phát lại.
///
/// Tab này chỉ dựng giao diện và phát yêu cầu; mọi ràng buộc giữa các chức năng
/// (đang phát lại thì không bật được nhận dữ liệu, v.v.) do MainWindow quyết
/// định, vì chỉ nơi đó mới biết đủ trạng thái của cả bốn chức năng.
class RecordTab : public QWidget
{
    Q_OBJECT

public:
    explicit RecordTab(QWidget *parent = nullptr);

    /// Danh mục phiên vừa đổi — đổ lại ComboBox theo loại đang chọn.
    void setSessions(const QVector<RecSession> &all);

    void setRecording(bool on);
    void setRecordStats(const RecorderStats &s);

    void setReplaying(bool on);
    void setReplayStats(const PlayerStats &s);

    /// Dòng thông báo chung ở cuối tab.
    void setStatusText(const QString &text, bool isError = false);

    bool wantsRaw() const;
    bool wantsProcessed() const;

    /// True khi đang chọn phát lại dữ liệu gốc.
    bool replayRawSelected() const;

signals:
    void recordStartRequested(bool raw, bool processed);
    void recordStopRequested();

    /// `total` lấy từ header của file, để hiện tiến độ lúc phát lại.
    void replayStartRequested(const QString &path, bool raw, quint32 total,
                              double speed);
    void replayStopRequested();
    void replaySpeedChanged(double speed);

private:
    void refreshSessionList();
    void refreshInfo();
    void refreshEnabled();
    void openExternalFile();

    /// Phiên đang chọn (trong danh sách hoặc file mở ngoài). Trả về false khi
    /// chưa chọn gì.
    bool currentSession(RecSession &out) const;

    QVector<RecSession> m_sessions;   ///< toàn bộ danh mục, cả hai loại
    RecSession          m_external;   ///< tệp mở ngoài danh sách
    bool                m_hasExternal = false;

    bool m_recording = false;
    bool m_replaying = false;

    // --- group Ghi lưu ---
    QCheckBox   *m_rawBox   = nullptr;
    QCheckBox   *m_procBox  = nullptr;
    QPushButton *m_recBtn   = nullptr;
    QLabel      *m_recTime  = nullptr;
    QLabel      *m_recCount = nullptr;

    // --- group Phát lại ---
    QRadioButton *m_typeRaw     = nullptr;
    QRadioButton *m_typeProc    = nullptr;
    QRadioButton *m_srcList     = nullptr;
    QRadioButton *m_srcFile     = nullptr;
    QComboBox    *m_sessionBox  = nullptr;
    QPushButton  *m_openBtn     = nullptr;
    QLabel       *m_info        = nullptr;
    QPushButton  *m_playBtn     = nullptr;
    QLabel       *m_speedLabel  = nullptr;
    QComboBox    *m_speedBox    = nullptr;
    QLabel       *m_playTime    = nullptr;
    QLabel       *m_playCount   = nullptr;

    QLabel *m_status = nullptr;
};
