// Shared wavetables: 1024-sample single-cycle tables with a +1 guard sample
// for branch-free linear interpolation. Saw/square get a 2-mip band-limit:
// a full-harmonic table for low fundamentals and a reduced-harmonic table
// above the split. Residual aliasing above that is masked by the tiny
// speaker's rolloff — accepted as character, not fought. Pure C++.
#pragma once
#include "params.h"

namespace dsp {

constexpr int kTableSize = 1024;            // power of two
constexpr int kTableMask = kTableSize - 1;
constexpr float kMipSplitHz = 800.f;        // low/high table boundary

enum TableId : uint8_t { TblSine, TblTri, TblSawLo, TblSawHi, TblSqrLo, TblSqrHi, TblCount };

extern float gTables[TblCount][kTableSize + 1];

void initWavetables();  // call once before rendering

// Table for a waveform at a given fundamental (FatSaw and Pulse read the saw
// tables — Pulse is the difference of two saw reads, so it stays band-limited).
inline const float* tableFor(Waveform w, float freq) {
    switch (w) {
        case Waveform::Sine:     return gTables[TblSine];
        case Waveform::Triangle: return gTables[TblTri];
        case Waveform::Square:   return gTables[freq < kMipSplitHz ? TblSqrLo : TblSqrHi];
        default:                 return gTables[freq < kMipSplitHz ? TblSawLo : TblSawHi];
    }
}

// Branch-free linear-interpolated read from a 32-bit phase accumulator.
inline float tableRead(const float* tbl, uint32_t phase) {
    const uint32_t idx = phase >> 22;                          // top 10 bits
    const float frac = (float)((phase >> 6) & 0xFFFF) * (1.f / 65536.f);
    const float a = tbl[idx];
    return a + (tbl[idx + 1] - a) * frac;
}

}  // namespace dsp
