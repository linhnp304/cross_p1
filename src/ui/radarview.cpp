#include "ui/radarview.h"

#include "maps/geo.h"
#include "proc/plotstore.h"
#include "ui/radarvideo.h"
#include "proc/tracker.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSlider>
#include <QTransform>
#include <QWheelEvent>

#include <cmath>

namespace {

constexpr double kMinZoom = 3.0;
constexpr double kMaxZoom = 19.0;
constexpr int    kTileSize = 256;

/// Dưới ngưỡng này các vòng / đường nằm quá sát nhau, vẽ ra chỉ thành mảng đặc.
constexpr double kMinRingSpacingPx = 6.0;
constexpr double kMinAzSpacingPx   = 28.0;

const QColor kGridBright(198, 222, 88);
const QColor kSiteColor(255, 146, 38);
const QColor kTextColor(206, 226, 138);
const QColor kSweepColor(170, 255, 170);

/// Vùng cấm khởi tạo. Nền rất nhạt để không nuốt mất nền tạp phía dưới — nó là
/// vùng đánh dấu, không phải đối tượng cần nhìn.
const QColor kNoInitZoneEdge(255, 120, 90, 200);
const QColor kNoInitZoneFill(255, 120, 90, 40);

/// Nét vẽ của lưới, xếp từ lớp thưa (đậm) tới lớp dày (mảnh). Vòng cự ly tối
/// đa dùng nét đậm nhất để luôn nổi lên trên mọi lớp.
QPen gridPen(int alpha, double width)
{
    QPen pen(QColor(kGridBright.red(), kGridBright.green(), kGridBright.blue(),
                    alpha));
    pen.setWidthF(width);
    return pen;
}

// --- kích thước các hình điểm dấu / quỹ đạo ---
//
// Đo bằng điểm ảnh màn hình, **không** theo mức phóng: hình quỹ đạo là ký hiệu
// để nhận ra mục tiêu chứ không phải hình vẽ theo tỉ lệ, phóng to bản đồ mà ký
// hiệu cũng to lên thì chỉ tổ che mất bản đồ.

/// Cạnh bên của hình tam giác cân biểu diễn quỹ đạo, ở nấc kích thước gốc.
///
/// Hình vốn dĩ thon và nhọn nên để cạnh bên ngắn quá thì trên màn hình chỉ còn
/// là một vệt, không nhìn ra hướng.
constexpr double kTrackLegPx = 18.0;

/// Cạnh đáy so với cạnh bên. Mô tả giai đoạn 4 để 35%; giai đoạn 6 nới ra cho
/// đáy rộng hơn — tam giác nhọn quá thì ở nấc kích thước nhỏ nhất gần như
/// không phân biệt được với một đoạn thẳng.
constexpr double kTrackBaseRatio = 0.45;

/// Điểm dấu là hình vuông cạnh 30% kích thước quỹ đạo — nhỏ hơn tỉ lệ 40% của
/// giai đoạn 4, để hai lớp nằm chồng nhau vẫn tách bạch được.
constexpr double kPlotRatio = 0.30;

/// Vết lịch sử là hình tròn đường kính ~30% kích thước quỹ đạo.
constexpr double kHistoryRatio = 0.30;

/// Chấm điểm dấu đơn xung so với hình vuông điểm dấu tâm chùm. Cố tình nhỏ hẳn:
/// một mục tiêu để lại vài chục chấm nằm sát nhau, to bằng điểm dấu tâm chùm
/// thì cả chùm dính thành một vệt đặc và không đếm được xung nào ra xung nào.
constexpr double kRawPlotRatio = 0.45;

/// Chấm đơn xung không nhỏ hơn ngần này điểm ảnh — dưới mức đó thì ở nấc kích
/// thước nhỏ nhất chấm mảnh tới mức gần như không thấy.
constexpr double kRawPlotMinPx = 1.6;

/// Bán kính vùng bấm trúng một quỹ đạo, ở nấc kích thước gốc.
constexpr double kTrackHitPx = 11.0;

/// Kéo chuột quá ngần này điểm ảnh thì coi là kéo bản đồ, không phải bấm chọn.
constexpr double kClickSlopPx = 4.0;

/// Con trỏ của chế độ khoanh vùng cấm.
///
/// Phải tự vẽ chứ không lấy con trỏ có sẵn: con trỏ thường của panel này đã là
/// dấu thập rồi, mà mô tả giai đoạn đòi hình khác hẳn để nhìn là biết đang
/// trong chức năng vẽ. Dấu thập kèm một khung nhỏ ở góc dưới bên phải.
QCursor makeZoneCursor()
{
    constexpr int kSize = 24;
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Viền đen dưới nét trắng: con trỏ phải nhìn rõ trên cả nền bản đồ sáng lẫn
    // nền đen của panel.
    for (const auto &[colour, width] : {std::pair{QColor(0, 0, 0), 3.0},
                                        std::pair{QColor(255, 255, 255), 1.0}}) {
        p.setPen(QPen(colour, width));
        p.drawLine(0, 8, 16, 8);
        p.drawLine(8, 0, 8, 16);
        p.setBrush(Qt::NoBrush);
        p.drawRect(12, 12, 10, 10);
    }
    p.end();

    return QCursor(pm, 8, 8);
}

/// Hình tam giác cân của quỹ đạo, đỉnh quay theo hướng chuyển động.
QPolygonF trackShape(const QPointF &c, double headingDeg, double leg)
{
    const double base = leg * kTrackBaseRatio;
    const double half = base / 2.0;
    // Chiều cao suy từ cạnh bên và nửa cạnh đáy — giữ đúng tỉ lệ đã quy định
    // thay vì chọn bừa một chiều cao.
    const double h = std::sqrt(std::max(1.0, leg * leg - half * half));

    // Trong hệ "bắc hướng lên": đỉnh ở trên, đáy ở dưới, trọng tâm ở gốc.
    const QPointF pts[3] = {
        QPointF(0.0,  -h * 0.6),
        QPointF(-half, h * 0.4),
        QPointF( half, h * 0.4),
    };

    const double a = headingDeg * geo::kDeg2Rad;
    const double s = std::sin(a), k = std::cos(a);

    QPolygonF poly;
    poly.reserve(3);
    for (const QPointF &p : pts) {
        // Quay theo phương vị (từ hướng bắc, chiều kim đồng hồ) trong hệ toạ
        // độ màn hình có trục y hướng xuống.
        poly << QPointF(c.x() + p.x() * k - p.y() * s,
                        c.y() + p.x() * s + p.y() * k);
    }
    return poly;
}

} // namespace

