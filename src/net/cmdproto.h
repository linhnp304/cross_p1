#pragma once

// Bốn gói lệnh điều khiển đài và trạng thái phản hồi của chúng.
//
// Mỗi lệnh và trạng thái phản hồi của nó dùng **chung một bố cục gói tin**, chỉ
// khác trường Category. Vì vậy ở đây mỗi loại gói chỉ mô tả **một lần**: một
// bảng trường, dùng cho cả việc đóng gói lệnh, tách trạng thái, lẫn việc dựng
// giao diện điều khiển. Viết tay ba lần cho ba việc đó là ba danh sách phải giữ
// khớp nhau bằng mắt — lệch một ô là mọi trường phía sau sai hết mà không có gì
// báo (xem chú thích cùng ý ở packetio.h).
//
// Vị trí của trường thứ i trong gói là từ **5 + i**: năm từ đầu luôn là Header,
// Category, Length, Serial, Time, và từ cuối cùng là CheckSum. Nhờ vậy thêm bớt
// một trường ở giữa bảng là mọi thứ tự động dịch theo, không phải sửa chỉ số.
//
// Trường chưa dùng đến (DR1, DR5, VR10) vẫn phải có mặt trong bảng vì chúng
// chiếm chỗ thật trong gói tin — chỉ là không có giao diện.

#include <QByteArray>
#include <QtEndian>
#include <QtGlobal>

#include <cmath>
#include <cstring>
#include <iterator>

namespace cmdproto {

/// Kiểu điều khiển của một trường trên giao diện.
enum class Widget {
    None,      ///< chưa dùng đến — chiếm một từ trong gói, không có giao diện
    Radio,     ///< nhóm nút chọn
    Combo,     ///< hộp chọn
    Spin,      ///< ô nhập số nguyên
    SpinF,     ///< ô nhập số thực (có hệ số quy đổi, hoặc trường kiểu float)
    ReadOnly,  ///< ô nhập bị khoá, chỉ hiện giá trị trạng thái trả về
};

/// Cách hiện giá trị của một trường chỉ nhận trạng thái.
enum class Format {
    Number,
    HwVersion,   ///< 0xyyMMddhh (dạng hex) -> yyyy/MM/dd-hh
};

/// Một lựa chọn của Radio/Combo. Giá trị **không** nhất thiết bằng chỉ số:
/// DSPV_Out nhảy 0,1,2,4,8 còn FKSL_Filter đánh số từ 1.
struct Option {
    quint32     value;
    const char *label;
};

/// Một trường trong gói tin.
///
/// `factor` là cầu nối giữa hai thang số: **giá trị trong gói = round(giá trị
/// trên giao diện × factor)**. Trường không quy đổi thì factor = 1.
///
/// `dividend` khác 0 thì hai thang số nối với nhau **nghịch đảo** thay vì tỉ
/// lệ: **giá trị trong gói = round(dividend / giá trị trên giao diện)**, và
/// `factor` không dùng tới.
struct Field {
    const char *name   = "";     ///< tên trường trong mô tả giao thức
    const char *label  = "";     ///< nhãn trên giao diện
    const char *tip    = "";     ///< tooltip, rỗng là không có
    Widget      widget = Widget::None;
    quint32     def    = 0;      ///< mặc định, **dạng gói tin**
    double      lo     = 0.0;    ///< dải trên giao diện
    double      hi     = 4294967295.0;
    double      factor = 1.0;
    double      dividend = 0.0;  ///< khác 0: quy đổi nghịch đảo
    int         decimals = 0;
    bool        isSigned = false;
    Format      format   = Format::Number;
    const Option *options = nullptr;
    int           optionCount = 0;

    /// Giá trị trong gói quay vòng theo modulo này; 0 là không quay vòng. Dùng
    /// cho các trường góc: 360 độ quy ra đúng 4096 nấc encoder, vượt dải 0..4095
    /// mà mô tả giao thức nêu, trong khi 4096 nấc lại chính là 0 độ.
    int wrap = 0;

    /// Trường lấy giá trị gửi đi từ một trường của gói **khác**: số hiệu group
    /// và vị trí trường bên đó. -1 là trường bình thường. Xem mirrored().
    int mirrorGroup = -1;
    int mirrorIndex = -1;

