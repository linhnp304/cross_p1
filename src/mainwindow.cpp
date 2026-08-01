#include "mainwindow.h"

#include "appinfo.h"
#include "ascope.h"
#include "beamparamsdialog.h"
#include "colorstab.h"
#include "connectiontab.h"
#include "geo.h"
#include "lanstatus.h"
#include "packetio.h"
#include "paramstab.h"
#include "plotlistwindow.h"
#include "radarview.h"
#include "settingstab.h"
#include "tilecache.h"
#include "trackinfopopup.h"
#include "tracklisttab.h"
#include "trackparamsdialog.h"
#include "udplink.h"
#include "udpsender.h"

#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

/// Nhịp vẽ lại panel 1 và cửa sổ biên độ. 25 hình/giây là đủ mượt với tốc độ
/// quay 6 vòng/phút, mà vẫn để dành phần lớn CPU cho việc nhận và giải mã.
constexpr int kTickMs = 40;

/// Làm mờ thưa hơn nhịp vẽ: một lượt làm mờ đụng cả triệu điểm ảnh, mà mắt
/// không phân biệt được mờ 25 lần hay 13 lần mỗi giây.
constexpr int kMinFadeMs = 75;

/// Mở cổng xong bao lâu mà chưa có gói nào thì coi là hỏng. Đài phát liên tục
/// vài trăm gói mỗi giây nên 3 giây là quá đủ để gói đầu tiên về; để ngắn hơn
/// thì lần nào bấm Kết nối cũng loé cảnh báo đỏ một nhịp.
constexpr int kNoDataWarnMs = 3000;

/// Nhịp làm mới bảng danh sách quỹ đạo. Quỹ đạo chỉ đổi mỗi vòng quét (~10
/// giây), nên nửa giây là đã nhanh hơn cần thiết nhiều lần.
constexpr int kListRefreshMs = 500;

/// Bao lâu không thấy ăng-ten quay hết vòng thì tự chốt sổ một vòng quét.
///
/// Nếu không có cái này, đài ngừng phát RAW_P giữa chừng là mọi quỹ đạo đang có
/// đứng nguyên trên màn hình mãi mãi: không ai cập nhật, cũng không ai xoá.
constexpr qint64 kScanTimeoutMs = 30000;

QLabel *makeStatusLabel()
{
    auto *l = new QLabel;
    l->setStyleSheet(QStringLiteral("color: #9fb4c8; padding: 0 10px;"));
    return l;
}

QString formatLatLng(double lat, double lng)
{
    return QStringLiteral("%1, %2").arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6);
}

