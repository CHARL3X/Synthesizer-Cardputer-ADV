// Grid -> pitch mapping: the isomorphic layout, degree mapping, key/octave
// math, and note-name helpers. Pure C++.
#pragma once
#include <cstdint>
#include "scales.h"

namespace dsp {

// The musical layout of the 4x10 note grid. Rows are "strings" (string 0 =
// bottom = lowest), columns step right. With scaleLock on, every key is the
// NEXT SCALE DEGREE (degree mapping: zero dead keys, sliding a chord shape
// is a diatonic transposition — "you can't really hit a wrong note").
// With lock off (or shift held = momentary chromatic) columns are semitones.
struct Layout {
    uint8_t rootSemis = 9;        // A
    uint8_t scaleIdx = kDefaultScale;
    int8_t  octave = 3;           // base octave of string 0, col 0 (a touch low
                                  // so there's room to solo up over a backing)
    uint8_t rowIntervalSemis = 5; // string-to-string interval (a fourth)
    bool    scaleLock = true;
};

constexpr int kGridStrings = 4;
constexpr int kGridCols = 10;

// Row offset in *scale degrees*, derived from the semitone row interval so
// "a fourth between strings" survives the degree mapping for any scale size
// (pent: 5*5/12 -> 2 degrees = A->D; major: 5*7/12 -> 3 degrees = perfect 4th).
inline int rowDegrees(const Layout& l) {
    const int len = kScales[l.scaleIdx].len;
    int d = (l.rowIntervalSemis * len + 6) / 12;  // rounded
    return d < 1 ? 1 : d;
}

// Fractional MIDI note for a grid position. chromaticOverride = shift held.
inline float gridToMidi(const Layout& l, int string, int col, bool chromaticOverride) {
    const float base = 12.f * (l.octave + 1) + l.rootSemis;  // A oct4 -> 69 (A440)
    if (l.scaleLock && !chromaticOverride) {
        const Scale& sc = kScales[l.scaleIdx];
        const int deg = string * rowDegrees(l) + col;
        const int oct = deg / sc.len;
        const int idx = deg % sc.len;
        return base + 12.f * oct + sc.steps[idx];
    }
    return base + string * l.rowIntervalSemis + col;
}

// True if the chromatic pitch at (string, col) lands on a scale tone — used
// by the UI to highlight in-scale keys when lock is OFF.
inline bool chromaticInScale(const Layout& l, int string, int col) {
    const Scale& sc = kScales[l.scaleIdx];
    const int semis = (string * l.rowIntervalSemis + col) % 12;
    for (int i = 0; i < sc.len; ++i)
        if (sc.steps[i] == semis) return true;
    return false;
}

// A diatonic chord rooted at a grid position — the backing for the auto
// progression. With scale lock it stacks thirds into a real major/minor/dim
// triad that is ALWAYS in key, the same "you can't hit a wrong note" guarantee
// the melody gets — but the triad is built from the scale's HARMONY PARENT, not
// its own degrees. For 7-note scales the parent is the scale itself, so diatonic
// progressions are unchanged. For pentatonic/blues the parent is the diatonic
// scale they're carved from, so the backing stays consonant minor/major triads
// while the solo keeps the literal scale: the blues ♭5 "blue note" stays a
// melodic color over the chords and never lands as a chord tone (it snaps onto
// the nearest harmony tone if you tap it as a step). Chromatic (lock off) falls
// back to a power voicing (root, fifth, octave). Voiced one octave under the
// grid pitch, like the drones — a low pad to solo over. Writes up to maxOut
// fractional MIDI notes (root first) and returns the count.
inline int chordPitches(const Layout& l, int string, int col, bool chromatic,
                        float* out, int maxOut) {
    if (maxOut <= 0) return 0;
    const float base = 12.f * (l.octave + 1) + l.rootSemis - 12.f;  // -1 oct: bass pad
    int n = 0;
    if (l.scaleLock && !chromatic) {
        const Scale& sc = kScales[l.scaleIdx];
        const Scale& hsc = kScales[sc.harm];      // 7-note triad source
        const int deg0 = string * rowDegrees(l) + col;
        const float octBase = base + 12.f * (deg0 / sc.len);  // tonic at this octave
        const int pc = sc.steps[deg0 % sc.len];   // tapped tone, semitones over tonic
        // Snap that tapped pitch class onto the nearest harmony-scale degree, so
        // non-parent tones (the blue note) become a consonant chord root.
        int hd = 0, bestDiff = 99;
        for (int i = 0; i < hsc.len; ++i) {
            const int diff = pc > hsc.steps[i] ? pc - hsc.steps[i] : hsc.steps[i] - pc;
            if (diff < bestDiff) { bestDiff = diff; hd = i; }
        }
        const int thirds[3] = {0, 2, 4};  // stacked diatonic thirds = a triad
        for (int k = 0; k < 3 && n < maxOut; ++k) {
            const int deg = hd + thirds[k];
            out[n++] = octBase + 12.f * (deg / hsc.len) + hsc.steps[deg % hsc.len];
        }
    } else {
        const float root = base + string * l.rowIntervalSemis + col;
        const float voicing[3] = {0.f, 7.f, 12.f};  // power: root, fifth, octave
        for (int k = 0; k < 3 && n < maxOut; ++k) out[n++] = root + voicing[k];
    }
    return n;
}

// Pitch-class name (no octave) of a fractional MIDI note — the progression
// step labels. Integer-rounds without <cmath> (kept out of this hot header).
inline const char* pitchClassName(float midi) {
    const int m = (int)(midi + 0.5f);
    return kNoteNames[((m % 12) + 12) % 12];
}

float midiToFreq(float midi);

// Nearest note name + signed cents, e.g. "A4", -50..+50.
void midiToNoteCents(float midi, char* nameOut, int nameCap, int& centsOut);

}  // namespace dsp
