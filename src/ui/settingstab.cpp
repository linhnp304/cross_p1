#include "ui/settingstab.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {

/// Nhãn cạnh thanh trượt tốc độ mờ. Giá trị 0 là một chế độ riêng chứ không
/// phải "0 giây", nên phải gọi thẳng tên ra.
QString fadeText(int seconds)
{
    return seconds == 0 ? SettingsTab::tr("Không mờ")
                        : SettingsTab::tr("%1 giây").arg(seconds);
}

} // namespace

SettingsTab::SettingsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // --- Nền bản đồ số ---------------------------------------------------
    auto *mapBox = new QGroupBox(tr("Nền bản đồ số"), this);
    auto *mapForm = new QFormLayout(mapBox);
    mapForm->setLabelAlignment(Qt::AlignLeft);

    // CheckBox ẩn/hiện và ComboBox chọn kiểu nền nằm cùng một hàng.
    m_mapVisible = new QCheckBox(tr("Hiển thị nền bản đồ"), mapBox);
    m_mapStyle = new QComboBox(mapBox);
    m_mapStyle->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_mapStyle->setMinimumContentsLength(12);

    auto *mapRow = new QHBoxLayout;
    mapRow->addWidget(m_mapVisible, 0);
    mapRow->addWidget(m_mapStyle, 1);
    mapForm->addRow(mapRow);

    // Ẩn/hiện từng lớp của kiểu nền TC, xếp ngay dưới ComboBox chọn kiểu nền.
    m_tcAirRoutes  = new QCheckBox(tr("Hiện đường bay dân dụng"), mapBox);
    m_tcAirports   = new QCheckBox(tr("Hiện sân bay"), mapBox);
    m_tcRivers     = new QCheckBox(tr("Hiện sông ngòi"), mapBox);
    m_tcPlaceNames = new QCheckBox(tr("Hiện tên địa danh"), mapBox);
    m_tcProvinces  = new QCheckBox(tr("Hiện danh giới tỉnh/thành phố"), mapBox);
    for (auto *b : tcBoxes())
        mapForm->addRow(b);

    m_brightness = new QSlider(Qt::Horizontal, mapBox);
    m_brightness->setRange(0, 100);
    auto *brightValue = new QLabel(mapBox);
    brightValue->setMinimumWidth(36);
    brightValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *brightRow = new QHBoxLayout;
    brightRow->addWidget(m_brightness, 1);
    brightRow->addWidget(brightValue, 0);
    mapForm->addRow(tr("Độ sáng"), brightRow);

    // --- Tâm đài ---------------------------------------------------------
    auto *siteBox = new QGroupBox(tr("Tâm đài"), this);
    auto *siteForm = new QFormLayout(siteBox);

    m_siteLat = new QDoubleSpinBox(siteBox);
    m_siteLat->setDecimals(6);
    m_siteLat->setRange(-85.0, 85.0);
    m_siteLat->setSingleStep(0.001);
    siteForm->addRow(tr("Vĩ độ (lat)"), m_siteLat);

    m_siteLng = new QDoubleSpinBox(siteBox);
    m_siteLng->setDecimals(6);
    m_siteLng->setRange(-180.0, 180.0);
    m_siteLng->setSingleStep(0.001);
    siteForm->addRow(tr("Kinh độ (lng)"), m_siteLng);

    m_applySite = new QPushButton(tr("Áp dụng"), siteBox);
    siteForm->addRow(QString(), m_applySite);

    // --- Lưới cự ly / phương vị ------------------------------------------
    auto *gridBox = new QGroupBox(tr("Vòng cự ly và phương vị"), this);
    auto *gridForm = new QFormLayout(gridBox);

    // Đơn vị nằm ở nhãn chứ không phải hậu tố trong ô: ô nhập tới 0.001 km mà
    // còn kèm chữ "km" thì phần số bị đẩy hẹp lại, gõ vào rất vướng.
    m_maxRange = new QDoubleSpinBox(gridBox);
    m_maxRange->setDecimals(3);
    m_maxRange->setRange(AppSettings::kMinRangeKm, AppSettings::kMaxRangeKm);
    m_maxRange->setSingleStep(0.1);
    gridForm->addRow(tr("Cự ly tối đa (km)"), m_maxRange);

    // Hai dãy radio nằm chung một widget cha, nên phải tách nhóm loại trừ bằng
    // QButtonGroup — nếu không, chọn bên này sẽ bỏ chọn bên kia.
    m_ring5   = new QRadioButton(tr("5 km"), gridBox);
    m_ring1   = new QRadioButton(tr("1 km"), gridBox);
    m_ring05  = new QRadioButton(tr("0.5 km"), gridBox);
    m_ring01  = new QRadioButton(tr("0.1 km"), gridBox);
    m_ringOff = new QRadioButton(tr("Tắt"), gridBox);
    auto *ringGroup = new QButtonGroup(this);
    auto *ringRow = new QHBoxLayout;
    for (auto *b : {m_ring5, m_ring1, m_ring05, m_ring01, m_ringOff}) {
        ringGroup->addButton(b);
        ringRow->addWidget(b);
    }
    ringRow->addStretch(1);
    gridForm->addRow(tr("Vòng tròn cự ly"), ringRow);

    m_az30  = new QRadioButton(tr("30°"), gridBox);
    m_az10  = new QRadioButton(tr("10°"), gridBox);
    m_az5   = new QRadioButton(tr("5°"), gridBox);
    m_azOff = new QRadioButton(tr("Tắt"), gridBox);
    auto *azGroup = new QButtonGroup(this);
    auto *azRow = new QHBoxLayout;
    for (auto *b : {m_az30, m_az10, m_az5, m_azOff}) {
        azGroup->addButton(b);
        azRow->addWidget(b);
    }
    azRow->addStretch(1);
    gridForm->addRow(tr("Đường chia độ"), azRow);

    // --- Nền tạp ra đa ---------------------------------------------------
    auto *videoBox = new QGroupBox(tr("Nền tạp ra đa"), this);
    auto *videoForm = new QFormLayout(videoBox);

    m_videoFade = new QSlider(Qt::Horizontal, videoBox);
    m_videoFade->setRange(0, 10);
    m_videoFade->setPageStep(1);
    m_videoFade->setTickPosition(QSlider::TicksBelow);
    m_videoFade->setTickInterval(1);
    m_videoFadeText = new QLabel(videoBox);
    m_videoFadeText->setMinimumWidth(76);
    m_videoFadeText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *fadeRow = new QHBoxLayout;
    fadeRow->addWidget(m_videoFade, 1);
    fadeRow->addWidget(m_videoFadeText, 0);
    videoForm->addRow(tr("Tốc độ mờ video"), fadeRow);

    // --- Điểm dấu và quỹ đạo ---------------------------------------------
    auto *objBox = new QGroupBox(tr("Điểm dấu và quỹ đạo"), this);
    auto *objForm = new QFormLayout(objBox);

    m_showTracks    = new QCheckBox(tr("Hiện quỹ đạo"), objBox);
    m_showTrackInfo = new QCheckBox(tr("Hiện thông tin quỹ đạo"), objBox);
    m_showPlots     = new QCheckBox(tr("Hiện điểm dấu"), objBox);
    m_showPlotInfo  = new QCheckBox(tr("Hiện thông tin điểm dấu"), objBox);
    m_showRawPlots  = new QCheckBox(tr("Hiện điểm dấu đơn xung"), objBox);

    m_showTrackInfo->setToolTip(tr("Số đầu tốp phía trên quỹ đạo, phương vị - "
                                   "cự ly bên phải"));
    m_showPlotInfo->setToolTip(tr("Phương vị - cự ly bên cạnh điểm dấu"));
    m_showRawPlots->setToolTip(tr("Mỗi xung phát hiện trong gói RAW_P một chấm "
                                  "nhỏ, trước khi gom chùm — để soi thuật toán "
                                  "tâm chùm.\nXoá sớm hơn điểm dấu tâm chùm một "
                                  "giây."));

    // Thụt vào để nhìn ra ngay mục nào phụ thuộc mục nào — ô con bị khoá khi ô
    // cha tắt, mà không thụt thì trông như bốn ô ngang hàng tự dưng khoá nhau.
    m_showTrackInfo->setStyleSheet(QStringLiteral("margin-left: 18px;"));
    m_showPlotInfo->setStyleSheet(QStringLiteral("margin-left: 18px;"));

    // Đơn xung **không** thụt vào dưới "Hiện điểm dấu": nó là một lớp riêng,
    // hiện được cả khi lớp điểm dấu tâm chùm đang tắt.
    for (auto *b : {m_showTracks, m_showTrackInfo, m_showPlots, m_showPlotInfo,
                    m_showRawPlots})
        objForm->addRow(b);

    m_history = new QSlider(Qt::Horizontal, objBox);
    m_history->setRange(0, AppSettings::kMaxHistory);
    m_history->setPageStep(10);
    m_historyText = new QLabel(objBox);
    m_historyText->setMinimumWidth(76);
    m_historyText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *histRow = new QHBoxLayout;
    histRow->addWidget(m_history, 1);
    histRow->addWidget(m_historyText, 0);
    objForm->addRow(tr("Độ dài vết lịch sử quỹ đạo"), histRow);

    m_histPoints = new QRadioButton(tr("Điểm"), objBox);
    m_histLine   = new QRadioButton(tr("Đường"), objBox);
    m_histPoints->setToolTip(tr("Mỗi vết một chấm tròn, màu theo trạng thái quỹ "
                                "đạo lúc để lại vết"));
    m_histLine->setToolTip(tr("Đường nối các vết, kéo dài tới vị trí hiện tại "
                              "của quỹ đạo"));
    auto *histStyleGroup = new QButtonGroup(this);
    auto *histStyleRow = new QHBoxLayout;
    for (auto *b : {m_histPoints, m_histLine}) {
        histStyleGroup->addButton(b);
        histStyleRow->addWidget(b);
    }
    histStyleRow->addStretch(1);
    objForm->addRow(tr("Dạng vết lịch sử quỹ đạo"), histStyleRow);

    objForm->addRow(tr("Kích thước điểm dấu"), makeSizeRow(objBox, m_plotSize));
    objForm->addRow(tr("Kích thước quỹ đạo"),  makeSizeRow(objBox, m_trackSize));

    root->addWidget(mapBox);
    root->addWidget(siteBox);
    root->addWidget(gridBox);
    root->addWidget(videoBox);
    root->addWidget(objBox);
    root->addStretch(1);

    // --- Nối tín hiệu ----------------------------------------------------
    connect(m_brightness, &QSlider::valueChanged, brightValue,
            [brightValue](int v) { brightValue->setText(QString::number(v)); });

    connect(m_mapVisible, &QCheckBox::toggled, this, &SettingsTab::emitChange);
    connect(m_mapStyle, &QComboBox::currentIndexChanged, this, &SettingsTab::emitChange);
    connect(m_brightness, &QSlider::valueChanged, this, &SettingsTab::emitChange);
    for (auto *b : tcBoxes())
        connect(b, &QCheckBox::toggled, this, &SettingsTab::emitChange);

    // Kiểu nền chỉ có nghĩa khi đang bật hiển thị bản đồ; các ô ẩn/hiện lớp thì
    // còn phải đúng kiểu nền TC nữa.
    connect(m_mapVisible, &QCheckBox::toggled, m_mapStyle, &QWidget::setEnabled);
    connect(m_mapVisible, &QCheckBox::toggled, this, &SettingsTab::updateTcEnabled);
    connect(m_mapStyle, &QComboBox::currentIndexChanged,
            this, &SettingsTab::updateTcEnabled);
    connect(m_maxRange, &QDoubleSpinBox::valueChanged, this, &SettingsTab::emitChange);

    connect(m_videoFade, &QSlider::valueChanged, this,
            [this](int v) { m_videoFadeText->setText(fadeText(v)); });
    connect(m_videoFade, &QSlider::valueChanged, this, &SettingsTab::emitChange);

    for (auto *b : {m_ring5, m_ring1, m_ring05, m_ring01, m_ringOff,
                    m_az30, m_az10, m_az5, m_azOff,
                    m_histPoints, m_histLine})
        connect(b, &QRadioButton::toggled, this, &SettingsTab::emitChange);

    for (auto *b : {m_showTracks, m_showTrackInfo, m_showPlots, m_showPlotInfo,
                    m_showRawPlots}) {
        connect(b, &QCheckBox::toggled, this, &SettingsTab::emitChange);
        connect(b, &QCheckBox::toggled, this, &SettingsTab::updateObjectEnabled);
    }

    connect(m_history, &QSlider::valueChanged, this, [this](int v) {
        m_historyText->setText(v == 0 ? tr("Không vẽ") : tr("%1 vết").arg(v));
    });
    connect(m_history, &QSlider::valueChanged, this, &SettingsTab::emitChange);

    // Toạ độ tâm đài chỉ có hiệu lực khi bấm "Áp dụng".
    connect(m_applySite, &QPushButton::clicked, this, &SettingsTab::emitChange);

    setSettings(m_settings);
}