/// Bọc một tab trong vùng cuộn: panel hẹp vẫn dùng được, và bề rộng tối thiểu
/// của form không ép splitter phá vỡ tỉ lệ 70/30.
QScrollArea *wrapInScroll(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(appinfo::displayName());

    m_settings.load();   // giữ mặc định nếu chưa có file
    if (!m_params.load() || m_params.rx.isEmpty())
        m_params.rx = AppParams::defaultRx();

    m_radar = new RadarView(this);
    m_radar->setVideo(&m_video);
    m_radar->setSources(&m_plots, &m_tracker);

    m_link = new UdpLink(this);
    m_link->setZfbeat(m_params.zfbeat);

    m_sender = new UdpSender(this);

    m_clock.start();

    // Panel 1 (70%) | Panel 2 (30%)
    auto *hSplit = new QSplitter(Qt::Horizontal, this);
    hSplit->setChildrenCollapsible(false);
    hSplit->setHandleWidth(2);
    hSplit->addWidget(m_radar);
    hSplit->addWidget(buildRightColumn());
    hSplit->setStretchFactor(0, 7);
    hSplit->setStretchFactor(1, 3);
    hSplit->setSizes({700, 300});

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(hSplit, 1);
    root->addWidget(buildStatusBar(), 0);
    setCentralWidget(central);

    connect(m_radar, &RadarView::cursorGeoChanged, this,
            &MainWindow::updateCursorLabel);

    connect(m_settingsTab, &SettingsTab::settingsChanged, this,
            &MainWindow::applySettings);
    connect(m_paramsTab, &ParamsTab::paramsApplied, this, &MainWindow::applyParams);
    connect(m_connectionTab, &ConnectionTab::endpointsChanged,
            this, &MainWindow::applyEndpoints);
    connect(m_connectionTab, &ConnectionTab::txEndpointsChanged,
            this, &MainWindow::applyTxEndpoints);
    connect(m_sender, &UdpSender::failed, this, [this](const QString &msg) {
        m_connectionTab->setStatusText(msg, true);
    });
    connect(m_connectionTab, &ConnectionTab::connectRequested,
            this, &MainWindow::startLink);
    connect(m_connectionTab, &ConnectionTab::disconnectRequested,
            this, &MainWindow::stopLink);

    // Màu sắc là cài đặt hiển thị nên vào chung AppSettings, chỉ khác chỗ nhập.
    connect(m_colorsTab, &ColorsTab::colorsChanged, this, [this](const AppColors &c) {
        m_settings.colors = c;
        m_radar->setSettings(m_settings);
        m_settings.save();
    });

    connect(m_paramsTab, &ParamsTab::beamParamsRequested, this, [this] {
        if (!m_beamDialog) {
            m_beamDialog = new BeamParamsDialog(this);
            connect(m_beamDialog, &BeamParamsDialog::applied, this,
                    [this](const BeamParams &b) {
                        m_params.beam = b;
                        pushProcessingParams();
                        m_params.save();
                    });
            // Vị trí ban đầu đặt trên khu vực panel 2, để không che panel 1
            // đang chiến đấu.
            m_beamDialog->adjustSize();
            const QPoint at = mapToGlobal(QPoint(width() * 7 / 10 + 16, 60));
            m_beamDialog->move(at);
        }
        m_beamDialog->setParams(m_params.beam);
        m_beamDialog->show();
        m_beamDialog->raise();
        m_beamDialog->activateWindow();
    });

    connect(m_paramsTab, &ParamsTab::trackParamsRequested, this, [this] {
        if (!m_trackDialog) {
            m_trackDialog = new TrackParamsDialog(this);
            connect(m_trackDialog, &TrackParamsDialog::applied, this,
                    [this](const TrackParams &t) {
                        m_params.track = t;
                        pushProcessingParams();
                        m_params.save();
                    });
            m_trackDialog->adjustSize();
            m_trackDialog->move(mapToGlobal(QPoint(width() * 7 / 10 + 16, 60)));
        }
        m_trackDialog->setParams(m_params.track);
        m_trackDialog->show();
        m_trackDialog->raise();
        m_trackDialog->activateWindow();
    });

    // --- bảng danh sách quỹ đạo ---
    connect(m_trackTab, &TrackListTab::watchChanged, this,
            [this](quint32 id, bool on) {
                if (Track *t = m_tracker.find(id))
                    t->watched = on;
                m_radar->update();
            });
    connect(m_trackTab, &TrackListTab::topChangeRequested,
            this, &MainWindow::changeTop);
    connect(m_trackTab, &TrackListTab::altitudeChanged,
            this, &MainWindow::changeAltitude);
    connect(m_trackTab, &TrackListTab::classifyChanged,
            this, &MainWindow::changeClassify);
    connect(m_trackTab, &TrackListTab::removeRequested,
            this, &MainWindow::removeTrack);
    connect(m_trackTab, &TrackListTab::plotListRequested,
            this, &MainWindow::showPlotList);
    connect(m_trackTab, &TrackListTab::trackActivated, this, [this](quint32 id) {
        // Mở từ bảng thì popup bật ra cạnh chính quỹ đạo trên panel 1, không
        // phải cạnh con trỏ trong bảng — mắt đang nhìn bản đồ chứ không nhìn bảng.
        if (const Track *t = m_tracker.find(id)) {
            const QPoint at = m_radar->mapToGlobal(
                m_radar->geoToScreenPoint(t->lat, t->lng));
            showTrackInfo(id, at);
        }
    });

    // --- tương tác trên panel 1 ---
    connect(m_radar, &RadarView::trackClicked, this, &MainWindow::showTrackInfo);
    connect(m_radar, &RadarView::trackContextMenu, this, &MainWindow::showTrackMenu);

    // Lỗi mở cổng hiện ngay dưới bảng, không cắt ngang bằng hộp thoại — mở
    // nhiều cổng thì sẽ là nhiều hộp thoại liên tiếp.
    connect(m_link, &UdpLink::failed, this, [this](const QString &msg) {
        m_connectionTab->setStatusText(msg, true);
    });

    // --- nhịp lấy dữ liệu và vẽ ---
    m_tick = new QTimer(this);
    m_tick->setInterval(kTickMs);
    connect(m_tick, &QTimer::timeout, this, &MainWindow::onTick);

    m_statusTick = new QTimer(this);
    m_statusTick->setInterval(1000);
    connect(m_statusTick, &QTimer::timeout, this, &MainWindow::refreshLinkStatus);

    // Đồng hồ hệ thống.
    auto *clock = new QTimer(this);
    connect(clock, &QTimer::timeout, this, [this] {
        m_timeLabel->setText(
            QDateTime::currentDateTime().toString(QStringLiteral("dd/MM/yyyy  HH:mm:ss")));
    });
    clock->start(1000);
    m_timeLabel->setText(
        QDateTime::currentDateTime().toString(QStringLiteral("dd/MM/yyyy  HH:mm:ss")));

    // Ctrl+Q (macOS: Cmd+Q) để thoát, F11 bật/tắt toàn màn hình thật sự.
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this,
                  [] { QApplication::quit(); });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] {
        isFullScreen() ? showMaximized() : showFullScreen();
    });

    // Danh sách kiểu nền lấy từ chính các thư mục có trên đĩa. Nếu kiểu đang
    // lưu trong cấu hình không còn (đổi máy, xoá bớt bản đồ) thì lùi về kiểu
    // đầu tiên còn dùng được, để ComboBox không trỏ vào chỗ trống.
    QVector<TileSetInfo> styles = TileCache::listStyles(m_settings.resolvedTilesDir());

    // Tiền tố nguồn để phân biệt với lớp bản đồ TC tự dựng. Thêm ở đây chứ
    // không sửa nhãn trong tileset.json, để tải lại tile là không mất tiền tố.
    for (TileSetInfo &s : styles)
        s.label = tr("MT - %1").arg(s.label);

    // Lớp bản đồ TC luôn có trong danh sách, kể cả khi chưa có dữ liệu — chọn
    // vào sẽ thấy dòng nhắc chỉ đúng thư mục còn thiếu.
    styles.push_back({QString::fromLatin1(kTcStyleId), tr("TC")});
    m_settingsTab->setAvailableStyles(styles);

    const bool stillThere = std::any_of(styles.cbegin(), styles.cend(),
        [this](const TileSetInfo &s) { return s.id == m_settings.mapStyle; });
    if (!stillThere)
        m_settings.mapStyle = styles.first().id;

    // Thang cự ly đang để tự động thì giá trị trong file cấu hình chỉ là dấu
    // vết của lần chạy trước — tính lại từ tham số cho khớp ngay lúc khởi động.
    if (m_params.autoRange)
        m_settings.maxRangeKm = qBound(AppSettings::kMinRangeKm, m_params.rmaxKm(),
                                       AppSettings::kMaxRangeKm);

    // Ghi lại ngay lúc khởi động: file luôn tồn tại để người dùng có cái mà
    // sửa (tilesDir, mapStyle), và phản ánh đúng những gì phần mềm đang dùng
    // sau khi đã chuyển đổi bố cục cũ hay lùi về kiểu nền còn dùng được.
    m_settings.save();
    m_params.save();

    // Trước khi rê chuột, thanh trạng thái hiện luôn toạ độ tâm đài.
    m_settingsTab->setSettings(m_settings);
    m_paramsTab->setParams(m_params);
    m_colorsTab->setColors(m_settings.colors);
    m_connectionTab->setParams(m_params);
    m_sender->setEndpoints(m_params.tx);   // lúc này mọi dòng đều đang tắt gửi
    m_trackTab->setSettings(m_settings);
    m_radar->setSettings(m_settings);
    m_ascope->setMaxRangeKm(m_settings.maxRangeKm);
    pushProcessingParams();
    refreshLanHosts();

    m_siteLabel->setText(tr("Tâm đài  %1")
                             .arg(formatLatLng(m_settings.siteLat, m_settings.siteLng)));
    updateCursorLabel(m_settings.siteLat, m_settings.siteLng);

    resize(1920, 1080);
}

