#include "morph.h"

#include <cmath>

namespace dsp {

namespace {

inline float lin(float a, float b, float t) { return a + (b - a) * t; }

// Geometric lerp for Hz/time-shaped values: equal ratios sound like equal
// steps. The epsilon keeps a zero endpoint (e.g. attack 0 s) finite while
// staying inaudibly close to it.
inline float geo(float a, float b, float t) {
    constexpr float eps = 1e-3f;
    return expf(lin(logf(a + eps), logf(b + eps), t)) - eps;
}

}  // namespace

SynthParams morphParams(const SynthParams& a, const SynthParams& b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) {
        SynthParams out = b;
        // live-mod fields are the caller's per-frame state, never blended
        out.bendCents = a.bendCents;
        out.vibratoCents = a.vibratoCents;
        out.cutoffModOct = a.cutoffModOct;
        out.volMod = a.volMod;
        out.tempoBpm = a.tempoBpm;
        out.tiltAVal = a.tiltAVal;
        out.tiltBVal = a.tiltBVal;
        out.voiceCount = a.voiceCount;  // never yank sounding voices mid-blend
        return out;
    }

    SynthParams o = a;  // live-mod fields + voiceCount ride along from a
    const bool far = t >= 0.5f;

    // discretes switch at the midpoint
    o.wave = far ? b.wave : a.wave;
    o.glideMode = far ? b.glideMode : a.glideMode;
    o.filterMode = far ? b.filterMode : a.filterMode;
    o.delaySync = far ? b.delaySync : a.delaySync;
    o.lfo1Shape = far ? b.lfo1Shape : a.lfo1Shape;
    o.lfo1Sync = far ? b.lfo1Sync : a.lfo1Sync;
    o.lfo2Shape = far ? b.lfo2Shape : a.lfo2Shape;
    o.lfo2Sync = far ? b.lfo2Sync : a.lfo2Sync;

    // time/Hz-shaped: geometric
    o.attackS = geo(a.attackS, b.attackS, t);
    o.decayS = geo(a.decayS, b.decayS, t);
    o.releaseS = geo(a.releaseS, b.releaseS, t);
    o.glideS = geo(a.glideS, b.glideS, t);
    o.cutoffHz = geo(a.cutoffHz, b.cutoffHz, t);
    o.fenvAtkS = geo(a.fenvAtkS, b.fenvAtkS, t);
    o.fenvDecS = geo(a.fenvDecS, b.fenvDecS, t);
    o.delayTimeS = geo(a.delayTimeS, b.delayTimeS, t);
    o.lfo1RateHz = geo(a.lfo1RateHz, b.lfo1RateHz, t);
    o.lfo2RateHz = geo(a.lfo2RateHz, b.lfo2RateHz, t);
    o.modEnvAtkS = geo(a.modEnvAtkS, b.modEnvAtkS, t);
    o.modEnvDecS = geo(a.modEnvDecS, b.modEnvDecS, t);

    // levels/mixes/depths: linear (in-range in -> in-range out)
    o.sustain = lin(a.sustain, b.sustain, t);
    o.resonance = lin(a.resonance, b.resonance, t);
    o.masterVol = lin(a.masterVol, b.masterVol, t);
    o.detuneCents = lin(a.detuneCents, b.detuneCents, t);
    o.fenvOct = lin(a.fenvOct, b.fenvOct, t);
    o.subLevel = lin(a.subLevel, b.subLevel, t);
    o.noiseLevel = lin(a.noiseLevel, b.noiseLevel, t);
    o.drive = lin(a.drive, b.drive, t);
    o.autoVibCents = lin(a.autoVibCents, b.autoVibCents, t);
    o.chorusDepth = lin(a.chorusDepth, b.chorusDepth, t);
    o.delayMix = lin(a.delayMix, b.delayMix, t);
    o.delayFb = lin(a.delayFb, b.delayFb, t);
    o.reverbMix = lin(a.reverbMix, b.reverbMix, t);
    o.reverbSize = lin(a.reverbSize, b.reverbSize, t);

    // mod slots: same routing -> lerp the depth; different routing -> fade a's
    // depth out toward the midpoint, then b's in (mod never jumps, only breathes)
    for (int i = 0; i < kModSlots; ++i) {
        const ModSlot& sa = a.slots[i];
        const ModSlot& sb = b.slots[i];
        if (sa.src == sb.src && sa.dest == sb.dest) {
            o.slots[i] = sa;
            o.slots[i].depth = lin(sa.depth, sb.depth, t);
        } else if (!far) {
            o.slots[i] = sa;
            o.slots[i].depth = sa.depth * (1.f - 2.f * t);
        } else {
            o.slots[i] = sb;
            o.slots[i].depth = sb.depth * (2.f * t - 1.f);
        }
    }
    return o;
}

}  // namespace dsp
