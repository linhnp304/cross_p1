#include "appcolors.h"

#include <QCoreApplication>

std::array<ColorEntry, 12> AppColors::entries()
{
    // QCoreApplication::translate thay cho tr(): AppColors không phải QObject,
    // mà nhãn thì vẫn cần đi qua bộ dịch như mọi chuỗi khác trên giao diện.
    const auto t = [](const char *s) {
        return QCoreApplication::translate("AppColors", s);
    };

    return {{
        {"plotRadar",       t("Điểm dấu ra đa"),          &plotRadar},
        {"plotIff",         t("Điểm dấu IFF"),            &plotIff},
        {"trackPlain",      t("Quỹ đạo chưa phân loại"),  &trackPlain},
        {"trackClassified", t("Quỹ đạo đã phân loại"),    &trackClassified},
        {"trackIff",        t("Quỹ đạo hợp nhất IFF"),    &trackIff},
        {"historyTracking", t("Vết — đang bám"),          &historyTracking},
        {"historyCoasting", t("Vết — ngoại suy"),         &historyCoasting},
        {"historyOther",    t("Vết — trạng thái khác"),   &historyOther},
        {"labelBackground", t("Nền ô text theo dõi"),     &labelBackground},
        {"labelBorder",     t("Viền ô text theo dõi"),    &labelBorder},
        {"labelText",       t("Chữ ô text theo dõi"),     &labelText},
        {"predictWindow",   t("Cửa sổ dự đoán"),          &predictWindow},
    }};
}

QJsonObject AppColors::toJson() const
{
    QJsonObject o;
    // entries() cần đối tượng không hằng vì trả về con trỏ sửa được; ở đây chỉ
    // đọc nên chép ra một bản tạm là xong, rẻ hơn viết hai phiên bản entries().
    AppColors copy = *this;
    for (const ColorEntry &e : copy.entries()) {
        // HexArgb giữ được cả độ trong suốt — nền ô text theo dõi phải trong
        // suốt thì mới nhìn được các lớp đồ hoạ phía dưới.
        o[QString::fromLatin1(e.key)] = e.value->name(QColor::HexArgb);
    }
    return o;
}

void AppColors::fromJson(const QJsonObject &o)
{
    for (const ColorEntry &e : entries()) {
        const QString s = o.value(QString::fromLatin1(e.key)).toString();
        if (s.isEmpty())
            continue;
        // File sửa tay có thể ghi sai tên màu; giữ nguyên mặc định chứ không
        // để một màu vô hiệu lọt vào và biến đối tượng đó thành đen tuyền.
        if (const QColor c = QColor::fromString(s); c.isValid())
            *e.value = c;
    }
}