QWidget *MainWindow::buildRightColumn()
{
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);

    m_connectionTab = new ConnectionTab;
    m_paramsTab     = new ParamsTab;
    m_colorsTab     = new ColorsTab;
    m_settingsTab   = new SettingsTab;
    m_trackTab      = new TrackListTab;

    // Tab "Danh sách" không bọc trong vùng cuộn: bảng tự cuộn được rồi, bọc
    // thêm một lớp nữa là hai thanh cuộn lồng nhau.
    tabs->addTab(m_trackTab, tr("Danh sách"));
    tabs->addTab(wrapInScroll(m_connectionTab), tr("Kết nối"));
    tabs->addTab(wrapInScroll(m_paramsTab), tr("Tham số"));
    tabs->addTab(wrapInScroll(m_colorsTab), tr("Màu sắc"));

    auto *settingsScroll = wrapInScroll(m_settingsTab);
    tabs->addTab(settingsScroll, tr("Cài đặt"));
    tabs->setCurrentWidget(settingsScroll);

    m_ascope = new AScope;

    // Panel 2.1 (70% chiều dọc) | Panel 2.2 (30%)
    auto *vSplit = new QSplitter(Qt::Vertical);
    vSplit->setChildrenCollapsible(false);
    vSplit->setHandleWidth(2);
    vSplit->addWidget(tabs);
    vSplit->addWidget(m_ascope);
    vSplit->setStretchFactor(0, 7);
    vSplit->setStretchFactor(1, 3);
    vSplit->setSizes({700, 300});
    return vSplit;
}

