#include "maps/shapefile.h"

#include <QFile>
#include <QStringDecoder>

#include <cstring>

namespace {

/// Shapefile trộn hai thứ tự byte: phần tiêu đề bản ghi là big-endian, còn nội
/// dung hình là little-endian. Đọc bằng memcpy để không phạm luật bí danh kiểu.
qint32 beInt32(const QByteArray &b, int off)
{
    const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + off;
    return qint32((quint32(p[0]) << 24) | (quint32(p[1]) << 16)
                | (quint32(p[2]) << 8) | quint32(p[3]));
}

qint32 leInt32(const QByteArray &b, int off)
{
    const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + off;
    return qint32(quint32(p[0]) | (quint32(p[1]) << 8)
                | (quint32(p[2]) << 16) | (quint32(p[3]) << 24));
}

double leDouble(const QByteArray &b, int off)
{
    quint64 bits = 0;
    const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + off;
    for (int i = 7; i >= 0; --i)
        bits = (bits << 8) | quint64(p[i]);
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

quint16 leUInt16(const QByteArray &b, int off)
{
    const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + off;
    return quint16(quint16(p[0]) | (quint16(p[1]) << 8));
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

} // namespace

QVector<ShpShape> shapefile::read(const QString &path)
{
    QVector<ShpShape> shapes;
    const QByteArray b = readAll(path);
    if (b.size() < 100)
        return shapes;

    if (beInt32(b, 0) != 9994)   // mã nhận dạng của định dạng shapefile
        return shapes;

    // Độ dài ghi trong tiêu đề tính bằng "word" 16 bit. Tệp bị cắt cụt thì lấy
    // theo kích thước thật để không đọc lố.
    const int fileLen = beInt32(b, 24) * 2;
    const int end = qMin(fileLen, int(b.size()));

    int i = 100;   // hết tiêu đề tệp
    while (i + 12 <= end) {
        const int contentLen = beInt32(b, i + 4) * 2;
        const int rec = i + 8;
        if (contentLen < 4 || rec + contentLen > b.size())
            break;

        const int shapeType = leInt32(b, rec);
        ShpShape shape;
        shape.shapeType = shapeType;

        switch (shapeType) {
        case 1: case 11: case 21:            // Point / PointZ / PointM
            if (rec + 20 <= b.size()) {
                shape.parts.push_back({QPointF(leDouble(b, rec + 4),
                                               leDouble(b, rec + 12))});
                shapes.push_back(shape);
            }
            break;

        case 3: case 5: case 13: case 15: case 23: case 25: {   // PolyLine / Polygon (+Z/M)
            if (rec + 44 > b.size())
                break;
            const int numParts  = leInt32(b, rec + 36);
            const int numPoints = leInt32(b, rec + 40);
            if (numParts <= 0 || numPoints <= 0)
                break;

            const int partsOff  = rec + 44;
            const int pointsOff = partsOff + numParts * 4;
            if (qint64(pointsOff) + qint64(numPoints) * 16 > b.size())
                break;

            QVector<int> partIdx(numParts + 1);
            for (int p = 0; p < numParts; ++p)
                partIdx[p] = leInt32(b, partsOff + p * 4);
            partIdx[numParts] = numPoints;

            shape.parts.reserve(numParts);
            for (int p = 0; p < numParts; ++p) {
                const int from = partIdx[p];
                const int cnt  = partIdx[p + 1] - from;
                if (cnt <= 0 || from < 0 || from + cnt > numPoints)
                    continue;
                QVector<QPointF> pts;
                pts.reserve(cnt);
                for (int k = 0; k < cnt; ++k) {
                    const int off = pointsOff + (from + k) * 16;
                    pts.push_back(QPointF(leDouble(b, off), leDouble(b, off + 8)));
                }
                shape.parts.push_back(std::move(pts));
            }
            if (!shape.parts.isEmpty())
                shapes.push_back(shape);
            break;
        }

        default:
            break;   // hình rỗng (0) và các loại chưa dùng: bỏ qua
        }

        i = rec + contentLen;
    }
    return shapes;
}

QStringList shapefile::readDbfColumn(const QString &path, const QString &column)
{
    const QByteArray b = readAll(path);
    if (b.size() < 32)
        return {};

    const int numRecords = leInt32(b, 4);
    const int headerSize = leUInt16(b, 8);
    const int recordSize = leUInt16(b, 10);
    if (numRecords <= 0 || headerSize <= 32 || recordSize <= 0)
        return {};

    // Mỗi trường được mô tả bằng 32 byte, danh sách kết thúc bằng byte 0x0D.
    int fieldOffset = 1;   // byte đầu mỗi bản ghi là cờ đánh dấu đã xoá
    int targetOffset = -1;
    int targetLength = 0;
    for (int off = 32; off + 32 <= headerSize && quint8(b[off]) != 0x0D; off += 32) {
        const QByteArray raw = b.mid(off, 11);
        const int nul = raw.indexOf('\0');
        const QString name = QString::fromLatin1(nul >= 0 ? raw.left(nul) : raw);
        const int len = quint8(b[off + 16]);
        if (name.compare(column, Qt::CaseInsensitive) == 0) {
            targetOffset = fieldOffset;
            targetLength = len;
        }
        fieldOffset += len;
    }

    QStringList out;
    out.reserve(numRecords);
    if (targetOffset < 0) {
        for (int r = 0; r < numRecords; ++r)
            out << QString();
        return out;
    }

    // Bảng mã của .dbf khai trong tệp .cpg đi kèm, nhưng dữ liệu đang dùng đều
    // là UTF-8 hoặc ASCII nên đọc thẳng bằng UTF-8, ký tự lạ thành ký tự thay thế.
    auto decoder = QStringDecoder(QStringDecoder::Utf8);
    for (int r = 0; r < numRecords; ++r) {
        const qint64 recOff = qint64(headerSize) + qint64(r) * recordSize;
        if (recOff + recordSize > b.size())
            break;
        const QByteArray raw = b.mid(int(recOff) + targetOffset, targetLength);
        out << QString(decoder.decode(raw)).remove(QChar('\0')).trimmed();
    }
    while (out.size() < numRecords)
        out << QString();
    return out;
}
