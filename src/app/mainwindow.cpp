#include "app/mainwindow.h"

#include "app/appinfo.h"
#include "ui/adf4159window.h"
#include "ui/ascope.h"
#include "ui/beamparamsdialog.h"
#include "ui/colorstab.h"
#include "ui/compacttabs.h"
#include "ui/connectiontab.h"
#include "ui/controltab.h"
#include "ui/filterwindow.h"
#include "proc/filterdata.h"
#include "maps/geo.h"
#include "ui/lanstatus.h"
#include "net/packetio.h"
#include "net/syncproto.h"
#include "ui/paramstab.h"
#include "record/player.h"
#include "ui/plotlistwindow.h"
#include "ui/radarview.h"
#include "record/recorder.h"
#include "ui/recordtab.h"
#include "ui/settingstab.h"
#include "maps/tilecache.h"
#include "ui/trackinfopopup.h"
#include "ui/tracklisttab.h"
#include "ui/trackparamsdialog.h"
#include "net/udplink.h"
#include "net/udpsender.h"

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
#include <QTime>
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
    const bool hadParams = m_params.load();
    if (!hadParams || m_params.rx.isEmpty())
        m_params.rx = AppParams::defaultRx();
    // Bảng cổng gửi chỉ dựng sẵn khi **chưa từng có** file tham số: người dùng
    // xoá hết dòng đi là có ý của họ, dựng lại sau mỗi lần khởi động thì dòng
    // vừa xoá cứ mọc lại mãi.
    if (!hadParams)
        m_params.tx = AppParams::defaultTx();

    // Thư mục ./filter dựng ngay lúc chạy, kể cả khi chưa ai mở cửa sổ bộ lọc:
    // người lắp đặt phải tự copy bốn file hệ số vào đó, mà thư mục chưa tồn tại
    // thì không có chỗ nào trên máy chỉ ra rằng phải copy vào đâu.
    filterdata::ensureDir();

    m_radar = new RadarView(this);
    m_radar->setVideo(&m_video);
    m_radar->setSources(&m_plots, &m_rawPlots, &m_tracker);

    m_link = new UdpLink(this);
    m_link->setVideoScale(m_params.videoScale());

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
    connect(m_connectionTab, &ConnectionTab::statusFollowsCommandChanged,
            this, &MainWindow::applyStatusFollowsCommand);
    connect(m_sender, &UdpSender::failed, this, [this](const QString &msg) {
        m_connectionTab->setStatusText(msg, true);
    });
    // Trạng thái về trên chính socket gửi lệnh đi đúng con đường của trạng thái
    // nhận ở bảng cổng nhận: vào tab "Điều khiển" và vào file ghi lưu. Chỗ này
    // đã ở luồng giao diện rồi nên không phải qua hàng đợi nào.
    connect(m_sender, &UdpSender::statusReceived, this, [this](const QByteArray &dg) {
        if (m_recorder->wantsProc())
            m_recorder->push(rec::RecType::Other, dg);
        routeStatus(dg);
    });
    connect(m_connectionTab, &ConnectionTab::connectRequested,
            this, &MainWindow::startLink);
    connect(m_connectionTab, &ConnectionTab::disconnectRequested,
            this, &MainWindow::stopLink);
    connect(m_connectionTab, &ConnectionTab::sendStartRequested,
            this, &MainWindow::startSending);
    connect(m_connectionTab, &ConnectionTab::sendStopRequested,
            this, &MainWindow::stopSending);
    // Nút "Thoát phần mềm" đi qua close() chứ không gọi quit() thẳng: nó phải
    // chịu đúng cái chặn và câu hỏi lại của closeEvent(), y như dấu nhân trên
    // thanh tiêu đề.
    connect(m_connectionTab, &ConnectionTab::exitRequested, this, &MainWindow::close);

    // --- lệnh điều khiển ---
    connect(m_controlTab, &ControlTab::commandReady,
            this, &MainWindow::sendCommand);
    connect(m_controlTab, &ControlTab::valuesChanged, this, [this] {
        m_params.control = m_controlTab->values();
        // ZFbeat, GainU và DataSend đều nằm trong ba gói lệnh này, mà cả ba đều
        // là đầu vào của phép tính Video[1024] — đổi một cái là nền tạp phải
        // đổi theo ngay từ gói kế tiếp.
        pushVideoScale();
        m_params.save();
    });
    connect(m_controlTab, &ControlTab::statusReceived,
            this, &MainWindow::applyStatusToParams);
    connect(m_controlTab, &ControlTab::adf4159Requested,
            this, &MainWindow::showAdf4159);
    connect(m_controlTab, &ControlTab::filterRequested,
            this, &MainWindow::showFilterWindow);
    connect(m_controlTab, &ControlTab::lockChanged, this, [this](bool locked) {
        // Hai cửa sổ điều khiển riêng cũng ra lệnh cho đài, mà nút mở chúng thì
        // nằm trong tab vừa bị khoá — để cửa sổ mở tiếp thì khoá điều khiển
        // chẳng khoá được đường lệnh nào cả.
        if (locked) {
            if (m_adfWindow)
                m_adfWindow->close();
            if (m_filterWindow)
                m_filterWindow->close();
            return;
        }
        // Vừa chiếm quyền điều khiển: nói cho các máy tính khác trong hệ thống
        // biết, để chúng tự khoá lại. Tín hiệu này chỉ phát khi trạng thái thật
        // sự đổi (xem ControlTab::lockChanged), nên đúng một gói cho một cú bấm.
        broadcastCtrlSync();
    });

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

    // Khoanh vùng cấm khởi tạo trên bản đồ: tab "Tham số" mở chế độ, panel 1
    // nhận hai lần bấm chuột rồi trả về một dòng đã tính sẵn phương vị / cự ly.
    connect(m_paramsTab, &ParamsTab::drawZoneRequested, this, [this] {
        m_radar->setZoneDrawMode(!m_radar->zoneDrawMode());
    });
    connect(m_radar, &RadarView::zoneDrawModeChanged,
            m_paramsTab, &ParamsTab::setDrawingZone);
    connect(m_radar, &RadarView::zoneDrawn,
            m_paramsTab, &ParamsTab::addZoneFromMap);

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
    m_controlTab->setValues(m_params.control);
    m_connectionTab->setParams(m_params);
    m_connectionTab->setRunning(false);
    m_connectionTab->setTxRunning(false);

    // Mở sẵn socket của các dòng "Command". setParams() ở trên cố ý không phát
    // tín hiệu (nó chỉ đổ dữ liệu vào bảng), nên nếu không gọi ở đây thì tới
    // tận lúc trắc thủ sửa bảng cổng mới có socket nào — mà lệnh điều khiển
    // đầu tiên của ca trực thì đi trước lúc đó rất lâu.
    m_sender->setReceiveStatus(m_params.statusFollowsCommand);
    m_sender->setEndpoints(activeTxEndpoints());
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
    refreshExitState();

    resize(1920, 1080);
}

