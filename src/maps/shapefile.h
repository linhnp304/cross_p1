#pragma once

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

/// Một hình trong tệp shapefile: gồm nhiều phần (part), mỗi phần là một dãy
/// điểm. Toạ độ giữ nguyên như trong tệp — có thể là độ (kinh, vĩ) hoặc mét
/// theo phép chiếu ghi trong .prj, nên đọc xong phải qua PrjProjection.
struct ShpShape {
    int shapeType = 0;
    QVector<QVector<QPointF>> parts;
};

namespace shapefile {

/// Đọc tệp ESRI Shapefile (.shp) chỉ bằng thư viện chuẩn của Qt.
/// Hỗ trợ Point(1), PolyLine(3), Polygon(5) và biến thể Z/M (11,13,15,21,23,25).
/// Tệp không có / không đúng định dạng trả về danh sách rỗng.
QVector<ShpShape> read(const QString &path);

/// Giá trị (chuỗi) của một cột trong tệp thuộc tính dBASE (.dbf) đi kèm
/// shapefile, theo đúng thứ tự bản ghi. Không thấy cột thì trả về các chuỗi rỗng.
QStringList readDbfColumn(const QString &path, const QString &column);

} // namespace shapefile
