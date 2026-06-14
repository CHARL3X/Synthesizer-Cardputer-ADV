// Master send-effects block: the "space" the dry voices were missing. Sits
// after the filter + soft clip, processes the mono mix in place once per
// block. Three sends, each bypassed when its mix is 0 (so a pure tone stays
// pure and the native tests' default-param paths are untouched):
//
//   chorus  — dual-tap modulated delay; the ensemble shimmer that turns one
//             oscillator into a section. The single biggest "this isn't a
//             $30 keyboard anymore" lever on a mono synth.
//   delay   — damped feedback echo; repeats darken as they decay (tape feel).
//   reverb  — compact Freeverb-style tail (4 combs + 2 allpass, tunings
//             scaled from the 44.1k originals to our 32k rate).
//
// Allocation-free: every line buffer is a fixed member array sized for 32 kHz,
// so this stays pure C++ and ports to dedicated hardware unchanged. No
// Arduino/M5/IDF, no millis().
#pragma once
#include "params.h"

namespace dsp {

class Fx {
public:
    void init(float sr);
    // Process n mono samples in place. Reads the six fx fields off p each block.
    void process(float* buf, int n, const SynthParams& p);
    // Flush all tails to silence — called on panic (AllOff) and the NaN guard
    // so a reverb/echo never rings on after "everything dies".
    void reset();

private:
    // ---- chorus ----------------------------------------------------------
    static constexpr int kChorusMax = 1024;   // 32 ms @ 32k; base ~11ms + mod
    float chBuf_[kChorusMax] = {0.f};
    int   chW_ = 0;
    float chLfo_ = 0.f;                        // LFO phase, radians

    // ---- delay -----------------------------------------------------------
    static constexpr int kDelayMax = 19200;    // 600 ms @ 32k — long enough for
                                               // a tempo-synced echo (e.g. a
                                               // dotted-eighth down to ~75 bpm)
    float dlBuf_[kDelayMax] = {0.f};
    int   dlW_ = 0;
    float dlDamp_ = 0.f;                        // one-pole state in the fb path

    // ---- reverb (mono Freeverb-lite) ------------------------------------
    // Comb/allpass tunings live in fx.cpp (file-local) to dodge the C++14
    // odr-use rule for runtime-indexed static member arrays. Keep this length
    // in sync with kCombLen{810,926,1032,1130} + kApLen{403,320} there.
    static constexpr int kNComb = 4;
    static constexpr int kNAllpass = 2;
    static constexpr int kRvbBufLen = 810 + 926 + 1032 + 1130 + 403 + 320;  // 4621
    float rvBuf_[kRvbBufLen] = {0.f};          // all comb+allpass lines, packed
    float* combBuf_[kNComb] = {nullptr};
    float* apBuf_[kNAllpass] = {nullptr};
    int    combIdx_[kNComb] = {0};
    int    apIdx_[kNAllpass] = {0};
    float  combLp_[kNComb] = {0.f};            // per-comb damping state

    float sr_ = 32000.f;
    bool  rvReady_ = false;
};

}  // namespace dsp