QWidget *MainWindow::buildStatusBar()
{
    auto *bar = new QFrame;
    bar->setFixedHeight(26);
    bar->setStyleSheet(QStringLiteral(
        "QFrame { background: #131b24; border-top: 1px solid #23303d; }"));

    m_timeLabel   = makeStatusLabel();
    m_cursorLabel = makeStatusLabel();
    m_siteLabel   = makeStatusLabel();

    // Nhóm giữa nằm chính giữa: hai bên dùng cùng hệ số giãn nên rộng bằng nhau.
    auto *left = new QWidget(bar);
    auto *leftLay = new QHBoxLayout(left);
    leftLay->setContentsMargins(6, 2, 0, 2);
    leftLay->setSpacing(0);
    m_lan = new LanIndicator(left);
    leftLay->addWidget(m_lan, 0);
    leftLay->addStretch(1);

    auto *centre = new QWidget(bar);
    auto *centreLay = new QHBoxLayout(centre);
    centreLay->setContentsMargins(0, 0, 0, 0);
    centreLay->setSpacing(0);
    centreLay->addWidget(m_timeLabel);
    centreLay->addWidget(m_cursorLabel);

    auto *right = new QWidget(bar);
    auto *rightLay = new QHBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);
    rightLay->addStretch(1);
    rightLay->addWidget(m_siteLabel);

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 0, 6, 0);
    lay->setSpacing(0);
    lay->addWidget(left, 1);
    lay->addWidget(centre, 0);
    lay->addWidget(right, 1);
    return bar;
}

// ------------------------------------------------------------- cài đặt -----

void MainWindow::applySettings(const AppSettings &s)
{
    // Tab "Cài đặt" không giữ màu và danh sách phân loại, nên bản nó gửi lên
    // mang giá trị mặc định — giữ lại phần của mình chứ không nhận đè.
    const AppColors   colors  = m_settings.colors;
    const QStringList classes = m_settings.classifyNames;

    m_settings = s;
    m_settings.colors        = colors;
    m_settings.classifyNames = classes;

    m_radar->setSettings(m_settings);
    m_ascope->setMaxRangeKm(m_settings.maxRangeKm);
    m_tracker.setSite(m_settings.siteLat, m_settings.siteLng);
    m_tracker.setHistoryLimit(AppSettings::kMaxHistory);
    m_trackTab->setSettings(m_settings);
    m_siteLabel->setText(tr("Tâm đài  %1")
                             .arg(formatLatLng(m_settings.siteLat, m_settings.siteLng)));
    m_settings.save();
}

void MainWindow::applyParams(const AppParams &p)
{
    // Chỉ lấy phần tham số kỹ thuật: danh sách cổng do tab "Kết nối" giữ, bản
    // sao bên tab "Tham số" có thể đã cũ.
    m_params.fs             = p.fs;
    m_params.b              = p.b;
    m_params.tc             = p.tc;
    m_params.zfbeat         = p.zfbeat;
    m_params.autoFromStatus = p.autoFromStatus;
    m_params.autoRange      = p.autoRange;
    m_params.sectorOn       = p.sectorOn;
    m_params.sectorStart    = p.sectorStart;
    m_params.sectorStop     = p.sectorStop;
    m_params.clampToRange();

    m_link->setZfbeat(m_params.zfbeat);
    if (m_params.autoRange)
        updateRangeFromParams();

    pushProcessingParams();
    m_params.save();
}

