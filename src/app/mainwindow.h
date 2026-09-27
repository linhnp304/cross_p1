#pragma once

#include "app/appparams.h"
#include "app/appsettings.h"
#include "proc/beamcenter.h"
#include "proc/plotstore.h"
#include "ui/radarvideo.h"
#include "record/recordfile.h"
#include "proc/tracker.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <deque>

class Adf4159Window;
class AScope;
class BeamParamsDialog;
class ColorsTab;
class ConnectionTab;
class ControlTab;
class FilterWindow;
class LanIndicator;
class ParamsTab;
class Player;
class PlotListWindow;
class QLabel;
class QTimer;
class RadarView;
class Recorder;
class RecordTab;
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

protected:
    /// Chặn, rồi hỏi lại, trước khi thoát. Đóng nhầm cửa sổ giữa ca trực là mất
    /// cả nền tạp đang tích, mọi quỹ đạo đang bám và phần ghi lưu đang dở.
    void closeEvent(QCloseEvent *e) override;

private:
    /// Các chức năng đang bật mà phải tắt trước khi thoát phần mềm — bốn chức
    /// năng mở/đóng cổng mạng hay file. Rỗng là thoát được.
    ///
    /// Một chỗ tính duy nhất cho hai nơi dùng: cái chặn trong closeEvent() và
    /// việc khoá nút "Thoát phần mềm" bên tab "Kết nối". Hai nơi mà xét hai
    /// danh sách khác nhau thì có lúc nút mở mà bấm vào lại bị chặn.
    QStringList exitBlockers() const;

    /// Cập nhật nút "Thoát phần mềm" theo exitBlockers().
    void refreshExitState();

    QWidget *buildRightColumn();
    QWidget *buildStatusBar();

    void applySettings(const AppSettings &s);

    /// Nhận tham số kỹ thuật mới từ tab "Tham số". Chỉ lấy phần tham số, danh
    /// sách cổng vẫn do tab "Kết nối" giữ.
    void applyParams(const AppParams &p);

    /// Nhận danh sách cổng mới từ tab "Kết nối".
    void applyEndpoints(const QVector<NetEndpoint> &rx);
    void applyTxEndpoints(const QVector<NetEndpoint> &tx);

    /// Ô "Tự động cấu hình cổng nhận Status..." vừa đổi.
    void applyStatusFollowsCommand(bool on);

    /// Bảng cổng nhận đang có hiệu lực. Khác m_params.rx đúng một chỗ: khi
    /// statusFollowsCommand đang bật thì dòng "Status" bị bỏ ra, vì trạng thái
    /// lúc ấy về trên chính socket gửi lệnh (xem UdpSender::statusReceived).
    QVector<NetEndpoint> activeRxEndpoints() const;

    /// Bảng cổng gửi đang có hiệu lực. Dòng Plot/Track chỉ mở khi công tắc
    /// chung "Bắt đầu gửi dữ liệu" đang bật; dòng Command và CtrlSync thì không
    /// phụ thuộc nó — lệnh điều khiển và gói chiếm quyền đi ra vì trắc thủ vừa
    /// bấm một nút, chứ không phải vì đang có dòng dữ liệu nào chảy.
    QVector<NetEndpoint> activeTxEndpoints() const;

    /// Gửi một gói lệnh điều khiển ra các dòng "Command".
    void sendCommand(int group, const QByteArray &datagram);

    /// Cùng đường đi ấy, cho hai gói lệnh của kit tạo tín hiệu ADF4159.
    void sendAdfCommand(int kind, const QByteArray &datagram);

    /// Cùng đường đi ấy, cho bốn gói nạp hệ số bộ lọc.
    void sendFilterCommand(int kind, const QByteArray &datagram);

    /// Câu nhắc khi bảng cổng gửi chưa có dòng "Command" nào dùng được. Hai
    /// đường gửi lệnh dùng chung một câu và chung một cờ "đã nhắc rồi".
    static QString noCommandPortMsg();
    void warnNoCommandPort();

    /// Mở cửa sổ điều khiển kit ADF4159, dựng nó ở lần mở đầu tiên.
    void showAdf4159();

    /// Mở cửa sổ "Điều khiển các bộ lọc", dựng nó ở lần mở đầu tiên. Khác cửa sổ
    /// ADF4159 ở một chỗ: **mỗi lần mở đều đọc lại bốn file dữ liệu**, xem
    /// FilterWindow::reload().
    void showFilterWindow();

    // --- đồng bộ điều khiển giữa nhiều máy tính ---

    /// Quảng bá một gói CTRL_SYNC mang địa chỉ máy này — gọi mỗi khi trắc thủ mở
    /// khóa điều khiển. Các máy khác nghe được sẽ tự khóa lại.
    void broadcastCtrlSync();

    /// Một gói CTRL_SYNC vừa về. Gói do chính máy này quảng bá thì bỏ qua (đã xử
    /// lý ngay lúc bấm nút); gói của máy khác thì khóa điều khiển lại và cập nhật
    /// nhãn CtrlIP.
    void applyCtrlSync(const QByteArray &datagram);

    /// Đưa một gói vừa nhận ở đường trạng thái tới đúng nơi đọc nó: gói
    /// CTRL_SYNC tách ra trước, rồi tới tab "Điều khiển", cửa sổ bộ lọc, cửa sổ
    /// ADF4159 — nơi nào nhận thì dừng ở đó.
    void routeStatus(const QByteArray &datagram);

    /// Đẩy cách tính Video[1024] xuống luồng nhận dữ liệu.
    void pushVideoScale();

    /// Lấy tham số kỹ thuật từ gói trạng thái lệnh điều khiển vừa nhận, khi ô
    /// "Tự động nhận từ trạng thái lệnh điều khiển" đang bật. Hiện mới nối được
    /// **Fs** với ADC_Sample_Rate của CMD_DSP_R; B và Tc chưa có trong gói lệnh
    /// nào nên vẫn phải nhập tay.
    void applyStatusToParams(int group);

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

    /// Góc đường quét trên thanh trạng thái: phương vị của lượt quét RAW_V mới
    /// nhất. Chưa có dữ liệu thì hiện 0.000.
    void updateSweepLabel();

    void startLink();
    void stopLink();

    void startSending();
    void stopSending();

    // --- ghi lưu và phát lại ---
    void startRecording(bool raw, bool proc);
    void stopRecording();
    void startReplay(const QString &path, bool raw, quint32 total, double speed);
    void stopReplay();

    /// Dựng lại trạng thái hiển thị và xử lý về vạch xuất phát. Dùng chung cho
    /// lúc bắt đầu nhận dữ liệu thật và lúc bắt đầu phát lại — cả hai đều mở
    /// một dòng dữ liệu mới, mà chùm xung dở dang với quỹ đạo của lần trước thì
    /// không còn liên quan gì tới nó.
    void resetProcessing();

    /// Đưa một bản ghi lấy từ file vào đúng chỗ của nó trong đường xử lý. Trả
    /// về true khi bản ghi đó vừa vẽ thêm một lượt quét vào ảnh nền tạp — nơi
    /// gọi gom lại để chỉ dựng lại đường biên độ một lần cho cả nhịp.
    bool applyReplayItem(const rec::RecItem &item);

    /// Chạy/dừng nhịp vẽ theo việc còn nguồn dữ liệu nào đang chảy hay không.
    void syncTimers();

    /// Ghi một lượt quét đã giải mã vào file dữ liệu đã xử lý. Chỉ dùng ở đường
    /// phát lại — lúc nhận thật thì chính luồng mạng ghi, xem UdpWorker::read().
    void recordSweep(const rawpkt::RawVSweep &s);

    /// Nhịp lấy dữ liệu ra khỏi bộ đệm, dựng nền tạp rồi vẽ lại.
    void onTick();
    void refreshLinkStatus();
    void refreshRecordStatus();

    /// Xử lý một chu kỳ RAW_P: tách chùm xung, sinh điểm dấu, đẩy vào bộ bám.
    void processCycle(const rawpkt::RawPCycle &cycle);

    /// Điểm dấu nhận sẵn từ gói PlotTC — vào thẳng bộ bám, không qua tách chùm.
    void processExternalPlots(const QVector<PlotTC> &plots);

    /// Đẩy nhịp đường quét của bộ bám bằng phương vị RAW_V, khi nguồn không
    /// phát RAW_P.
    void trackScanFromVideo(quint32 azimuth);

    /// Ăng-ten vừa quay hết một vòng: đo lại chu kỳ vòng quét.
    void endScan();

    /// Đổ danh sách quỹ đạo hiện hành sang tab "Danh sách".
    void refreshTrackList();

    // --- các thao tác cập nhật quỹ đạo bằng tay ---
    void changeTop(quint32 id, quint32 top);
    void changeAltitude(quint32 id, quint32 metres);
    void changeClassify(quint32 id, quint32 classify);
    void removeTrack(quint32 id);

    /// Xoá sạch lớp điểm dấu đang vẽ trên panel 1.
    void clearAllPlots();

    /// Gom điểm dấu đơn xung của một chu kỳ RAW_P vào lớp hiển thị.
    void collectRawPlots(const rawpkt::RawPCycle &cycle, qint64 nowMs);

    /// Xoá sạch danh sách quỹ đạo, có hỏi lại trước.
    void clearAllTracks();

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
    ControlTab    *m_controlTab    = nullptr;
    RecordTab     *m_recordTab     = nullptr;
    TrackListTab  *m_trackTab      = nullptr;
    AScope        *m_ascope        = nullptr;

    // Các cửa sổ phụ dựng theo yêu cầu rồi giữ lại: mở đi mở lại nhiều lần thì
    // giữ nguyên vị trí và nội dung người dùng đang xem.
    BeamParamsDialog  *m_beamDialog  = nullptr;
    TrackParamsDialog *m_trackDialog = nullptr;
    PlotListWindow    *m_plotList    = nullptr;
    TrackInfoPopup    *m_trackInfo   = nullptr;
    Adf4159Window     *m_adfWindow   = nullptr;
    FilterWindow      *m_filterWindow = nullptr;

    QLabel       *m_timeLabel   = nullptr;
    QLabel       *m_cursorLabel = nullptr;
    QLabel       *m_sweepLabel  = nullptr;
    QLabel       *m_siteLabel   = nullptr;
    LanIndicator *m_lan         = nullptr;

    UdpLink    *m_link       = nullptr;
    UdpSender  *m_sender     = nullptr;
    Recorder   *m_recorder   = nullptr;
    Player     *m_player     = nullptr;
    QTimer     *m_tick       = nullptr;
    QTimer     *m_statusTick = nullptr;
    RadarVideo  m_video;

    /// Nút "Bắt đầu gửi dữ liệu" đang bật. Khác với ô "Gửi" của từng dòng: đây
    /// là công tắc chung, tắt thì không dòng dữ liệu nào giữ socket cả (dòng
    /// Command không nằm dưới công tắc này).
    bool m_txOn = false;

    /// Đã nhắc người dùng cấu hình cổng gửi lệnh điều khiển rồi. Không có cờ
    /// này thì mỗi lần vặn một nút là một hộp thoại — mà lúc chưa cấu hình thì
    /// nút nào cũng vặn hụt.
    bool m_warnedNoCommandPort = false;

    /// Số đếm của gói CTRL_SYNC gửi đi. Không lưu xuống file: dãy này chỉ để bên
    /// nhận nhìn ra thứ tự trong một phiên, mà mở phần mềm lên là đang khóa điều
    /// khiển nên chưa chiếm quyền gì.
    quint32 m_ctrlSyncSerial = 0;

    /// Đang phát lại dữ liệu gốc (khác với dữ liệu đã qua xử lý). Quyết định cả
    /// đường đi của dữ liệu lẫn việc có cho bật ghi lưu hay không.
    bool m_replayRaw = false;

    BeamCenter   m_beams;
    Tracker      m_tracker;
    PlotStore    m_plots;

    /// Điểm dấu đơn xung đang vẽ. Chỉ được đổ vào khi lớp đó đang bật: một mục
    /// tiêu để lại vài chục chấm mỗi vòng quét, gom sẵn để đấy phòng khi trắc
    /// thủ bật lên là gánh nặng suốt ca trực chỉ để phục vụ vài phút xem xét.
    RawPlotStore m_rawPlots;

    /// Đệm dùng lại giữa các nhịp, khỏi cấp phát 25 lần mỗi giây.
    QVector<rawpkt::RawVSweep> m_drained;
    QVector<rawpkt::RawPCycle> m_cycles;
    QVector<PlotTC>            m_newPlots;
    QVector<PlotTC>            m_rxPlots;
    QVector<QByteArray>        m_rxStatus;
    std::deque<rec::RecItem>   m_replayed;

    /// Phương vị RAW_P của chu kỳ trước, để nhận ra lúc ăng-ten quay hết vòng.
    int m_lastPAzimuth = -1;

    /// Tổng số nấc encoder đã quét được kể từ lần chốt vòng quét gần nhất.
    int m_scanProgress = 0;

    /// Hai trường trên, nhưng đo trên dòng RAW_V. Chỉ dùng khi **không có**
    /// RAW_P: nguồn chỉ phát nền tạp kèm điểm dấu tính sẵn (công cụ kiểm tra
    /// bộ lọc Kalman) thì không còn ai chốt sổ vòng quét, mà chốt sổ theo hết
    /// giờ thì cả chục giây mới một lần — bộ bám coi như đứng im.
    int m_lastVAzimuth = -1;
    int m_scanProgressV = 0;

    /// Thời điểm chu kỳ RAW_P gần nhất. Còn RAW_P thì RAW_V không được chốt sổ,
    /// nếu không một vòng quét bị chốt hai lần.
    qint64 m_lastRawPMs = 0;

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