RadarView::RadarView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setMinimumSize(320, 240);
    setAutoFillBackground(false);

    m_zoomSlider = new QSlider(Qt::Vertical, this);
    m_zoomSlider->setRange(int(kMinZoom * 100), int(kMaxZoom * 100));
    m_zoomSlider->setSingleStep(25);
    m_zoomSlider->setPageStep(100);
    m_zoomSlider->setToolTip(tr("Phóng to / thu nhỏ"));
    m_zoomSlider->setFixedHeight(160);

    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingSlider)
            return;
        setZoom(v / 100.0, QPointF(width() / 2.0, height() / 2.0));
    });

    m_center = geo::toWorld(m_settings.siteLat, m_settings.siteLng);
    syncZoomSlider();
}

// ---------------------------------------------------------------- cấu hình --

void RadarView::setSettings(const AppSettings &s)
{
    const bool siteMoved = !qFuzzyCompare(s.siteLat, m_settings.siteLat)
                        || !qFuzzyCompare(s.siteLng, m_settings.siteLng);
    const bool rangeChanged = !qFuzzyCompare(s.maxRangeKm, m_settings.maxRangeKm);

    m_settings = s;

    // Mở lại bộ bản đồ khi đổi kiểu nền hoặc đổi đường dẫn (và ở lần đầu). Mở
    // hỏng cũng không sao — panel vẫn chạy với nền đen và hiện dòng nhắc.
    // Dữ liệu chỉ nạp khi kiểu nền đó thực sự được chọn: bộ shapefile của lớp
    // TC mất vài trăm mili giây để dựng hình, không nên nạp lúc đang dùng tile.
    if (s.isTcStyle()) {
        const QString dir = s.resolvedVectorDir();
        if (dir != m_vector.directory() || !m_vector.isValid())
            m_vector.load(dir);
    } else {
        const QString dir = s.resolvedStyleDir();
        if (dir != m_tiles.directory() || !m_tiles.isValid())
            m_tiles.open(dir);
    }

    invalidateCaches();

    if (siteMoved || rangeChanged)
        resetView();
    else
        update();
}

void RadarView::setVideo(const RadarVideo *video)
{
    m_video = video;
    update();
}

void RadarView::setSources(const PlotStore *plots, const RawPlotStore *rawPlots,
                           const Tracker *tracker)
{
    m_plots    = plots;
    m_rawPlots = rawPlots;
    m_tracker  = tracker;
    update();
}

void RadarView::setDrawPredictWindow(bool on)
{
    m_drawPredictWindow = on;
    update();
}

void RadarView::setNoInitZones(const QVector<NoInitZone> &zones, bool show)
{
    m_zones     = zones;
    m_showZones = show;
    update();
}

void RadarView::setZoneDrawMode(bool on)
{
    const ZoneDraw want = on ? ZoneDraw::First : ZoneDraw::Off;
    if (m_zoneDraw == want)
        return;

    m_zoneDraw = want;
    if (on) {
        // Con trỏ đổi hẳn hình để nhìn là biết panel đang ở chế độ khác — bấm
        // chuột lúc này không kéo bản đồ nữa. Nhận bàn phím để còn bấm Esc thoát.
        setCursor(makeZoneCursor());
        setFocus(Qt::OtherFocusReason);
    } else {
        setCursor(Qt::CrossCursor);
    }
    emit zoneDrawModeChanged(on);
    update();
}

void RadarView::screenToPolar(const QPointF &pos, double &azmDeg,
                              double &rangeM) const
{
    double lat = 0.0, lng = 0.0;
    geo::fromWorld(screenToWorld(pos), lat, lng);

    double km = 0.0;
    geo::bearingDistance(m_settings.siteLat, m_settings.siteLng, lat, lng,
                         azmDeg, km);
    rangeM = km * 1000.0;
}

QPoint RadarView::geoToScreenPoint(double lat, double lng) const
{
    return geoToScreen(lat, lng).toPoint();
}

void RadarView::resetView()
{
    // Lúc này widget có thể chưa được bố trí xong nên chưa biết kích thước thật;
    // để lần vẽ kế tiếp tính giúp.
    m_needsFit = true;
    invalidateCaches();
    update();
}

void RadarView::invalidateCaches()
{
    m_cachesDirty = true;
}

void RadarView::fitToRange()
{
    m_needsFit = false;
    m_center = geo::toWorld(m_settings.siteLat, m_settings.siteLng);

    // Chọn mức phóng sao cho đường kính cự ly tối đa lọt gọn trong khung nhìn,
    // chừa thêm chút lề để còn thấy nhãn phương vị.
    const double span = qMax(1.0, double(qMin(width(), height())));
    const double wantPxPerKm =
        span / (2.3 * qMax(AppSettings::kMinRangeKm, m_settings.maxRangeKm));
    const double mpwu = geo::metersPerWorldUnit(m_settings.siteLat);
    const double scale = wantPxPerKm * mpwu / 1000.0;

    m_zoom = qBound(kMinZoom, std::log2(scale / kTileSize), kMaxZoom);
    syncZoomSlider();
}

// ------------------------------------------------------- chuyển đổi toạ độ --

QPointF RadarView::worldToScreen(const QPointF &w) const
{
    const double scale = kTileSize * std::pow(2.0, m_zoom);
    return QPointF((w.x() - m_center.x()) * scale + width()  / 2.0,
                   (w.y() - m_center.y()) * scale + height() / 2.0);
}

QPointF RadarView::screenToWorld(const QPointF &p) const
{
    const double scale = kTileSize * std::pow(2.0, m_zoom);
    return QPointF((p.x() - width()  / 2.0) / scale + m_center.x(),
                   (p.y() - height() / 2.0) / scale + m_center.y());
}

QPointF RadarView::geoToScreen(double lat, double lng) const
{
    return worldToScreen(geo::toWorld(lat, lng));
}

QTransform RadarView::worldTransform() const
{
    const double scale = kTileSize * std::pow(2.0, m_zoom);
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(scale, scale);
    t.translate(-m_center.x(), -m_center.y());
    return t;
}

double RadarView::pixelsPerKm() const
{
    const double scale = kTileSize * std::pow(2.0, m_zoom);
    return 1000.0 / geo::metersPerWorldUnit(m_settings.siteLat) * scale;
}