QLayout *SettingsTab::makeSizeRow(
    QWidget *parent, std::array<QRadioButton *, AppSettings::kMaxSizeStep> &out)
{
    // Nhãn là chính hệ số nhân chứ không phải số thứ tự nấc: "1..5" thì phải
    // nhớ nấc nào ứng với cỡ nào, còn "50%" thì đọc là biết.
    static constexpr const char *kLabels[AppSettings::kMaxSizeStep] = {
        "50%", "75%", "100%", "150%", "200%"
    };

    auto *group = new QButtonGroup(this);
    auto *row   = new QHBoxLayout;
    for (int i = 0; i < AppSettings::kMaxSizeStep; ++i) {
        out[size_t(i)] = new QRadioButton(QString::fromLatin1(kLabels[i]), parent);
        group->addButton(out[size_t(i)]);
        row->addWidget(out[size_t(i)]);
        connect(out[size_t(i)], &QRadioButton::toggled, this, &SettingsTab::emitChange);
    }
    row->addStretch(1);
    return row;
}

int SettingsTab::sizeStepOf(
    const std::array<QRadioButton *, AppSettings::kMaxSizeStep> &row)
{
    for (int i = 0; i < AppSettings::kMaxSizeStep; ++i) {
        if (row[size_t(i)] && row[size_t(i)]->isChecked())
            return i + 1;
    }
    return AppSettings::kDefaultSizeStep;
}

