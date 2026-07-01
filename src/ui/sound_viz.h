// The shared sound-visualization language: small drawn primitives (waveform,
// ADSR curve, filter response, gauges, LFO shapes) used by the identity card,
// the quick-edit layer and the settings screen — one hand draws all three.
#pragma once
#include <M5Cardputer.h>

#include "../dsp/params.h"

namespace viz {

// One cycle of the waveform, sampled from the real wavetable (the icon IS the
// wave). FatSaw draws two detuned traces; Pulse an asymmetric-duty square.
void drawWave(M5Canvas& c, int x, int y, int w, int h, dsp::Waveform wv, uint16_t col);

// ADSR polyline. Segment widths are time-proportional with a floor, so a 5 ms
// attack still reads as a stroke instead of vanishing.
void drawEnv(M5Canvas& c, int x, int y, int w, int h, float atkS, float decS,
             float sus, float relS, uint16_t col);

// Stylized filter response: LP/HP/BP/notch, cutoff on a log 80 Hz..12 kHz
// axis, resonance bump at the corner.
void drawFilter(M5Canvas& c, int x, int y, int w, int h, dsp::FilterMode mode,
                float cutoffHz, float res, uint16_t col);

// Fill bar (outline + fill), and the center-zero variant for bipolar amounts.
void drawGauge(M5Canvas& c, int x, int y, int w, int h, float fill01, uint16_t col);
void drawBipolar(M5Canvas& c, int x, int y, int w, int h, float val, uint16_t col);

// Two cycles of the LFO shape (fixed pseudo-random steps for S&H).
void drawLfoIcon(M5Canvas& c, int x, int y, int w, int h, dsp::LfoShape shape, uint16_t col);

// Mini filter-mode glyph (the response curve shrunk to icon size).
inline void drawFilterIcon(M5Canvas& c, int x, int y, int w, int h, dsp::FilterMode m,
                           uint16_t col) {
    drawFilter(c, x, y, w, h, m, 1000.f, 0.5f, col);
}

}  // namespace viz