double RadarView::maxRangeRadiusPx() const
{
    // Đo bằng chính phép chiếu dùng để vẽ vòng tròn, chứ không nhân
    // pixelsPerKm(): hai cách lệch nhau chút ít vì Mercator, và mép ảnh nền
    // tạp mà không trùng vòng cự ly tối đa thì nhìn ra ngay.
    double lat = 0.0, lng = 0.0;
    geo::destination(m_settings.siteLat, m_settings.siteLng, 0.0,
                     m_settings.maxRangeKm, lat, lng);
    const QPointF c = geoToScreen(m_settings.siteLat, m_settings.siteLng);
    const QPointF n = geoToScreen(lat, lng);
    return std::hypot(n.x() - c.x(), n.y() - c.y());
}

// -------------------------------------------------------------------- vẽ ---

void RadarView::paintEvent(QPaintEvent *)
{
    if (m_needsFit)
        fitToRange();

    const QSize want = size() * devicePixelRatioF();
    if (m_cachesDirty || m_mapCache.size() != want)
        rebuildCaches();

    QPainter p(this);
    p.drawPixmap(0, 0, m_mapCache);

    // Nền tạp nằm dưới lưới: lưới vẽ nhạt nên nền tạp phủ lên là mất lưới.
    drawVideo(p);

    p.drawPixmap(0, 0, m_gridCache);

    p.setRenderHint(QPainter::Antialiasing, true);
    drawSweepLine(p);
    drawSiteMarker(p);

    // Vùng cấm khởi tạo nằm dưới lớp điểm dấu và quỹ đạo: nó là vùng nền đánh
    // dấu một khu vực, không được che mất chính thứ đang xảy ra trong khu đó.
    drawNoInitZones(p);

    // Thứ tự lớp cố định: điểm dấu đơn xung dưới cùng, rồi vết lịch sử, cửa sổ
    // dự đoán, quỹ đạo, ô text theo dõi, trên cùng là điểm dấu tâm chùm. Điểm
    // dấu là dữ liệu thô của lần quét vừa rồi, không được để quỹ đạo (kết quả
    // suy ra) che mất.
    //
    // Đơn xung nằm dưới cùng dù còn thô hơn nữa: một mục tiêu để lại cả một đám
    // chấm, vẽ đè lên trên là nuốt mất đúng cái điểm dấu tâm chùm mà người dùng
    // bật lớp này lên để soi.
    if (m_settings.showRawPlots)
        drawRawPlots(p);

    if (m_settings.showTracks) {
        drawTrackHistory(p);
        if (m_drawPredictWindow)
            drawPredictWindows(p);
        drawTracks(p);
        drawWatchLabels(p);
    }
    if (m_settings.showPlots)
        drawPlots(p);
}

void RadarView::rebuildCaches()
{
    m_cachesDirty = false;

    const qreal dpr = devicePixelRatioF();
    const QSize want = size() * dpr;

    m_mapCache = QPixmap(want);
    m_mapCache.setDevicePixelRatio(dpr);
    m_gridCache = QPixmap(want);
    m_gridCache.setDevicePixelRatio(dpr);
    m_gridCache.fill(Qt::transparent);

    // --- lớp nền bản đồ ---
    {
        QPainter p(&m_mapCache);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.fillRect(rect(), Qt::black);   // tắt bản đồ thì nền đen tuyệt đối

        int tilesDrawn = 0;
        if (m_settings.mapVisible) {
            if (m_settings.isTcStyle())
                drawVectorMap(p);
            else
                tilesDrawn = drawMap(p);
        }

        if (m_settings.mapVisible) {
            const QString note = mapNote(tilesDrawn);
            if (!note.isEmpty()) {
                p.setPen(QColor(110, 125, 140));
                p.drawText(rect().adjusted(10, 0, -10, -8),
                           Qt::AlignLeft | Qt::AlignBottom, note);
            }
        }
    }

    // --- lớp lưới cự ly / phương vị ---
    {
        QPainter p(&m_gridCache);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        drawRangeRings(p);
        drawAzimuthLines(p);
    }
}

QString RadarView::mapNote(int tilesDrawn) const
{
    if (m_settings.isTcStyle()) {
        // Dữ liệu của lớp TC là của mình, không phải ghi nguồn như tile MapTiler.
        if (!m_vector.isValid())
            return tr("Chưa có dữ liệu bản đồ TC trong %1").arg(m_vector.directory());
        return {};
    }

    if (!m_tiles.isValid())
        return tr("Chưa tải dữ liệu bản đồ — xem README, mục Nền bản đồ số");
    if (tilesDrawn == 0)
        return tr("Vùng này chưa có dữ liệu bản đồ ở mức phóng hiện tại — "
                  "thu nhỏ lại, hoặc tải thêm tile cho khu vực");
    // Giấy phép MapTiler/OpenStreetMap bắt buộc ghi nguồn khi hiển thị.
    return m_tiles.attribution();
}

void RadarView::drawVectorMap(QPainter &p) const
{
    VectorMapOptions opt;
    opt.showAirRoutes  = m_settings.tcAirRoutes;
    opt.showAirports   = m_settings.tcAirports;
    opt.showRivers     = m_settings.tcRivers;
    opt.showPlaceNames = m_settings.tcPlaceNames;
    opt.showProvinces  = m_settings.tcProvinces;
    opt.brightness     = m_settings.mapBrightness;

    m_vector.draw(p, rect(), worldTransform(), opt);
}

