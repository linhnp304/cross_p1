#include "radarview.h"

#include "geo.h"
#include "radarvideo.h"

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

/// Nét vẽ của lưới, xếp từ lớp thưa (đậm) tới lớp dày (mảnh). Vòng cự ly tối
/// đa dùng nét đậm nhất để luôn nổi lên trên mọi lớp.
QPen gridPen(int alpha, double width)
{
    QPen pen(QColor(kGridBright.red(), kGridBright.green(), kGridBright.blue(),
                    alpha));
    pen.setWidthF(width);
    return pen;
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

    // Nhãn đặt ở phía tây chứ không phía bắc: các nhãn cự ly 5 km đều xếp dọc
    // tia bắc, cự ly tối đa mà gần một bội của 5 km là hai nhãn chồng lên nhau.
    double lat = 0.0, lng = 0.0;
    geo::destination(m_settings.siteLat, m_settings.siteLng, 270.0, r, lat, lng);
    const QPointF at = geoToScreen(lat, lng);

    p.setPen(kTextColor);
    p.drawText(QRectF(at.x() + 8, at.y() - 8, 80, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 km").arg(r, 0, 'g', 6));
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
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragLastPos = e->position();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(e);
}

void RadarView::mouseMoveEvent(QMouseEvent *e)
{
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