void MainWindow::pushProcessingParams()
{
    m_beams.setParams(m_params.beam);
    m_beams.setSector(m_params.sectorOn, m_params.sectorStart, m_params.sectorStop);
    // Ô cự ly quy ra mét theo đúng cự ly tối đa đang dùng để vẽ, không phải
    // theo giá trị tính từ Fs/B/Tc — hai giá trị đó lệch nhau khi trắc thủ tự
    // nhập cự ly tối đa bên tab "Cài đặt".
    m_beams.setMaxRangeMeters(m_settings.maxRangeKm * 1000.0);

    m_tracker.setParams(m_params.track);
    m_tracker.setSite(m_settings.siteLat, m_settings.siteLng);
    m_tracker.setHistoryLimit(AppSettings::kMaxHistory);
    m_tracker.setMaxRangeMeters(m_settings.maxRangeKm * 1000.0);

    m_radar->setDrawPredictWindow(m_params.track.drawWindow);
}

void MainWindow::applyEndpoints(const QVector<NetEndpoint> &rx)
{
    m_params.rx = rx;
    m_params.save();
    refreshLanHosts();
}

void MainWindow::applyTxEndpoints(const QVector<NetEndpoint> &tx)
{
    m_params.tx = tx;
    // Mở lại toàn bộ socket gửi theo bảng mới — kể cả khi chỉ bật/tắt một ô
    // "Gửi", vì dòng tắt thì không giữ socket nào cả.
    m_sender->setEndpoints(m_params.tx);
    m_params.save();
    refreshLanHosts();
}

void MainWindow::sendPlots(const QVector<PlotTC> &plots)
{
    if (m_sender->activeCount() == 0)
        return;
    for (const PlotTC &p : plots)
        m_sender->send(TxKind::Plot, packetio::buildPlot(p));
}

void MainWindow::sendPendingTracks()
{
    if (m_sender->activeCount() == 0) {
        // Vẫn phải xoá dấu, nếu không thì lúc bật gửi lên sẽ phụt ra một loạt
        // gói cũ tích từ trước đó.
        m_tracker.takePending([](const Track &) {});
        return;
    }
    m_tracker.takePending([this](const Track &t) {
        m_sender->send(TxKind::Track, packetio::buildTrack(t));
    });
}

void MainWindow::flushRemovedTracks()
{
    // Trạng thái "xoá" (6) phải đi được ra ngoài, dù quỹ đạo bị xoá bằng tay
    // hay bằng thuật toán: hệ thống nhận không biết thì nó giữ quỹ đạo đó trên
    // màn hình của mình vĩnh viễn.
    for (const Track &t : m_tracker.takeRemoved())
        m_sender->send(TxKind::Track, packetio::buildTrack(t));
}

void MainWindow::updateRangeFromParams()
{
    const double km = qBound(AppSettings::kMinRangeKm, m_params.rmaxKm(),
                             AppSettings::kMaxRangeKm);
    if (qFuzzyCompare(km, m_settings.maxRangeKm))
        return;

    m_settings.maxRangeKm = km;
    m_settingsTab->setSettings(m_settings);   // không phát tín hiệu ngược lại
    m_radar->setSettings(m_settings);
    m_ascope->setMaxRangeKm(km);
    m_beams.setMaxRangeMeters(km * 1000.0);
    m_tracker.setMaxRangeMeters(km * 1000.0);
    m_settings.save();
}

void MainWindow::updateCursorLabel(double lat, double lng)
{
    // Kinh/vĩ độ để đối chiếu với bản đồ, phương vị/cự ly để đọc theo cách của
    // trắc thủ — cùng một điểm, hai cách nhìn, nên để cạnh nhau.
    double bearingDeg = 0.0, distKm = 0.0;
    geo::bearingDistance(m_settings.siteLat, m_settings.siteLng, lat, lng,
                         bearingDeg, distKm);

    m_cursorLabel->setText(tr("Con trỏ  %1   Phương vị %2°  Cự ly %3 km")
                               .arg(formatLatLng(lat, lng))
                               .arg(bearingDeg, 0, 'f', 3)
                               .arg(distKm, 0, 'f', 3));
}

// -------------------------------------------------------------- kết nối ----

void MainWindow::startLink()
{
    if (m_params.rx.isEmpty()) {
        m_connectionTab->setStatusText(tr("Chưa có cổng nào trong bảng"), true);
        return;
    }

    m_video.clear();
    m_ascope->clearTrace();
    m_fadeClock.restart();

    // Bắt đầu một phiên mới thì xoá sạch trạng thái xử lý: chùm xung dở dang
    // và quỹ đạo của phiên trước không còn liên quan gì tới dữ liệu sắp tới.
    m_beams.reset();
    m_tracker.clear();
    m_plots.clear();
    m_lastPAzimuth = -1;
    m_scanProgress = 0;
    m_lastScanMs   = m_clock.elapsed();
    refreshTrackList();
    pushProcessingParams();

    m_connectionTab->setStatusText(QString());
    m_link->setZfbeat(m_params.zfbeat);
    m_link->start(m_params.rx);
    m_connectionTab->setRunning(true);
    m_linkClock.restart();

    m_tick->start();
    m_statusTick->start();
    refreshLinkStatus();
}