int RadarView::drawMap(QPainter &p)
{
    if (!m_tiles.isValid())
        return 0;

    // Chọn mức tile theo mật độ điểm ảnh. Tile 512px phủ đúng vùng của tile
    // 256px cùng chỉ số, nên mức tile thấp hơn mức phóng một bậc. Làm tròn lên
    // để tile luôn được thu nhỏ khi vẽ — phóng to tile lên sẽ bị mờ.
    const double scale = kTileSize * std::pow(2.0, m_zoom);
    const double offset = std::log2(m_tiles.tileSize() / double(kTileSize));
    const int z = qBound(m_tiles.minZoom(),
                         int(std::ceil(m_zoom - offset - 1e-6)),
                         m_tiles.maxZoom());

    const double n = std::pow(2.0, z);
    const double tilePx = scale / n;          // bề rộng một tile trên màn hình
    if (tilePx < 1.0)
        return 0;

    const QPointF tl = screenToWorld(QPointF(0, 0));
    const QPointF br = screenToWorld(QPointF(width(), height()));

    const int last = int(n) - 1;
    const int x0 = qBound(0, int(std::floor(tl.x() * n)), last);
    const int x1 = qBound(0, int(std::floor(br.x() * n)), last);
    const int y0 = qBound(0, int(std::floor(tl.y() * n)), last);
    const int y1 = qBound(0, int(std::floor(br.y() * n)), last);

    // Vẽ theo toạ độ nguyên để các tile kề nhau không hở đường chỉ trắng.
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    int drawn = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            const QPixmap pm = m_tiles.tile(z, x, y);
            if (pm.isNull())
                continue;
            const QPointF a = worldToScreen(QPointF(x / n, y / n));
            const QPointF b = worldToScreen(QPointF((x + 1) / n, (y + 1) / n));
            const QRect dst(QPoint(int(std::floor(a.x())), int(std::floor(a.y()))),
                            QPoint(int(std::ceil(b.x())) - 1, int(std::ceil(b.y())) - 1));
            p.drawPixmap(dst, pm);
            ++drawn;
        }
    }

    // Độ sáng: phủ đen mờ lên trên. 100 = nguyên bản, 0 = tối hẳn.
    const int dim = (100 - m_settings.mapBrightness) * 255 / 100;
    if (drawn > 0 && dim > 0)
        p.fillRect(rect(), QColor(0, 0, 0, dim));

    return drawn;
}

void RadarView::drawRangeRings(QPainter &p) const
{
    // Vòng cự ly tối đa vẽ cả khi đã tắt vòng tròn cự ly: nó là biên của màn
    // hình ra đa, và là chỗ để các đường chia độ kết thúc.
    if (m_settings.ringMode != RingMode::Off) {
        const RingMode mode = m_settings.ringMode;

        // Vẽ từ lớp dày nhất tới lớp thưa nhất để nét đậm luôn nằm trên. Bước
        // tính bằng đơn vị 0.1 km: 1 = 0.1 km, 5 = 0.5 km, 10 = 1 km, 50 = 5 km.
        if (mode == RingMode::R01)
            drawRingLayer(p, 1, 5, gridPen(28, 1.0), false);    // 0.1 km, bỏ trùng 0.5
        if (mode == RingMode::R01 || mode == RingMode::R05)
            drawRingLayer(p, 5, 10, gridPen(48, 1.0), false);   // 0.5 km, bỏ trùng 1
        if (mode != RingMode::R5)
            drawRingLayer(p, 10, 50, gridPen(90, 1.0), false);  // 1 km, bỏ trùng 5
        drawRingLayer(p, 50, 0, gridPen(165, 1.6), true);       // 5 km, có nhãn
    }

    drawMaxRangeRing(p);
}

void RadarView::drawRingLayer(QPainter &p, int stepTenthKm, int skipTenthKm,
                              const QPen &pen, bool withLabels) const
{
    const double stepKm = stepTenthKm * 0.1;
    if (stepKm * pixelsPerKm() < kMinRingSpacingPx)
        return;

    // Dừng hẳn bên trong cự ly tối đa: vòng ngoài cùng là việc của
    // drawMaxRangeRing, nếu không hai vòng sẽ chồng nhau khi cự ly tối đa
    // đúng bằng một bội của bước.
    const int lastTenthKm = int(std::floor(m_settings.maxRangeKm * 10.0 - 1e-6));
    if (lastTenthKm < stepTenthKm)
        return;

    p.setPen(pen);
    constexpr int kSegments = 180;

    for (int u = stepTenthKm; u <= lastTenthKm; u += stepTenthKm) {
        if (skipTenthKm > 0 && u % skipTenthKm == 0)
            continue;   // bán kính này đã có ở lớp thưa hơn

        const double r = u * 0.1;
        QPolygonF poly;
        poly.reserve(kSegments + 1);
        for (int i = 0; i <= kSegments; ++i) {
            double lat = 0.0, lng = 0.0;
            geo::destination(m_settings.siteLat, m_settings.siteLng,
                             i * 360.0 / kSegments, r, lat, lng);
            poly << geoToScreen(lat, lng);
        }
        p.drawPolyline(poly);

        if (withLabels) {
            double lat = 0.0, lng = 0.0;
            geo::destination(m_settings.siteLat, m_settings.siteLng, 0.0, r, lat, lng);
            const QPointF at = geoToScreen(lat, lng);
            // Đặt bên trái tia bắc để không đụng nhãn phương vị 0 độ ở vành ngoài.
            p.setPen(kTextColor);
            p.drawText(QRectF(at.x() - 68, at.y() - 16, 60, 15),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("%1 km").arg(r, 0, 'g', 4));
            p.setPen(pen);
        }
    }
}

void RadarView::drawMaxRangeRing(QPainter &p) const
{
    const double r = m_settings.maxRangeKm;
    if (r * pixelsPerKm() < 2.0)
        return;

    constexpr int kSegments = 240;
    QPolygonF poly;
    poly.reserve(kSegments + 1);
    for (int i = 0; i <= kSegments; ++i) {
        double lat = 0.0, lng = 0.0;
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         i * 360.0 / kSegments, r, lat, lng);
        poly << geoToScreen(lat, lng);
    }

    p.setPen(gridPen(200, 1.8));
    p.drawPolyline(poly);

    // Không ghi nhãn cự ly ở vòng ngoài cùng: nó luôn rơi vào chỗ có đường chia
    // độ đi qua, và giá trị đó đã có sẵn trong ô "Cự ly tối đa" của tab Cài đặt.
}

void RadarView::drawAzimuthLines(QPainter &p) const
{
    if (m_settings.azimuthMode == AzimuthMode::Off)
        return;

    const AzimuthMode mode = m_settings.azimuthMode;
    if (mode == AzimuthMode::A5)
        drawAzimuthLayer(p, 5, 10, gridPen(48, 1.0), false);   // 5 độ, bỏ trùng 10
    if (mode == AzimuthMode::A5 || mode == AzimuthMode::A10)
        drawAzimuthLayer(p, 10, 30, gridPen(90, 1.0), false);  // 10 độ, bỏ trùng 30
    drawAzimuthLayer(p, 30, 0, gridPen(165, 1.6), true);       // 30 độ, có nhãn
}