QStringList MainWindow::exitBlockers() const
{
    QStringList busy;
    if (m_link->isRunning())
        busy << tr("Nhận dữ liệu");
    if (m_txOn)
        busy << tr("Gửi dữ liệu");
    if (m_recorder->isRunning())
        busy << tr("Ghi lưu");
    if (m_player->isRunning())
        busy << tr("Phát lại");
    return busy;
}

void MainWindow::refreshExitState()
{
    m_connectionTab->setExitBlockers(exitBlockers());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // Bốn chức năng này phải do chính trắc thủ tắt, phần mềm không tự tắt hộ:
    // mỗi cái đều là một việc đang chạy dở mà người ngồi trước máy mới biết đã
    // xong hay chưa — nhất là ghi lưu, tắt hộ thì file dừng ở một chỗ không ai
    // chọn.
    const QStringList busy = exitBlockers();
    if (!busy.isEmpty()) {
        QMessageBox::information(
            this, appinfo::displayName(),
            tr("Tắt các chức năng sau rồi mới thoát được phần mềm:\n\n• %1")
                .arg(busy.join(QStringLiteral("\n• "))));
        e->ignore();
        return;
    }

    if (QMessageBox::question(this, appinfo::displayName(), tr("Thoát phần mềm?"),
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
    // Bảy tab với bề rộng mặc định thì tab cuối bị đẩy ra ngoài panel —
    // CompactTabWidget tự bớt đệm cho vừa. Xem CompactTabBar.
    auto *tabs = new CompactTabWidget;
    tabs->setDocumentMode(true);

    m_connectionTab = new ConnectionTab;
    m_controlTab    = new ControlTab;
    m_paramsTab     = new ParamsTab;
    m_recordTab     = new RecordTab;
    m_colorsTab     = new ColorsTab;
    m_settingsTab   = new SettingsTab;
    m_trackTab      = new TrackListTab;

    // Tab "Danh sách" không bọc trong vùng cuộn: bảng tự cuộn được rồi, bọc
    // thêm một lớp nữa là hai thanh cuộn lồng nhau.
    tabs->addTab(m_trackTab, tr("Danh sách"));
    // Tab "Điều khiển" cũng không bọc: nó tự cuộn phần thân của mình, để hai
    // nút khóa/mở khóa và ADF4159 ghim được ở đầu tab. Xem ControlTab.
    tabs->addTab(m_controlTab, tr("Điều khiển"));

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

    // Panel 2.1 (80% chiều dọc) | Panel 2.2 (20%)
    auto *vSplit = new QSplitter(Qt::Vertical);
    vSplit->setChildrenCollapsible(false);
    vSplit->setHandleWidth(2);
    vSplit->addWidget(tabs);
    vSplit->addWidget(m_ascope);
    vSplit->setStretchFactor(0, 8);
    vSplit->setStretchFactor(1, 2);
    vSplit->setSizes({800, 200});
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
    m_sweepLabel  = makeStatusLabel();
    m_siteLabel   = makeStatusLabel();

    // Nhóm giữa nằm chính giữa thanh: hai bên cùng hệ số giãn, **và** cùng bỏ
    // qua bề rộng mong muốn của mình (QSizePolicy::Ignored). Chỉ đặt hệ số giãn
    // thôi thì chưa đủ — hệ số chỉ chia phần dư, mà nhóm phải còn rộng hơn nhóm
    // trái cả trăm điểm ảnh, nên nhóm giữa bị đẩy lệch hẳn sang trái.
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
    centreLay->addWidget(m_sweepLabel);

    auto *right = new QWidget(bar);
    auto *rightLay = new QHBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);
    rightLay->addStretch(1);
    rightLay->addWidget(m_siteLabel);

    left->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    right->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 0, 6, 0);
    lay->setSpacing(0);
    lay->addWidget(left, 1);
    lay->addWidget(centre, 0);
    lay->addWidget(right, 1);

    updateSweepLabel();
    return bar;
}

// ------------------------------------------------------------- cài đặt -----

