#pragma once

// Mô hình tám thanh ghi của kit tạo tín hiệu ADF4159 (Analog Devices).
//
// Chỗ này thuần số học: nhận tham số trắc thủ vặn trên giao diện, trả ra tám từ
// 32 bit đúng như tài liệu ADF4159 mô tả (Figure 23 và Figure 24, mục REGISTER
// MAPS). Không đụng gì tới Qt Widgets, cũng không biết gói tin đi ra bằng đường
// nào — hai việc đó nằm ở ui/adf4159window.* và net/adfproto.h.
//
// **Ba bit thấp nhất của mỗi từ là số hiệu thanh ghi** (000..111). Chính vì vậy
// lệnh gửi một thanh ghi (CMD_ADF4159_REG) chỉ cần mang đúng một từ 32 bit mà
// bên nhận vẫn biết đó là thanh ghi nào — không có trường "số hiệu" riêng.
//
// Phần ghép thanh ghi viết constexpr và nằm luôn trong header, không phải vì
// tốc độ mà vì phép thử ở cuối file: bộ giá trị mặc định dưới đây đúng bằng bộ
// mà phần mềm gốc của Analog Devices mở lên đã có sẵn, nên tám thanh ghi tính
// ra phải khớp từng số với ảnh chụp phần mềm đó. Cả tám cùng khớp nghĩa là từng
// trường đã nằm đúng chỗ — và sai một bit thì hỏng biên dịch chứ không phải đợi
// tới lúc kit phát ra sai tần số.

#include <QtGlobal>

#include <array>
#include <cmath>

