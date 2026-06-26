#include "patch_codec.h"

#include <cstring>

namespace store {
namespace {

// Wire types. The type byte doubles as the value width in bytes, so the decoder
// can skip a record it doesn't recognize without a separate length field.
constexpr uint8_t T_U8  = 1;
constexpr uint8_t T_F32 = 4;

constexpr uint8_t kMagic0 = 'G';
constexpr uint8_t kMagic1 = 'P';
constexpr uint8_t kFormatVersion = 1;  // bump ONLY if the envelope itself changes,
                                       // never when adding fields (that's the point)

// Permanent, APPEND-ONLY tag numbers. THE ONE RULE: never reuse or renumber a
// tag. Add new params with new numbers; a removed param just leaves a gap. This
// is what makes every old save loadable forever. Reserved ranges:
//   1..39    dsp::SynthParams sound fields
//   40..99   modulation (LFOs / mod-env / matrix slots) — filled in with the mod matrix
//   100..    tilt personality (lives outside SynthParams)
enum Tag : uint16_t {
    T_wave = 1, T_glideMode, T_attackS, T_decayS, T_sustain, T_releaseS, T_glideS,
    T_cutoffHz, T_resonance, T_masterVol, T_detuneCents, T_voiceCount,
    T_fenvAtkS, T_fenvDecS, T_fenvOct, T_subLevel, T_noiseLevel, T_drive, T_autoVibCents,
    T_chorusDepth, T_delayMix, T_delayTimeS, T_delayFb, T_delaySync, T_reverbMix, T_reverbSize,

    // modulation (mod matrix) — range 40..99
    T_lfo1Rate = 40, T_lfo1Shape, T_lfo1Sync,
    T_lfo2Rate, T_lfo2Shape, T_lfo2Sync,
    T_modEnvAtk, T_modEnvDec,
    // the kModSlots routing slots: tag = kSlotTagBase + slot*3 + {0=src,1=dest,2=depth}

