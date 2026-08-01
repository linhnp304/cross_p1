#pragma once

#include "appsettings.h"
#include "plottrack.h"

#include <QFrame>

class QLabel;

/// Popup xem nhanh thông tin một quỹ đạo.
///
/// Qt::Popup nên bấm ra ngoài là tự đóng — đúng cái người dùng chờ đợi ở một
/// ô thông tin bật lên giữa lúc đang chiến đấu, không phải một cửa sổ nữa phải
/// đi tìm nút đóng.
class TrackInfoPopup : public QFrame
{
    Q_OBJECT

public:
    explicit TrackInfoPopup(QWidget *parent = nullptr);

    /// Hiện thông tin của `t` tại vị trí `globalPos` (toạ độ màn hình).
    void showTrack(const Track &t, const AppSettings &s, const QPoint &globalPos);

private:
    QLabel *m_text = nullptr;
};
