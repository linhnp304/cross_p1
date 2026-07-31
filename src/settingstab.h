#pragma once

#include "appsettings.h"
#include "tilecache.h"

#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
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