void MainWindow::stopLink()
{
    m_link->stop();
    m_tick->stop();
    m_statusTick->stop();
    m_connectionTab->setRunning(false);

    // Giữ nguyên hình đang có trên màn hình: dừng nhận dữ liệu chứ không phải
    // xoá màn hình, trắc thủ còn xem lại vệt cuối cùng.
    m_radar->update();
    refreshLinkStatus();
}

void MainWindow::onTick()
{
    m_link->drain(m_drained);
    for (const rawpkt::RawVSweep &s : m_drained)
        m_video.addSweep(s);

    if (!m_drained.isEmpty())
        m_ascope->setTrace(m_video.lastSweep());

    // RAW_P: tách chùm xung và bám quỹ đạo. Chạy ở đây chứ không ở luồng mạng
    // vì tham số đổi được lúc đang chạy và kết quả đi thẳng ra giao diện — để
    // chung một luồng thì không phải khoá gì cả.
    m_link->drainPlots(m_cycles);
    for (const rawpkt::RawPCycle &c : m_cycles)
        processCycle(c);

    const qint64 now = m_clock.elapsed();

    // Đài ngừng phát RAW_P giữa chừng thì không ai chốt sổ vòng quét nữa — tự
    // chốt để quỹ đạo cũ còn được ngoại suy rồi xoá đi.
    if (m_link->isRunning() && now - m_lastScanMs > kScanTimeoutMs)
        endScan();

    m_plots.expire(now, m_params.beam.showSec);

    // Xoá quỹ đạo quá hạn theo đồng hồ, không theo vòng quét — xem expireStale().
    m_tracker.expireStale(now);

    // Gửi đi phần vừa đổi. Quỹ đạo bị xoá gửi kèm trạng thái "xoá" (6) — hệ
    // thống nhận cần biết để bỏ nó khỏi màn hình của họ, nếu không nó nằm lại
    // đó vĩnh viễn.
    sendPendingTracks();
    flushRemovedTracks();

    const qint64 elapsed = m_fadeClock.isValid() ? m_fadeClock.elapsed() : 0;
    if (elapsed >= kMinFadeMs) {
        m_video.fade(m_settings.videoFadeSec, int(elapsed));
        m_fadeClock.restart();
    }

    if (now - m_lastListMs >= kListRefreshMs) {
        m_lastListMs = now;
        if (m_tracker.takeDirty())
            refreshTrackList();
    }

    m_radar->update();
}

void MainWindow::processCycle(const rawpkt::RawPCycle &cycle)
{
    const int azimuth = int(cycle.workAzimuth());

    // Phương vị tụt hẳn về đầu dải là ăng-ten vừa quay hết một vòng. Chốt sổ
    // **trước** khi xử lý chu kỳ này, để điểm dấu của chu kỳ mới thuộc về vòng
    // quét mới.
    //
    // Chỉ nhìn một bước tụt là chưa đủ: một gói tới muộn, hay một nguồn thứ hai
    // cùng phát vào cổng này, cũng tạo ra đúng dấu hiệu đó. Mà chốt sổ nhầm thì
    // mọi quỹ đạo đang khởi tạo bị xoá trước khi kịp đủ tiêu chuẩn — màn hình
    // sạch bong, không quỹ đạo nào hình thành, và không có gì báo là đang sai.
    // Nên đòi thêm điều kiện: ăng-ten phải thực sự quét được hơn nửa vòng kể từ
    // lần chốt trước.
    if (m_lastPAzimuth >= 0) {
        const int delta = azimuth - m_lastPAzimuth;
        if (delta >= 0)
            m_scanProgress += delta;
        else if (delta > -rawpkt::kAzimuthSteps / 2)
            ;   // lùi nhẹ: rung của encoder, không tính vào tiến độ
        else if (m_scanProgress >= rawpkt::kAzimuthSteps / 2) {
            endScan();
            m_scanProgress = 0;
        }
    }
    m_lastPAzimuth = azimuth;

    const qint64  now    = m_clock.elapsed();
    const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());

    m_newPlots.clear();
    m_beams.process(cycle, timeMs, now, m_newPlots);
    if (m_newPlots.isEmpty())
        return;

    for (PlotTC &p : m_newPlots) {
        // Toạ độ địa lý tính một lần ở đây: panel 1 vẽ lại 25 lần mỗi giây, mà
        // phép đổi phương vị/cự ly sang kinh vĩ độ không rẻ.
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         p.azmDeg(), p.rangeKm(), p.lat, p.lng);
        m_plots.add(p);
        m_tracker.addPlot(p, now);
    }

    if (m_plotList && m_plotList->isVisible())
        m_plotList->addPlots(m_newPlots);

    sendPlots(m_newPlots);
}

