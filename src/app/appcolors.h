#pragma once

// Màu của các đối tượng đồ hoạ trên panel 1, sửa được trong tab "Màu sắc".
//
// Để riêng khỏi AppSettings để tab "Màu sắc" chỉ nhận đúng phần của mình, và
// để thêm một đối tượng đồ hoạ mới chỉ phải đụng vào một chỗ. Vẫn lưu chung
// trong file cấu hình của trắc thủ (khoá "colors").

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <array>

/// Một mục màu: mã khoá trong JSON, nhãn trên giao diện, con trỏ tới màu.
struct ColorEntry {
    const char *key;
    QString     label;
    QColor     *value;
};

struct AppColors {
    // --- lưới toạ độ ---
    //
    /// Vòng cự ly, đường chia độ và nhãn của chúng. Một màu cho cả lưới: độ
    /// đậm nhạt giữa các lớp (0.1 km mảnh nhất, vòng cự ly tối đa đậm nhất) do
    /// RadarView::gridPen() tính ra từ màu này, không phải khai riêng từng lớp.
    /// Đặt độ trong suốt < 255 thì cả lưới mờ đi theo đúng tỉ lệ ấy.
    QColor grid {198, 222, 88};          ///< vàng chanh

    // --- điểm dấu ---
    QColor plotRadar {255,  70,  60};    ///< điểm dấu tâm chùm ra đa — đỏ
    QColor plotIff   {170, 170, 170};    ///< điểm dấu IFF — xám (chưa có IFF)
    QColor plotRaw   {190, 110, 255};    ///< điểm dấu đơn xung — tím

    // --- quỹ đạo ---
    QColor trackPlain      {255, 214,  10};   ///< chưa phân loại — vàng
    QColor trackClassified { 70, 150, 255};   ///< đã gán phân loại — xanh biển
    QColor trackIff        {170, 170, 170};   ///< hợp nhất IFF — xám

    // --- vết lịch sử quỹ đạo ---
    QColor historyTracking { 70, 150, 255};   ///< đang bám — xanh biển
    QColor historyCoasting {255,  70,  60};   ///< ngoại suy — đỏ
    QColor historyOther    {255, 150,  40};   ///< các trạng thái còn lại — cam

    /// Đường nối các vết, khi dạng vết đang để "Đường". Một màu duy nhất chứ
    /// không đổi theo trạng thái như ba màu trên: đoạn nối nằm **giữa** hai vết
    /// có thể khác trạng thái nhau, tô theo đầu nào cũng là sai một nửa.
    QColor historyLine     {255, 214,  10};   ///< vàng

    // --- ô text theo dõi liên tục ---
    QColor labelBackground {255, 245, 170,  55};   ///< vàng nhạt, trong suốt
    QColor labelBorder     {255, 245, 170, 170};
    QColor labelText       {130, 255, 210};        ///< xanh sáng, tương phản nền

    // --- cửa sổ dự đoán của bộ lọc ---
    QColor predictWindow   {120, 200, 255, 130};

    /// Danh sách mọi màu, đúng thứ tự hiện trên tab "Màu sắc". Dùng chung cho
    /// việc dựng giao diện và cho việc lưu/nạp JSON, nên thêm màu mới chỉ phải
    /// sửa đúng một chỗ.
    std::array<ColorEntry, 15> entries();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);
};