void SettingsTab::updateObjectEnabled()
{
    m_showTrackInfo->setEnabled(m_showTracks->isChecked());
    m_showPlotInfo->setEnabled(m_showPlots->isChecked());
}

std::array<QCheckBox *, 5> SettingsTab::tcBoxes() const
{
    return {m_tcAirRoutes, m_tcAirports, m_tcRivers, m_tcPlaceNames, m_tcProvinces};
}

void SettingsTab::updateTcEnabled()
{
    const bool tc = m_mapVisible->isChecked()
                 && m_mapStyle->currentData().toString() == QLatin1String(kTcStyleId);
    for (auto *b : tcBoxes())
        b->setEnabled(tc);
}

void SettingsTab::setAvailableStyles(const QVector<TileSetInfo> &styles)
{
    m_loading = true;
    const QString keep = m_mapStyle->currentData().toString();

    m_mapStyle->clear();
    for (const TileSetInfo &s : styles)
        m_mapStyle->addItem(s.label, s.id);

    if (styles.isEmpty()) {
        m_mapStyle->addItem(tr("(chưa tải bản đồ)"), QString());
        m_mapStyle->setEnabled(false);
    }
    const int i = m_mapStyle->findData(keep);
    if (i >= 0)
        m_mapStyle->setCurrentIndex(i);

    m_loading = false;
    updateTcEnabled();
}