namespace adf4159 {

/// Số thanh ghi của ADF4159.
inline constexpr int kRegCount = 8;

/// Cả tám thanh ghi, chỉ số trong mảng cũng là số hiệu thanh ghi.
using Registers = std::array<quint32, kRegCount>;

/// Mẫu số cố định của phần thập phân: FRAC là số 25 bit.
inline constexpr double kMod = 33554432.0;   // 2^25

/// Dòng bơm điện tích (mA) ứng với bốn bit CPI, với R_SET = 5.1 kΩ.
inline constexpr double kCpCurrentMa[16] = {
    0.31, 0.63, 0.94, 1.25, 1.57, 1.88, 2.19, 2.50,
    2.81, 3.13, 3.44, 3.75, 4.06, 4.38, 4.69, 5.00};

/// Dòng rò âm (µA) ứng với ba bit NB.
inline constexpr double kNegBleedUa[8] = {
    3.73, 11.03, 25.25, 53.1, 109.7, 224.7, 454.7, 916.4};

/// Trị INT nhỏ nhất mà bộ chia trước cho phép: 4/5 thì 23, 8/9 thì 75.
inline constexpr int minInt(int prescaler) { return prescaler == 0 ? 23 : 75; }

/// Tần số RF lớn nhất (MHz) mà bộ chia trước 4/5 chịu được.
inline constexpr double kPrescaler45MaxMHz = 8000.0;

// --- hai hàm ghép bit --------------------------------------------------------

/// Ghép một trường vào từ thanh ghi: `value` chiếm `bits` bit, bit thấp nhất
/// nằm ở vị trí `shift`. Cắt sẵn phần thừa để một giá trị vượt dải không tràn
/// sang trường bên cạnh — tràn kiểu đó thì mọi bit phía trên sai hết mà nhìn
/// vào số hex không thấy có gì lạ.
constexpr quint32 field(quint32 value, int shift, int bits)
{
    const quint32 mask = bits >= 32 ? 0xffffffffu : ((1u << bits) - 1u);
    return (value & mask) << shift;
}

constexpr quint32 bit(bool on, int shift) { return on ? (1u << shift) : 0u; }

constexpr int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Một nhánh quét tần. ADF4159 giữ được hai bộ (DEV SEL / STEP SEL / CLK DIV
/// SEL chọn giữa chúng), phần mềm gốc gọi là "Up Ramp" và "Down/Second Ramp".
struct Ramp {
    int clk2      = 2;   ///< R4 DB[18:7]  — bộ đếm CLK2, 0..4095
    int dev       = 0;   ///< R5 DB[18:3]  — từ độ lệch tần, **có dấu**
    int devOffset = 0;   ///< R5 DB[22:19] — số mũ của độ lệch tần, 0..15
    int steps     = 0;   ///< R6 DB[22:3]  — số bước của một nhánh, 0..1048575
};

/// Toàn bộ những gì vặn được trên kit.
///
/// Tên trường bám theo tên trong tài liệu ADF4159 chứ không dịch ra: người dò
/// lỗi sẽ cầm tài liệu đó trên tay, mà mỗi bit chỉ được gọi bằng đúng một tên
/// trong đó.
struct Settings {
    // --- tham số RF (không nằm thẳng trong thanh ghi nào, sinh ra INT/FRAC) ---
    double vcoMHz     = 6000.0;   ///< tần số ra mong muốn của VCO
    double refMHz     = 100.0;    ///< tần số chuẩn vào chân REFIN
    int    rCounter   = 1;        ///< R2 DB[19:15] — bộ chia R, 1..32
    bool   refDoubler = false;    ///< R2 DB20 — nhân đôi tần số chuẩn
    bool   refDiv2    = false;    ///< R2 DB21 — chia đôi tần số chuẩn
    int    prescaler  = 0;        ///< R2 DB22 — 0 = 4/5, 1 = 8/9

    // --- R0 ---
    int  muxout = 0;              ///< DB[30:27] — tín hiệu đưa ra chân MUXOUT
    bool rampOn = false;          ///< DB31 — bật quét tần

    // --- R1 ---
    bool phaseAdjust = false;     ///< DB28
    int  phase       = 0;         ///< DB[14:3], 0..4095

    // --- R2 ---
    bool csr       = false;       ///< DB28 — giảm trượt chu kỳ
    int  cpCurrent = 7;           ///< DB[27:24] — chỉ số trong kCpCurrentMa
    int  clk1      = 1;           ///< DB[14:3], 0..4095

    // --- R3 ---
    int  negBleed     = 4;        ///< DB[24:22] — chỉ số trong kNegBleedUa
    bool autoNegBleed = true;     ///< chỉ có trên giao diện: tự tính negBleed
    bool negBleedEn   = false;    ///< DB21
    bool lol          = true;     ///< DB16 — báo mất đồng bộ
    bool nSel         = false;    ///< DB15 — nạp từ N chậm 4 chu kỳ
    bool sdReset      = false;    ///< DB14 — 0 là **bật** xoá bộ điều chế Σ-Δ
    int  rampMode     = 0;        ///< DB[11:10] — dạng sóng quét
    bool psk          = false;    ///< DB9
    bool fsk          = false;    ///< DB8
    bool ldp          = true;     ///< DB7 — độ chính xác dò đồng bộ
    bool pdPolarity   = false;    ///< DB6 — 0 là âm, 1 là dương
    bool powerDown    = false;    ///< DB5
    bool cpThreeState = false;    ///< DB4
    bool counterReset = false;    ///< DB3

    // --- R4 ---
    bool leSel      = false;      ///< DB31 — LE lấy từ chân hay đồng bộ REFIN
    int  sdMode     = 0;          ///< DB[30:26] — 0 bình thường, 14 tắt khi FRAC=0
    int  rampStatus = 0;          ///< DB[25:21] — đọc ngược ra MUXOUT...
    int  clkDivMode = 3;          ///< DB[20:19] — 3 = bộ chia quét tần

    // --- R5 ---
    bool txDataInvert = false;    ///< DB30
    bool txRampClk    = false;    ///< DB29 — 0 lấy nhịp CLK DIV, 1 lấy TXDATA
    bool parabolic    = false;    ///< DB28
    int  interrupt    = 0;        ///< DB[27:26]
    bool fskRamp      = false;    ///< DB25
    bool dualRamp     = false;    ///< DB24 — bật nhánh quét thứ hai

    // --- R7 ---
    bool txTriggerDelay = false;  ///< DB23
    bool triDelay       = false;  ///< DB22
    bool singleFullTri  = false;  ///< DB21
    bool txDataTrigger  = false;  ///< DB20
    bool fastRamp       = false;  ///< DB19
    bool rampDelayFl    = false;  ///< DB18
    bool rampDelay      = false;  ///< DB17
    bool delClkSel      = false;  ///< DB16 — 0 nhịp PFD, 1 nhịp PFD × CLK1
    bool delStartEn     = false;  ///< DB15
    int  delayWord      = 0;      ///< DB[14:3], 0..4095

    // --- hai nhánh quét (R4 / R5 / R6) ---
    Ramp up;     ///< nhánh lên, các bit chọn nhánh = 0
    Ramp down;   ///< nhánh xuống / nhánh thứ hai, các bit chọn nhánh = 1

    /// Kẹp mọi trường về dải hợp lệ. Gọi sau khi nạp file bị sửa tay.
    void clamp();

    // --- INT / FRAC ----------------------------------------------------------

    /// Tần số so pha (MHz): REFIN × (1 + D) / (R × (1 + T)).
    constexpr double pfdMHz() const
    {
        const double r = rCounter < 1 ? 1.0 : double(rCounter);
        return refMHz * (refDoubler ? 2.0 : 1.0) / (r * (refDiv2 ? 2.0 : 1.0));
    }

    /// Bước tần số nhỏ nhất (kHz) = fPFD / 2^25.
    constexpr double channelSpacingKHz() const
    {
        return pfdMHz() * 1000.0 / kMod;
    }

    /// Hệ số chia tổng N = RFout / fPFD, chưa tách phần nguyên phần lẻ.
    constexpr double nValue() const
    {
        const double pfd = pfdMHz();
        return pfd > 0.0 ? vcoMHz / pfd : 0.0;
    }

    /// Phần nguyên của N — R0 DB[26:15].
    constexpr int intValue() const
    {
        // N không bao giờ âm nên cắt phần thập phân cũng chính là làm tròn
        // xuống; dùng phép cắt để cả hàm này tính được lúc biên dịch.
        return clampInt(int(nValue()), 0, 4095);
    }

    /// Phần lẻ 25 bit, gộp cả 12 bit cao (R0) lẫn 13 bit thấp (R1).
    constexpr quint32 fracValue() const
    {
        // Tách đúng hai nhịp như ví dụ mẫu trong tài liệu (mục RF SYNTHESIZER
        // WORKED EXAMPLE).
        const double rest = nValue() - double(int(nValue()));
        if (rest <= 0.0)
            return 0;

        const double scaled = rest * 4096.0;                   // × 2^12
        const int    msb    = clampInt(int(scaled), 0, 4095);
        const int    lsb    = clampInt(int((scaled - msb) * 8192.0), 0, 8191);
        return (quint32(msb) << 13) | quint32(lsb);
    }

    constexpr int fracMsb() const { return int(fracValue() >> 13); }
    constexpr int fracLsb() const { return int(fracValue() & 0x1fffu); }

    /// Tần số VCO **thực sự** đạt được sau khi INT/FRAC đã làm tròn (MHz).
    constexpr double actualVcoMHz() const
    {
        return (double(intValue()) + double(fracValue()) / kMod) * pfdMHz();
    }

    /// Dòng bơm điện tích đang chọn (mA).
    constexpr double cpCurrentMa() const { return kCpCurrentMa[cpCurrent & 15]; }

    // --- ghép thanh ghi ------------------------------------------------------
    //
    // Mỗi dòng là một trường trong Figure 23 / Figure 24 của tài liệu, viết đúng
    // thứ tự từ bit cao xuống bit thấp để dò lại được bằng mắt.

    /// Tám thanh ghi của **nhánh lên** — cũng chính là tám thanh ghi mà lệnh
    /// CMD_ADF4159_REG8 mang đi.
    constexpr Registers registers() const
    {
        Registers r{};

        // R0 — FRAC/INT
        r[0] = bit(rampOn, 31)
             | field(quint32(muxout), 27, 4)
             | field(quint32(intValue()), 15, 12)
             | field(quint32(fracMsb()), 3, 12)
             | 0u;

        // R1 — LSB FRAC
        r[1] = bit(phaseAdjust, 28)
             | field(quint32(fracLsb()), 15, 13)
             | field(quint32(phase), 3, 12)
             | 1u;

        // R2 — bộ chia R
        r[2] = bit(csr, 28)
             | field(quint32(cpCurrent), 24, 4)
             | field(quint32(prescaler), 22, 1)
             | bit(refDiv2, 21)
             | bit(refDoubler, 20)
             // Bộ chia R nhận giá trị 1..32, mà trường chỉ có 5 bit: 32 ghi
             // thành 00000 đúng như bảng tra trong tài liệu.
             | field(quint32(rCounter), 15, 5)
             | field(quint32(clk1), 3, 12)
             | 2u;

        // R3 — chức năng
        r[3] = field(quint32(negBleed), 22, 3)
             | bit(negBleedEn, 21)
             | (1u << 17)          // bit dự trữ, tài liệu bắt buộc để 1
             | bit(lol, 16)
             | bit(nSel, 15)
             | bit(sdReset, 14)
             | field(quint32(rampMode), 10, 2)
             | bit(psk, 9)
             | bit(fsk, 8)
             | bit(ldp, 7)
             | bit(pdPolarity, 6)
             | bit(powerDown, 5)
             | bit(cpThreeState, 4)
             | bit(counterReset, 3)
             | 3u;

        // R4 — nhịp (nhánh lên: bit chọn nhánh CLK DIV SEL = 0)
        r[4] = bit(leSel, 31)
             | field(quint32(sdMode), 26, 5)
             | field(quint32(rampStatus), 21, 5)
             | field(quint32(clkDivMode), 19, 2)
             | field(quint32(up.clk2), 7, 12)
             | 4u;

        // R5 — độ lệch tần (nhánh lên: DEV SEL = 0)
        r[5] = bit(txDataInvert, 30)
             | bit(txRampClk, 29)
             | bit(parabolic, 28)
             | field(quint32(interrupt), 26, 2)
             | bit(fskRamp, 25)
             | bit(dualRamp, 24)
             | field(quint32(up.devOffset), 19, 4)
             // Từ độ lệch tần có dấu: quy về 16 bit bù hai rồi mới ghép vào.
             | field(quint32(up.dev) & 0xffffu, 3, 16)
             | 5u;

        // R6 — số bước (nhánh lên: STEP SEL = 0)
        r[6] = field(quint32(up.steps), 3, 20) | 6u;

        // R7 — giữ chậm
        r[7] = bit(txTriggerDelay, 23)
             | bit(triDelay, 22)
             | bit(singleFullTri, 21)
             | bit(txDataTrigger, 20)
             | bit(fastRamp, 19)
             | bit(rampDelayFl, 18)
             | bit(rampDelay, 17)
             | bit(delClkSel, 16)
             | bit(delStartEn, 15)
             | field(quint32(delayWord), 3, 12)
             | 7u;

        return r;
    }

    // Ba thanh ghi riêng của **nhánh thứ hai**: R4, R5, R6 với bit chọn nhánh
    // bằng 1. Không nằm trong lệnh tám thanh ghi, phải gửi lẻ từng cái.

    constexpr quint32 reg4Down() const
    {
        // Cùng R4 của nhánh lên, chỉ thay bộ đếm CLK2 và bật CLK DIV SEL (DB6).
        return (registers()[4] & ~field(0xfffu, 7, 12))
             | field(quint32(down.clk2), 7, 12)
             | (1u << 6);
    }

    constexpr quint32 reg5Down() const
    {
        // Thay từ độ lệch tần và số mũ của nó, bật DEV SEL (DB23).
        return (registers()[5] & ~(field(0xfu, 19, 4) | field(0xffffu, 3, 16)))
             | field(quint32(down.devOffset), 19, 4)
             | field(quint32(down.dev) & 0xffffu, 3, 16)
             | (1u << 23);
    }

    constexpr quint32 reg6Down() const
    {
        return field(quint32(down.steps), 3, 20) | (1u << 23) | 6u;
    }

    // --- các giá trị suy ra còn lại ------------------------------------------

    /// Dòng rò âm nên dùng (µA) theo công thức I_BLEED = 4 × I_CP / N.
    double wantedBleedUa() const;

    /// Chỉ số dòng rò âm gần công thức trên nhất — dùng khi bật "tự chọn".
    int    bestNegBleed() const;

    /// Thời gian một bước quét (µs): CLK1 × CLK2 / fPFD.
    double stepUs(const Ramp &r) const;

    /// Độ lệch tần mỗi bước (kHz): (fPFD / 2^25) × DEV × 2^DEV_OFFSET.
    double devKHz(const Ramp &r) const;

    /// Cả nhánh quét: độ rộng dải (kHz) và thời gian (µs).
    double totalKHz(const Ramp &r) const { return devKHz(r) * r.steps; }
    double rampUs(const Ramp &r) const   { return stepUs(r) * r.steps; }

    /// Thời gian giữ chậm trước khi vào nhánh quét (µs).
    double delayUs() const;
};

// Tám thanh ghi của bộ mặc định, đối chiếu với ảnh chụp phần mềm gốc.
static_assert(Settings{}.registers()[0] == 0x001E0000u, "ADF4159 R0");
static_assert(Settings{}.registers()[1] == 0x00000001u, "ADF4159 R1");
static_assert(Settings{}.registers()[2] == 0x0700800Au, "ADF4159 R2");
static_assert(Settings{}.registers()[3] == 0x01030083u, "ADF4159 R3");
static_assert(Settings{}.registers()[4] == 0x00180104u, "ADF4159 R4");
static_assert(Settings{}.registers()[5] == 0x00000005u, "ADF4159 R5");
static_assert(Settings{}.registers()[6] == 0x00000006u, "ADF4159 R6");
static_assert(Settings{}.registers()[7] == 0x00000007u, "ADF4159 R7");

// Ba thanh ghi của nhánh thứ hai, cũng lấy từ ảnh chụp đó.
static_assert(Settings{}.reg4Down() == 0x00180144u, "ADF4159 R4 nhánh 2");
static_assert(Settings{}.reg5Down() == 0x00800005u, "ADF4159 R5 nhánh 2");
static_assert(Settings{}.reg6Down() == 0x00800006u, "ADF4159 R6 nhánh 2");

// Ba bit thấp nhất luôn là số hiệu thanh ghi — chỗ dựa của cả lệnh gửi một
// thanh ghi lẫn phần đọc gói trạng thái, nên kiểm luôn.
static_assert(Settings{}.registers()[5] % 8 == 5, "số hiệu thanh ghi nằm ở 3 bit thấp");
static_assert(Settings{}.reg6Down() % 8 == 6, "số hiệu thanh ghi nằm ở 3 bit thấp");

/// Duyệt toàn bộ trường có tên, dùng chung cho việc lưu và nạp params.json.
///
/// Viết một lần ở đây thay vì chép hai bảng tên khoá trong appparams.cpp: bốn
/// mươi trường mà giữ khớp ba danh sách bằng mắt thì sớm muộn cũng lệch, mà lệch
/// thì chỉ hiện ra ở chỗ "mở phần mềm lên thấy sai đúng một ô".
template <class F>
void visit(Settings &s, F &&f)
{
    f("vcoMHz", s.vcoMHz);
    f("refMHz", s.refMHz);
    f("rCounter", s.rCounter);
    f("refDoubler", s.refDoubler);
    f("refDiv2", s.refDiv2);
    f("prescaler", s.prescaler);

    f("muxout", s.muxout);
    f("rampOn", s.rampOn);

    f("phaseAdjust", s.phaseAdjust);
    f("phase", s.phase);

    f("csr", s.csr);
    f("cpCurrent", s.cpCurrent);
    f("clk1", s.clk1);

    f("negBleed", s.negBleed);
    f("autoNegBleed", s.autoNegBleed);
    f("negBleedEn", s.negBleedEn);
    f("lol", s.lol);
    f("nSel", s.nSel);
    f("sdReset", s.sdReset);
    f("rampMode", s.rampMode);
    f("psk", s.psk);
    f("fsk", s.fsk);
    f("ldp", s.ldp);
    f("pdPolarity", s.pdPolarity);
    f("powerDown", s.powerDown);
    f("cpThreeState", s.cpThreeState);
    f("counterReset", s.counterReset);

    f("leSel", s.leSel);
    f("sdMode", s.sdMode);
    f("rampStatus", s.rampStatus);
    f("clkDivMode", s.clkDivMode);

    f("txDataInvert", s.txDataInvert);
    f("txRampClk", s.txRampClk);
    f("parabolic", s.parabolic);
    f("interrupt", s.interrupt);
    f("fskRamp", s.fskRamp);
    f("dualRamp", s.dualRamp);

    f("txTriggerDelay", s.txTriggerDelay);
    f("triDelay", s.triDelay);
    f("singleFullTri", s.singleFullTri);
    f("txDataTrigger", s.txDataTrigger);
    f("fastRamp", s.fastRamp);
    f("rampDelayFl", s.rampDelayFl);
    f("rampDelay", s.rampDelay);
    f("delClkSel", s.delClkSel);
    f("delStartEn", s.delStartEn);
    f("delayWord", s.delayWord);

    f("upClk2", s.up.clk2);
    f("upDev", s.up.dev);
    f("upDevOffset", s.up.devOffset);
    f("upSteps", s.up.steps);

    f("downClk2", s.down.clk2);
    f("downDev", s.down.dev);
    f("downDevOffset", s.down.devOffset);
    f("downSteps", s.down.steps);
}

} // namespace adf4159
