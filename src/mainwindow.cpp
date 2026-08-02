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
#include "player.h"
#include "plotlistwindow.h"
#include "radarview.h"
#include "recorder.h"
#include "recordtab.h"
#include "settingstab.h"
#include "tilecache.h"
#include "trackinfopopup.h"
#include "tracklisttab.h"
#include "trackparamsdialog.h"
#include "udplink.h"
#include "udpsender.h"

#include <QApplication>
#include <QCloseEvent>
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

    // Ghi lưu và phát lại, mỗi cái một luồng riêng. Luồng mạng đẩy thẳng vào
    // hàng đợi của bộ ghi, không đi vòng qua luồng giao diện.
    m_recorder = new Recorder(this);
    m_link->setSpool(m_recorder->spool());
    m_player = new Player(this);

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
    connect(m_connectionTab, &ConnectionTab::sendStartRequested,
            this, &MainWindow::startSending);
    connect(m_connectionTab, &ConnectionTab::sendStopRequested,
            this, &MainWindow::stopSending);

    // --- ghi lưu và phát lại ---
    connect(m_recordTab, &RecordTab::recordStartRequested,
            this, &MainWindow::startRecording);
    connect(m_recordTab, &RecordTab::recordStopRequested,
            this, &MainWindow::stopRecording);
    connect(m_recordTab, &RecordTab::replayStartRequested,
            this, &MainWindow::startReplay);
    connect(m_recordTab, &RecordTab::replayStopRequested,
            this, &MainWindow::stopReplay);
    connect(m_recordTab, &RecordTab::replaySpeedChanged, this, [this](double v) {
        m_player->setSpeed(v);
    });

    connect(m_recorder, &Recorder::sessionsChanged,
            m_recordTab, &RecordTab::setSessions);
    connect(m_recorder, &Recorder::failed, this, [this](const QString &msg) {
        m_recordTab->setStatusText(msg, true);
    });
    connect(m_player, &Player::failed, this, [this](const QString &msg) {
        m_recordTab->setStatusText(msg, true);
    });
    connect(m_player, &Player::reachedEnd, this, [this] {
        // Hết file: vét nốt phần còn lại rồi trả nút bấm về trạng thái ban đầu.
        // Không xoá màn hình — trắc thủ còn xem lại khung hình cuối cùng, giống
        // hệt lúc dừng nhận dữ liệu.
        onTick();
        m_recordTab->setReplaying(false);
        m_recordTab->setStatusText(tr("Đã phát lại hết file"));
        syncTimers();
    });

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
    connect(m_trackTab, &TrackListTab::clearPlotsRequested,
            this, &MainWindow::clearAllPlots);
    connect(m_trackTab, &TrackListTab::clearTracksRequested,
            this, &MainWindow::clearAllTracks);
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
    connect(m_statusTick, &QTimer::timeout, this, [this] {
        refreshLinkStatus();
        refreshRecordStatus();
    });

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
    // Đi qua close() chứ không gọi thẳng quit(): phím tắt cũng phải hỏi lại như
    // khi bấm nút đóng cửa sổ, mà Ctrl+Q lại là phím dễ bấm nhầm hơn nhiều.
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this, [this] { close(); });
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
    m_connectionTab->setRunning(false);
    m_connectionTab->setTxRunning(false);
    m_trackTab->setSettings(m_settings);

    // Duyệt ./records ngay lúc khởi động, nhưng trên luồng ghi lưu: thư mục có
    // vài nghìn phiên thì việc mở từng file đọc 64 byte đủ để cửa sổ chậm hiện.
    m_recorder->refreshSessions();
    m_radar->setSettings(m_settings);
    m_ascope->setMaxRangeKm(m_settings.maxRangeKm);
    pushProcessingParams();
    refreshLanHosts();

    m_siteLabel->setText(tr("Tâm đài  %1")
                             .arg(formatLatLng(m_settings.siteLat, m_settings.siteLng)));
    updateCursorLabel(m_settings.siteLat, m_settings.siteLng);

    resize(1920, 1080);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QString text = tr("Thoát phần mềm?");
    if (m_recorder->isRunning()) {
        text += tr("\n\nĐang ghi lưu — file sẽ được đóng lại đầy đủ trước khi "
                   "thoát, phần đã ghi không mất.");
    }

    if (QMessageBox::question(this, appinfo::displayName(), text,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes) {
        e->ignore();
        return;
    }

    e->accept();

    // Các cửa sổ phụ (tham số chùm xung, tham số quỹ đạo, thông tin chi tiết
    // điểm dấu) cũng là cửa sổ, còn cái nào mở thì Qt chưa coi là đã đóng cửa
    // sổ cuối cùng. Gọi thẳng quit() để đóng cửa sổ chính là thoát hẳn.
    QApplication::quit();
}