void SettingsTab::setSettings(const AppSettings &s)
{
    m_loading = true;
    m_settings = s;

    const int styleIndex = m_mapStyle->findData(s.mapStyle);
    if (styleIndex >= 0)
        m_mapStyle->setCurrentIndex(styleIndex);
    m_mapStyle->setEnabled(s.mapVisible && m_mapStyle->count() > 0
                           && !m_mapStyle->itemData(0).toString().isEmpty());

    m_mapVisible->setChecked(s.mapVisible);
    m_brightness->setValue(s.mapBrightness);
    m_tcAirRoutes->setChecked(s.tcAirRoutes);
    m_tcAirports->setChecked(s.tcAirports);
    m_tcRivers->setChecked(s.tcRivers);
    m_tcPlaceNames->setChecked(s.tcPlaceNames);
    m_tcProvinces->setChecked(s.tcProvinces);
    m_siteLat->setValue(s.siteLat);
    m_siteLng->setValue(s.siteLng);
    m_maxRange->setValue(s.maxRangeKm);
    m_videoFade->setValue(s.videoFadeSec);
    m_videoFadeText->setText(fadeText(s.videoFadeSec));

    m_showTracks->setChecked(s.showTracks);
    m_showTrackInfo->setChecked(s.showTrackInfo);
    m_showPlots->setChecked(s.showPlots);
    m_showPlotInfo->setChecked(s.showPlotInfo);
    m_showRawPlots->setChecked(s.showRawPlots);
    m_history->setValue(s.trackHistory);
    m_historyText->setText(s.trackHistory == 0 ? tr("Không vẽ")
                                               : tr("%1 vết").arg(s.trackHistory));

    if (s.historyStyle == HistoryStyle::Points)
        m_histPoints->setChecked(true);
    else
        m_histLine->setChecked(true);

    m_plotSize[size_t(qBound(AppSettings::kMinSizeStep, s.plotSizeStep,
                             AppSettings::kMaxSizeStep) - 1)]->setChecked(true);
    m_trackSize[size_t(qBound(AppSettings::kMinSizeStep, s.trackSizeStep,
                              AppSettings::kMaxSizeStep) - 1)]->setChecked(true);

    switch (s.ringMode) {
    case RingMode::R5:  m_ring5->setChecked(true);   break;
    case RingMode::R1:  m_ring1->setChecked(true);   break;
    case RingMode::R05: m_ring05->setChecked(true);  break;
    case RingMode::R01: m_ring01->setChecked(true);  break;
    case RingMode::Off: m_ringOff->setChecked(true); break;
    }

    switch (s.azimuthMode) {
    case AzimuthMode::A30: m_az30->setChecked(true);  break;
    case AzimuthMode::A10: m_az10->setChecked(true);  break;
    case AzimuthMode::A5:  m_az5->setChecked(true);   break;
    case AzimuthMode::Off: m_azOff->setChecked(true); break;
    }

    m_loading = false;
    updateTcEnabled();
    updateObjectEnabled();
}

