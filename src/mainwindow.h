#pragma once

#include "appparams.h"
#include "appsettings.h"
#include "radarvideo.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QVector>

class AScope;
class ConnectionTab;
class LanIndicator;
class ParamsTab;
class QLabel;
class QTimer;
class RadarView;
class SettingsTab;
class UdpLink;

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

    /// Địa chỉ IP trong các bảng cổng, đã bỏ trùng lặp — cho cửa sổ trạng thái mạng.
    void refreshLanHosts();

    AppSettings m_settings;
    AppParams   m_params;

    RadarView     *m_radar         = nullptr;
    SettingsTab   *m_settingsTab   = nullptr;
    ParamsTab     *m_paramsTab     = nullptr;
    ConnectionTab *m_connectionTab = nullptr;
    AScope        *m_ascope        = nullptr;

    QLabel       *m_timeLabel   = nullptr;
    QLabel       *m_cursorLabel = nullptr;
    QLabel       *m_siteLabel   = nullptr;
    LanIndicator *m_lan         = nullptr;

    UdpLink    *m_link       = nullptr;
    QTimer     *m_tick       = nullptr;
    QTimer     *m_statusTick = nullptr;
    RadarVideo  m_video;

    /// Đệm dùng lại giữa các nhịp, khỏi cấp phát 25 lần mỗi giây.
    QVector<rawpkt::RawVSweep> m_drained;

    /// Đo khoảng thời gian thật giữa hai lần làm mờ — nhịp vẽ có thể trồi sụt,
    /// mà tốc độ mờ thì phải theo đồng hồ chứ không theo số khung hình.
    QElapsedTimer m_fadeClock;

    /// Đếm từ lúc bấm Kết nối, để biết khi nào việc chưa có gói nào đáng coi
    /// là hỏng chứ không còn là đang chờ gói đầu tiên.
    QElapsedTimer m_linkClock;
};
