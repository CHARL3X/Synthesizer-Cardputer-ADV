// Scale tables. Pure C++.
#pragma once
#include <cstdint>

namespace dsp {

struct Scale {
    const char* name;
    const char* shortName;
    uint8_t len;
    uint8_t steps[12];  // semitone offsets within one octave
    // Index into kScales of this scale's HARMONY PARENT — the consonant 7-note
    // scale the backing progression builds its triads from (see chordPitches).
    // A pentatonic or blues scale stacked into triads on its own degrees makes
    // dissonant piles (the blues "blue note", a ♭5, is a *melodic* color and a
    // foul chord tone), so those borrow the diatonic scale they're carved from.
    // A 7-note scale is its own parent, so diatonic progressions are unchanged.
    uint8_t harm;
};

// kScales indices, named so the harmony-parent column below reads as music.
enum {
    SC_CHROM, SC_MAJOR, SC_MINOR, SC_MAJ_PENT, SC_MIN_PENT, SC_BLUES,
    SC_DORIAN, SC_MIXO, SC_HARM_MIN, SC_PHR_DOM, SC_LYDIAN, SC_WHOLE, SC_HIRA,
};

constexpr Scale kScales[] = {
    //  name             short   len  steps                              harmony parent
    {"Chromatic",     "chrom", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, SC_MAJOR},
    {"Major",         "maj",    7, {0, 2, 4, 5, 7, 9, 11},                 SC_MAJOR},
    {"Natural minor", "min",    7, {0, 2, 3, 5, 7, 8, 10},                  SC_MINOR},
    {"Major pent",    "Mpent",  5, {0, 2, 4, 7, 9},                         SC_MAJOR},
    {"Minor pent",    "mpent",  5, {0, 3, 5, 7, 10},                        SC_MINOR},
    {"Blues",         "blues",  6, {0, 3, 5, 6, 7, 10},                     SC_MINOR},
    {"Dorian",        "dorian", 7, {0, 2, 3, 5, 7, 9, 10},                  SC_DORIAN},
    {"Mixolydian",    "mixo",   7, {0, 2, 4, 5, 7, 9, 10},                  SC_MIXO},
    // appended after v0.4 — append-only: stored scale indices stay valid
    {"Harmonic minor","harm",   7, {0, 2, 3, 5, 7, 8, 11},                  SC_HARM_MIN},
    {"Phrygian dom",  "phdom",  7, {0, 1, 4, 5, 7, 8, 10},                  SC_PHR_DOM},
    {"Lydian",        "lyd",    7, {0, 2, 4, 6, 7, 9, 11},                  SC_LYDIAN},
    {"Whole tone",    "whole",  6, {0, 2, 4, 6, 8, 10},                     SC_LYDIAN},
    {"Hirajoshi",     "hira",   5, {0, 2, 3, 7, 8},                         SC_MINOR},
};
constexpr int kScaleCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
constexpr int kDefaultScale = 4;  // minor pentatonic — first touch sounds good

constexpr const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};

}  // namespace dsp
