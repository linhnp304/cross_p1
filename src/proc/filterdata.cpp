#include "proc/filterdata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {

/// Một dòng file đọc ra số hex 32 bit. False nếu dòng không phải số hex.
///
/// Dòng rỗng cũng là **lỗi** chứ không phải bỏ qua: số dòng là thứ định vị giá
/// trị vào đúng phần tử Filter[], nên bỏ qua một dòng rỗng ở giữa là làm lệch
/// toàn bộ phần còn lại của bộ lọc mà không có gì báo.
bool parseHexLine(const QString &line, quint32 &out)
{
    QString s = line.trimmed();
    if (s.isEmpty())
        return false;
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        s = s.mid(2);

    bool ok = false;
    const qulonglong v = s.toULongLong(&ok, 16);
    if (!ok || v > 0xffffffffull)
        return false;
    out = quint32(v);
    return true;
}

} // namespace

QString filterdata::dirPath()
{
    return QCoreApplication::applicationDirPath() + QLatin1String("/filter");
}

QString filterdata::filePath(const char *fileName)
{
    return dirPath() + QLatin1Char('/') + QLatin1String(fileName);
}

bool filterdata::ensureDir()
{
    return QDir().mkpath(dirPath());
}

filterdata::Result filterdata::load(const QString &path, int count,
                                    int linesPerValue)
{
    Result r;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.error = QCoreApplication::translate(
                      "filterdata", "thiếu file dữ liệu bộ lọc: %1")
                      .arg(QDir::toNativeSeparators(path));
        return r;
    }

    const int need = count * linesPerValue;
    QVector<quint32> lines;
    lines.reserve(need);

    QTextStream in(&f);
    int lineNo = 0;
    while (lines.size() < need && !in.atEnd()) {
        const QString line = in.readLine();
        ++lineNo;

        quint32 v = 0;
        if (!parseHexLine(line, v)) {
            r.error = QCoreApplication::translate(
                          "filterdata",
                          "lỗi đọc dữ liệu bộ lọc %1: dòng %2 không phải số hex "
                          "(\"%3\")")
                          .arg(QFileInfo(path).fileName())
                          .arg(lineNo)
                          .arg(line.trimmed().left(32));
            return r;
        }
        lines.push_back(v);
    }

    if (lines.size() < need) {
        r.error = QCoreApplication::translate(
                      "filterdata",
                      "lỗi đọc dữ liệu bộ lọc %1: chỉ có %2 dòng, cần %3 dòng")
                      .arg(QFileInfo(path).fileName())
                      .arg(lines.size())
                      .arg(need);
        return r;
    }

    r.values.reserve(count);
    if (linesPerValue == 1) {
        r.values = lines;
    } else {
        // Hai dòng một phần tử: dòng trước là nửa cao. Mặt nạ 0xffff đúng theo
        // mô tả giao thức — file dữ liệu chỉ mang số 16 bit, nhưng chặn lại ở
        // đây thì một file sai cũng không đẩy rác sang nửa cao của phần tử.
        for (int i = 0; i < count; ++i) {
            const quint32 hi = lines.at(i * 2);
            const quint32 lo = lines.at(i * 2 + 1);
            r.values.push_back(((hi & 0xffffu) << 16) + lo);
        }
    }
    return r;
}
