#pragma once

// Khởi tạo và bám quỹ đạo bằng bộ lọc Kalman.
//
// Mô hình: vận tốc không đổi, trong hệ Đề-các cục bộ quanh tâm đài (x hướng
// đông, y hướng bắc, đơn vị mét). Chọn Đề-các chứ không phải toạ độ cực vì mục
// tiêu bay thẳng là đường thẳng trong Đề-các nhưng là đường cong trong toạ độ
// cực — bộ lọc cực sẽ phải "đuổi theo" mỗi khi mục tiêu bay ngang qua đài.
//
// Phép đo vẫn ở dạng cực (đài đo cự ly và phương vị), nên ma trận sai số đo
// được xoay từ cực sang Đề-các ở mỗi lần cập nhật.
//
// Nhịp làm việc là **vòng quét**: mỗi điểm dấu tới thì thử ghép vào quỹ đạo
// đang có, hết một vòng quay ăng-ten thì chốt sổ — quỹ đạo nào không được ghép
// thì ngoại suy, ngoại suy quá số vòng cho phép thì xoá.

#include "plottrack.h"
#include "procparams.h"

#include <QVector>

class Tracker
{
public:
    void setParams(const TrackParams &p) { m_params = p; }
    const TrackParams &params() const { return m_params; }

    /// Toạ độ tâm đài, để quy đổi phương vị/cự ly sang kinh vĩ độ.
    void setSite(double lat, double lng) { m_siteLat = lat; m_siteLng = lng; }

    /// Số vết lịch sử giữ lại tối đa cho mỗi quỹ đạo.
    void setHistoryLimit(int n) { m_historyLimit = qBound(0, n, 100); }

    /// Cự ly tối đa của đài (mét). Quỹ đạo ngoại suy ra khỏi vùng phủ thì xoá:
    /// ngoài đó không bao giờ có điểm dấu nào tới cập nhật nữa, để lại chỉ là
    /// một hình tam giác trôi mãi trên màn hình.
    void setMaxRangeMeters(double m) { m_maxRangeM = m; }

    /// Xoá toàn bộ danh sách (bắt đầu một phiên nhận mới).
    void clear();

    /// Ghép một điểm dấu vào quỹ đạo, hoặc mở quỹ đạo mới.
    void addPlot(const PlotTC &plot, qint64 nowMs);

    /// Chốt sổ một vòng quét: ngoại suy các quỹ đạo không được cập nhật, xét
    /// tiêu chuẩn khởi tạo, xoá các quỹ đạo quá hạn.
    /// `periodMs` là chu kỳ vòng quét đo được, dùng làm bước dự đoán.
    void endScan(qint64 nowMs, qint64 periodMs);

    /// Xoá các quỹ đạo quá lâu không có điểm dấu nào ghép vào (tham số
    /// staleSec). Gọi theo nhịp vẽ chứ không theo vòng quét: đây là cái chốt
    /// cuối cùng, phải còn tác dụng cả khi ăng-ten ngừng quay và không còn ai
    /// chốt sổ vòng quét nữa.
    void expireStale(qint64 nowMs);

    const QVector<Track> &tracks() const { return m_tracks; }

    /// Quỹ đạo theo định danh, nullptr nếu không còn.
    Track *find(quint32 id);
    const Track *find(quint32 id) const;

    /// Xoá bằng tay. Trả về false nếu không tìm thấy.
    ///
    /// Quỹ đạo bị chuyển sang trạng thái "xoá" và báo qua removed() trước khi
    /// biến khỏi danh sách — giai đoạn sau còn phải gửi trạng thái đó đi hệ
    /// thống khác.
    bool remove(quint32 id);

    /// Đổi số đầu tốp. False nếu số đó đã có quỹ đạo khác dùng.
    bool setTop(quint32 id, quint32 top);