void MainWindow::applySettings(const AppSettings &s)
{
    // Tab "Cài đặt" không giữ màu và danh sách phân loại, nên bản nó gửi lên
    // mang giá trị mặc định — giữ lại phần của mình chứ không nhận đè.
    const AppColors   colors  = m_settings.colors;
    const QStringList classes = m_settings.classifyNames;

    const bool hadRawPlots = m_settings.showRawPlots;

    m_settings = s;
    m_settings.colors        = colors;
    m_settings.classifyNames = classes;

    // Tắt lớp đơn xung là bỏ luôn phần đã gom: không tắt ngay thì bật lại sau
    // đó vài giây, đám chấm cũ hiện lên lại như chưa từng tắt.
    if (hadRawPlots && !m_settings.showRawPlots)
        m_rawPlots.clear();

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
    m_params.multiV         = p.multiV;
    m_params.autoFromStatus = p.autoFromStatus;
    m_params.autoRange      = p.autoRange;
    m_params.sectors        = p.sectors;
    m_params.noInitZones    = p.noInitZones;
    m_params.showNoInitZones = p.showNoInitZones;
    m_params.clampToRange();

    pushVideoScale();
    if (m_params.autoRange)
        updateRangeFromParams();

    pushProcessingParams();
    m_params.save();
}

void MainWindow::pushVideoScale()
{
    m_link->setVideoScale(m_params.videoScale());
}

void MainWindow::applyStatusToParams(int group)
{
    if (!m_params.autoFromStatus || group != cmdproto::GroupDspR)
        return;

    const cmdproto::Packet &p = cmdproto::kPackets[cmdproto::GroupDspR];
    const int at = cmdproto::indexOf(p, "ADC_Sample_Rate");
    quint32 raw = 0;
    if (at < 0 || !m_controlTab->statusValue(group, "ADC_Sample_Rate", raw))
        return;

    // Ngoài dải hợp lệ thì **bỏ qua**, không kẹp lại: đài chưa đặt tần số lấy
    // mẫu thì nó trả về 0, mà kẹp 0 lên thành 0.1 là âm thầm đổi cả thang cự ly
    // sang một con số không ai chọn.
    const double fs = cmdproto::toUi(p.fields[at], raw);
    if (fs < AppParams::kFsMin || fs > AppParams::kFsMax
        || qFuzzyCompare(fs, m_params.fs))
        return;

    m_params.fs = fs;
    m_paramsTab->setFs(fs);   // không phát tín hiệu ngược lại
    if (m_params.autoRange)
        updateRangeFromParams();
    m_params.save();
}

void MainWindow::pushProcessingParams()
{
    m_beams.setParams(m_params.beam);
    m_beams.setSectors(m_params.sectors);
    // Ô cự ly quy ra mét theo đúng cự ly tối đa đang dùng để vẽ, không phải
    // theo giá trị tính từ Fs/B/Tc — hai giá trị đó lệch nhau khi trắc thủ tự
    // nhập cự ly tối đa bên tab "Cài đặt".
    m_beams.setMaxRangeMeters(m_settings.maxRangeKm * 1000.0);

    m_tracker.setParams(m_params.track);
    m_tracker.setSite(m_settings.siteLat, m_settings.siteLng);
    m_tracker.setHistoryLimit(AppSettings::kMaxHistory);
    m_tracker.setMaxRangeMeters(m_settings.maxRangeKm * 1000.0);
    m_tracker.setNoInitZones(m_params.noInitZones);

    m_radar->setNoInitZones(m_params.noInitZones, m_params.showNoInitZones);
    m_radar->setDrawPredictWindow(m_params.track.drawWindow);
}

void MainWindow::applyEndpoints(const QVector<NetEndpoint> &rx)
{
    m_params.rx = rx;
    m_params.save();
    refreshLanHosts();
}

QVector<NetEndpoint> MainWindow::activeRxEndpoints() const
{
    if (!m_params.statusFollowsCommand)
        return m_params.rx;

    QVector<NetEndpoint> out;
    out.reserve(m_params.rx.size());
    for (const NetEndpoint &e : m_params.rx) {
        if (!AppParams::isStatusRow(e))
            out.push_back(e);
    }
    return out;
}

void MainWindow::applyStatusFollowsCommand(bool on)
{
    m_params.statusFollowsCommand = on;
    m_sender->setReceiveStatus(on);
    m_params.save();
    // Bảng cổng nhận bị khoá trong lúc đang nhận (ConnectionTab::setRunning),
    // ô này cũng vậy — nên không có cảnh dòng "Status" vừa bị bỏ qua mà socket
    // của nó vẫn đang mở.
}

QVector<NetEndpoint> MainWindow::activeTxEndpoints() const
{
    QVector<NetEndpoint> out;
    out.reserve(m_params.tx.size());
    for (const NetEndpoint &e : m_params.tx) {
        if (e.alwaysSends() || m_txOn)
            out.push_back(e);
    }
    return out;
}

void MainWindow::applyTxEndpoints(const QVector<NetEndpoint> &tx)
{
    m_params.tx = tx;
    // Mở lại toàn bộ socket gửi theo bảng mới — kể cả khi chỉ bật/tắt một ô
    // "Gửi", vì dòng tắt thì không giữ socket nào cả.
    m_sender->setEndpoints(activeTxEndpoints());

    // Bảng vừa đổi thì đáng nhắc lại một lần nữa nếu cổng lệnh vẫn còn thiếu.
    m_warnedNoCommandPort = false;
    m_params.save();
    refreshLanHosts();
}