void MainWindow::endScan()
{
    const qint64 now = m_clock.elapsed();
    m_tracker.endScan(now, now - m_lastScanMs);
    m_lastScanMs = now;

    // Quỹ đạo vừa bị xoá **không** lấy ra ở đây: nhịp vẽ mới là nơi duy nhất
    // rút danh sách đó ra, để chắc chắn chúng được gửi đi trước khi biến mất.
    refreshTrackList();
}

void MainWindow::refreshTrackList()
{
    m_trackTab->setTracks(m_tracker.tracks());
}

void MainWindow::refreshLinkStatus()
{
    const LinkStats s = m_link->stats();
    if (!m_link->isRunning()) {
        m_connectionTab->setStatusText(
            tr("Đã dừng — nhận được %1 gói RAW_V").arg(s.rawV));
        return;
    }

    // Mở được cổng mà không có gói nào là lỗi hay gặp nhất lúc lắp đặt, và
    // cũng là lỗi khó đoán nhất: nút bấm xong không báo gì, chỉ có màn hình
    // trống. Chỉ ra sẵn ba chỗ cần xem thay vì để trắc thủ ngồi đoán.
    if (s.rawV == 0 && s.rawP == 0 && s.other == 0) {
        if (m_linkClock.isValid() && m_linkClock.elapsed() >= kNoDataWarnMs) {
            m_connectionTab->setStatusText(
                tr("Đã mở cổng nhưng %1 giây rồi chưa nhận được gói nào — kiểm "
                   "tra tường lửa của máy, LocalIP đã đúng card nối với đài "
                   "chưa, và đài đã phát chưa")
                    .arg(m_linkClock.elapsed() / 1000), true);
        } else {
            m_connectionTab->setStatusText(tr("Đã mở cổng — đang chờ dữ liệu"));
        }
        return;
    }

    // Có gói về nhưng không gói nào đúng giao thức: mạng thông, sai chỗ khác.
    if (s.rawV == 0 && s.rawP == 0) {
        m_connectionTab->setStatusText(
            tr("Nhận được %1 gói nhưng không gói nào đúng giao thức RAW_V/RAW_P "
               "— nhiều khả năng sai cổng").arg(s.other), true);
        return;
    }

    QString text = tr("Đang nhận — RAW_V: %1, RAW_P: %2").arg(s.rawV).arg(s.rawP);
    if (s.other > 0)
        text += tr(", gói lạ: %1").arg(s.other);
    if (s.droppedV > 0 || s.droppedP > 0 || s.droppedRec > 0) {
        text += tr(" — bỏ bớt %1 lượt quét, %2 chu kỳ điểm dấu, %3 gói ghi lưu")
                    .arg(s.droppedV).arg(s.droppedP).arg(s.droppedRec);
    }
    if (m_sender->activeCount() > 0) {
        text += tr(" | đang gửi qua %1 cổng — Plot: %2, Track: %3")
                    .arg(m_sender->activeCount())
                    .arg(m_sender->sentPlots())
                    .arg(m_sender->sentTracks());
    }
    m_connectionTab->setStatusText(text);
}

// ------------------------------------------------- cập nhật quỹ đạo tay ----

void MainWindow::changeTop(quint32 id, quint32 top)
{
    if (top == 0) {
        QMessageBox::warning(this, appinfo::displayName(),
                             tr("Số đầu tốp phải là số nguyên dương."));
        refreshTrackList();
        return;
    }

    if (!m_tracker.setTop(id, top)) {
        // Trùng số đầu tốp là lỗi phải nói ra: hai quỹ đạo cùng số thì mọi
        // trao đổi bằng lời giữa các vị trí đều hỏng.
        QMessageBox::warning(this, appinfo::displayName(),
                             tr("Số đầu tốp %1 đã có quỹ đạo khác dùng — "
                                "chọn số khác.").arg(top));
    }
    refreshTrackList();
}

void MainWindow::changeAltitude(quint32 id, quint32 metres)
{
    if (Track *t = m_tracker.find(id))
        t->altitudeManual = metres;
    refreshTrackList();
}

