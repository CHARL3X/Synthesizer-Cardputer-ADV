// TPT state-variable lowpass (Zavalishin). Stable up to Nyquist — matters
// because cutoff is user-rangeable to 12 kHz at a 32 kHz rate, where a
// Chamberlin SVF would blow up. Pure C++.
#pragma once
#include <cmath>
#include <cstdint>

namespace dsp {

class Svf {
public:
    void init(float sr) {
        sr_ = sr;
        reset();
    }

    // Output tap: this is a multimode SVF — the TPT structure computes LP/BP/HP
    // simultaneously, so the four modes are free (just a different combination
    // of the same v1/v2 each sample). mode: 0=LP 1=HP 2=BP 3=notch.
    enum Mode : uint8_t { LP, HP, BP, Notch };

    // res 0..0.95; called once per block with the smoothed cutoff + the mode.
    void set(float cutoffHz, float res, uint8_t mode = LP) {
        if (cutoffHz < 40.f) cutoffHz = 40.f;
        const float ny = sr_ * 0.49f;
        if (cutoffHz > ny) cutoffHz = ny;
        const float g = tanf(3.14159265f * cutoffHz / sr_);
        k_ = 2.f - 1.9f * res;
        a1_ = 1.f / (1.f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
        mode_ = mode;
    }

    inline float process(float x) {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;  // bandpass state
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;  // lowpass state
        ic1_ = 2.f * v1 - ic1_;
        ic2_ = 2.f * v2 - ic2_;
        switch (mode_) {
            case HP:    return x - k_ * v1 - v2;
            case BP:    return v1;
            case Notch: return x - k_ * v1;  // = HP + LP
            default:    return v2;           // LP
        }
    }

    void reset() { ic1_ = ic2_ = 0.f; }

private:
    float sr_ = 32000.f;
    float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f, k_ = 1.f;
    float ic1_ = 0.f, ic2_ = 0.f;
    uint8_t mode_ = LP;
};

}  // namespace dsp