QString MainWindow::noCommandPortMsg()
{
    // Không có dòng "Command" nào mở được: hoặc bảng cổng gửi không còn dòng
    // đó, hoặc dòng đó chưa tích ô "Gửi", hoặc địa chỉ đích chưa hợp lệ. Cả ba
    // đều dẫn tới cùng một việc phải làm nên nói chung một câu.
    return tr("Chưa gửi được lệnh điều khiển — vào tab \"Kết nối\", bảng \"Cổng "
              "UDP gửi dữ liệu\", cấu hình một dòng loại \"Command\" với RemoteIP "
              "và RemotePort của đài rồi tích ô \"Gửi\".");
}

void MainWindow::sendCommand(int group, const QByteArray &datagram)
{
    const bool ok = m_sender->send(TxKind::Command, datagram);
    m_controlTab->setSendResult(group, ok);
    if (ok) {
        m_controlTab->setStatusText(
            tr("Đã gửi %1 lệnh điều khiển").arg(m_sender->sentCommands()));
        return;
    }

    m_controlTab->setStatusText(noCommandPortMsg(), true);
    warnNoCommandPort();
}

void MainWindow::sendAdfCommand(int kind, const QByteArray &datagram)
{
    // Cùng dòng "Command" với bốn gói lệnh của tab "Điều khiển": kit ADF4159
    // nằm trong cùng một đài, không có đường gửi riêng.
    const bool ok = m_sender->send(TxKind::Command, datagram);
    m_adfWindow->setSendResult(kind, ok);
    if (!ok) {
        m_adfWindow->setStatusText(noCommandPortMsg(), true);
        warnNoCommandPort();
    }
}

void MainWindow::sendFilterCommand(int kind, const QByteArray &datagram)
{
    // Cùng dòng "Command" với mọi lệnh khác: bộ lọc nằm trong chính khối DSP của
    // đài, không có đường gửi riêng.
    const bool ok = m_sender->send(TxKind::Command, datagram);
    m_filterWindow->setSendResult(kind, ok);
    if (!ok) {
        m_filterWindow->setStatusText(noCommandPortMsg(), true);
        warnNoCommandPort();
    }
}

void MainWindow::warnNoCommandPort()
{
    // Hộp thoại đúng một lần: chưa cấu hình thì nút nào vặn cũng hụt, mà mỗi
    // lần hụt một hộp thoại thì không dùng nổi giao diện.
    //
    // Lùi sang nhịp sự kiện kế tiếp chứ không mở ngay: chỗ này đang nằm giữa
    // chuỗi tín hiệu của một ô nhập vừa đổi giá trị, mà hộp thoại chặn thì
    // chuỗi đó dừng lại giữa chừng cho tới khi người dùng bấm nút.
    if (m_warnedNoCommandPort)
        return;

    m_warnedNoCommandPort = true;
    QTimer::singleShot(0, this, [this] {
        QMessageBox::warning(this, appinfo::displayName(), noCommandPortMsg());
    });
}

void MainWindow::showAdf4159()
{
    if (!m_adfWindow) {
        m_adfWindow = new Adf4159Window(this);
        connect(m_adfWindow, &Adf4159Window::commandReady,
                this, &MainWindow::sendAdfCommand);
        connect(m_adfWindow, &Adf4159Window::settingsChanged, this, [this] {
            m_params.adf = m_adfWindow->settings();
            m_params.save();
        });
        m_adfWindow->setSettings(m_params.adf);
        // Cửa sổ này rộng nên đặt lệch hẳn sang trái, khác hai cửa sổ tham số
        // vốn nép vào khu vực panel 2.
        m_adfWindow->move(mapToGlobal(QPoint(40, 40)));
    }
    m_adfWindow->show();
    m_adfWindow->raise();
    m_adfWindow->activateWindow();
}

void MainWindow::showFilterWindow()
{
    if (!m_filterWindow) {
        m_filterWindow = new FilterWindow(this);
        connect(m_filterWindow, &FilterWindow::commandReady,
                this, &MainWindow::sendFilterCommand);
    }
    // Đọc lại đĩa **mỗi lần mở**, khác cửa sổ ADF4159 (cửa sổ ấy giữ nguyên giá
    // trị đang có trên màn hình). Thư mục ./filter là chỗ người lắp đặt copy file
    // vào, nên sửa file rồi mở lại cửa sổ phải thấy ngay giá trị mới.
    m_filterWindow->reload();
    m_filterWindow->show();
    m_filterWindow->raise();
    m_filterWindow->activateWindow();
}

