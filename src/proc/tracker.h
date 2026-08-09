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
// Nhịp làm việc là **vòng quét**, nhưng mỗi quỹ đạo có nhịp riêng: chốt sổ khi
// đường quét đi hết cửa sổ dự đoán của chính nó, chứ không phải cả danh sách
// cùng chốt lúc ăng-ten qua hướng bắc. Quỹ đạo ở phương vị 90° mà mất điểm dấu
// thì biết ngay lúc đường quét vừa lướt qua, không phải chờ thêm 3/4 vòng nữa —
// trắc thủ thấy hình ngoại suy đúng lúc mục tiêu mất, không muộn mấy giây.
//
// Quỹ đạo nào không được ghép thì ngoại suy, ngoại suy quá số vòng cho phép thì
// xoá.
//
// Chưa đủ tiêu chuẩn khởi tạo thì quỹ đạo nằm ở danh sách **ứng viên** riêng:
// mới chỉ là một cửa sổ dự đoán đang chờ vòng sau, chưa hiển thị và chưa gửi đi
// đâu. Để riêng chứ không đánh dấu bằng một cờ trong cùng danh sách, vì mọi nơi
// vẽ và gửi đều đọc từ tracks() — thêm cờ là thêm chừng ấy chỗ phải nhớ lọc, và
// quên một chỗ thì lỗi lại đúng là cái đang phải sửa.

#include "proc/plottrack.h"
#include "proc/procparams.h"

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

    /// Các vùng cấm khởi tạo quỹ đạo. Chỉ chặn việc **mở** quỹ đạo mới; quỹ đạo
    /// đã có bay qua vùng vẫn được bám tiếp.
    void setNoInitZones(const QVector<NoInitZone> &z) { m_noInitZones = z; }

    /// Xoá toàn bộ danh sách (bắt đầu một phiên nhận mới).
    void clear();

    /// Ghép một điểm dấu vào quỹ đạo, hoặc mở một cửa sổ dự đoán mới.
    void addPlot(const PlotTC &plot, qint64 nowMs);

    /// Báo phương vị hiện thời của ăng-ten (độ). Gọi mỗi khi có gói mang phương
    /// vị — đây là thứ đẩy nhịp chốt sổ của từng quỹ đạo.
    void onSweep(double beamAzmDeg, qint64 nowMs);

    /// Chu kỳ một vòng quay ăng-ten đo được (ms). Dùng làm bước dự đoán và để
    /// quay đường quét ảo khi nguồn không mang phương vị.
    void setScanPeriod(qint64 periodMs);

    /// Nhịp đồng hồ, gọi theo nhịp vẽ. Khi không gói nào mang phương vị ăng-ten
    /// (nguồn chỉ phát PlotTC, hoặc đài vừa ngừng phát) thì đường quét được
    /// quay bằng đồng hồ theo chu kỳ đo được lần cuối — nếu không, quỹ đạo cũ
    /// nằm lại mãi vì không có gì chốt sổ cho chúng nữa.
    void tickClock(qint64 nowMs);

    /// Xoá các quỹ đạo quá lâu không có điểm dấu nào ghép vào (tham số
    /// staleSec). Gọi theo nhịp vẽ chứ không theo vòng quét: đây là cái chốt
    /// cuối cùng, phải còn tác dụng cả khi ăng-ten ngừng quay và không còn ai
    /// chốt sổ vòng quét nữa.
    void expireStale(qint64 nowMs);

    /// Các quỹ đạo **đã khởi tạo** — chỉ những cái này được vẽ, được liệt kê và
    /// được gửi đi.
    const QVector<Track> &tracks() const { return m_tracks; }

    /// Các cửa sổ dự đoán đang chờ đủ tiêu chuẩn khởi tạo. Chỉ dùng để vẽ khi
    /// trắc thủ bật "Vẽ cửa sổ dự đoán" — nhìn thấy cửa sổ nào đang chờ là cách
    /// duy nhất để kiểm tra mắt thường xem tiêu chuẩn khởi tạo có chạy đúng không.
    const QVector<Track> &candidates() const { return m_cand; }

    /// Bỏ hết các cửa sổ đang chờ. Chúng chưa từng được báo ra ngoài nên biến
    /// mất lặng lẽ, không có gói trạng thái "xoá" nào cả.
    void clearCandidates();

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

    /// Vị trí này có nằm trong một vùng cấm khởi tạo đang bật không.
    bool inNoInitZone(double azmDeg, double rangeM) const
    {
        for (const NoInitZone &z : m_noInitZones) {
            if (z.on && z.contains(azmDeg, rangeM))
                return true;
        }
        return false;
    }

    /// Nửa góc mà cửa sổ đó chắn ở cự ly `rangeM` — để dựng cửa sổ dự đoán ở
    /// dạng cực cho gói tin Track và cho việc vẽ.
    double gateHalfAngleDeg(double gateM, double rangeM) const;

    /// Đặt mốc chốt sổ cho vòng tới: mép sau của cửa sổ dự đoán. `firstRound`
    /// cho vòng đầu tiên của một cửa sổ vừa mở — vòng đó chốt ngay trong lượt
    /// quét này chứ không phải một vòng ăng-ten nữa.
    void armRound(Track &t, bool firstRound = false) const;

    /// Phần chung của việc chốt sổ một vòng: dồn bit phát hiện, ngoại suy nếu
    /// vòng vừa rồi không có điểm dấu.
    void closeCommon(Track &t, qint64 nowMs);

    /// Số phận của một ứng viên sau khi chốt sổ một vòng.
    enum class CandOutcome {
        Wait,      ///< chưa đủ tiêu chuẩn nhưng vẫn còn cửa, chờ vòng sau
        Promote,   ///< đủ tiêu chuẩn khởi tạo, thành quỹ đạo
        Drop,      ///< hết cửa, huỷ cửa sổ dự đoán
    };

    /// Chốt sổ một ứng viên.
    CandOutcome closeCandidate(Track &t, qint64 nowMs);

    /// Chốt sổ một quỹ đạo đã khởi tạo. Trả về true nếu nó phải bị xoá.
    bool closeTrack(Track &t, qint64 nowMs);

    /// Chốt sổ mọi quỹ đạo mà đường quét đã đi qua mốc.
    void closeDueRounds(qint64 nowMs);

    TrackParams m_params;

    QVector<Track> m_tracks;
    QVector<Track> m_cand;
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

    /// Góc quét cộng dồn (độ), tăng mãi không quay vòng. Mọi mốc chốt sổ đều đo
    /// trên thang này, nên không chỗ nào phải lý sự chuyện vắt qua hướng bắc.
    double  m_sweepDeg = 0.0;

    /// Phương vị ăng-ten lần cuối nhận được, -1 khi chưa có gói nào.
    double  m_lastBeamDeg = -1.0;

    /// Mốc đồng hồ của gói mang phương vị gần nhất, và của lần quay đường quét
    /// ảo gần nhất. Xem tickClock().
    qint64  m_lastSweepMs   = 0;
    qint64  m_virtualSweepMs = 0;

    double  m_maxRangeM = 1218.0;

    QVector<NoInitZone> m_noInitZones;
};