void SettingsTab::emitChange()
{
    if (m_loading)
        return;

    m_settings.mapVisible    = m_mapVisible->isChecked();
    m_settings.mapBrightness = m_brightness->value();
    if (const QString id = m_mapStyle->currentData().toString(); !id.isEmpty())
        m_settings.mapStyle = id;
    m_settings.tcAirRoutes   = m_tcAirRoutes->isChecked();
    m_settings.tcAirports    = m_tcAirports->isChecked();
    m_settings.tcRivers      = m_tcRivers->isChecked();
    m_settings.tcPlaceNames  = m_tcPlaceNames->isChecked();
    m_settings.tcProvinces   = m_tcProvinces->isChecked();
    m_settings.maxRangeKm    = m_maxRange->value();
    m_settings.videoFadeSec  = m_videoFade->value();

    m_settings.showTracks    = m_showTracks->isChecked();
    m_settings.showTrackInfo = m_showTrackInfo->isChecked();
    m_settings.showPlots     = m_showPlots->isChecked();
    m_settings.showPlotInfo  = m_showPlotInfo->isChecked();
    m_settings.showRawPlots  = m_showRawPlots->isChecked();
    m_settings.trackHistory  = m_history->value();
    m_settings.historyStyle  = m_histPoints->isChecked() ? HistoryStyle::Points
                                                         : HistoryStyle::Line;
    m_settings.plotSizeStep  = sizeStepOf(m_plotSize);
    m_settings.trackSizeStep = sizeStepOf(m_trackSize);

    if (m_ring5->isChecked())        m_settings.ringMode = RingMode::R5;
    else if (m_ring1->isChecked())   m_settings.ringMode = RingMode::R1;
    else if (m_ring05->isChecked())  m_settings.ringMode = RingMode::R05;
    else if (m_ring01->isChecked())  m_settings.ringMode = RingMode::R01;
    else                             m_settings.ringMode = RingMode::Off;

    if (m_az30->isChecked())         m_settings.azimuthMode = AzimuthMode::A30;
    else if (m_az10->isChecked())    m_settings.azimuthMode = AzimuthMode::A10;
    else if (m_az5->isChecked())     m_settings.azimuthMode = AzimuthMode::A5;
    else                             m_settings.azimuthMode = AzimuthMode::Off;

    // Chỉ nhận toạ độ mới khi người dùng bấm "Áp dụng".
    if (sender() == m_applySite) {
        m_settings.siteLat = m_siteLat->value();
        m_settings.siteLng = m_siteLng->value();
    }

    emit settingsChanged(m_settings);
}
