#include "sound_viz.h"

#include <cmath>

#include "../dsp/wavetables.h"
#include "theme.h"

namespace viz {

namespace {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Polyline helper: y values in 0..1 (0 = bottom), mapped into the rect.
inline int yMap(int y, int h, float v01) {
    return y + h - 1 - (int)(clamp01(v01) * (h - 1) + 0.5f);
}

// Sample a wavetable at phase 0..1 (uses the low-mip table — the icon shows
// the full-harmonic character, ripple and all).
inline float tblAt(const float* t, float ph) {
    ph -= floorf(ph);
    return t[(int)(ph * dsp::kTableSize) & dsp::kTableMask];
}

// One trace of `fn(phase)->-1..1` across the rect.
template <typename F>
void trace(M5Canvas& c, int x, int y, int w, int h, uint16_t col, F fn) {
    int py = 0;
    for (int i = 0; i < w; ++i) {
        const float v = fn((float)i / (float)(w - 1));
        const int yy = yMap(y, h, v * 0.5f + 0.5f);
        if (i > 0) c.drawLine(x + i - 1, py, x + i, yy, col);
        py = yy;
    }
}

}  // namespace

void drawWave(M5Canvas& c, int x, int y, int w, int h, dsp::Waveform wv, uint16_t col) {
    c.drawFastHLine(x, y + h / 2, w, theme::kLine);  // zero axis
    switch (wv) {
        case dsp::Waveform::Pulse:  // stylized 30% duty — the PWM identity
            trace(c, x, y, w, h, col, [](float ph) { return ph < 0.3f ? 0.9f : -0.9f; });
            break;
        case dsp::Waveform::FatSaw: {  // two detuned saws: the second, dimmer
            const float* t = dsp::gTables[dsp::TblSawLo];
            trace(c, x, y, w, h, theme::scale(col, 110),
                  [t](float ph) { return tblAt(t, ph * 1.08f + 0.06f) * 0.9f; });
            trace(c, x, y, w, h, col, [t](float ph) { return tblAt(t, ph) * 0.9f; });
            break;
        }
        default: {
            const float* t = dsp::tableFor(wv, 100.f);  // low mip: full character
            trace(c, x, y, w, h, col, [t](float ph) { return tblAt(t, ph) * 0.9f; });
            break;
        }
    }
}

void drawEnv(M5Canvas& c, int x, int y, int w, int h, float atkS, float decS,
             float sus, float relS, uint16_t col) {
    // sqrt-compress the times so short stages stay visible next to long ones;
    // the sustain plateau gets a fixed quarter of the width.
    const float a = sqrtf(atkS), d = sqrtf(decS), r = sqrtf(relS);
    const float tSum = a + d + r;
    const int plateau = w / 4;
    const int avail = w - plateau - 3;  // 3 = minimum 1px per stage
    auto seg = [&](float t) {
        const int px = tSum > 0.f ? (int)(avail * t / tSum) : avail / 3;
        return px < 1 ? 1 : px;
    };
    const int wA = seg(a), wD = seg(d), wR = seg(r);
    const int yBase = y + h - 1, yPeak = y, ySus = yMap(y, h, clamp01(sus));
    int xx = x;
    c.drawFastHLine(x, yBase, w, theme::kLine);  // baseline
    c.drawLine(xx, yBase, xx + wA, yPeak, col);                 // attack
    xx += wA;
    c.drawLine(xx, yPeak, xx + wD, ySus, col);                  // decay
    xx += wD;
    c.drawFastHLine(xx, ySus, plateau, col);                    // sustain
    xx += plateau;
    c.drawLine(xx, ySus, xx + wR, yBase, col);                  // release
}

void drawFilter(M5Canvas& c, int x, int y, int w, int h, dsp::FilterMode mode,
                float cutoffHz, float res, uint16_t col) {
    // log-frequency axis, 80 Hz .. 12 kHz; the response is drawn as a per-
    // column level curve — smooth enough at icon size, cheap everywhere.
    const float fLo = logf(80.f), fHi = logf(12000.f);
    float cut = (logf(cutoffHz < 80.f ? 80.f : (cutoffHz > 12000.f ? 12000.f : cutoffHz)) - fLo) /
                (fHi - fLo);
    const float bump = clamp01(res) * 0.45f;   // resonance peak height
    const float slope = 3.5f;                  // rolloff steepness (per unit axis)
    c.drawFastHLine(x, y + h - 1, w, theme::kLine);
    int py = 0;
    for (int i = 0; i < w; ++i) {
        const float p = (float)i / (float)(w - 1);
        const float dp = p - cut;
        float lvl;
        switch (mode) {
            case dsp::FilterMode::HP:
                lvl = dp > 0.f ? 0.75f : 0.75f + dp * slope;
                break;
            case dsp::FilterMode::BP:
                lvl = 0.85f - fabsf(dp) * slope * 0.8f;
                break;
            case dsp::FilterMode::Notch:
                lvl = 0.75f - (fabsf(dp) < 0.16f ? (0.16f - fabsf(dp)) * 5.f : 0.f);
                break;
            default:  // LP
                lvl = dp < 0.f ? 0.75f : 0.75f - dp * slope;
                break;
        }
        // resonance: a bump centred on the corner (LP/HP/BP; notch stays clean)
        if (mode != dsp::FilterMode::Notch)
            lvl += bump * expf(-dp * dp * 90.f);
        const int yy = yMap(y, h, clamp01(lvl));
        if (i > 0) c.drawLine(x + i - 1, py, x + i, yy, col);
        py = yy;
    }
}

void drawGauge(M5Canvas& c, int x, int y, int w, int h, float fill01, uint16_t col) {
    c.drawRect(x, y, w, h, theme::kLine);
    const int fw = (int)((w - 2) * clamp01(fill01) + 0.5f);
    if (fw > 0) c.fillRect(x + 1, y + 1, fw, h - 2, col);
}

void drawBipolar(M5Canvas& c, int x, int y, int w, int h, float val, uint16_t col) {
    c.drawRect(x, y, w, h, theme::kLine);
    const int mid = x + w / 2;
    const float v = val < -1.f ? -1.f : (val > 1.f ? 1.f : val);
    const int fw = (int)((w / 2 - 1) * fabsf(v) + 0.5f);
    if (fw > 0) {
        if (v >= 0.f) c.fillRect(mid, y + 1, fw, h - 2, col);
        else          c.fillRect(mid - fw, y + 1, fw, h - 2, col);
    }
    c.drawFastVLine(mid, y - 1, h + 2, theme::kDim);  // the zero tick
}

void drawLfoIcon(M5Canvas& c, int x, int y, int w, int h, dsp::LfoShape shape, uint16_t col) {
    if (shape == dsp::LfoShape::SH) {  // fixed pseudo-random step pattern
        static const float kSteps[8] = {0.7f, -0.4f, 0.2f, -0.9f, 0.5f, -0.1f, 0.9f, -0.6f};
        int py = 0;
        for (int i = 0; i < 8; ++i) {
            const int x0 = x + i * w / 8, x1 = x + (i + 1) * w / 8;
            const int yy = yMap(y, h, kSteps[i] * 0.5f + 0.5f);
            c.drawFastHLine(x0, yy, x1 - x0, col);
            if (i > 0) c.drawFastVLine(x0, yy < py ? yy : py, (yy < py ? py - yy : yy - py) + 1, col);
            py = yy;
        }
        return;
    }
    trace(c, x, y, w, h, col, [shape](float ph) {
        const float p2 = ph * 2.f - floorf(ph * 2.f);  // two cycles
        switch (shape) {
            case dsp::LfoShape::Tri:    return p2 < 0.5f ? p2 * 4.f - 1.f : 3.f - p2 * 4.f;
            case dsp::LfoShape::Saw:    return p2 * 2.f - 1.f;
            case dsp::LfoShape::Square: return p2 < 0.5f ? 0.9f : -0.9f;
            default:                    return sinf(p2 * 6.2831853f);
        }
    });
}

}  // namespace viz
