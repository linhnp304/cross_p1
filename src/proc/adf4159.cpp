#include "proc/adf4159.h"

namespace adf4159 {

namespace {

double clampDouble(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void clampRamp(Ramp &r)
{
    r.clk2      = clampInt(r.clk2, 0, 4095);
    r.dev       = clampInt(r.dev, -32768, 32767);
    r.devOffset = clampInt(r.devOffset, 0, 15);
    r.steps     = clampInt(r.steps, 0, 1048575);
}

} // namespace

void Settings::clamp()
{
    vcoMHz   = clampDouble(vcoMHz, 0.5, 13000.0);
    refMHz   = clampDouble(refMHz, 1.0, 300.0);
    rCounter = clampInt(rCounter, 1, 32);

    prescaler = clampInt(prescaler, 0, 1);
    muxout    = clampInt(muxout, 0, 15);
    phase     = clampInt(phase, 0, 4095);
    cpCurrent = clampInt(cpCurrent, 0, 15);
    clk1      = clampInt(clk1, 0, 4095);

    negBleed = clampInt(negBleed, 0, 7);
    rampMode = clampInt(rampMode, 0, 3);

    sdMode     = clampInt(sdMode, 0, 31);
    rampStatus = clampInt(rampStatus, 0, 31);
    clkDivMode = clampInt(clkDivMode, 0, 3);

    interrupt = clampInt(interrupt, 0, 3);
    delayWord = clampInt(delayWord, 0, 4095);

    clampRamp(up);
    clampRamp(down);
}

double Settings::wantedBleedUa() const
{
    const double n = nValue();
    if (n <= 0.0)
        return 0.0;
    return 4.0 * cpCurrentMa() * 1000.0 / n;   // mA -> µA
}

int Settings::bestNegBleed() const
{
    const double want = wantedBleedUa();
    int    best    = 0;
    double bestGap = -1.0;
    for (int i = 0; i < 8; ++i) {
        const double gap = std::fabs(kNegBleedUa[i] - want);
        if (bestGap < 0.0 || gap < bestGap) {
            bestGap = gap;
            best    = i;
        }
    }
    return best;
}

double Settings::stepUs(const Ramp &r) const
{
    const double pfd = pfdMHz();
    if (pfd <= 0.0)
        return 0.0;
    // fPFD tính bằng MHz nên thương số ra thẳng µs.
    return double(clk1) * double(r.clk2) / pfd;
}

double Settings::devKHz(const Ramp &r) const
{
    // fRES = fPFD / 2^25, rồi nhân với DEV × 2^DEV_OFFSET.
    const double res = pfdMHz() * 1000.0 / kMod;
    return res * double(r.dev) * std::pow(2.0, double(r.devOffset));
}

double Settings::delayUs() const
{
    const double pfd = pfdMHz();
    if (pfd <= 0.0)
        return 0.0;
    // Nhịp giữ chậm lấy từ PFD, hoặc PFD × CLK1 khi cần khoảng chậm dài hơn.
    const double clk = delClkSel ? double(clk1) : 1.0;
    return double(delayWord) * clk / pfd;
}

} // namespace adf4159