void MainWindow::broadcastCtrlSync()
{
    // Địa chỉ đi trong gói là LocalIP của dòng "CtrlSync_S" — đúng như mô tả giao
    // thức. Không lấy địa chỉ của card mạng đang thật sự đi ra: hai thứ có thể
    // khác nhau (LocalIP để trống nghĩa là "theo bảng định tuyến"), mà bên nhận
    // thì so trường này với LocalIP của dòng "CtrlSync_R" của chính nó.
    const QString local = m_params.ctrlSyncTxLocalIp();
    if (local.isEmpty()) {
        m_controlTab->setCtrlIpText(QString(), false);
        m_controlTab->setStatusText(
            tr("Chưa đồng bộ được quyền điều khiển — vào tab \"Kết nối\", bảng "
               "\"Cổng UDP gửi dữ liệu\", thêm một dòng loại \"CtrlSync_S\"."),
            true);
        return;
    }

    // Trường CtrlIP chỉ có chỗ cho một địa chỉ IPv4. Ô LocalIP nhận cả địa chỉ
    // IPv6 (bảng chỉ kiểm tra "có phải địa chỉ hợp lệ không"), mà gửi đi thì nó
    // thành số 0 — các máy khác đọc ra "0.0.0.0" và không đối chiếu được với gì.
    bool ipv4 = false;
    const quint32 ctrlIp = QHostAddress(local).toIPv4Address(&ipv4);
    if (!ipv4) {
        m_controlTab->setCtrlIpText(QString(), false);
        m_controlTab->setStatusText(
            tr("LocalIP của dòng \"CtrlSync_S\" (%1) không phải địa chỉ IPv4 — "
               "gói chiếm quyền điều khiển không mang được địa chỉ nào.")
                .arg(local), true);
        return;
    }

    ++m_ctrlSyncSerial;
    const quint32 timeMs = quint32(QTime::currentTime().msecsSinceStartOfDay());
    const QByteArray dg =
        syncproto::build(ctrlIp, m_ctrlSyncSerial, timeMs);

    if (!m_sender->send(TxKind::CtrlSync, dg)) {
        // Gói không ra khỏi máy thì số Serial đó chưa dùng — trả lại, cùng lối
        // với hai đường gửi lệnh.
        --m_ctrlSyncSerial;
        m_controlTab->setStatusText(
            tr("Không gửi được gói chiếm quyền điều khiển — kiểm tra dòng "
               "\"CtrlSync_S\" trong bảng \"Cổng UDP gửi dữ liệu\"."), true);
    } else {
        m_controlTab->setStatusText(
            tr("Đã chiếm quyền điều khiển và báo cho các máy khác (%1 gói)")
                .arg(m_sender->sentCtrlSync()));
    }

    // Nhãn đi theo cú bấm nút, không đợi gói quảng bá vừa gửi về tới cổng nhận:
    // gói ấy có về hay không còn tuỳ mạng và tuỳ đã bấm "Bắt đầu nhận dữ liệu"
    // chưa, mà chính máy này thì đã mở khoá rồi.
    m_controlTab->setCtrlIpText(local, true);
}

void MainWindow::applyCtrlSync(const QByteArray &datagram)
{
    quint32 serial = 0, timeMs = 0, ctrlIp = 0;
    syncproto::parse(datagram.constData(), serial, timeMs, ctrlIp);
    const QHostAddress who(ctrlIp);

    // Gói quảng bá của chính máy này cũng về đúng cổng "CtrlSync_R" của nó —
    // cùng cổng, cùng dải quảng bá. Nhận ra bằng cách so trường CtrlIP với
    // LocalIP của dòng "CtrlSync_R", đúng như mô tả giao thức nêu. Gói của mình
    // thì bỏ qua: nhãn và trạng thái khoá đã xử lý ngay lúc bấm nút.
    const QString mine = m_params.ctrlSyncLocalIp();
    if (!mine.isEmpty()
        && QHostAddress(mine).isEqual(who, QHostAddress::TolerantConversion))
        return;

    m_controlTab->setCtrlIpText(who.toString(), false);
    if (!m_controlTab->isLocked()) {
        // Máy khác vừa chiếm quyền: khoá lại ngay. setLocked(true) cũng đóng hai
        // cửa sổ điều khiển riêng và kéo các ô về giá trị đài đang báo — đúng
        // việc phải làm, vì từ lúc này tab chỉ còn để theo dõi.
        m_controlTab->setLocked(true);
        m_controlTab->setStatusText(
            tr("Máy %1 vừa chiếm quyền điều khiển — máy này đã tự khoá lại.")
                .arg(who.toString()), true);
    }
}

void MainWindow::routeStatus(const QByteArray &datagram)
{
    // Gói chiếm quyền điều khiển đi chung hàng đợi với trạng thái lệnh (xem
    // StatusQueue), nên lọc nó ra ngay đây — nó không phải trạng thái phản hồi
    // của lệnh nào cả.
    if (syncproto::isSync(datagram.constData(), datagram.size())) {
        applyCtrlSync(datagram);
        return;
    }

    if (m_controlTab->applyStatus(datagram))
        return;
    // Trạng thái của kit ADF4159 và của lệnh nạp bộ lọc chỉ đọc được khi cửa sổ
    // tương ứng đã dựng — mà cả hai cũng chỉ trả lời khi có lệnh gửi đi, mà lệnh
    // thì chỉ gửi được từ chính cửa sổ đó. Nên chưa mở lần nào thì cũng chưa có
    // gì để mất.
    if (m_filterWindow && m_filterWindow->applyStatus(datagram))
        return;
    if (m_adfWindow)
        m_adfWindow->applyStatus(datagram);
}

void MainWindow::sendPlots(const QVector<PlotTC> &plots)
{
    // Điểm dấu vừa sinh ra phải đi được ra hai nơi: cổng gửi và file ghi lưu.
    // Gói tin dựng một lần rồi dùng cho cả hai — hai nơi cần đúng cùng một dãy
    // byte, mà dựng nó không phải là việc rẻ.
    const bool tx  = m_sender->activeCount(TxKind::Plot) > 0;
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
    const bool tx  = m_sender->activeCount(TxKind::Track) > 0;
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

    m_cursorLabel->setText(tr("Con trỏ  %1   %2° - %3 km")
                               .arg(formatLatLng(lat, lng))
                               .arg(bearingDeg, 0, 'f', 3)
                               .arg(distKm, 0, 'f', 3));
}

void MainWindow::updateSweepLabel()
{
    // Phương vị của lượt quét RAW_V mới nhất, đã quy ra độ. Chưa nhận được gói
    // nào thì lastAngleDeg() trả -1 — hiện 0.000 chứ không hiện số âm.
    const double deg = m_video.lastAngleDeg();
    m_sweepLabel->setText(
        tr("Đường quét  %1°").arg(deg < 0.0 ? 0.0 : deg, 0, 'f', 3));
}

