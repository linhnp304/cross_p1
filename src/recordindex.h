#pragma once

// Danh mục các phiên ghi lưu, giữ trong một cơ sở dữ liệu SQLite.
//
// File .rec đã tự mang đủ thông tin trong 64 byte header, nên về nguyên tắc chỉ
// cần duyệt thư mục là dựng lại được danh sách. Danh mục này là **bản chép sẵn**
// của các header đó: mở ComboBox danh sách phiên thì không phải mở hàng nghìn
// file để đọc 64 byte mỗi file, và thông tin của phiên vừa ghi xong có ngay mà
// không phải quét lại đĩa.
//
// Vì thế đĩa luôn là nguồn đúng, còn bảng chỉ là bộ nhớ đệm: xoá file .db đi thì
// lần chạy sau dựng lại đầy đủ từ chính các file .rec.

#include "recordfile.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

class QDateTime;

/// Một phiên ghi lưu — đúng một file .rec.
struct RecSession {
    QString path;            ///< đường dẫn tuyệt đối
    quint32 kind     = 0;    ///< rec::kKindRaw hoặc rec::kKindProc
    qint64  startSec = 0;    ///< giây từ epoch, UTC
    qint64  endSec   = 0;
    quint32 total = 0, rawV = 0, rawP = 0, video = 0, plot = 0, track = 0,
            other = 0;
    qint64  bytes = 0;

    /// Tên hiện trong ComboBox danh sách: thời gian bắt đầu ghi, giờ địa phương.
    QString label() const;
};

/// Bảng danh mục. Toàn bộ đối tượng này sống trên **một luồng duy nhất** (luồng
/// ghi lưu) — kết nối QSqlDatabase gắn chặt với luồng tạo ra nó.
class RecordIndex
{
public:
    ~RecordIndex();

    /// Thư mục gốc ./records, nằm cạnh file chạy.
    static QString rootDir();

    /// Thư mục ./records/yyyy/MM/dd của một thời điểm, tạo mới nếu chưa có.
    /// Trả về chuỗi rỗng kèm `error` nếu không tạo được.
    static QString dayDir(const QDateTime &at, QString &error);

    /// Mở (và tạo nếu cần) ./records/index.db.
    bool open(QString &error);

    /// Duyệt lại toàn bộ ./records: đọc header của file mới, bỏ hàng của file
    /// đã bị xoá. Chạy một lần lúc khởi động.
    void rescan();

    /// Thêm hoặc cập nhật một phiên. Gọi mỗi khi đóng xong một file.
    void upsert(const RecSession &s);

    /// Toàn bộ danh mục, mới nhất đứng đầu.
    QVector<RecSession> sessions();

    /// Đọc header của một file .rec bất kỳ. Dùng cho cả việc duyệt thư mục lẫn
    /// việc mở tệp ghi lưu nằm ngoài danh sách.
    static bool probe(const QString &path, RecSession &out, QString &error);

private:
    QSqlDatabase m_db;
    QString      m_conn;
};