void RadarView::drawAzimuthLayer(QPainter &p, int stepDeg, int skipDeg,
                                 const QPen &pen, bool withLabels) const
{
    const double r = m_settings.maxRangeKm;

    // Khoảng cách giữa hai đường kề nhau, đo ở vành ngoài.
    const double arcPx = 2.0 * geo::kPi * r * pixelsPerKm() * stepDeg / 360.0;
    if (arcPx < kMinAzSpacingPx)
        return;

    const QPointF centre = geoToScreen(m_settings.siteLat, m_settings.siteLng);

    for (int deg = 0; deg < 360; deg += stepDeg) {
        if (skipDeg > 0 && deg % skipDeg == 0)
            continue;   // phương vị này đã có ở lớp thưa hơn

        double lat = 0.0, lng = 0.0;
        geo::destination(m_settings.siteLat, m_settings.siteLng, deg, r, lat, lng);
        const QPointF outer = geoToScreen(lat, lng);

        p.setPen(pen);
        p.drawLine(centre, outer);

        if (withLabels) {
            // Đẩy nhãn ra ngoài vành một chút, theo đúng hướng của tia.
            QPointF dir = outer - centre;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 1.0) {
                dir /= len;
                const QPointF at = outer + dir * 14.0;
                p.setPen(kTextColor);
                p.drawText(QRectF(at.x() - 22, at.y() - 9, 44, 18),
                           Qt::AlignCenter, QString::number(deg));
            }
        }
    }
}