// -------------------------------------------------------------- kết nối ----

void MainWindow::resetProcessing()
{
    m_video.clear();
    m_ascope->clearTrace();
    m_fadeClock.restart();
    updateSweepLabel();   // xoá nền tạp là mất luôn lượt quét cuối: về 0.000

    // Mở một dòng dữ liệu mới thì xoá sạch trạng thái xử lý: chùm xung dở dang
    // và quỹ đạo của lần trước không còn liên quan gì tới dữ liệu sắp tới.
    m_beams.reset();
    m_tracker.clear();
    m_plots.clear();
    m_rawPlots.clear();
    m_lastPAzimuth = -1;
    m_scanProgress = 0;
    m_lastVAzimuth = -1;
    m_scanProgressV = 0;
    m_lastRawPMs   = 0;
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

    // Ba trong bốn chức năng chặn đường thoát đi qua đây (nhận dữ liệu, ghi lưu,
    // phát lại); chức năng thứ tư — gửi dữ liệu — tự gọi lấy.
    refreshExitState();
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

    const QVector<NetEndpoint> rx = activeRxEndpoints();
    if (rx.isEmpty()) {
        // Bảng còn dòng mà danh sách có hiệu lực lại rỗng thì chỉ có một lý do:
        // dòng duy nhất còn lại là dòng "Status" đang bị ô tự động bỏ qua.
        m_connectionTab->setStatusText(
            m_params.rx.isEmpty()
                ? tr("Chưa có cổng nào trong bảng")
                : tr("Bảng chỉ còn dòng \"Status\", mà dòng đó đang bị bỏ qua — "
                     "thêm dòng cổng nhận dữ liệu, hoặc bỏ tích ô tự động"),
            true);
        return;
    }

    resetProcessing();

    m_connectionTab->setStatusText(QString());
    pushVideoScale();
    m_link->start(rx);
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
    m_sender->setEndpoints(activeTxEndpoints());
    m_connectionTab->setTxRunning(true);
    refreshExitState();

    // Bảng có dòng nhưng không dòng nào tích ô "Gửi" thì bật nút cũng không có
    // gì đi ra — nói thẳng, thay vì để trắc thủ ngồi chờ một cổng không mở.
    if (m_sender->activeCount(TxKind::Plot) == 0
        && m_sender->activeCount(TxKind::Track) == 0) {
        m_connectionTab->setStatusText(
            tr("Đã bật gửi dữ liệu nhưng chưa dòng nào tích ô \"Gửi\""), true);
    }
    refreshLinkStatus();
}

void MainWindow::stopSending()
{
    m_txOn = false;
    // Dòng "Command" vẫn giữ socket: nút này chỉ tắt dòng dữ liệu điểm dấu và
    // quỹ đạo, chứ không có lý do gì để tắt luôn đường điều khiển đài.
    m_sender->setEndpoints(activeTxEndpoints());
    m_connectionTab->setTxRunning(false);
    refreshExitState();
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
        rawpkt::decodeRawV(item.payload.constData(),
                           rawpkt::VideoGain::from(m_params.videoScale()), sweep);
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
        m_sender->send(TxKind::Plot, item.payload);
        break;
    }
    case rec::RecType::Track: {
        Track t;
        if (!packetio::parseTrack(item.payload.constData(), item.payload.size(), t))
            break;
        m_tracker.applyExternal(t, m_clock.elapsed());
        m_sender->send(TxKind::Track, item.payload);
        break;
    }
    case rec::RecType::Other:
        // Trạng thái lệnh điều khiển đã ghi lại thì phát lại được luôn: xem lại
        // một phiên là thấy đúng đài đang ở trạng thái nào lúc đó. Gói khác
        // (trạng thái hệ thống) thì applyStatus() trả về false và bỏ qua.
        routeStatus(item.payload);
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

    // Điểm dấu do hệ thống khác tính sẵn: bỏ qua hẳn bộ tách chùm, đi thẳng vào
    // bộ bám quỹ đạo.
    m_link->drainPlotTc(m_rxPlots);
    if (!m_rxPlots.isEmpty())
        processExternalPlots(m_rxPlots);

    // Trạng thái phản hồi lệnh điều khiển. Ra khỏi luồng mạng dưới dạng
    // datagram nguyên vẹn, tách trường ngay tại tab "Điều khiển".
    m_link->drainStatus(m_rxStatus);
    for (const QByteArray &dg : m_rxStatus)
        routeStatus(dg);

    // Chốt sổ vòng quét theo RAW_V khi nguồn không phát RAW_P — xem m_lastVAzimuth.
    if (!m_drained.isEmpty())
        trackScanFromVideo(m_drained.last().azimuth);

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
        // Đường quét chạy bằng đồng hồ khi không gói nào mang phương vị ăng-ten
        // — nguồn chỉ phát PlotTC, hay đài vừa ngừng phát giữa chừng. Có phương
        // vị thật thì hàm này tự đứng ngoài.
        m_tracker.tickClock(now);

        // Xoá quỹ đạo quá hạn theo đồng hồ, không theo vòng quét — xem expireStale().
        m_tracker.expireStale(now);
    }

    m_plots.expire(now, m_params.beam.showSec);

    // Đơn xung xoá trước điểm dấu tâm chùm đúng một giây. Chúng là **đầu vào**
    // của phép gom chùm: đám chấm biến mất trước, điểm dấu do chúng sinh ra còn
    // nán lại — nhìn là biết ngay cái nào đẻ ra cái nào.
    m_rawPlots.expire(now, qMax(1, m_params.beam.showSec - 1));

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

    updateSweepLabel();
    m_radar->update();
}