    /// Giá trị gửi đi lấy từ gói khác chứ không phải từ ô của chính nó.
    bool mirrors() const { return mirrorGroup >= 0; }

    /// Trường này có gửi giá trị đi trong lệnh không. Trường chỉ nhận trạng
    /// thái thì lệnh gửi đi gán = 0, đúng mô tả giao thức — trừ trường soi giá
    /// trị gói khác: ô của nó khoá, nhưng giá trị vẫn phải đi trong lệnh.
    bool sends() const { return widget != Widget::ReadOnly || mirrors(); }

    /// Có chỗ cho người dùng đụng vào không.
    bool editable() const
    {
        return widget != Widget::None && widget != Widget::ReadOnly;
    }
};

// --- các hàm dựng bảng ------------------------------------------------------
//
// Bảng trường viết bằng những hàm này thay vì khởi tạo tuần tự 14 thành viên:
// đọc lên là so được từng dòng với mô tả giao thức, mà thêm một thuộc tính mới
// vào Field cũng không phải sửa lại cả bốn bảng.

constexpr Field unusedField(const char *name)
{
    Field f;
    f.name = name;
    return f;
}

constexpr Field radio(const char *name, const char *label, quint32 def,
                      const Option *options, int count, const char *tip = "")
{
    Field f;
    f.name        = name;
    f.label       = label;
    f.tip         = tip;
    f.widget      = Widget::Radio;
    f.def         = def;
    f.options     = options;
    f.optionCount = count;
    return f;
}

/// Hộp chọn liệt kê sẵn các số nguyên từ `lo` tới `hi`.
constexpr Field combo(const char *name, const char *label, quint32 def,
                      double lo, double hi, const char *tip = "")
{
    Field f;
    f.name   = name;
    f.label  = label;
    f.tip    = tip;
    f.widget = Widget::Combo;
    f.def    = def;
    f.lo     = lo;
    f.hi     = hi;
    return f;
}

constexpr Field spin(const char *name, const char *label, quint32 def,
                     double lo, double hi, const char *tip = "")
{
    Field f;
    f.name   = name;
    f.label  = label;
    f.tip    = tip;
    f.widget = Widget::Spin;
    f.def    = def;
    f.lo     = lo;
    f.hi     = hi;
    return f;
}

/// Ô nhập số thực có hệ số quy đổi sang giá trị trong gói tin.
constexpr Field scaled(const char *name, const char *label, quint32 def,
                       double lo, double hi, double factor, int decimals,
                       const char *tip = "")
{
    Field f;
    f.name     = name;
    f.label    = label;
    f.tip      = tip;
    f.widget   = Widget::SpinF;
    f.def      = def;
    f.lo       = lo;
    f.hi       = hi;
    f.factor   = factor;
    f.decimals = decimals;
    return f;
}

/// Như scaled nhưng trường trong gói là số nguyên **có dấu** 32 bit.
constexpr Field scaledSigned(const char *name, const char *label, quint32 def,
                             double lo, double hi, double factor, int decimals,
                             const char *tip = "")
{
    Field f   = scaled(name, label, def, lo, hi, factor, decimals, tip);
    f.isSigned = true;
    return f;
}

/// Ô nhập số thực quy đổi **nghịch đảo**: giá trị trong gói =
/// round(dividend / giá trị trên giao diện).
///
/// Số 0 ở một đầu ứng với số 0 ở đầu kia chứ không phải vô cùng — xem
/// toUi()/fromUi().
constexpr Field reciprocal(const char *name, const char *label, quint32 def,
                           double lo, double hi, double dividend, int decimals,
                           const char *tip = "")
{
    Field f;
    f.name     = name;
    f.label    = label;
    f.tip      = tip;
    f.widget   = Widget::SpinF;
    f.def      = def;
    f.lo       = lo;
    f.hi       = hi;
    f.dividend = dividend;
    f.decimals = decimals;
    return f;
}

/// Ô khoá, nhưng giá trị **gửi đi** lấy thẳng từ một trường của gói khác.
///
/// Trên giao diện giống hệt readOnly(): ô xám, chỉ hiện giá trị trạng thái đài
/// trả về, người dùng không nhập được. Khác ở lúc đóng gói lệnh — thay vì gửi 0
/// như trường chỉ nhận trạng thái, nó gửi giá trị đang có của trường được trỏ
/// tới. Quy đổi hiển thị (factor/decimals) vẫn phải khai đủ ở đây, vì ô này hiện
/// giá trị theo đơn vị của **chính nó**.
/// Dải lo/hi vẫn phải khai dù ô không nhập được: fromUi() kẹp theo dải này, mà
/// để mặc định là 0..2^32 thì mọi giá trị âm bị kẹp về 0.
constexpr Field mirrored(const char *name, const char *label, int group,
                         int index, double lo, double hi, double factor,
                         int decimals, const char *tip = "")
{
    Field f;
    f.name        = name;
    f.label       = label;
    f.tip         = tip;
    f.widget      = Widget::ReadOnly;
    f.lo          = lo;
    f.hi          = hi;
    f.factor      = factor;
    f.decimals    = decimals;
    f.mirrorGroup = group;
    f.mirrorIndex = index;
    return f;
}

constexpr Field readOnly(const char *name, const char *label,
                         Format format = Format::Number, const char *tip = "")
{
    Field f;
    f.name   = name;
    f.label  = label;
    f.tip    = tip;
    f.widget = Widget::ReadOnly;
    f.format = format;
    return f;
}

/// Số hiệu group, cũng là chỉ số trong bảng kPackets ở cuối tệp. Khai báo ngay
/// đây chứ không cạnh bảng ấy, vì một trường có thể trỏ sang group khác — xem
/// mirrored().
enum Group { GroupAnten = 0, GroupCommon, GroupDspR, GroupDspS, GroupCount };

/// So tên trường lúc biên dịch. Chỉ dùng cho fieldIndex(), nên không cần lo
/// con trỏ rỗng: mọi trường trong bảng đều có tên.
constexpr bool sameName(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/// Vị trí một trường trong bảng, tra bằng **tên trong mô tả giao thức**. Trỏ
/// sang trường khác bằng tên chứ không bằng số: chèn thêm một trường vào giữa
/// bảng là mọi chỉ số viết tay sẽ trỏ lệch mà không có gì báo.
template <int N>
constexpr int fieldIndex(const Field (&fields)[N], const char *name)
{
    for (int i = 0; i < N; ++i) {
        if (sameName(fields[i].name, name))
            return i;
    }
    return -1;
}

// --- CMD_ANTEN: điều khiển ăng ten ------------------------------------------

inline constexpr Option kAtEn[]   = {{0, "Dừng"},     {1, "Quay"}};
inline constexpr Option kAtSync[] = {{0, "Độc lập"},  {1, "Đồng bộ"}};

inline constexpr Field kAntenFields[] = {
    radio("AT_En",    "Quay ăng ten",     0, kAtEn,   2),
    radio("AT_Sync",  "Quay đồng bộ",     0, kAtSync, 2),
    combo("AT_Speed", "Tốc độ quay (v/p)", 6, 1, 30),
    readOnly("AT_Azm", "Phương vị trả về", Format::Number,
             "Chỉ hiện giá trị trạng thái trả về"),
};

// --- CMD_COMMON: tham số chung ----------------------------------------------

inline constexpr Option kDataSend[] = {
    {0, "Tắt"}, {1, "Fbeat"}, {2, "Doppler"}, {3, "Beam"}};
inline constexpr Option kPcEn[] = {{0, "PC1"}, {1, "PC2"}, {2, "All"}};
inline constexpr Option kTxEn[] = {
    {0, "Tắt phát"}, {1, "Liên tục"}, {2, "Rẻ quạt"}};

/// Một độ ứng với bao nhiêu nấc encoder — dùng cho Beta1/Beta2/AzmOffset/
/// FixEncoder. Đúng phép quy đổi mô tả giao thức nêu: 4096/360.
inline constexpr double kEncPerDeg = 4096.0 / 360.0;

/// Số chữ số thập phân của các ô nhập góc.
///
/// **Một** chữ số, không phải hai, và đây là con số duy nhất đúng: một nấc
/// encoder bằng 360/4096 = 0.088 độ, nên giá trị gõ vào bị làm tròn tới nấc gần
/// nhất, sai lệch nhiều nhất 0.044 độ. Hiện một chữ số thập phân thì sai lệch
/// đó nằm gọn dưới nửa đơn vị cuối và giá trị gõ vào hiện lại đúng như cũ; hiện
/// hai chữ số thì gõ 359.00 mở ra lần sau thành 359.03.
inline constexpr int kAngleDecimals = 1;

/// Số nấc encoder một vòng. Giá trị trong gói chạy 0..4095.
inline constexpr int kEncSteps = 4096;

/// Ô nhập một góc: giao diện **0..360 độ**, trong gói là **nấc encoder 0..4095**.
///
/// Cho quay vòng vì 360 độ quy ra đúng 4096 nấc — vượt dải mà mô tả giao thức
/// nêu, mà 4096 nấc lại chính là 0 độ. Quay vòng thì gõ 360 ra 0 nấc, đúng cả
/// về góc lẫn về dải; không quay vòng thì gói mang một giá trị đài không hiểu.
constexpr Field angle(const char *name, const char *label, quint32 def,
                      const char *tip = "")
{
    Field f = scaled(name, label, def, 0.0, 360.0, kEncPerDeg, kAngleDecimals, tip);
    f.wrap = kEncSteps;
    return f;
}

/// Như angle() nhưng ô bị khoá và giá trị gửi đi soi từ gói khác — xem
/// mirrored().
constexpr Field angleMirrored(const char *name, const char *label, int group,
                              int index, const char *tip = "")
{
    Field f = mirrored(name, label, group, index, 0.0, 360.0, kEncPerDeg,
                       kAngleDecimals, tip);
    f.wrap = kEncSteps;
    return f;
}

inline constexpr Field kCommonFields[] = {
    radio("DataSend", "Chọn dữ liệu", 0, kDataSend, 4,
          "Chuyển mạch chọn loại dữ liệu để truyền đi"),
    spin("WordStart", "Vẽ búp sóng (StartWord)", 0, 0, 4294967295.0,
         "Vị trí bắt đầu lấy dữ liệu vẽ búp sóng"),
    spin("WordNum", "Vẽ búp sóng (NumWord)", 0, 0, 4294967295.0,
         "Số word lấy dữ liệu vẽ búp sóng"),
    radio("PCEn", "Chọn điểm nhận dữ liệu", 2, kPcEn, 3),
    spin("Attn", "Steps Attn", 0, 0, 4294967295.0),
    radio("TxEn", "Nối/Tắt phát", 0, kTxEn, 3),
    combo("FilterSL", "Bộ lọc FBeat", 1, 1, 5, "Chọn bộ lọc tương tự FBeat SFC"),
    angle("Beta1", "Phương vị đầu (độ)", 0, "Phương vị đầu rẻ quạt phát"),
    // 4085 nấc = 359.0 độ. Mô tả giai đoạn ghi mặc định 360, nhưng 360 quy ra
    // 4096 nấc, tức 0 độ — không phải ý của mô tả. 359 là giá trị lớn nhất còn
    // nằm gọn trong dải 0..4095.
    angle("Beta2", "Phương vị cuối (độ)", 4085, "Phương vị cuối rẻ quạt phát"),
    angle("AzmOffset", "Bù góc phương Bắc (độ)", 0),
};

// --- CMD_DSP_R: tham số DSP kênh cự ly --------------------------------------

inline constexpr Option kDspdOut[] = {
    {0, "IQ"}, {1, "Biên độ"}, {2, "Bộ đếm KT"}};
inline constexpr Option kIzpEn[] = {
    {0, "Tắt"}, {1, "Ngoài"}, {2, "Trong"}, {3, "UDP"}};
inline constexpr Option kOffOn[] = {{0, "Tắt"}, {1, "Bật"}};
inline constexpr Option kIfDsp[] = {
    {0, "Tắt"}, {1, "Kênh A"}, {2, "Kênh B"}, {3, "Tạo giả"}};
inline constexpr Option kDataBuff[] = {{0, "Video"}, {1, "FFT"}};
inline constexpr Option kDataOut[] = {
    {0, "Fbeat"}, {1, "CT33"}, {2, "CT32"}, {3, "Fklmi"}};
inline constexpr Option kStfEn[] = {{0, "Chọn STF"}, {1, "Bỏ STF"}};

inline constexpr Field kDspRFields[] = {
    unusedField("DR1"),
    radio("DSPD_Out", "DSPD Output", 0, kDspdOut, 3,
          "Kiểu dữ liệu f beat đầy vào bộ đệm truyền UDP"),
    // Giá trị trong gói là **nghịch đảo** của tần số lấy mẫu chứ không phải
    // chính nó: ADC_Sample_Rate = round(200/Fs), Fs=2 MHz đi ra thành 100. Ô
    // nhập vẫn là Fs theo MHz, và Fs cũng là con số dùng cho công thức cự ly
    // (MainWindow::applyStatusToParams) — không phải con số nằm trong gói.
    reciprocal("ADC_Sample_Rate", "ADC (MHz)", 0, 0.0, 100.0, 200.0, 3,
               "ADC sample rate (MHz) (200/value)"),
    scaled("IZP_ms", "IZP (ms)", 2000000, 0.1, 10000.0, 200000.0, 1,
           "Chu kỳ xung kích trong"),
    unusedField("DR5"),
    radio("IZP_En", "IZP enable", 0, kIzpEn, 4),
    radio("Encoder_Sim", "Giả quay", 0, kOffOn, 2),
    readOnly("Beta_Back", "Beta back", Format::Number,
             "Phương vị hiện tại trả về"),
    radio("IF_DSP", "Chọn đầu vào", 0, kIfDsp, 4,
          "Chọn tín hiệu IF đầu vào DSP trong chu kỳ"),
    combo("FIR_Scale", "FIR scale", 0, 0, 7,
          "Số lần nhân hai tín hiệu đầu ra bộ lọc FIR"),
    combo("FFTD_Scale", "FFTD scale", 0, 0, 7,
          "Số lần nhân hai tín hiệu đầu ra bộ lọc FFT trong chu kỳ"),
    spin("ZFbeat", "ZFbeat", 32768, 1, 4294967295.0,
         "Biên độ tín hiệu đầu ra FFT f beat lấy hiển thị video"),
    readOnly("HW_Version", "Phiên bản HW", Format::HwVersion),
    radio("Data_Buff", "Dữ liệu tới chuyển mạch ra", 1, kDataBuff, 2,
          "Chọn dữ liệu vào bộ đệm 1024 word 32 truyền UDP"),
    radio("Data_Out", "Chọn dữ liệu ra doppler", 0, kDataOut, 4,
          "Chọn dữ liệu ra cấp cho kênh xử lý doppler"),
    radio("FIR_Filter", "Bộ lọc FIR", 0, kOffOn, 2,
          "Bỏ/Chọn bộ lọc FIR tín hiệu sau ADC"),
    spin("Doppler_Sim", "Tần số doppler tạo giả", 0, 0, 1023,
         "Kênh f beat được tạo giả tín hiệu doppler"),
    radio("Win_Fc", "WinFc", 0, kOffOn, 2,
          "Nhân hàm cửa sổ với dữ liệu vào FFT f beat"),
    radio("IZP_Sync_Fs", "IZPSyncFs", 0, kOffOn, 2,
          "Tạo xung kích đồng bộ với tần số lấy mẫu"),
    scaled("IZP_Zn", "IZPZn (us)", 4000, 0.0, 21474836.0, 200.0, 2,
           "Giữ chậm xung kích ngoài"),
    radio("Buffer_En", "BufferEn", 0, kOffOn, 2,
          "Tắt: Dữ liệu ADC lấy mẫu đẩy trực tiếp vào bộ FFT f beat\n"
          "Bật: Chọn đệm dữ liệu vào"),
    // Không có ô nhập riêng: giá trị gửi đi lấy thẳng từ AzmOffset của
    // CMD_COMMON, còn ô trên giao diện chỉ hiện giá trị đài trả về.
    angleMirrored("FixEncoder", "Bù góc phương Bắc (độ)", GroupCommon,
                  fieldIndex(kCommonFields, "AzmOffset"),
                  "Lấy theo Bù góc phương Bắc của Tham số chung"),
    radio("STF_En", "STF", 0, kStfEn, 2,
          "Chọn STF suy giảm tín hiệu f beat vùng gần lấy ra để hiển thị"),
};

// --- CMD_DSP_S: tham số DSP kênh tốc độ -------------------------------------

inline constexpr Option kDspvOut[] = {
    {0, "MTD"}, {1, "Zin"}, {2, "Di"}, {4, "Kx"}, {8, "CFAR"}};
inline constexpr Option kFkslFilter[] = {
    {0, "Max[L R]"}, {1, "Max[0 31]"}};
inline constexpr Option kSasl[] = {
    {0, "Tắt"}, {1, "2 mẫu"}, {2, "3 mẫu"}, {3, "4 mẫu"}};

inline constexpr Field kDspSFields[] = {
    spin("P_FK_Min", "pfKmin", 3, 0, 31, "Ngưỡng doppler min"),
    spin("P_FK_Max", "pfKmax", 28, 0, 31, "Ngưỡng doppler max"),
    radio("DSPV_Out", "DataOutSL", 0, kDspvOut, 5,
          "Dữ liệu kênh xử lý doppler lấy ra truyền UDP"),
    spin("FK_X", "fKx", 17, 0, 31,
         "Kênh doppler lấy dữ liệu truyền UDP để phân tích"),
    radio("MTI_En", "MTIEn", 0, kOffOn, 2,
          "Chọn bộ lọc MTI trước khi vào MTD"),
    // Mô tả giai đoạn 11 thu dải giá trị từ 1..5 xuống còn 0,1 và đổi hẳn ý
    // nghĩa của trường. Không nêu giá trị mặc định mới, nên lấy lựa chọn đầu
    // tiên — đúng như trường này vẫn làm khi dải còn là 1..5.
    radio("FKSL_Filter", "FilterfKSL", 0, kFkslFilter, 2,
          "Chọn kiểu bộ lọc doppler"),
    spin("GainU", "GainU", 32768, 1, 4294967295.0,
         "Biên độ tín hiệu đầu ra FFT32 lấy đi truyền UDP"),
    spin("KF_Min", "fKmin", 0, 0, 31, "Ngưỡng f doppler min"),
    spin("FK_Max", "fKmax", 31, 0, 31, "Ngưỡng f doppler max"),
    unusedField("VR10"),
    combo("FFTV_Scale", "FFTV_SCALESL", 0, 0, 7,
          "Số bit dịch trái tín hiệu đầu ra FFT32"),
    spin("Fbeat_R", "Di", 10, 0, 1023,
         "Kênh f beat (cự ly) lấy tín hiệu FFT32 doppler truyền UDP"),
    scaled("U0_FK", "U0fK", 1, 0.1, 100.0, 10.0, 1,
           "Hệ số ngưỡng tạp trong cửa sổ fK doppler"),
    spin("H0_Fbeat", "H0", 1, 1, 15,
         "Ngưỡng hạng xét xung đơn trong cửa sổ Fbeat"),
    scaled("U0_W", "U0w", 4, 0.1, 100.0, 10.0, 1,
           "Hệ số ngưỡng tạp trong cửa sổ fBeat"),
    spin("A0", "A0", 15, 0, 4294967295.0, "Ngưỡng biên độ xung đơn"),
    spin("P_CS", "pCS", 10, 0, 1023,
         "Cửa sóng Fbeat (cự ly) cho phép xung đơn"),
    spin("DA", "dA", 3, 0, 4294967295.0, "Độ tương phản biên độ mẫu test"),
    radio("SASL", "SASL", 0, kSasl, 4,
          "Bộ tích phân biên độ fBeat (lấy trung bình)"),
    radio("MTK_En", "MTKEn", 0, kOffOn, 2,
          "Tắt: Bỏ nhân hàm cửa sổ MTK\nBật: Chọn nhân hàm cửa sổ MTK"),
    combo("DSI", "dSI", 0, 0, 4, "Độ cách ly mẫu test CFAR"),
};

// --- bảng bốn gói -----------------------------------------------------------

/// Mô tả một loại gói lệnh điều khiển.
struct Packet {
    const char  *label;   ///< nhãn group trên giao diện
    const char  *key;     ///< khoá lưu trong params.json
    quint32      header;
    quint32      cmdCategory;
    quint32      statusCategory;
    const Field *fields;
    int          fieldCount;

    /// Năm từ đầu + các trường + CheckSum.
    constexpr int words() const { return 5 + fieldCount + 1; }
    constexpr int size() const { return words() * 4; }
};

inline constexpr Packet kPackets[GroupCount] = {
    {"Điều khiển ăng ten",       "anten",  0xa4a3a2a1u, 0x7018u, 0x70180u,
     kAntenFields,  int(std::size(kAntenFields))},
    {"Tham số chung",            "common", 0x04030201u, 0x0018u, 0x0180u,
     kCommonFields, int(std::size(kCommonFields))},
    {"Tham số DSP kênh cự ly",   "dspr",   0xd4d3d2d1u, 0x8018u, 0x80180u,
     kDspRFields,   int(std::size(kDspRFields))},
    {"Tham số DSP kênh tốc độ",  "dsps",   0xe4e3e2e1u, 0x9018u, 0x90180u,
     kDspSFields,   int(std::size(kDspSFields))},
};

// Độ dài gói tính ra từ bảng phải khớp trường Length của mô tả giao thức. Sót
// hay thừa một trường thì bắt được ngay lúc biên dịch, chứ để tới lúc chạy là
// đài nhận một gói dài sai và không có gì báo.
static_assert(kPackets[GroupAnten].size()  == 10 * 4, "CMD_ANTEN: Length = 10*4");
static_assert(kPackets[GroupCommon].size() == 16 * 4, "CMD_COMMON: Length = 16*4");
static_assert(kPackets[GroupDspR].size()   == 29 * 4, "CMD_DSP_R: Length = 29*4");
static_assert(kPackets[GroupDspS].size()   == 27 * 4, "CMD_DSP_S: Length = 27*4");

// Trường soi giá trị gói khác phải trỏ tới một trường **có thật**: fieldIndex()
// trả -1 khi không thấy tên, và -1 thì mirrors() coi như trường thường, tức là
// lệnh sẽ lặng lẽ gửi giá trị của ô đang khoá. Chặn ngay tại đây.
static_assert(fieldIndex(kCommonFields, "AzmOffset") >= 0,
              "FixEncoder soi AzmOffset của CMD_COMMON — không còn trường đó nữa");

/// Số trường nhiều nhất của một group — đủ chỗ cho một bộ đệm tĩnh.
inline constexpr int kMaxFields = 23;
static_assert(kPackets[GroupDspR].fieldCount <= kMaxFields, "kMaxFields quá nhỏ");

// --- quy đổi giữa giá trị trên giao diện và giá trị trong gói ----------------

/// Giá trị hiện trên giao diện, suy ra từ giá trị trong gói tin.
inline double toUi(const Field &f, quint32 raw)
{
    if (f.dividend != 0.0)
        return raw == 0 ? 0.0 : f.dividend / double(raw);
    if (f.isSigned)
        return double(qint32(raw)) / f.factor;
    // Trường quay vòng: đài trả về một giá trị ngoài dải thì đưa về trong dải
    // chứ không hiện một góc lớn hơn 360 độ.
    if (f.wrap > 0)
        raw %= quint32(f.wrap);
    return double(raw) / f.factor;
}

/// Chiều ngược lại. Giá trị vào bị kẹp về dải của giao diện trước đã — ô nhập
/// đã chặn rồi, nhưng file cấu hình sửa tay thì chưa.
inline quint32 fromUi(const Field &f, double ui)
{
    ui = qBound(f.lo, ui, f.hi);
    if (f.dividend != 0.0) {
        // Số 0 đi thẳng thành số 0, không phải vô cùng: mặc định của trường là
        // 0, và đài cũng trả về 0 khi chưa đặt tần số lấy mẫu.
        if (ui <= 0.0)
            return 0;
        return quint32(qBound(0.0, std::round(f.dividend / ui), 4294967295.0));
    }
    const double raw = std::round(ui * f.factor);
    if (f.isSigned)
        return quint32(qint32(qBound(-2147483648.0, raw, 2147483647.0)));

    const quint32 out = quint32(qBound(0.0, raw, 4294967295.0));
    // 360 độ ra 4096 nấc: quay vòng về 0 để giá trị trong gói luôn nằm trong
    // dải mà mô tả giao thức nêu.
    return f.wrap > 0 ? out % quint32(f.wrap) : out;
}

/// Giá trị mặc định của cả một group, dạng gói tin.
inline void defaults(const Packet &p, quint32 *out)
{
    for (int i = 0; i < p.fieldCount; ++i)
        out[i] = p.fields[i].def;
}

/// Chỉ số của trường tên `name` trong một group; -1 nếu không có.
inline int indexOf(const Packet &p, const char *name)
{
    for (int i = 0; i < p.fieldCount; ++i) {
        if (std::strcmp(p.fields[i].name, name) == 0)
            return i;
    }
    return -1;
}

/// Giá trị lệnh điều khiển của cả bốn group, dạng gói tin.
///
/// Cỡ cố định chứ không phải mảng động: bảng trường là hằng số biết trước lúc
/// biên dịch, mà cấu trúc này được chép qua lại giữa tab điều khiển, AppParams
/// và luồng hiển thị.
struct Values {
    quint32 v[GroupCount][kMaxFields] = {};

    Values() { reset(); }

    void reset()
    {
        for (int g = 0; g < GroupCount; ++g)
            defaults(kPackets[g], v[g]);
    }

    /// Giá trị của trường tên `name`; `fallback` nếu group không có trường đó.
    quint32 byName(int group, const char *name, quint32 fallback = 0) const
    {
        if (group < 0 || group >= GroupCount)
            return fallback;
        const int i = indexOf(kPackets[group], name);
        return i < 0 ? fallback : v[group][i];
    }
};

// --- đóng gói và tách -------------------------------------------------------

inline quint32 wordAt(const char *data, int index)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data) + index * 4);
}