void RadarView::drawSiteMarker(QPainter &p) const
{
    const QPointF c = geoToScreen(m_settings.siteLat, m_settings.siteLng);

    p.setPen(QPen(kSiteColor, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, 5.0, 5.0);
    p.drawLine(c + QPointF(-9, 0), c + QPointF(-6, 0));
    p.drawLine(c + QPointF(6, 0),  c + QPointF(9, 0));
    p.drawLine(c + QPointF(0, -9), c + QPointF(0, -6));
    p.drawLine(c + QPointF(0, 6),  c + QPointF(0, 9));

    p.setBrush(kSiteColor);
    p.drawEllipse(c, 1.6, 1.6);
}

void RadarView::drawVideo(QPainter &p) const
{
    if (!m_video || !m_video->hasInk())
        return;

    const double r = maxRangeRadiusPx();
    if (r < 1.0)
        return;

    const QPointF c = geoToScreen(m_settings.siteLat, m_settings.siteLng);
    const QRectF dst(c.x() - r, c.y() - r, 2 * r, 2 * r);
    if (!dst.intersects(QRectF(rect())))
        return;   // đĩa nền tạp đã trôi hẳn ra ngoài khung nhìn

    // Nội suy khi co giãn: ảnh nền tạp có bán kính cố định, không nội suy thì
    // lúc phóng to sẽ thấy rõ từng điểm ảnh vuông.
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(dst, m_video->image());
}

void RadarView::drawSweepLine(QPainter &p) const
{
    if (!m_video || m_video->lastAngleDeg() < 0.0)
        return;

    double lat = 0.0, lng = 0.0;
    geo::destination(m_settings.siteLat, m_settings.siteLng,
                     m_video->lastAngleDeg(), m_settings.maxRangeKm, lat, lng);

    QPen pen(kSweepColor, 1.4);
    p.setPen(pen);
    p.drawLine(geoToScreen(m_settings.siteLat, m_settings.siteLng),
               geoToScreen(lat, lng));
}

// --------------------------------------------- điểm dấu và quỹ đạo --------

double RadarView::trackLegPx() const
{
    return kTrackLegPx * AppSettings::sizeScale(m_settings.trackSizeStep);
}

double RadarView::plotSizePx() const
{
    return kTrackLegPx * kPlotRatio * AppSettings::sizeScale(m_settings.plotSizeStep);
}

double RadarView::rawPlotSizePx() const
{
    // Đi theo nấc của điểm dấu: chỉnh nấc điểm dấu là chỉnh cả hai lớp điểm dấu
    // cùng lúc, không phải đi tìm thêm một núm nữa.
    return std::max(kRawPlotMinPx, plotSizePx() * kRawPlotRatio);
}

double RadarView::historyDiameterPx() const
{
    // Vết đi theo nấc của quỹ đạo, không phải nấc của điểm dấu: nó là vết của
    // chính quỹ đạo đó.
    return trackLegPx() * kHistoryRatio;
}

double RadarView::trackHitPx() const
{
    return kTrackHitPx * AppSettings::sizeScale(m_settings.trackSizeStep);
}

QColor RadarView::trackColor(const Track &t) const
{
    const AppColors &c = m_settings.colors;
    if (t.type == TrackType::RadarIff)
        return c.trackIff;
    return t.classify != 0 ? c.trackClassified : c.trackPlain;
}

void RadarView::drawTrackHistory(QPainter &p) const
{
    if (!m_tracker)
        return;

    const AppColors &col  = m_settings.colors;
    const bool       line = m_settings.historyStyle == HistoryStyle::Line;
    const double     r    = historyDiameterPx() / 2.0;

    // Đường nối kéo dài tới tận vị trí hiện tại của quỹ đạo, không dừng ở vết
    // cuối: vết chỉ được ghi lại theo vòng quét, để đứt ở đó thì đầu đường luôn
    // hụt lại phía sau hình tam giác một quãng.
    if (line)
        p.setPen(QPen(col.historyLine, 1.2));
    else
        p.setPen(Qt::NoPen);

    for (const Track &t : m_tracker->tracks()) {
        // Quỹ đạo đang được theo dõi liên tục thì hiện đủ vết, kể cả khi thanh
        // trượt đang để ít vết hoặc để 0 — đó là điểm khác nhau giữa "theo dõi"
        // và "nhìn qua".
        const int want = t.watched ? t.history.size() : m_settings.trackHistory;
        const int from = qMax(0, t.history.size() - want);

        if (line) {
            if (from >= t.history.size())
                continue;
            QPolygonF poly;
            poly.reserve(t.history.size() - from + 1);
            for (int i = from; i < t.history.size(); ++i) {
                const TrackPoint &h = t.history.at(i);
                poly << geoToScreen(h.lat, h.lng);
            }
            poly << geoToScreen(t.lat, t.lng);
            p.drawPolyline(poly);
            continue;
        }

        for (int i = from; i < t.history.size(); ++i) {
            const TrackPoint &h = t.history.at(i);
            switch (h.status) {
            case TrackStatus::Tracking:  p.setBrush(col.historyTracking); break;
            case TrackStatus::Coasting:  p.setBrush(col.historyCoasting); break;
            default:                     p.setBrush(col.historyOther);    break;
            }
            p.drawEllipse(geoToScreen(h.lat, h.lng), r, r);
        }
    }
    p.setBrush(Qt::NoBrush);
}

void RadarView::drawPredictWindows(QPainter &p) const
{
    if (!m_tracker)
        return;

    p.setBrush(Qt::NoBrush);

    const auto drawList = [&](const QVector<Track> &list, Qt::PenStyle style) {
        p.setPen(QPen(m_settings.colors.predictWindow, 1.0, style));

        for (const Track &t : list) {
            const double r1 = t.windowRange1 * 0.0001;   // 0.1 m -> km
            const double r2 = t.windowRange2 * 0.0001;
            const double a1 = t.windowAzm1 * 0.01;
            double a2 = t.windowAzm2 * 0.01;
            if (a2 < a1)
                a2 += 360.0;   // cửa sổ vắt qua hướng bắc

            // Vẽ hình quạt bằng đường gấp khúc theo chính phép chiếu đang dùng —
            // vẽ cung tròn trên màn hình sẽ lệch khỏi lưới cự ly vì Mercator.
            constexpr int kSteps = 12;
            QPolygonF poly;
            poly.reserve(2 * (kSteps + 1) + 1);
            for (int i = 0; i <= kSteps; ++i) {
                double lat = 0.0, lng = 0.0;
                geo::destination(m_settings.siteLat, m_settings.siteLng,
                                 a1 + (a2 - a1) * i / kSteps, r2, lat, lng);
                poly << geoToScreen(lat, lng);
            }
            for (int i = kSteps; i >= 0; --i) {
                double lat = 0.0, lng = 0.0;
                geo::destination(m_settings.siteLat, m_settings.siteLng,
                                 a1 + (a2 - a1) * i / kSteps, r1, lat, lng);
                poly << geoToScreen(lat, lng);
            }
            poly << poly.first();
            p.drawPolyline(poly);
        }
    };

    drawList(m_tracker->tracks(), Qt::DashLine);

    // Cửa sổ của các ứng viên chưa đủ tiêu chuẩn khởi tạo — nét chấm để phân
    // biệt. Chúng không có hình tam giác nào đi kèm (chưa phải quỹ đạo), nên
    // đây là cách duy nhất nhìn thấy tiêu chuẩn khởi tạo đang làm việc: cửa sổ
    // mở ra ở vòng có điểm dấu đầu tiên, rồi hoặc chín thành quỹ đạo, hoặc biến
    // mất.
    drawList(m_tracker->candidates(), Qt::DotLine);
}

void RadarView::drawTracks(QPainter &p) const
{
    if (!m_tracker)
        return;

    const QFontMetricsF fm(p.font());
    const double leg = trackLegPx();

    for (const Track &t : m_tracker->tracks()) {
        const QPointF c = geoToScreen(t.lat, t.lng);
        const QColor  k = trackColor(t);

        p.setPen(QPen(k, 1.4));
        // Quỹ đạo đang ngoại suy để rỗng ruột: nhìn là biết ngay vị trí này do
        // thuật toán đoán chứ không phải vừa đo được.
        p.setBrush(t.status == TrackStatus::Coasting ? QBrush(Qt::NoBrush) : QBrush(k));
        p.drawPolygon(trackShape(c, t.headingDeg(), leg));

        if (!m_settings.showTrackInfo)
            continue;

        p.setPen(k);

        // Số đầu tốp phía trên hình, phương vị - cự ly bên phải. Điểm dấu và
        // quỹ đạo của cùng một mục tiêu gần như trùng vị trí, nên thông tin của
        // hai lớp phải nằm về hai phía khác nhau mới không chồng chữ lên nhau.
        p.drawText(QPointF(c.x() + leg * 0.45, c.y() - leg * 0.55),
                   QString::number(t.top));

        p.drawText(QPointF(c.x() + leg * 0.7,
                           c.y() + fm.ascent() / 2.0 - 1.0),
                   QStringLiteral("%1-%2").arg(t.azmDeg(), 0, 'f', 2)
                                          .arg(t.rangeM(), 0, 'f', 1));
    }
    p.setBrush(Qt::NoBrush);
}

void RadarView::drawWatchLabels(QPainter &p) const
{
    if (!m_tracker)
        return;

    const AppColors &col = m_settings.colors;
    const QFontMetricsF fm(p.font());
    const double lineH = fm.height();

    for (const Track &t : m_tracker->tracks()) {
        if (!t.watched)
            continue;

        QStringList lines;
        lines << tr("Tốp: %1").arg(t.top)
              << tr("VT: %1° - %2 m").arg(t.azmDeg(), 0, 'f', 2)
                                      .arg(t.rangeM(), 0, 'f', 1)
              << tr("V: %1 m/s").arg(t.speedMs(), 0, 'f', 1)
              << tr("H: %1°").arg(t.headingDeg(), 0, 'f', 2);
        if (t.altitude() != 0)
            lines << tr("ĐC: %1 m").arg(t.altitude());
        if (const QString name = m_settings.classifyName(t.classify); !name.isEmpty())
            lines << tr("Loại: %1").arg(name);
        lines << tr("NL: %1").arg(t.amplitude);

        double w = 0.0;
        for (const QString &s : lines)
            w = qMax(w, fm.horizontalAdvance(s));

        constexpr double kPad = 5.0;
        const QSizeF box(w + 2 * kPad, lines.size() * lineH + 2 * kPad);

        // Đặt ngược hướng chuyển động: phía trước quỹ đạo là chỗ trắc thủ đang
        // nhìn tới, không nên che.
        //
        // Khoảng đẩy ra phải tính theo cỡ ô text, không phải một hằng số: ô
        // text cao cả trăm điểm ảnh mà chỉ đẩy ra vài chục thì tâm ô nằm ngoài
        // quỹ đạo nhưng thân ô vẫn phủ kín cả quỹ đạo lẫn vết của nó.
        const double a  = (t.headingDeg() + 180.0) * geo::kDeg2Rad;
        const double dx = std::sin(a);
        const double dy = -std::cos(a);
        const double reach = trackLegPx()
                           + std::abs(dx) * box.width() / 2.0
                           + std::abs(dy) * box.height() / 2.0;

        const QPointF c = geoToScreen(t.lat, t.lng);
        const QPointF anchor(c.x() + dx * reach, c.y() + dy * reach);

        QRectF rect(anchor.x() - box.width() / 2.0, anchor.y() - box.height() / 2.0,
                    box.width(), box.height());
        // Giữ ô text trong khung nhìn: quỹ đạo sát mép mà ô text tràn ra ngoài
        // thì mất đúng thông tin đang cần theo dõi.
        rect.moveLeft(qBound(2.0, rect.left(), width() - box.width() - 2.0));
        rect.moveTop(qBound(2.0, rect.top(), height() - box.height() - 2.0));

        // Mũi tên nối từ ô text trỏ vào quỹ đạo.
        p.setPen(QPen(col.labelBorder, 1.0));
        p.drawLine(rect.center(), c);

        p.setBrush(col.labelBackground);
        p.drawRoundedRect(rect, 3.0, 3.0);
        p.setBrush(Qt::NoBrush);

        p.setPen(col.labelText);
        for (int i = 0; i < lines.size(); ++i) {
            p.drawText(QPointF(rect.left() + kPad,
                               rect.top() + kPad + fm.ascent() + i * lineH),
                       lines.at(i));
        }
    }
}

void RadarView::drawPlots(QPainter &p) const
{
    if (!m_plots)
        return;

    const QFontMetricsF fm(p.font());
    const QColor k    = m_settings.colors.plotRadar;
    const double size = plotSizePx();
    const double h    = size / 2.0;

    p.setPen(Qt::NoPen);
    p.setBrush(k);
    for (const PlotTC &plot : m_plots->plots()) {
        const QPointF c = geoToScreen(plot.lat, plot.lng);
        p.drawRect(QRectF(c.x() - h, c.y() - h, size, size));
    }
    p.setBrush(Qt::NoBrush);

    if (!m_settings.showPlotInfo)
        return;

    // Thông tin điểm dấu đặt **bên trái** hình vuông, vì thông tin quỹ đạo đã
    // chiếm phía trên và bên phải. Điểm dấu và quỹ đạo của cùng một mục tiêu
    // gần như trùng vị trí, để cùng một phía là hai dòng chữ đè lên nhau.
    p.setPen(k);
    for (const PlotTC &plot : m_plots->plots()) {
        const QPointF c = geoToScreen(plot.lat, plot.lng);
        const QString text = QStringLiteral("%1° - %2 m")
                                 .arg(plot.azmDeg(), 0, 'f', 2)
                                 .arg(plot.rangeM(), 0, 'f', 1);
        p.drawText(QPointF(c.x() - h - 3.0 - fm.horizontalAdvance(text),
                           c.y() + fm.ascent() / 2.0 - 1.0),
                   text);
    }
}

void RadarView::drawRawPlots(QPainter &p) const
{
    if (!m_rawPlots)
        return;

    const double size = rawPlotSizePx();
    const double h    = size / 2.0;

    // Khử răng cưa tắt hẳn cho lớp này: chấm chỉ vài điểm ảnh nên khử răng cưa
    // chỉ làm nó nhoè đi, mà lớp này lại là lớp đông nhất — vài nghìn chấm mỗi
    // lần vẽ, 25 lần mỗi giây.
    const bool aa = p.testRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::Antialiasing, false);

    p.setPen(Qt::NoPen);
    p.setBrush(m_settings.colors.plotRaw);
    for (const RawPlotDot &dot : m_rawPlots->plots()) {
        const QPointF c = geoToScreen(dot.lat, dot.lng);
        p.drawRect(QRectF(c.x() - h, c.y() - h, size, size));
    }
    p.setBrush(Qt::NoBrush);

    p.setRenderHint(QPainter::Antialiasing, aa);
}