    /// Nhận một quỹ đạo từ **bên ngoài** thuật toán — hiện chỉ dùng khi phát
    /// lại file dữ liệu đã qua xử lý.
    ///
    /// Lúc đó bộ bám không chạy: quỹ đạo trong file đã là kết quả cuối cùng của
    /// một phiên trước, tính lại là ra kết quả khác chứ không phải tái hiện.
    /// Nhưng mọi thứ vẽ quỹ đạo lên màn hình (panel 1, bảng danh sách, popup)
    /// đều đọc từ đây, nên cách gọn nhất là đổ thẳng vào chính danh sách này và
    /// để bộ bám đứng yên.
    ///
    /// Trạng thái "xoá" (6) rút quỹ đạo khỏi danh sách, đúng như ý nghĩa của nó
    /// ở phía hệ thống nhận.
    void applyExternal(const Track &t, qint64 nowMs);

    /// Các quỹ đạo vừa chuyển sang trạng thái xoá kể từ lần gọi trước.
    /// Lấy ra là danh sách rỗng đi.
    QVector<Track> takeRemoved();

    /// Gọi `fn(const Track &)` cho từng quỹ đạo vừa được cập nhật kể từ lần gọi
    /// trước, rồi xoá dấu. Dạng gọi lại chứ không trả về danh sách: một Track
    /// mang theo cả vec-tơ vết lịch sử, chép ra rồi mới dùng là phí.
    template <typename Fn>
    void takePending(Fn fn)
    {
        for (Track &t : m_tracks) {
            if (!t.pendingSend)
                continue;
            t.pendingSend = false;
            fn(const_cast<const Track &>(t));
        }
    }

    /// True khi danh sách vừa đổi (thêm, bớt, hoặc cập nhật) — để bảng danh
    /// sách khỏi phải dựng lại 25 lần mỗi giây.
    bool takeDirty();

private:
    /// Toạ độ Đề-các cục bộ (mét) của một điểm dấu.
    static void toXY(double azmDeg, double rangeM, double &x, double &y);

    /// Đưa trạng thái quỹ đạo (kx, kp) tiến `dt` giây. Không đụng tới các
    /// trường hiển thị.
    void predict(Track &t, double dt) const;

    /// Cập nhật bộ lọc bằng một phép đo Đề-các kèm ma trận sai số 2x2.
    void correct(Track &t, double zx, double zy, const double r[4]) const;

    /// Ma trận sai số đo ở dạng Đề-các, xoay từ (sigma cự ly, sigma phương vị).
    void measurementNoise(double azmDeg, double rangeM, double r[4]) const;

    /// Đồng bộ các trường hiển thị (azm, range, vận tốc, hướng, lat/lng, cửa
    /// sổ dự đoán) từ trạng thái bộ lọc.
    void refresh(Track &t);

    /// Ghi một vết lịch sử tại vị trí hiện tại.
    void pushHistory(Track &t);

    /// Bán kính cửa sổ liên kết (mét) khi dự đoán quỹ đạo tiến `dt` giây.
    double gateMeters(const Track &t, double dt) const;

    /// Nửa góc mà cửa sổ đó chắn ở cự ly `rangeM` — để dựng cửa sổ dự đoán ở
    /// dạng cực cho gói tin Track và cho việc vẽ.
    double gateHalfAngleDeg(double gateM, double rangeM) const;

    TrackParams m_params;

    QVector<Track> m_tracks;
    QVector<Track> m_removed;

    quint32 m_nextId  = 1;
    quint32 m_serial  = 0;
    double  m_siteLat = 21.028;
    double  m_siteLng = 105.852;
    int     m_historyLimit = 100;
    bool    m_dirty = false;

    /// Chu kỳ vòng quét đo được (giây). Dùng để mở cửa sổ liên kết cho quỹ đạo
    /// chưa biết vận tốc và để dựng cửa sổ dự đoán. Mặc định 6 vòng/phút.
    double  m_scanSec = 10.0;

    double  m_maxRangeM = 1218.0;
};