/// Đóng một gói lệnh điều khiển. `values` có đúng p.fieldCount phần tử.
inline QByteArray buildCommand(const Packet &p, const quint32 *values,
                               quint32 serial, quint32 timeMs)
{
    QByteArray out(p.size(), Qt::Uninitialized);
    auto *raw = reinterpret_cast<uchar *>(out.data());

    const auto put = [raw](int index, quint32 v) {
        qToLittleEndian(v, raw + index * 4);
    };

    put(0, p.header);
    put(1, p.cmdCategory);
    put(2, quint32(p.size()));
    put(3, serial);
    put(4, timeMs);
    for (int i = 0; i < p.fieldCount; ++i)
        put(5 + i, p.fields[i].sends() ? values[i] : 0u);
    put(p.words() - 1, 0);   // CheckSum — chưa dùng

    return out;
}

/// Gói này là trạng thái phản hồi của lệnh nào? nullptr nếu không phải.
///
/// Xét cả Header lẫn Category chứ không riêng cái nào: từ 11/08/2026 bốn gói đã
/// khác nhau ở cả hai (trước đó CMD_DSP_R và CMD_DSP_S dùng chung Category, chỉ
/// Header là phân biệt được). Xét cả hai thì mô tả giao thức có đổi lại kiểu ấy
/// nữa cũng không phải sửa hàm này.
inline const Packet *statusPacket(const char *data, int len)
{
    if (len < 12)
        return nullptr;
    const quint32 header   = wordAt(data, 0);
    const quint32 category = wordAt(data, 1);

    for (const Packet &p : kPackets) {
        if (p.header == header && p.statusCategory == category
            && len >= p.size())
            return &p;
    }
    return nullptr;
}

inline bool isStatus(const char *data, int len)
{
    return statusPacket(data, len) != nullptr;
}

/// Tách phần thân một gói trạng thái. `values` có đúng p.fieldCount phần tử.
inline void parseStatus(const Packet &p, const char *data, quint32 &serial,
                        quint32 &timeMs, quint32 *values)
{
    serial = wordAt(data, 3);
    timeMs = wordAt(data, 4);
    for (int i = 0; i < p.fieldCount; ++i)
        values[i] = wordAt(data, 5 + i);
}

} // namespace cmdproto