QPolygonF RadarView::zonePolygon(const NoInitZone &z) const
{
    // Cùng cách với drawPredictWindows: đi theo cung bằng đường gấp khúc trong
    // chính phép chiếu đang dùng, chứ vẽ cung tròn trên màn hình thì mép vùng
    // sẽ lệch khỏi vòng cự ly.
    const double a1 = z.azm1;
    double       a2 = z.azm2;
    if (a2 < a1)
        a2 += 360.0;   // vùng vắt qua hướng bắc

    const double r1 = z.range1 / 1000.0;   // mét -> km
    const double r2 = z.range2 / 1000.0;

    constexpr int kSteps = 24;
    QPolygonF poly;
    poly.reserve(2 * (kSteps + 1) + 1);
    for (int i = 0; i <= kSteps; ++i) {
        double lat = 0.0, lng = 0.0;
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         a1 + (a2 - a1) * i / kSteps, r2, lat, lng);
        poly << geoToScreen(lat, lng);
    }
    for (int i = kSteps; i >= 0; --i) {
        double lat = 0.0, lng = 0.0;
        geo::destination(m_settings.siteLat, m_settings.siteLng,
                         a1 + (a2 - a1) * i / kSteps, r1, lat, lng);
        poly << geoToScreen(lat, lng);
    }
    poly << poly.first();
    return poly;
}

void RadarView::drawNoInitZones(QPainter &p) const
{
    const bool drawing = m_zoneDraw == ZoneDraw::Second;
    if (!m_showZones && !drawing)
        return;

    p.setPen(QPen(kNoInitZoneEdge, 1.2, Qt::DashLine));
    p.setBrush(kNoInitZoneFill);

    if (m_showZones) {
        for (const NoInitZone &z : m_zones) {
            if (z.on)
                p.drawPolygon(zonePolygon(z));
        }
    }

    // Khung xem trước đi theo chuột. Vẽ cả khi ô "Hiện vùng cấm" đang tắt —
    // đang khoanh mà không thấy mình khoanh cái gì thì không khoanh được.
    if (drawing) {
        double azm2 = 0.0, range2 = 0.0;
        screenToPolar(m_zoneCursor, azm2, range2);

        NoInitZone z;
        z.range1 = std::min(m_zoneRange1, range2);
        z.range2 = std::max(m_zoneRange1, range2);
        if (normalizeDeg360(azm2 - m_zoneAzm1) <= 180.0) {
            z.azm1 = m_zoneAzm1;
            z.azm2 = azm2;
        } else {
            z.azm1 = azm2;
            z.azm2 = m_zoneAzm1;
        }
        p.drawPolygon(zonePolygon(z));
    }

    p.setBrush(Qt::NoBrush);
}

