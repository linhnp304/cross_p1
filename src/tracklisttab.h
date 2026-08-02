#pragma once

#include "appsettings.h"
#include "plottrack.h"

#include <QVector>
#include <QWidget>

class QTableWidget;

/// Tab "Danh sách" trong panel 2.1 — bảng quản lý quỹ đạo.
///
/// Bảng vừa hiển thị vừa cho sửa: số đầu tốp, độ cao và phân loại sửa được tại
/// chỗ, còn vị trí / vận tốc / hướng thì do thuật toán ghi. Vì thế việc làm
/// mới bảng phải cẩn thận: ô đang được sửa dở mà bị ghi đè thì trắc thủ gõ
/// giữa chừng lại mất chữ.
class TrackListTab : public QWidget
{
    Q_OBJECT

public:
    explicit TrackListTab(QWidget *parent = nullptr);

    /// Danh sách tên phân loại, để dựng ComboBox trong cột "Loại".
    void setSettings(const AppSettings &s);

    /// Cập nhật bảng theo danh sách quỹ đạo hiện hành.
    void setTracks(const QVector<Track> &tracks);

    /// Cuộn tới và chọn dòng của một quỹ đạo (khi bấm vào nó trên panel 1).
    void selectTrack(quint32 id);

signals:
    void watchChanged(quint32 id, bool on);
    void topChangeRequested(quint32 id, quint32 top);
    void altitudeChanged(quint32 id, quint32 metres);
    void classifyChanged(quint32 id, quint32 classify);
    void removeRequested(quint32 id);

    /// Kích đúp vào một ô **không sửa được** — mở popup thông tin quỹ đạo.
    void trackActivated(quint32 id);

    /// Bấm nút "Thông tin chi tiết điểm dấu".
    void plotListRequested();

    /// Xoá sạch lớp điểm dấu đang vẽ trên panel 1. Không đụng tới cửa sổ
    /// "Thông tin chi tiết điểm dấu" — cửa sổ đó là một dòng chảy riêng và đã
    /// có nút xoá của nó.
    void clearPlotsRequested();

    /// Xoá sạch danh sách quỹ đạo.
    void clearTracksRequested();

private:
    /// Dựng lại toàn bộ dòng. Chỉ gọi khi tập quỹ đạo đổi, vì nó tạo lại cả
    /// các widget trong ô.
    void rebuild(const QVector<Track> &tracks);

    /// Cập nhật chữ trong các ô của một dòng đã có.
    void refreshRow(int row, const Track &t);

    /// Định danh quỹ đạo ở dòng `row`.
    quint32 idAt(int row) const;

    void onItemChanged(class QTableWidgetItem *item);

    AppSettings   m_settings;
    QTableWidget *m_table = nullptr;
    bool          m_loading = false;

    /// Định danh các quỹ đạo đang có trong bảng, đúng thứ tự dòng. Dùng để
    /// biết khi nào phải dựng lại bảng thay vì chỉ cập nhật chữ.
    QVector<quint32> m_ids;
};
