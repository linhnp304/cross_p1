#include "mainwindow.h"

#include "appinfo.h"
#include "ascope.h"
#include "connectiontab.h"
#include "lanstatus.h"
#include "paramstab.h"
#include "radarview.h"
#include "settingstab.h"
#include "tilecache.h"
#include "udplink.h"

#include <QApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QKeySequence>
#include <QLabel>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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

/// Khung rỗng cho các phần sẽ làm ở giai đoạn sau.
QWidget *makePlaceholder(const QString &text)
{
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::NoFrame);
    auto *lay = new QVBoxLayout(frame);
    auto *label = new QLabel(text, frame);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);   // để chuỗi dài không đội bề rộng tối thiểu lên
    label->setStyleSheet(QStringLiteral("color: #4a5866;"));
    lay->addWidget(label);
    return frame;
}

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

    m_link = new UdpLink(this);
    m_link->setZfbeat(m_params.zfbeat);

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
            [this](double lat, double lng) {
                m_cursorLabel->setText(tr("Con trỏ  %1").arg(formatLatLng(lat, lng)));
            });

    connect(m_settingsTab, &SettingsTab::settingsChanged, this,
            &MainWindow::applySettings);
    connect(m_paramsTab, &ParamsTab::paramsApplied, this, &MainWindow::applyParams);
    connect(m_connectionTab, &ConnectionTab::endpointsChanged,
            this, &MainWindow::applyEndpoints);
    connect(m_connectionTab, &ConnectionTab::connectRequested,
            this, &MainWindow::startLink);
    connect(m_connectionTab, &ConnectionTab::disconnectRequested,
            this, &MainWindow::stopLink);

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
    m_connectionTab->setParams(m_params);
    m_radar->setSettings(m_settings);
    m_ascope->setMaxRangeKm(m_settings.maxRangeKm);
    refreshLanHosts();

    m_siteLabel->setText(tr("Tâm đài  %1")
                             .arg(formatLatLng(m_settings.siteLat, m_settings.siteLng)));
    m_cursorLabel->setText(tr("Con trỏ  %1")
                               .arg(formatLatLng(m_settings.siteLat, m_settings.siteLng)));

    resize(1920, 1080);
}

QWidget *MainWindow::buildRightColumn()
{
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);

    m_connectionTab = new ConnectionTab;
    m_paramsTab     = new ParamsTab;
    m_settingsTab   = new SettingsTab;

    tabs->addTab(makePlaceholder(tr("Danh sách quỹ đạo — làm ở giai đoạn sau")),
                 tr("Danh sách"));
    tabs->addTab(wrapInScroll(m_connectionTab), tr("Kết nối"));
    tabs->addTab(wrapInScroll(m_paramsTab), tr("Tham số"));

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
    m_settings = s;
    m_radar->setSettings(s);
    m_ascope->setMaxRangeKm(s.maxRangeKm);
    m_siteLabel->setText(tr("Tâm đài  %1").arg(formatLatLng(s.siteLat, s.siteLng)));
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
    m_params.clampToRange();

    m_link->setZfbeat(m_params.zfbeat);
    if (m_params.autoRange)
        updateRangeFromParams();

    m_params.save();
}

void MainWindow::applyEndpoints(const QVector<NetEndpoint> &rx)
{
    m_params.rx = rx;
    m_params.save();
    refreshLanHosts();
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
    m_settings.save();
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

    const qint64 elapsed = m_fadeClock.isValid() ? m_fadeClock.elapsed() : 0;
    if (elapsed >= kMinFadeMs) {
        m_video.fade(m_settings.videoFadeSec, int(elapsed));
        m_fadeClock.restart();
    }

    m_radar->update();
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
    if (s.droppedV > 0 || s.droppedRec > 0) {
        text += tr(" — bỏ bớt %1 lượt quét, %2 gói ghi lưu")
                    .arg(s.droppedV).arg(s.droppedRec);
    }
    m_connectionTab->setStatusText(text);
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