QWidget *MainWindow::buildRightColumn()
{
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);

    m_connectionTab = new ConnectionTab;
    m_paramsTab     = new ParamsTab;
    m_recordTab     = new RecordTab;
    m_colorsTab     = new ColorsTab;
    m_settingsTab   = new SettingsTab;
    m_trackTab      = new TrackListTab;

    // Tab "Danh sách" không bọc trong vùng cuộn: bảng tự cuộn được rồi, bọc
    // thêm một lớp nữa là hai thanh cuộn lồng nhau.
    tabs->addTab(m_trackTab, tr("Danh sách"));

    // Tab "Kết nối" là tab mặc định: mở phần mềm lên thì việc đầu tiên bao giờ
    // cũng là bấm cho dữ liệu chảy vào.
    auto *connectionScroll = wrapInScroll(m_connectionTab);
    tabs->addTab(connectionScroll, tr("Kết nối"));
    tabs->addTab(wrapInScroll(m_paramsTab), tr("Tham số"));
    tabs->addTab(wrapInScroll(m_recordTab), tr("Ghi lưu"));
    tabs->addTab(wrapInScroll(m_colorsTab), tr("Màu sắc"));
    tabs->addTab(wrapInScroll(m_settingsTab), tr("Cài đặt"));
    tabs->setCurrentWidget(connectionScroll);

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
    // "Gửi", vì dòng tắt thì không giữ socket nào cả. Công tắc chung đang tắt
    // thì bảng mới cũng không mở socket nào.
    m_sender->setEndpoints(m_txOn ? m_params.tx : QVector<NetEndpoint>{});
    m_params.save();
    refreshLanHosts();
}

void MainWindow::sendPlots(const QVector<PlotTC> &plots)
{
    // Điểm dấu vừa sinh ra phải đi được ra hai nơi: cổng gửi và file ghi lưu.
    // Gói tin dựng một lần rồi dùng cho cả hai — hai nơi cần đúng cùng một dãy
    // byte, mà dựng nó không phải là việc rẻ.
    const bool tx  = m_sender->activeCount() > 0;
    const bool log = m_recorder->wantsProc();
    if (!tx && !log)
        return;

    for (const PlotTC &p : plots) {
        const QByteArray dg = packetio::buildPlot(p);
        if (tx)
            m_sender->send(TxKind::Plot, dg);
        if (log)
            m_recorder->push(rec::RecType::Plot, dg);
    }
}

void MainWindow::sendPendingTracks()
{
    const bool tx  = m_sender->activeCount() > 0;
    const bool log = m_recorder->wantsProc();

    // Vẫn phải rút dấu kể cả khi không gửi và không ghi, nếu không thì lúc bật
    // lên sẽ phụt ra một loạt gói cũ tích từ trước đó.
    m_tracker.takePending([this, tx, log](const Track &t) {
        if (!tx && !log)
            return;
        const QByteArray dg = packetio::buildTrack(t);
        if (tx)
            m_sender->send(TxKind::Track, dg);
        if (log)
            m_recorder->push(rec::RecType::Track, dg);
    });
}