void MainWindow::processExternalPlots(const QVector<PlotTC> &plots)
{
    const qint64 now = m_clock.elapsed();

    for (const PlotTC &in : plots) {
        PlotTC p = in;
        // Toạ độ địa lý không nằm trong gói tin — tính một lần ở đây, đúng như
        // đường điểm dấu tính tại chỗ.
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         p.azmDeg(), p.rangeKm(), p.lat, p.lng);
        m_plots.add(p);
        m_tracker.addPlot(p, now);
    }

    if (m_plotList && m_plotList->isVisible())
        m_plotList->addPlots(plots);

    // Cố ý **không** gọi sendPlots(): gói đã được ghi lưu ngay ở luồng mạng, và
    // phát lại nguyên si thứ vừa nhận ra cổng gửi thì rất dễ thành vòng lặp khi
    // hai bảng cổng trỏ vào nhau.
}

void MainWindow::trackScanFromVideo(quint32 azimuth)
{
    // Còn RAW_P thì để RAW_P đẩy nhịp — nó mới là dòng mang điểm dấu.
    const qint64 now = m_clock.elapsed();
    if (m_lastRawPMs != 0 && now - m_lastRawPMs < kScanTimeoutMs) {
        m_lastVAzimuth = -1;
        m_scanProgressV = 0;
        return;
    }

    m_tracker.onSweep(rawpkt::azimuthToDeg(azimuth), now);

    const int a = int(azimuth % rawpkt::kAzimuthSteps);
    if (m_lastVAzimuth >= 0) {
        const int delta = a - m_lastVAzimuth;
        if (delta >= 0)
            m_scanProgressV += delta;
        else if (delta > -rawpkt::kAzimuthSteps / 2)
            ;   // lùi nhẹ: rung của encoder
        else if (m_scanProgressV >= rawpkt::kAzimuthSteps / 2) {
            endScan();
            m_scanProgressV = 0;
        }
    }
    m_lastVAzimuth = a;
}

void MainWindow::processCycle(const rawpkt::RawPCycle &cycle)
{
    m_lastRawPMs = m_clock.elapsed();

    const int azimuth = int(cycle.azimuth);

    // Đẩy đường quét của bộ bám **trước** khi tách chùm chu kỳ này: quỹ đạo nào
    // vừa bị đường quét bỏ lại phía sau thì chốt sổ ngay lúc này, để điểm dấu
    // sắp tính ra thuộc về vòng mới của nó chứ không phải vòng vừa khép.
    m_tracker.onSweep(rawpkt::azimuthToDeg(quint32(azimuth)), m_lastRawPMs);

    // Phương vị tụt hẳn về đầu dải là ăng-ten vừa quay hết một vòng — mốc duy
    // nhất để đo chu kỳ vòng quét.
    //
    // Chỉ nhìn một bước tụt là chưa đủ: một gói tới muộn, hay một nguồn thứ hai
    // cùng phát vào cổng này, cũng tạo ra đúng dấu hiệu đó. Mà đo nhầm thì chu
    // kỳ vòng quét ra một con số vô lý, kéo theo cửa sổ dự đoán của mọi quỹ đạo
    // sai theo. Nên đòi thêm điều kiện: ăng-ten phải thực sự quét được hơn nửa
    // vòng kể từ lần đo trước.
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

    // Trước khi gom chùm: lớp đơn xung phải nhận **mọi** plot trong gói, kể cả
    // những plot mà thuật toán sắp loại đi. Nó chính là cái để nhìn ra thuật
    // toán đã loại những gì.
    if (m_settings.showRawPlots)
        collectRawPlots(cycle, now);

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
    // Chốt sổ từng quỹ đạo **không** làm ở đây nữa: mỗi quỹ đạo tự chốt khi
    // đường quét đi qua cửa sổ dự đoán của nó (Tracker::onSweep). Chỗ này chỉ
    // còn một việc — đo chu kỳ một vòng quay, thứ duy nhất phải nhìn cả vòng
    // mới biết.
    const qint64 now = m_clock.elapsed();
    m_tracker.setScanPeriod(now - m_lastScanMs);
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
        // Chỉ đếm dòng dữ liệu: dòng "Command" luôn mở nên đưa vào đây thì dòng
        // trạng thái lúc nào cũng có cái đuôi này, kể cả khi chưa gửi gì.
        const int n = m_sender->activeCount(TxKind::Plot)
                    + m_sender->activeCount(TxKind::Track);
        if (n == 0)
            return {};
        return tr(" | đang gửi qua %1 cổng — Plot: %2, Track: %3")
                   .arg(n)
                   .arg(m_sender->sentPlots())
                   .arg(m_sender->sentTracks());
    };

    // Đường trạng thái đi theo cổng gửi lệnh nằm ngoài hai nút bấm của tab này,
    // nên nó có phần đuôi riêng. Hiện cả số cổng: đó là số hệ điều hành vừa
    // chọn, không tra được ở đâu khác trong phần mềm.
    const auto statusSuffix = [this]() -> QString {
        const quint16 port = m_sender->commandLocalPort();
        if (!m_params.statusFollowsCommand || port == 0)
            return {};
        return tr(" | Status theo cổng lệnh %1: %2 gói")
                   .arg(port)
                   .arg(m_sender->recvStatus());
    };

    const LinkStats s = m_link->stats();
    if (!m_link->isRunning()) {
        m_connectionTab->setStatusText(
            tr("Đã dừng nhận — nhận được %1 gói RAW_V").arg(s.rawV)
            + txSuffix() + statusSuffix());
        return;
    }

    // Mở được cổng mà không có gói nào là lỗi hay gặp nhất lúc lắp đặt, và
    // cũng là lỗi khó đoán nhất: nút bấm xong không báo gì, chỉ có màn hình
    // trống. Chỉ ra sẵn ba chỗ cần xem thay vì để trắc thủ ngồi đoán.
    if (s.rawV == 0 && s.rawP == 0 && s.status == 0 && s.other == 0) {
        if (m_linkClock.isValid() && m_linkClock.elapsed() >= kNoDataWarnMs) {
            m_connectionTab->setStatusText(
                tr("Đã mở cổng nhưng %1 giây rồi chưa nhận được gói nào — kiểm "
                   "tra tường lửa của máy, LocalIP đã đúng card nối với đài "
                   "chưa, và đài đã phát chưa")
                    .arg(m_linkClock.elapsed() / 1000)
                + statusSuffix(), true);
        } else {
            m_connectionTab->setStatusText(tr("Đã mở cổng — đang chờ dữ liệu")
                                           + statusSuffix());
        }
        return;
    }

    // Có gói về nhưng không gói nào đúng giao thức: mạng thông, sai chỗ khác.
    // Gói trạng thái lệnh điều khiển thì không tính là "sai cổng" — cấu hình
    // đúng mà đài chưa phát dữ liệu cũng ra đúng cảnh này.
    if (s.rawV == 0 && s.rawP == 0 && s.status == 0) {
        m_connectionTab->setStatusText(
            tr("Nhận được %1 gói nhưng không gói nào đúng giao thức RAW_V/RAW_P "
               "— nhiều khả năng sai cổng").arg(s.other)
                + statusSuffix(), true);
        return;
    }

    QString text = tr("Đang nhận — RAW_V: %1, RAW_P: %2").arg(s.rawV).arg(s.rawP);
    if (s.status > 0)
        text += tr(", trạng thái lệnh: %1").arg(s.status);
    if (s.ctrlSync > 0)
        text += tr(", CtrlSync: %1").arg(s.ctrlSync);
    if (s.other > 0)
        text += tr(", gói lạ: %1").arg(s.other);
    if (s.droppedV > 0 || s.droppedP > 0 || s.droppedRec > 0) {
        text += tr(" — bỏ bớt %1 lượt quét, %2 chu kỳ điểm dấu, %3 gói ghi lưu")
                    .arg(s.droppedV).arg(s.droppedP).arg(s.droppedRec);
    }
    m_connectionTab->setStatusText(text + txSuffix() + statusSuffix());
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

