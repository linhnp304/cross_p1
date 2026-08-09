#pragma once

#include "app/appsettings.h"
#include "maps/tilecache.h"

#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLayout;
class QPushButton;
class QRadioButton;
class QSlider;

/// Tab "Cài đặt" trong panel 2.1.
class SettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsTab(QWidget *parent = nullptr);

    /// Đổ danh sách kiểu nền vào ComboBox. Gọi trước setSettings để mục đang
    /// chọn có chỗ mà hiện.
    void setAvailableStyles(const QVector<TileSetInfo> &styles);

    /// Đổ giá trị vào các ô nhập mà không phát tín hiệu thay đổi.
    void setSettings(const AppSettings &s);

signals:
    /// Phát mỗi khi có cài đặt mới cần áp dụng (và lưu xuống JSON).
    void settingsChanged(const AppSettings &s);

private:
    /// Gom giá trị hiện trên giao diện vào m_settings rồi phát tín hiệu.
    void emitChange();

    AppSettings m_settings;
    bool        m_loading = false;   ///< chặn vòng lặp tín hiệu khi đang nạp

    /// Bật/tắt 5 ô ẩn/hiện lớp — chỉ dùng được với kiểu nền TC.
    void updateTcEnabled();

    /// Khoá các ô con khi mục cha đang tắt (thông tin quỹ đạo / điểm dấu).
    void updateObjectEnabled();

    /// Năm ô ẩn/hiện lớp TC, đúng thứ tự hiện trên giao diện.
    std::array<QCheckBox *, 5> tcBoxes() const;

    QCheckBox      *m_mapVisible    = nullptr;
    QComboBox      *m_mapStyle      = nullptr;
    QSlider        *m_brightness    = nullptr;

    QCheckBox *m_tcAirRoutes  = nullptr;
    QCheckBox *m_tcAirports   = nullptr;
    QCheckBox *m_tcRivers     = nullptr;
    QCheckBox *m_tcPlaceNames = nullptr;
    QCheckBox *m_tcProvinces  = nullptr;

    QSlider        *m_videoFade     = nullptr;
    QLabel         *m_videoFadeText = nullptr;

    QCheckBox *m_showTracks    = nullptr;
    QCheckBox *m_showTrackInfo = nullptr;
    QCheckBox *m_showPlots     = nullptr;
    QCheckBox *m_showPlotInfo  = nullptr;
    QCheckBox *m_showRawPlots  = nullptr;
    QSlider   *m_history       = nullptr;
    QLabel    *m_historyText   = nullptr;

    QRadioButton *m_histPoints = nullptr;
    QRadioButton *m_histLine   = nullptr;

    /// Năm nấc kích thước, chỉ số 0 ứng với nấc 1.
    std::array<QRadioButton *, AppSettings::kMaxSizeStep> m_plotSize{};
    std::array<QRadioButton *, AppSettings::kMaxSizeStep> m_trackSize{};

    /// Dựng một dãy 5 radio nấc kích thước vào `out`, trả về hàng để đưa vào form.
    QLayout *makeSizeRow(QWidget *parent,
                         std::array<QRadioButton *, AppSettings::kMaxSizeStep> &out);

    /// Nấc đang chọn trong một dãy radio, mặc định nấc gốc nếu chưa chọn gì.
    static int sizeStepOf(
        const std::array<QRadioButton *, AppSettings::kMaxSizeStep> &row);

    QDoubleSpinBox *m_siteLat       = nullptr;
    QDoubleSpinBox *m_siteLng       = nullptr;
    QPushButton    *m_applySite     = nullptr;
    QDoubleSpinBox *m_maxRange      = nullptr;

    QRadioButton *m_ring5   = nullptr;
    QRadioButton *m_ring1   = nullptr;
    QRadioButton *m_ring05  = nullptr;
    QRadioButton *m_ring01  = nullptr;
    QRadioButton *m_ringOff = nullptr;

    QRadioButton *m_az30  = nullptr;
    QRadioButton *m_az10  = nullptr;
    QRadioButton *m_az5   = nullptr;
    QRadioButton *m_azOff = nullptr;
};