    T_tiltRoute = 100, T_tiltDepth, T_tiltRouteB, T_tiltDepthB,
};
constexpr uint16_t kSlotTagBase = 50;

// One row per persisted field: tag, wire type, and a pointer into a given
// PatchData instance. Built fresh against the instance being read/written so
// encode and decode share the exact same field set and can never drift.
struct Field {
    uint16_t tag;
    uint8_t  type;
    void*    ptr;
};
constexpr int kMaxFields = 64;  // headroom for the mod-matrix fields added later

int buildTable(PatchData& pd, Field* f) {
    dsp::SynthParams& s = pd.synth;
    int n = 0;
    // enum class : uint8_t is layout-compatible with uint8_t, so the byte at &enum
    // is its underlying value — safe to copy as a T_U8.
    f[n++] = {T_wave,        T_U8,  &s.wave};
    f[n++] = {T_glideMode,   T_U8,  &s.glideMode};
    f[n++] = {T_attackS,     T_F32, &s.attackS};
    f[n++] = {T_decayS,      T_F32, &s.decayS};
    f[n++] = {T_sustain,     T_F32, &s.sustain};
    f[n++] = {T_releaseS,    T_F32, &s.releaseS};
    f[n++] = {T_glideS,      T_F32, &s.glideS};
    f[n++] = {T_cutoffHz,    T_F32, &s.cutoffHz};
    f[n++] = {T_resonance,   T_F32, &s.resonance};
    f[n++] = {T_masterVol,   T_F32, &s.masterVol};
    f[n++] = {T_detuneCents, T_F32, &s.detuneCents};
    f[n++] = {T_voiceCount,  T_U8,  &s.voiceCount};
    f[n++] = {T_fenvAtkS,    T_F32, &s.fenvAtkS};
    f[n++] = {T_fenvDecS,    T_F32, &s.fenvDecS};
    f[n++] = {T_fenvOct,     T_F32, &s.fenvOct};
    f[n++] = {T_subLevel,    T_F32, &s.subLevel};
    f[n++] = {T_noiseLevel,  T_F32, &s.noiseLevel};
    f[n++] = {T_drive,       T_F32, &s.drive};
    f[n++] = {T_autoVibCents,T_F32, &s.autoVibCents};
    f[n++] = {T_chorusDepth, T_F32, &s.chorusDepth};
    f[n++] = {T_delayMix,    T_F32, &s.delayMix};
    f[n++] = {T_delayTimeS,  T_F32, &s.delayTimeS};
    f[n++] = {T_delayFb,     T_F32, &s.delayFb};
    f[n++] = {T_delaySync,   T_U8,  &s.delaySync};
    f[n++] = {T_reverbMix,   T_F32, &s.reverbMix};
    f[n++] = {T_reverbSize,  T_F32, &s.reverbSize};
    f[n++] = {T_lfo1Rate,    T_F32, &s.lfo1RateHz};
    f[n++] = {T_lfo1Shape,   T_U8,  &s.lfo1Shape};
    f[n++] = {T_lfo1Sync,    T_U8,  &s.lfo1Sync};
    f[n++] = {T_lfo2Rate,    T_F32, &s.lfo2RateHz};
    f[n++] = {T_lfo2Shape,   T_U8,  &s.lfo2Shape};
    f[n++] = {T_lfo2Sync,    T_U8,  &s.lfo2Sync};
    f[n++] = {T_modEnvAtk,   T_F32, &s.modEnvAtkS};
    f[n++] = {T_modEnvDec,   T_F32, &s.modEnvDecS};
    for (int i = 0; i < dsp::kModSlots; ++i) {
        f[n++] = {(uint16_t)(kSlotTagBase + i * 3 + 0), T_U8,  &s.slots[i].src};
        f[n++] = {(uint16_t)(kSlotTagBase + i * 3 + 1), T_U8,  &s.slots[i].dest};
        f[n++] = {(uint16_t)(kSlotTagBase + i * 3 + 2), T_F32, &s.slots[i].depth};
    }
    f[n++] = {T_tiltRoute,   T_U8,  &pd.tiltRoute};
    f[n++] = {T_tiltDepth,   T_F32, &pd.tiltDepth};
    f[n++] = {T_tiltRouteB,  T_U8,  &pd.tiltRouteB};
    f[n++] = {T_tiltDepthB,  T_F32, &pd.tiltDepthB};
    return n;
}

}  // namespace

size_t encodePatch(const PatchData& in, uint8_t* buf, size_t cap) {
    PatchData tmp = in;  // buildTable needs non-const pointers; encode reads them
    Field f[kMaxFields];
    const int nf = buildTable(tmp, f);
    size_t pos = 0;
    if (pos + 3 > cap) return 0;
    buf[pos++] = kMagic0;
    buf[pos++] = kMagic1;
    buf[pos++] = kFormatVersion;
    for (int i = 0; i < nf; ++i) {
        const uint8_t w = f[i].type;  // width == type code
        if (pos + 3u + w > cap) return 0;
        buf[pos++] = (uint8_t)(f[i].tag & 0xFF);
        buf[pos++] = (uint8_t)(f[i].tag >> 8);
        buf[pos++] = f[i].type;
        memcpy(buf + pos, f[i].ptr, w);
        pos += w;
    }
    return pos;
}

bool decodePatch(const uint8_t* buf, size_t len, PatchData& out) {
    if (len < 3 || buf[0] != kMagic0 || buf[1] != kMagic1) return false;
    if (buf[2] != kFormatVersion) return false;  // unknown envelope -> keep seed
    Field f[kMaxFields];
    const int nf = buildTable(out, f);
    size_t pos = 3;
    while (pos + 3 <= len) {
        const uint16_t tag = (uint16_t)(buf[pos] | (buf[pos + 1] << 8));
        const uint8_t type = buf[pos + 2];
        pos += 3;
        const uint8_t w = type;       // width == type code
        if (pos + w > len) break;     // truncated tail record -> keep what we have
        for (int i = 0; i < nf; ++i) {
            if (f[i].tag == tag && f[i].type == type) {
                memcpy(f[i].ptr, buf + pos, w);
                break;  // unmatched tag (unknown/removed) just falls through -> skipped
            }
        }
        pos += w;
    }
    return true;
}

}  // namespace store