void MainWindow::flushRemovedTracks()
{
    // Trạng thái "xoá" (6) phải đi được ra ngoài, dù quỹ đạo bị xoá bằng tay
    // hay bằng thuật toán: hệ thống nhận không biết thì nó giữ quỹ đạo đó trên
    // màn hình của mình vĩnh viễn. Lý do đó áp cho cả file ghi lưu — phát lại
    // mà thiếu gói xoá thì quỹ đạo nằm lại trên màn hình tới hết phiên.
    const bool log = m_recorder->wantsProc();
    for (const Track &t : m_tracker.takeRemoved()) {
        const QByteArray dg = packetio::buildTrack(t);
        m_sender->send(TxKind::Track, dg);
        if (log)
            m_recorder->push(rec::RecType::Track, dg);
    }
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

void MainWindow::resetProcessing()
{
    m_video.clear();
    m_ascope->clearTrace();
    m_fadeClock.restart();

    // Mở một dòng dữ liệu mới thì xoá sạch trạng thái xử lý: chùm xung dở dang
    // và quỹ đạo của lần trước không còn liên quan gì tới dữ liệu sắp tới.
    m_beams.reset();
    m_tracker.clear();
    m_plots.clear();
    m_lastPAzimuth = -1;
    m_scanProgress = 0;
    m_lastScanMs   = m_clock.elapsed();
    refreshTrackList();
    pushProcessingParams();
}

void MainWindow::syncTimers()
{
    // Nhịp vẽ chạy khi còn nguồn dữ liệu nào đang chảy; nhịp trạng thái chạy
    // thêm cả lúc chỉ ghi lưu, vì đồng hồ "Thời gian ghi" phải nhích mỗi giây
    // kể cả khi màn hình đứng yên.
    const bool feeding = m_link->isRunning() || m_player->isRunning();
    if (feeding && !m_tick->isActive())
        m_tick->start();
    else if (!feeding && m_tick->isActive())
        m_tick->stop();

    const bool busy = feeding || m_recorder->isRunning();
    if (busy && !m_statusTick->isActive())
        m_statusTick->start();
    else if (!busy && m_statusTick->isActive())
        m_statusTick->stop();
}

void MainWindow::startLink()
{
    if (m_player->isRunning()) {
        QMessageBox::information(
            this, appinfo::displayName(),
            tr("Không bật được chức năng nhận dữ liệu khi đang phát lại. "
               "Dừng phát lại trước đã."));
        m_connectionTab->setRunning(false);
        return;
    }

    if (m_params.rx.isEmpty()) {
        m_connectionTab->setStatusText(tr("Chưa có cổng nào trong bảng"), true);
        return;
    }

    resetProcessing();

    m_connectionTab->setStatusText(QString());
    m_link->setZfbeat(m_params.zfbeat);
    m_link->start(m_params.rx);
    m_connectionTab->setRunning(true);
    m_linkClock.restart();

    syncTimers();
    refreshLinkStatus();
}

void MainWindow::stopLink()
{
    m_link->stop();
    m_connectionTab->setRunning(false);
    syncTimers();

    // Giữ nguyên hình đang có trên màn hình: dừng nhận dữ liệu chứ không phải
    // xoá màn hình, trắc thủ còn xem lại vệt cuối cùng.
    m_radar->update();
    refreshLinkStatus();
}

void MainWindow::startSending()
{
    if (m_params.tx.isEmpty()) {
        m_connectionTab->setStatusText(
            tr("Chưa có cổng nào trong bảng gửi dữ liệu"), true);
        return;
    }

    m_txOn = true;
    m_sender->setEndpoints(m_params.tx);
    m_connectionTab->setTxRunning(true);

    // Bảng có dòng nhưng không dòng nào tích ô "Gửi" thì bật nút cũng không có
    // gì đi ra — nói thẳng, thay vì để trắc thủ ngồi chờ một cổng không mở.
    if (m_sender->activeCount() == 0) {
        m_connectionTab->setStatusText(
            tr("Đã bật gửi dữ liệu nhưng chưa dòng nào tích ô \"Gửi\""), true);
    }
    refreshLinkStatus();
}

void MainWindow::stopSending()
{
    m_txOn = false;
    m_sender->setEndpoints({});
    m_connectionTab->setTxRunning(false);
    refreshLinkStatus();
}

// ------------------------------------------------------------- ghi lưu -----

void MainWindow::startRecording(bool raw, bool proc)
{
    if (!raw && !proc) {
        m_recordTab->setStatusText(
            tr("Chọn ít nhất một loại dữ liệu để ghi"), true);
        return;
    }

    // Đang phát lại thì chỉ được ghi lại **kết quả xử lý** của dòng dữ liệu gốc
    // đang tái hiện. Ghi dữ liệu gốc lúc này chỉ là chép lại chính file đang
    // đọc, còn phát lại dữ liệu đã xử lý mà ghi tiếp cũng vậy.
    if (m_player->isRunning() && (!m_replayRaw || raw || !proc)) {
        QMessageBox::information(
            this, appinfo::displayName(),
            tr("Khi đang phát lại chỉ ghi lưu được dữ liệu đã qua xử lý, và "
               "chỉ khi đang phát lại dữ liệu gốc.\n\n"
               "Bỏ ô \"Ghi dữ liệu gốc\", tích ô \"Ghi dữ liệu đã xử lý\", rồi "
               "bấm lại."));
        return;
    }

    m_recorder->start(raw, proc);
    m_recordTab->setRecording(true);
    m_recordTab->setStatusText(QString());
    syncTimers();
    refreshRecordStatus();
}

void MainWindow::stopRecording()
{
    m_recorder->stop();
    m_recordTab->setRecording(false);
    syncTimers();
}

// ------------------------------------------------------------ phát lại -----

void MainWindow::startReplay(const QString &path, bool raw, quint32 total,
                             double speed)
{
    // Ba chức năng phải tắt trước khi tái hiện: dữ liệu thật trộn với dữ liệu
    // phát lại thì cả hai đều thành vô nghĩa.
    if (m_link->isRunning())
        stopLink();
    if (m_txOn)
        stopSending();
    if (m_recorder->isRunning())
        stopRecording();

    m_replayRaw = raw;
    resetProcessing();

    m_player->start(path, total, speed);
    m_recordTab->setReplaying(true);
    m_recordTab->setStatusText(
        raw ? tr("Đang phát lại dữ liệu gốc — toàn bộ đường xử lý chạy lại từ đầu")
            : tr("Đang phát lại dữ liệu đã qua xử lý — bộ bám quỹ đạo đứng yên, "
                 "màn hình hiện đúng cái đã ghi"));
    syncTimers();
}

void MainWindow::stopReplay()
{
    m_player->stop();
    m_replayed.clear();
    m_recordTab->setReplaying(false);
    syncTimers();

    // Giữ nguyên hình cuối cùng, giống lúc dừng nhận dữ liệu.
    m_radar->update();
}

void MainWindow::recordSweep(const rawpkt::RawVSweep &s)
{
    if (m_recorder->wantsProc()) {
        m_recorder->push(rec::RecType::Video,
                         rec::packVideo(s.azimuth, s.video.data()));
    }
}

bool MainWindow::applyReplayItem(const rec::RecItem &item)
{
    switch (item.type) {
    case rec::RecType::RawV: {
        // Giải mã lại bằng ZFbeat **đang đặt**, không phải giá trị lúc ghi: đây
        // chính là cái lợi của việc ghi dữ liệu gốc — chỉnh lại tham số rồi xem
        // lại cùng một phiên.
        rawpkt::RawVSweep sweep;
        if (!rawpkt::isRawV(item.payload.constData(), item.payload.size()))
            break;
        rawpkt::decodeRawV(item.payload.constData(), m_params.zfbeat, sweep);
        m_video.addSweep(sweep);
        recordSweep(sweep);
        return true;
    }
    case rec::RecType::RawP: {
        rawpkt::RawPCycle cycle;
        if (rawpkt::isRawP(item.payload.constData(), item.payload.size())) {
            rawpkt::decodeRawP(item.payload.constData(), cycle);
            processCycle(cycle);
        }
        break;
    }
    case rec::RecType::Video: {
        rawpkt::RawVSweep sweep;
        if (!rec::unpackVideo(item.payload, sweep.azimuth, sweep.video.data()))
            break;
        sweep.azimuth %= rawpkt::kAzimuthSteps;
        m_video.addSweep(sweep);
        return true;
    }
    case rec::RecType::Plot: {
        PlotTC p;
        if (!packetio::parsePlot(item.payload.constData(), item.payload.size(), p))
            break;
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         p.azmDeg(), p.rangeKm(), p.lat, p.lng);
        p.bornMs = m_clock.elapsed();
        m_plots.add(p);
        if (m_plotList && m_plotList->isVisible())
            m_plotList->addPlots({p});
        // Gói đã có sẵn dạng byte đúng chuẩn — chuyển tiếp nguyên vẹn thay vì
        // dựng lại, để hệ thống nhận thấy đúng cái đã ghi.
        if (m_sender->activeCount() > 0)
            m_sender->send(TxKind::Plot, item.payload);
        break;
    }
    case rec::RecType::Track: {
        Track t;
        if (!packetio::parseTrack(item.payload.constData(), item.payload.size(), t))
            break;
        m_tracker.applyExternal(t, m_clock.elapsed());
        if (m_sender->activeCount() > 0)
            m_sender->send(TxKind::Track, item.payload);
        break;
    }
    case rec::RecType::Other:
        // Trạng thái hệ thống / lệnh điều khiển — chưa giải mã, giai đoạn sau.
        break;
    }
    return false;
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

    // Dữ liệu phát lại đi vào đúng chỗ của dữ liệu thật, chỉ khác nguồn.
    m_player->drain(m_replayed);
    bool newSweep = false;
    for (const rec::RecItem &it : m_replayed)
        newSweep |= applyReplayItem(it);
    m_replayed.clear();

    // Đường biên độ dựng lại **một lần** cho cả nhịp, giống hệt đường nhận thật
    // ở trên: rebuildPolyline() không rẻ, mà ở tốc độ 8x thì một nhịp có cả
    // trăm lượt quét — vẽ lại từng lượt là ném đi 99% công sức đó.
    if (newSweep)
        m_ascope->setTrace(m_video.lastSweep());

    const qint64 now = m_clock.elapsed();

    // Phát lại dữ liệu **đã xử lý** thì bộ bám đứng yên: quỹ đạo trong file đã
    // là kết quả cuối cùng của phiên trước. Cho thuật toán chạy ở đây là nó
    // ngoại suy rồi xoá mất chính những quỹ đạo vừa đọc lên.
    const bool tracking = !(m_player->isRunning() && !m_replayRaw);

    if (tracking) {
        // Đài ngừng phát RAW_P giữa chừng thì không ai chốt sổ vòng quét nữa —
        // tự chốt để quỹ đạo cũ còn được ngoại suy rồi xoá đi.
        if ((m_link->isRunning() || m_player->isRunning())
            && now - m_lastScanMs > kScanTimeoutMs)
            endScan();

        // Xoá quỹ đạo quá hạn theo đồng hồ, không theo vòng quét — xem expireStale().
        m_tracker.expireStale(now);
    }

    m_plots.expire(now, m_params.beam.showSec);

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
    // Phần đuôi về phía gửi luôn nối vào cuối dòng, kể cả khi không nhận dữ
    // liệu: lúc phát lại thì gửi vẫn bật được, mà đó cũng chính là lúc cần nhìn
    // thấy số gói đang đi ra nhất.
    const auto txSuffix = [this]() -> QString {
        if (m_sender->activeCount() == 0)
            return {};
        return tr(" | đang gửi qua %1 cổng — Plot: %2, Track: %3")
                   .arg(m_sender->activeCount())
                   .arg(m_sender->sentPlots())
                   .arg(m_sender->sentTracks());
    };

    const LinkStats s = m_link->stats();
    if (!m_link->isRunning()) {
        m_connectionTab->setStatusText(
            tr("Đã dừng nhận — nhận được %1 gói RAW_V").arg(s.rawV) + txSuffix());
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
    m_connectionTab->setStatusText(text + txSuffix());
}

void MainWindow::refreshRecordStatus()
{
    if (m_recorder->isRunning())
        m_recordTab->setRecordStats(m_recorder->stats());
    if (m_player->isRunning())
        m_recordTab->setReplayStats(m_player->stats());
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

void MainWindow::clearAllPlots()
{
    // Chỉ lớp đang vẽ. Cửa sổ "Thông tin chi tiết điểm dấu" là một dòng chảy
    // riêng và đã có nút xoá của nó — xoá lây sang đó thì trắc thủ mất luôn
    // phần đang đọc dở.
    m_plots.clear();
    m_radar->update();
}

void MainWindow::clearAllTracks()
{
    const int n = m_tracker.tracks().size();
    if (n == 0)
        return;

    // Hỏi lại vì đây là thao tác hàng loạt và mất cả phần trắc thủ nhập tay
    // (đầu tốp, độ cao, phân loại) — thứ thuật toán không dựng lại được.
    if (QMessageBox::question(
            this, appinfo::displayName(),
            tr("Xoá toàn bộ %1 quỹ đạo đang có?\n\n"
               "Số đầu tốp, độ cao và phân loại đã nhập tay sẽ mất theo. Hệ "
               "thống nhận cũng được báo trạng thái xoá.").arg(n),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    // Đi qua đúng đường xoá bằng tay của từng quỹ đạo, để cái nào cũng được
    // chuyển sang trạng thái "xoá" (6) và báo ra ngoài. Chép danh sách định
    // danh trước vì remove() sửa ngay trên danh sách đang duyệt.
    QVector<quint32> ids;
    ids.reserve(n);
    for (const Track &t : m_tracker.tracks())
        ids.push_back(t.id);
    for (quint32 id : ids)
        m_tracker.remove(id);

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
