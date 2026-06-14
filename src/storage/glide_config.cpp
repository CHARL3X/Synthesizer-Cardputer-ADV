#include "glide_config.h"

#include <Preferences.h>

#include "../config.h"
#include "../dsp/scales.h"

namespace store {

namespace {
GlideConfig gCfg;
Preferences gPrefs;
bool gDirty = false;
uint32_t gDirtySince = 0;
uint16_t gOverrideMask = 0;  // cached per-slot override flags — the UI asks
                             // every frame; NVS must not be in that path

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Patch override blob. Version-guarded: SynthParams layout changes make old
// blobs invalid -> factory fallback, never garbage sound.
//
// v2 appended the roll-axis (tilt B) fields. The layout is APPEND-ONLY so a v1
// blob can be migrated in place instead of discarded — a saved sound from
// before the two-axis-tilt update keeps its synth + axis-A tilt, and just
// gains a neutral (Off) roll axis. The on-flash size differs between versions,
// so the loader accepts either size and disambiguates on the version byte.
//
// v3 grew SynthParams itself (the six send-effect fields). That changes the
// size of the embedded synth — and therefore of BOTH blob structs below — so
// pre-FX (v1/v2) saves no longer size-match and fall through to the factory
// patch. That is the intended migration: an old custom sound is replaced by
// the upgraded factory voice (now with its FX), never garbage. New saves are
// v3 and round-trip the FX with the slot.
//
// v4 grew SynthParams again (tempo-synced delay: delaySync + the live tempoBpm
// field). Same deal — v3 saves size-mismatch and fall back to the upgraded
// factory voice, which now carries the synced-delay personality.
constexpr uint8_t kPatchBlobVersion = 4;

struct PatchBlobV1 {  // frozen: byte-for-byte the original v1 layout
    uint8_t version;
    uint8_t tiltRoute;
    float tiltDepth;
    dsp::SynthParams synth;
};

struct PatchBlob {  // current (v2)
    uint8_t version;
    uint8_t tiltRoute;
    float tiltDepth;
    dsp::SynthParams synth;
    uint8_t tiltRouteB;   // NEW in v2: roll-axis route
    float tiltDepthB;     // NEW in v2: roll-axis depth
};

void patchKey(int slot, char* out) {
    out[0] = 'p';
    out[1] = (char)('0' + slot);
    out[2] = '\0';
}

// Read a slot's override, migrating a v1 blob forward. Returns true iff a
// valid override exists (v1 or v2). Single source of truth for the three
// call sites (mask scan, applyPatch, and the savePatch upgrade path).
bool loadBlob(const char* key, PatchBlob& out) {
    const size_t len = gPrefs.getBytesLength(key);
    if (len == sizeof(PatchBlob)) {
        if (gPrefs.getBytes(key, &out, sizeof out) == sizeof out && out.version == kPatchBlobVersion)
            return true;
        return false;
    }
    if (len == sizeof(PatchBlobV1)) {
        PatchBlobV1 b1;
        if (gPrefs.getBytes(key, &b1, sizeof b1) == sizeof b1 && b1.version == 1) {
            out.version = kPatchBlobVersion;
            out.tiltRoute = b1.tiltRoute;
            out.tiltDepth = b1.tiltDepth;
            out.synth = b1.synth;
            out.tiltRouteB = (uint8_t)dsp::TiltRoute::Off;  // neutral, inert
            out.tiltDepthB = 0.6f;
            return true;
        }
    }
    return false;
}
}  // namespace

GlideConfig& get() {
    return gCfg;
}

void begin() {
    gPrefs.begin(cfg::kNvsNamespace, false);
    GlideConfig d;  // defaults

    // scan override slots once; afterwards patchHasOverride is a bit test.
    // loadBlob counts both v1 and v2 saves as valid overrides (v1 migrates).
    gOverrideMask = 0;
    for (int i = 0; i < dsp::kPatchCount; ++i) {
        char key[3];
        patchKey(i, key);
        PatchBlob b;
        if (loadBlob(key, b)) gOverrideMask |= (uint16_t)(1u << i);
    }

    auto& s = gCfg.synth;
    s.wave = (dsp::Waveform)clampT<int>(gPrefs.getUChar("wave", (uint8_t)d.synth.wave), 0,
                                        (int)dsp::Waveform::Count - 1);
    s.glideMode = (dsp::GlideMode)clampT<int>(
        gPrefs.getUChar("gmode", (uint8_t)d.synth.glideMode), 0, (int)dsp::GlideMode::Count - 1);
    s.attackS  = clampT<int>(gPrefs.getInt("atk", (int)(d.synth.attackS * 1000)), 0, 2000) / 1000.f;
    s.decayS   = clampT<int>(gPrefs.getInt("dec", (int)(d.synth.decayS * 1000)), 1, 2000) / 1000.f;
    s.sustain  = clampT<int>(gPrefs.getInt("sus", (int)(d.synth.sustain * 100)), 0, 100) / 100.f;
    s.releaseS = clampT<int>(gPrefs.getInt("rel", (int)(d.synth.releaseS * 1000)), 1, 3000) / 1000.f;
    s.glideS   = clampT<int>(gPrefs.getInt("glide", (int)(d.synth.glideS * 1000)), 0, 2000) / 1000.f;
    s.cutoffHz = (float)clampT<int>(gPrefs.getInt("cut", (int)d.synth.cutoffHz), 80, 12000);
    s.resonance = clampT<int>(gPrefs.getInt("res", (int)(d.synth.resonance * 100)), 0, 95) / 100.f;
    s.masterVol = clampT<int>(gPrefs.getInt("vol", (int)(d.synth.masterVol * 100)), 0, 100) / 100.f;
    s.detuneCents = (float)clampT<int>(gPrefs.getInt("det", (int)d.synth.detuneCents), 0, 50);
    s.voiceCount = clampT<int>(gPrefs.getUChar("voices", d.synth.voiceCount), 1, dsp::kMaxVoices);

    // send effects (the live sound's FX state, so it survives a reboot like
    // every other synth param). Absent keys on an existing device default to
    // dry — the new lush presets load the moment any patch is selected.
    s.chorusDepth = clampT<int>(gPrefs.getInt("chorus", (int)(d.synth.chorusDepth * 100)), 0, 100) / 100.f;
    s.delayMix    = clampT<int>(gPrefs.getInt("dlymix", (int)(d.synth.delayMix * 100)), 0, 100) / 100.f;
    s.delayTimeS  = clampT<int>(gPrefs.getInt("dlytime", (int)(d.synth.delayTimeS * 1000)), 10, 600) / 1000.f;
    s.delayFb     = clampT<int>(gPrefs.getInt("dlyfb", (int)(d.synth.delayFb * 100)), 0, 90) / 100.f;
    s.delaySync   = clampT<int>(gPrefs.getUChar("dlysync", d.synth.delaySync), 0, dsp::kDelaySyncCount - 1);
    s.reverbMix   = clampT<int>(gPrefs.getInt("rvbmix", (int)(d.synth.reverbMix * 100)), 0, 100) / 100.f;
    s.reverbSize  = clampT<int>(gPrefs.getInt("rvbsize", (int)(d.synth.reverbSize * 100)), 0, 100) / 100.f;

    auto& l = gCfg.layout;
    l.rootSemis = clampT<int>(gPrefs.getUChar("root", d.layout.rootSemis), 0, 11);
    l.scaleIdx = clampT<int>(gPrefs.getUChar("scale", d.layout.scaleIdx), 0, dsp::kScaleCount - 1);
    l.octave = clampT<int>(gPrefs.getChar("oct", d.layout.octave), 1, 7);
    l.rowIntervalSemis = clampT<int>(gPrefs.getUChar("rowint", d.layout.rowIntervalSemis), 1, 12);
    l.scaleLock = gPrefs.getBool("lock", d.layout.scaleLock);

    gCfg.stringMode = gPrefs.getBool("strmode", d.stringMode);
    gCfg.octaveGlide = gPrefs.getBool("octgl", d.octaveGlide);
    gCfg.tiltRoute = (TiltRoute)clampT<int>(gPrefs.getUChar("tiltrt", (uint8_t)d.tiltRoute), 0,
                                            (int)TiltRoute::Count - 1);
    gCfg.tiltDepth = clampT<int>(gPrefs.getInt("tiltdep", (int)(d.tiltDepth * 100)), 0, 100) / 100.f;
    gCfg.tiltCenter = clampT<int>(gPrefs.getInt("tiltctr", 0), -1000, 1000) / 1000.f;
    gCfg.tiltRouteB = (TiltRoute)clampT<int>(gPrefs.getUChar("tiltrtb", (uint8_t)d.tiltRouteB), 0,
                                             (int)TiltRoute::Count - 1);
    gCfg.tiltDepthB = clampT<int>(gPrefs.getInt("tiltdepb", (int)(d.tiltDepthB * 100)), 0, 100) / 100.f;
    gCfg.tiltCenterB = clampT<int>(gPrefs.getInt("tiltctrb", 0), -1000, 1000) / 1000.f;
    gCfg.tiltOn = gPrefs.getBool("tilton", d.tiltOn);
    gCfg.tiltDual = gPrefs.getBool("tiltdual", d.tiltDual);
    gCfg.currentPatch = clampT<int>(gPrefs.getUChar("cpatch", d.currentPatch), 0,
                                    dsp::kPatchCount - 1);
    gCfg.jamRows = clampT<int>(gPrefs.getUChar("jamrows", d.jamRows), 0, 2);
    gCfg.droneVoicing = clampT<int>(gPrefs.getUChar("dvoice", d.droneVoicing), 0, 2);
    gCfg.jamMotion = clampT<int>(gPrefs.getUChar("jammot", d.jamMotion), 0, 3);
    gCfg.jamBpm = clampT<int>(gPrefs.getUShort("jambpm", d.jamBpm), 40, 240);
    gCfg.jamChordBeats = clampT<int>(gPrefs.getUChar("jamcbt", d.jamChordBeats), 1, 8);
    gCfg.bendMs = clampT<int>(gPrefs.getUShort("bendms", d.bendMs), 50, 1000);
    gCfg.bendRange = clampT<int>(gPrefs.getUChar("bendrg", d.bendRange), 1, 12);
    gCfg.scopeMode = clampT<int>(gPrefs.getUChar("scopemd", d.scopeMode), 0, 1);
    gCfg.bootSound = gPrefs.getBool("boot", d.bootSound);
    gCfg.seenIntro = gPrefs.getBool("intro", d.seenIntro);
}

void persistNow() {
    const auto& s = gCfg.synth;
    gPrefs.putUChar("wave", (uint8_t)s.wave);
    gPrefs.putUChar("gmode", (uint8_t)s.glideMode);
    gPrefs.putInt("atk", (int)(s.attackS * 1000));
    gPrefs.putInt("dec", (int)(s.decayS * 1000));
    gPrefs.putInt("sus", (int)(s.sustain * 100));
    gPrefs.putInt("rel", (int)(s.releaseS * 1000));
    gPrefs.putInt("glide", (int)(s.glideS * 1000));
    gPrefs.putInt("cut", (int)s.cutoffHz);
    gPrefs.putInt("res", (int)(s.resonance * 100));
    gPrefs.putInt("vol", (int)(s.masterVol * 100));
    gPrefs.putInt("det", (int)s.detuneCents);
    gPrefs.putUChar("voices", s.voiceCount);
    gPrefs.putInt("chorus", (int)(s.chorusDepth * 100));
    gPrefs.putInt("dlymix", (int)(s.delayMix * 100));
    gPrefs.putInt("dlytime", (int)(s.delayTimeS * 1000));
    gPrefs.putInt("dlyfb", (int)(s.delayFb * 100));
    gPrefs.putUChar("dlysync", s.delaySync);
    gPrefs.putInt("rvbmix", (int)(s.reverbMix * 100));
    gPrefs.putInt("rvbsize", (int)(s.reverbSize * 100));

    const auto& l = gCfg.layout;
    gPrefs.putUChar("root", l.rootSemis);
    gPrefs.putUChar("scale", l.scaleIdx);
    gPrefs.putChar("oct", l.octave);
    gPrefs.putUChar("rowint", l.rowIntervalSemis);
    gPrefs.putBool("lock", l.scaleLock);

    gPrefs.putBool("strmode", gCfg.stringMode);
    gPrefs.putBool("octgl", gCfg.octaveGlide);
    gPrefs.putUChar("tiltrt", (uint8_t)gCfg.tiltRoute);
    gPrefs.putInt("tiltdep", (int)(gCfg.tiltDepth * 100));
    gPrefs.putInt("tiltctr", (int)(gCfg.tiltCenter * 1000));
    gPrefs.putUChar("tiltrtb", (uint8_t)gCfg.tiltRouteB);
    gPrefs.putInt("tiltdepb", (int)(gCfg.tiltDepthB * 100));
    gPrefs.putInt("tiltctrb", (int)(gCfg.tiltCenterB * 1000));
    gPrefs.putBool("tilton", gCfg.tiltOn);
    gPrefs.putBool("tiltdual", gCfg.tiltDual);
    gPrefs.putUChar("cpatch", gCfg.currentPatch);
    gPrefs.putUChar("jamrows", gCfg.jamRows);
    gPrefs.putUChar("dvoice", gCfg.droneVoicing);
    gPrefs.putUChar("jammot", gCfg.jamMotion);
    gPrefs.putUShort("jambpm", gCfg.jamBpm);
    gPrefs.putUChar("jamcbt", gCfg.jamChordBeats);
    gPrefs.putUShort("bendms", gCfg.bendMs);
    gPrefs.putUChar("bendrg", gCfg.bendRange);
    gPrefs.putUChar("scopemd", gCfg.scopeMode);
    gPrefs.putBool("boot", gCfg.bootSound);
    gPrefs.putBool("intro", gCfg.seenIntro);
    gDirty = false;
}

void markDirty() {
    gDirty = true;
    gDirtySince = millis();
}

void tick(uint32_t nowMs) {
    if (gDirty && nowMs - gDirtySince >= cfg::kPersistDebounceMs) persistNow();
}

void resetDefaults() {
    const bool seen = gCfg.seenIntro;  // don't re-show the intro on reset
    gCfg = GlideConfig();
    gCfg.seenIntro = seen;
    persistNow();
}

// ---- sound slots -----------------------------------------------------------

bool patchHasOverride(int slot) {
    if (slot < 0 || slot >= dsp::kPatchCount) return false;
    return (gOverrideMask >> slot) & 1u;  // cached: called per UI frame
}

void applyPatch(int slot) {
    if (slot < 0 || slot >= dsp::kPatchCount) return;
    char key[3];
    patchKey(slot, key);
    PatchBlob b;
    if (loadBlob(key, b)) {
        gCfg.synth = b.synth;
        gCfg.tiltRoute = (TiltRoute)clampT<int>(b.tiltRoute, 0, (int)TiltRoute::Count - 1);
        gCfg.tiltDepth = clampT(b.tiltDepth, 0.f, 1.f);
        gCfg.tiltRouteB = (TiltRoute)clampT<int>(b.tiltRouteB, 0, (int)TiltRoute::Count - 1);
        gCfg.tiltDepthB = clampT(b.tiltDepthB, 0.f, 1.f);
    } else {
        const dsp::Patch& p = dsp::factoryPatches()[slot];
        gCfg.synth = p.synth;
        gCfg.tiltRoute = p.tiltRoute;
        gCfg.tiltDepth = p.tiltDepth;
        gCfg.tiltRouteB = p.tiltRouteB;
        gCfg.tiltDepthB = p.tiltDepthB;
    }
    gCfg.synth.bendCents = 0.f;  // live-mod fields never come from a patch
    gCfg.synth.vibratoCents = 0.f;
    gCfg.synth.cutoffModOct = 0.f;
    gCfg.synth.volMod = 1.f;
    gCfg.synth.tempoBpm = (float)gCfg.jamBpm;  // driven live, not baked
    gCfg.synth.voiceCount =
        (uint8_t)clampT<int>(gCfg.synth.voiceCount, 1, dsp::kMaxVoices);  // blob hygiene
    gCfg.currentPatch = (uint8_t)slot;
    markDirty();
}

bool savePatch(int slot) {
    if (slot < 0 || slot >= dsp::kPatchCount) return false;
    PatchBlob b;
    b.version = kPatchBlobVersion;
    b.tiltRoute = (uint8_t)gCfg.tiltRoute;
    b.tiltDepth = gCfg.tiltDepth;
    b.synth = gCfg.synth;
    b.synth.bendCents = 0.f;  // never bake a live bend into a patch
    b.synth.vibratoCents = 0.f;
    b.synth.cutoffModOct = 0.f;
    b.synth.volMod = 1.f;
    b.synth.tempoBpm = 120.f;  // tempo is live, not part of the saved sound
    b.tiltRouteB = (uint8_t)gCfg.tiltRouteB;  // v2: roll axis travels with the slot
    b.tiltDepthB = gCfg.tiltDepthB;
    char key[3];
    patchKey(slot, key);
    const bool ok = gPrefs.putBytes(key, &b, sizeof b) == sizeof b;
    if (ok) {
        gOverrideMask |= (uint16_t)(1u << slot);
        gCfg.currentPatch = (uint8_t)slot;
        markDirty();
    }
    return ok;
}

void clearOverride(int slot) {
    if (slot < 0 || slot >= dsp::kPatchCount) return;
    char key[3];
    patchKey(slot, key);
    gPrefs.remove(key);
    gOverrideMask &= (uint16_t)~(1u << slot);
}

const char* patchName(int slot) {
    if (slot < 0 || slot >= dsp::kPatchCount) return "?";
    return dsp::factoryPatches()[slot].name;
}

}  // namespace store