void RadarView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && m_zoneDraw != ZoneDraw::Off) {
        setZoneDrawMode(false);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

quint32 RadarView::trackAt(const QPointF &pos) const
{
    if (!m_tracker || !m_settings.showTracks)
        return 0;

    quint32 best = 0;
    double  bestDist = trackHitPx();

    for (const Track &t : m_tracker->tracks()) {
        const QPointF c = geoToScreen(t.lat, t.lng);
        const double d = std::hypot(c.x() - pos.x(), c.y() - pos.y());
        if (d <= bestDist) {
            bestDist = d;
            best     = t.id;
        }
    }
    return best;
}

// ------------------------------------------------------------ tương tác ----

void RadarView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    invalidateCaches();
    layoutZoomSlider();
}

void RadarView::layoutZoomSlider()
{
    if (!m_zoomSlider)
        return;
    const int w = m_zoomSlider->sizeHint().width();
    m_zoomSlider->setGeometry(width() - w - 14,
                              height() - m_zoomSlider->height() - 34,
                              w, m_zoomSlider->height());
}

void RadarView::mousePressEvent(QMouseEvent *e)
{
    // Chế độ khoanh vùng chiếm trọn chuột trái: hai lần bấm là xong một vùng.
    if (m_zoneDraw != ZoneDraw::Off) {
        if (e->button() == Qt::RightButton) {
            setZoneDrawMode(false);   // chuột phải huỷ, như mọi chế độ vẽ khác
            e->accept();
            return;
        }
        if (e->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(e);
            return;
        }

        if (m_zoneDraw == ZoneDraw::First) {
            screenToPolar(e->position(), m_zoneAzm1, m_zoneRange1);
            m_zoneCursor = e->position();
            m_zoneDraw   = ZoneDraw::Second;
            update();
            e->accept();
            return;
        }

        double azm2 = 0.0, range2 = 0.0;
        screenToPolar(e->position(), azm2, range2);

        NoInitZone z;
        z.on = true;   // vẽ tay ra thì gần như chắc chắn muốn dùng ngay
        z.range1 = std::min(m_zoneRange1, range2);
        z.range2 = std::max(m_zoneRange1, range2);

        // Người dùng có thể quét ngược chiều kim đồng hồ. Cạnh nào đi tới cạnh
        // kia bằng cung **ngắn hơn** thì cạnh đó là mép đầu — quét 30 độ mà
        // hiểu thành 330 độ còn lại thì vùng cấm phủ gần hết màn hình.
        const double diff = normalizeDeg360(azm2 - m_zoneAzm1);
        if (diff <= 180.0) {
            z.azm1 = m_zoneAzm1;
            z.azm2 = azm2;
        } else {
            z.azm1 = azm2;
            z.azm2 = m_zoneAzm1;
        }

        setZoneDrawMode(false);
        emit zoneDrawn(z);
        e->accept();
        return;
    }

    if (e->button() == Qt::RightButton) {
        // Chuột phải trên một quỹ đạo mở menu cập nhật; ra ngoài quỹ đạo thì
        // không làm gì (panel này chưa có menu ngữ cảnh nào khác).
        if (const quint32 id = trackAt(e->position()); id != 0) {
            emit trackContextMenu(id, e->globalPosition().toPoint());
            e->accept();
            return;
        }
    }

    if (e->button() == Qt::LeftButton) {
        m_pressedTrack = trackAt(e->position());
        m_pressPos     = e->position();
        m_dragging     = true;
        m_dragLastPos  = e->position();
        // Bấm trúng quỹ đạo thì chưa đổi con trỏ: có thể người dùng chỉ định
        // mở popup chứ không định kéo bản đồ.
        if (m_pressedTrack == 0)
            setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(e);
}

void RadarView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_zoneDraw == ZoneDraw::Second) {
        m_zoneCursor = e->position();
        update();
    }

    // Kéo đủ xa thì bỏ ý định mở popup, coi như đang kéo bản đồ.
    if (m_pressedTrack != 0) {
        const QPointF d = e->position() - m_pressPos;
        if (std::hypot(d.x(), d.y()) > kClickSlopPx) {
            m_pressedTrack = 0;
            setCursor(Qt::ClosedHandCursor);
        }
    }

    if (m_dragging) {
        const double scale = kTileSize * std::pow(2.0, m_zoom);
        const QPointF delta = e->position() - m_dragLastPos;
        m_dragLastPos = e->position();
        m_center -= QPointF(delta.x() / scale, delta.y() / scale);
        m_center.setY(qBound(0.0, m_center.y(), 1.0));
        invalidateCaches();
        update();   // rê chuột không kéo thì khung nhìn không đổi, khỏi vẽ lại
    }

    double lat = 0.0, lng = 0.0;
    geo::fromWorld(screenToWorld(e->position()), lat, lng);
    emit cursorGeoChanged(lat, lng);

    QWidget::mouseMoveEvent(e);
}

void RadarView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::CrossCursor);

        // Vẫn đúng quỹ đạo lúc bấm và chuột không bị kéo đi — mở popup.
        if (m_pressedTrack != 0 && trackAt(e->position()) == m_pressedTrack)
            emit trackClicked(m_pressedTrack, e->globalPosition().toPoint());
        m_pressedTrack = 0;
    }
    QWidget::mouseReleaseEvent(e);
}

void RadarView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        e->ignore();
        return;
    }
    setZoom(m_zoom + steps * 0.25, e->position());
    e->accept();
}

void RadarView::setZoom(double z, const QPointF &anchorScreen)
{
    const double newZoom = qBound(kMinZoom, z, kMaxZoom);
    if (qFuzzyCompare(newZoom, m_zoom))
        return;

    // Giữ nguyên điểm địa lý nằm dưới con trỏ khi phóng to / thu nhỏ.
    const QPointF anchorWorld = screenToWorld(anchorScreen);
    m_zoom = newZoom;
    const QPointF afterWorld = screenToWorld(anchorScreen);
    m_center += anchorWorld - afterWorld;
    m_center.setY(qBound(0.0, m_center.y(), 1.0));

    invalidateCaches();
    syncZoomSlider();
    update();
}

void RadarView::syncZoomSlider()
{
    if (!m_zoomSlider)
        return;
    m_updatingSlider = true;
    m_zoomSlider->setValue(int(std::lround(m_zoom * 100.0)));
    m_updatingSlider = false;
}