void MainWindow::changeClassify(quint32 id, quint32 classify)
{
    if (Track *t = m_tracker.find(id))
        t->classify = classify;
    refreshTrackList();
    m_radar->update();
}

void MainWindow::removeTrack(quint32 id)
{
    m_tracker.remove(id);
    // Gửi ngay chứ không chờ nhịp vẽ: xoá bằng tay vẫn làm được lúc đã dừng
    // kết nối, mà lúc đó nhịp vẽ không chạy.
    flushRemovedTracks();
    refreshTrackList();
    m_radar->update();
}

void MainWindow::showTrackMenu(quint32 id, const QPoint &globalPos)
{
    const Track *t = m_tracker.find(id);
    if (!t)
        return;

    const quint32 oldTop      = t->top;
    const quint32 oldAltitude = t->altitude();

    QMenu menu(this);
    menu.setTitle(tr("Tốp %1").arg(oldTop));

    QAction *topAct = menu.addAction(tr("Đổi đầu tốp..."));
    QAction *altAct = menu.addAction(tr("Nhập độ cao (mét)..."));

    QMenu *classifyMenu = menu.addMenu(tr("Nhận dạng"));
    QAction *noneAct = classifyMenu->addAction(tr("Chưa xác định"));
    noneAct->setData(0u);
    classifyMenu->addSeparator();
    for (int i = 0; i < m_settings.classifyNames.size(); ++i) {
        QAction *a = classifyMenu->addAction(m_settings.classifyNames.at(i));
        a->setData(quint32(i + 1));
    }

    menu.addSeparator();
    QAction *delAct = menu.addAction(tr("Xoá quỹ đạo"));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    // Quỹ đạo có thể đã bị thuật toán xoá trong lúc menu đang mở — tra lại
    // trước khi đụng vào nó.
    if (!m_tracker.find(id))
        return;

    if (chosen == topAct) {
        bool ok = false;
        const int v = QInputDialog::getInt(this, tr("Đổi đầu tốp"),
                                           tr("Số đầu tốp mới:"), int(oldTop),
                                           1, 999999, 1, &ok);
        if (ok)
            changeTop(id, quint32(v));
    } else if (chosen == altAct) {
        bool ok = false;
        const int v = QInputDialog::getInt(this, tr("Nhập độ cao"),
                                           tr("Độ cao (mét):"), int(oldAltitude),
                                           0, 100000, 10, &ok);
        if (ok)
            changeAltitude(id, quint32(v));
    } else if (chosen == delAct) {
        removeTrack(id);
    } else if (chosen->parent() == classifyMenu) {
        changeClassify(id, chosen->data().toUInt());
    }
}

void MainWindow::showTrackInfo(quint32 id, const QPoint &globalPos)
{
    const Track *t = m_tracker.find(id);
    if (!t)
        return;

    if (!m_trackInfo)
        m_trackInfo = new TrackInfoPopup(this);

    // Đặt ngược hướng chuyển động, giống ô text theo dõi: phía trước quỹ đạo
    // là chỗ trắc thủ đang nhìn tới.
    const double a = (t->headingDeg() + 180.0) * geo::kDeg2Rad;
    const QPoint at = globalPos + QPoint(int(std::sin(a) * 24.0),
                                         int(-std::cos(a) * 24.0));
    m_trackInfo->showTrack(*t, m_settings, at);
    m_trackTab->selectTrack(id);
}

void MainWindow::showPlotList()
{
    if (!m_plotList)
        m_plotList = new PlotListWindow(this);
    m_plotList->show();
    m_plotList->raise();
    m_plotList->activateWindow();
}

void MainWindow::refreshLanHosts()
{
    // Bỏ trùng lặp nhưng giữ thứ tự xuất hiện trong bảng, để danh sách trong
    // cửa sổ trạng thái đọc theo được với bảng cổng.
    QStringList hosts;
    QSet<QString> seen;
    const auto add = [&hosts, &seen](const QString &ip) {
        const QString s = ip.trimmed();
        if (s.isEmpty() || s == QLatin1String("0.0.0.0"))
            return;   // "mọi máy" thì không có gì mà ping
        if (!seen.contains(s)) {
            seen.insert(s);
            hosts << s;
        }
    };

    for (const QVector<NetEndpoint> *list : {&m_params.rx, &m_params.tx}) {
        for (const NetEndpoint &e : *list) {
            add(e.localIp);
            add(e.remoteIp);
        }
    }
    m_lan->setHosts(hosts);
}