void MainWindow::collectRawPlots(const rawpkt::RawPCycle &cycle, qint64 nowMs)
{
    // Cùng phép quy đổi ô cự ly ra mét với bộ gom chùm, và cùng cự ly tối đa
    // đang dùng để vẽ — sai một trong hai thì chấm đơn xung không nằm trên điểm
    // dấu tâm chùm mà nó sinh ra.
    //
    // Riêng phần bù phương vị / cự ly của "Hiệu chỉnh tâm chùm" **không** cộng
    // vào đây: lớp này là dữ liệu thô của đài, để đối chiếu xem thuật toán đã
    // làm gì với nó. Bù cả hai lớp thì không còn gì để đối chiếu.
    const double azmDeg   = rawpkt::azimuthToDeg(cycle.azimuth);
    const double maxRange = m_settings.maxRangeKm * 1000.0;

    for (int i = 0; i < cycle.count; ++i) {
        const double rangeM =
            rawpkt::cellToMeters(maxRange, cycle.plots[size_t(i)].range);
        RawPlotDot dot;
        dot.bornMs = nowMs;
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         azmDeg, rangeM / 1000.0, dot.lat, dot.lng);
        m_rawPlots.add(dot);
    }
}

void MainWindow::clearAllPlots()
{
    // Chỉ lớp đang vẽ. Cửa sổ "Thông tin chi tiết điểm dấu" là một dòng chảy
    // riêng và đã có nút xoá của nó — xoá lây sang đó thì trắc thủ mất luôn
    // phần đang đọc dở.
    m_plots.clear();
    m_rawPlots.clear();
    m_radar->update();
}

void MainWindow::clearAllTracks()
{
    const int n = m_tracker.tracks().size();
    if (n == 0)
        return;

    // Xoá thẳng, không hỏi lại: trắc thủ bấm nút này giữa lúc đang trực, thêm
    // một hộp thoại nữa là thêm một nhịp phải rời mắt khỏi màn hình.

    // Đi qua đúng đường xoá bằng tay của từng quỹ đạo, để cái nào cũng được
    // chuyển sang trạng thái "xoá" (6) và báo ra ngoài. Chép danh sách định
    // danh trước vì remove() sửa ngay trên danh sách đang duyệt.
    QVector<quint32> ids;
    ids.reserve(n);
    for (const Track &t : m_tracker.tracks())
        ids.push_back(t.id);
    for (quint32 id : ids)
        m_tracker.remove(id);

    // Cả các cửa sổ đang chờ đủ tiêu chuẩn khởi tạo: để lại thì vài giây nữa
    // chúng chín và màn hình lại có quỹ đạo, đúng cái mà trắc thủ vừa xoá đi.
    m_tracker.clearCandidates();

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
