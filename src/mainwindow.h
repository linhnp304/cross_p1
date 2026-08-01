#pragma once

#include "appparams.h"
#include "appsettings.h"
#include "beamcenter.h"
#include "plotstore.h"
#include "radarvideo.h"
#include "tracker.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QVector>

class AScope;
class BeamParamsDialog;
class ColorsTab;
class ConnectionTab;
class LanIndicator;
class ParamsTab;
class PlotListWindow;
class QLabel;
class QTimer;
class RadarView;
class SettingsTab;
class TrackInfoPopup;
class TrackListTab;
class TrackParamsDialog;
class UdpLink;
class UdpSender;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QWidget *buildRightColumn();
    QWidget *buildStatusBar();

    void applySettings(const AppSettings &s);

    /// Nhận tham số kỹ thuật mới từ tab "Tham số". Chỉ lấy phần tham số, danh
    /// sách cổng vẫn do tab "Kết nối" giữ.
    void applyParams(const AppParams &p);

    /// Nhận danh sách cổng mới từ tab "Kết nối".
    void applyEndpoints(const QVector<NetEndpoint> &rx);
    void applyTxEndpoints(const QVector<NetEndpoint> &tx);

    /// Đẩy các điểm dấu và quỹ đạo vừa có sang bảng cổng gửi.
    void sendPlots(const QVector<PlotTC> &plots);
    void sendPendingTracks();

    /// Rút danh sách quỹ đạo vừa bị xoá ra khỏi bộ bám và gửi đi. Đây là nơi
    /// **duy nhất** rút danh sách đó, để không quỹ đạo nào biến mất mà chưa kịp
    /// báo trạng thái "xoá" ra ngoài.
    void flushRemovedTracks();

    /// Đẩy tham số hiện hành xuống hai thuật toán xử lý.
    void pushProcessingParams();

    /// Tính lại cự ly tối đa từ tham số rồi cập nhật sang tab "Cài đặt".
    void updateRangeFromParams();

    /// Toạ độ con trỏ trên thanh trạng thái: kinh/vĩ độ kèm phương vị và cự ly
    /// tính từ tâm đài.
    void updateCursorLabel(double lat, double lng);

    void startLink();
    void stopLink();

    /// Nhịp lấy dữ liệu ra khỏi bộ đệm, dựng nền tạp rồi vẽ lại.
    void onTick();
    void refreshLinkStatus();

    /// Xử lý một chu kỳ RAW_P: tách chùm xung, sinh điểm dấu, đẩy vào bộ bám.
    void processCycle(const rawpkt::RawPCycle &cycle);

    /// Chốt sổ một vòng quét ăng-ten.
    void endScan();

    /// Đổ danh sách quỹ đạo hiện hành sang tab "Danh sách".
    void refreshTrackList();

    // --- các thao tác cập nhật quỹ đạo bằng tay ---
    void changeTop(quint32 id, quint32 top);
    void changeAltitude(quint32 id, quint32 metres);
    void changeClassify(quint32 id, quint32 classify);
    void removeTrack(quint32 id);

    /// Menu chuột phải trên một quỹ đạo.
    void showTrackMenu(quint32 id, const QPoint &globalPos);

    /// Popup thông tin nhanh của một quỹ đạo.
    void showTrackInfo(quint32 id, const QPoint &globalPos);

    void showPlotList();

    /// Địa chỉ IP trong các bảng cổng, đã bỏ trùng lặp — cho cửa sổ trạng thái mạng.
    void refreshLanHosts();

    AppSettings m_settings;
    AppParams   m_params;

    RadarView     *m_radar         = nullptr;
    SettingsTab   *m_settingsTab   = nullptr;
    ParamsTab     *m_paramsTab     = nullptr;
    ColorsTab     *m_colorsTab     = nullptr;
    ConnectionTab *m_connectionTab = nullptr;
    TrackListTab  *m_trackTab      = nullptr;
    AScope        *m_ascope        = nullptr;

    // Các cửa sổ phụ dựng theo yêu cầu rồi giữ lại: mở đi mở lại nhiều lần thì
    // giữ nguyên vị trí và nội dung người dùng đang xem.
    BeamParamsDialog  *m_beamDialog  = nullptr;
    TrackParamsDialog *m_trackDialog = nullptr;
    PlotListWindow    *m_plotList    = nullptr;
    TrackInfoPopup    *m_trackInfo   = nullptr;

    QLabel       *m_timeLabel   = nullptr;
    QLabel       *m_cursorLabel = nullptr;
    QLabel       *m_siteLabel   = nullptr;
    LanIndicator *m_lan         = nullptr;

    UdpLink    *m_link       = nullptr;
    UdpSender  *m_sender     = nullptr;
    QTimer     *m_tick       = nullptr;
    QTimer     *m_statusTick = nullptr;
    RadarVideo  m_video;

    BeamCenter  m_beams;
    Tracker     m_tracker;
    PlotStore   m_plots;

    /// Đệm dùng lại giữa các nhịp, khỏi cấp phát 25 lần mỗi giây.
    QVector<rawpkt::RawVSweep> m_drained;
    QVector<rawpkt::RawPCycle> m_cycles;
    QVector<PlotTC>            m_newPlots;

    /// Phương vị RAW_P của chu kỳ trước, để nhận ra lúc ăng-ten quay hết vòng.
    int m_lastPAzimuth = -1;

    /// Tổng số nấc encoder đã quét được kể từ lần chốt vòng quét gần nhất.
    int m_scanProgress = 0;

    /// Đồng hồ đơn điệu dùng chung cho hạn hiển thị điểm dấu và bước thời gian
    /// của bộ lọc — không dùng giờ trong ngày vì nó quay vòng lúc nửa đêm.
    QElapsedTimer m_clock;

    /// Thời điểm chốt vòng quét gần nhất, để đo chu kỳ vòng quét.
    qint64 m_lastScanMs = 0;

    /// Đo khoảng thời gian thật giữa hai lần làm mờ — nhịp vẽ có thể trồi sụt,
    /// mà tốc độ mờ thì phải theo đồng hồ chứ không theo số khung hình.
    QElapsedTimer m_fadeClock;

    /// Đếm từ lúc bấm Kết nối, để biết khi nào việc chưa có gói nào đáng coi
    /// là hỏng chứ không còn là đang chờ gói đầu tiên.
    QElapsedTimer m_linkClock;

    /// Bảng danh sách quỹ đạo làm mới thưa hơn nhịp vẽ: quỹ đạo chỉ đổi mỗi
    /// vòng quét, dựng lại bảng 25 lần mỗi giây là phí không.
    qint64 m_lastListMs = 0;
};
