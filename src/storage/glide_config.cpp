#include "glide_config.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "../config.h"
#include "../dsp/scales.h"
#include "patch_codec.h"

namespace store {

namespace {
GlideConfig gCfg;
Preferences gPrefs;
bool gNvsOk = false;  // did the NVS namespace actually open? if not, NOTHING
                      // persists — reads return defaults, writes silently no-op
uint32_t gBootCount = 0;     // DIAGNOSTIC: boots survived in NVS (see begin())
bool gWriteProbeOk = false;  // DIAGNOSTIC: did this boot's probe write+readback?
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

// Load a slot into a PatchData, seeding from the factory patch first so any
// field the stored blob predates keeps its factory default. Handles BOTH the
// new tagged format and the legacy binary blob (so un-migrated saves still
// load). Returns true iff an override blob exists for the slot.
bool loadPatchData(int slot, PatchData& out) {
    const dsp::Patch& fp = dsp::factoryPatches()[slot];
    out.synth = fp.synth;  // seed: defaults for anything the blob doesn't carry
    out.tiltRoute = (uint8_t)fp.tiltRoute;
    out.tiltDepth = fp.tiltDepth;
    out.tiltRouteB = (uint8_t)fp.tiltRouteB;
    out.tiltDepthB = fp.tiltDepthB;

    char key[3];
    patchKey(slot, key);
    const size_t len = gPrefs.getBytesLength(key);
    if (len == 0) return false;  // no override saved

    uint8_t buf[512];
    if (len >= 3 && len <= sizeof buf) {
        const size_t got = gPrefs.getBytes(key, buf, len);
        if (got == len && buf[0] == 'G' && buf[1] == 'P')
            return decodePatch(buf, len, out);  // tagged: overlays onto the seed
    }
    // legacy binary blob (pre-tagged saves): read via the frozen struct path
    PatchBlob b;
    if (loadBlob(key, b)) {
        out.synth = b.synth;
        out.tiltRoute = b.tiltRoute;
        out.tiltDepth = b.tiltDepth;
        out.tiltRouteB = b.tiltRouteB;
        out.tiltDepthB = b.tiltDepthB;
        return true;
    }
    return false;
}

// Apply a PatchData to the live config: the sound + tilt personality, with the
// same hygiene applyPatch always did (keep the player's master volume, never
// load live-mod fields, clamp voiceCount). Shared so the (future) SD-library
// load path and the NVS-slot path can't drift.
void applyPatchData(const PatchData& pd) {
    const float keepVol = gCfg.synth.masterVol;  // volume is the player's, not the sound's
    gCfg.synth = pd.synth;
    gCfg.tiltRoute = (TiltRoute)clampT<int>(pd.tiltRoute, 0, (int)TiltRoute::Count - 1);
    gCfg.tiltDepth = clampT(pd.tiltDepth, 0.f, 1.f);
    gCfg.tiltRouteB = (TiltRoute)clampT<int>(pd.tiltRouteB, 0, (int)TiltRoute::Count - 1);
    gCfg.tiltDepthB = clampT(pd.tiltDepthB, 0.f, 1.f);
    gCfg.synth.masterVol = keepVol;
    gCfg.synth.bendCents = 0.f;  // live-mod fields never come from a patch
    gCfg.synth.vibratoCents = 0.f;
    gCfg.synth.cutoffModOct = 0.f;
    gCfg.synth.volMod = 1.f;
    gCfg.synth.tempoBpm = (float)gCfg.jamBpm;  // driven live, not baked
    gCfg.synth.voiceCount =
        (uint8_t)clampT<int>(gCfg.synth.voiceCount, 1, dsp::kMaxVoices);  // blob hygiene
}
}  // namespace

GlideConfig& get() {
    return gCfg;
}

bool nvsHealthy() {
    return gNvsOk;
}

uint32_t bootCount() {
    return gBootCount;
}

bool writeProbeOk() {
    return gWriteProbeOk;
}

void begin() {
    // Open the namespace read/write. If it fails the instrument still plays,
    // but nothing the player changes survives a reboot — so don't fail
    // silently (Hard Rule #3). The usual cause is an NVS partition that was
    // never initialised or got into a bad state (e.g. a NO_FREE_PAGES /
    // NEW_VERSION_FOUND condition, which is common when the firmware runs
    // under a Launcher whose flashed partition layout differs from ours).
    // The standard ESP-IDF recovery is to erase + re-init NVS, then retry.
    gNvsOk = gPrefs.begin(cfg::kNvsNamespace, false);
    if (!gNvsOk) {
        Serial.println("[glide] NVS open failed — erasing + re-init, retrying");
        nvs_flash_erase();
        nvs_flash_init();
        gNvsOk = gPrefs.begin(cfg::kNvsNamespace, false);
        Serial.printf("[glide] NVS retry: %s\n", gNvsOk ? "ok" : "STILL FAILED");
    }

    // DIAGNOSTIC: prove whether writes actually survive a reboot. Read a boot
    // counter, increment, write+commit, read it BACK. gBootCount climbing
    // across power cycles => NVS persists (bug is elsewhere); stuck at 1 =>
    // writes don't survive (NVS full / wiped / not really committing). The
    // readback also tells us if the write was even accepted this session.
    const uint32_t prevBoot = gPrefs.getUInt("bootn", 0);
    gBootCount = prevBoot + 1;
    const size_t wrote = gPrefs.putUInt("bootn", gBootCount);
    const uint32_t readBack = gPrefs.getUInt("bootn", 0);
    gWriteProbeOk = (wrote == sizeof(uint32_t)) && (readBack == gBootCount);
    Serial.printf("[glide] boot #%u (prev=%u) write=%uB readback=%u probe=%s\n",
                  gBootCount, prevBoot, (unsigned)wrote, readBack,
                  gWriteProbeOk ? "ok" : "FAIL");

    GlideConfig d;  // defaults

    // scan override slots once; afterwards patchHasOverride is a bit test.
    // loadPatchData counts tagged saves AND legacy v1/v2/v4 blobs as overrides.
    gOverrideMask = 0;
    for (int i = 0; i < dsp::kPatchCount; ++i) {
        PatchData pd;
        if (loadPatchData(i, pd)) gOverrideMask |= (uint16_t)(1u << i);
    }

    // one-time: re-encode any legacy binary blobs into the tagged format, so
    // future sound-param additions stop size-mismatching them. Opportunistic —
    // loadPatchData still reads legacy blobs, so correctness doesn't depend on
    // this. NVS-full safe: a failed putBytes leaves the legacy blob untouched
    // and we DON'T set the sentinel, so it retries next boot once space frees up.
    if (gNvsOk && !gPrefs.getBool("tlv1", false)) {
        bool allOk = true;
        for (int i = 0; i < dsp::kPatchCount; ++i) {
            if (!((gOverrideMask >> i) & 1u)) continue;  // no override here
            PatchData pd;
            loadPatchData(i, pd);  // reads legacy or tagged, seeded from factory
            uint8_t buf[512];
            const size_t n = encodePatch(pd, buf, sizeof buf);
            char key[3];
            patchKey(i, key);
            if (n == 0 || gPrefs.putBytes(key, buf, n) != n) allOk = false;
        }
        if (allOk) gPrefs.putBool("tlv1", true);
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

    // patch character (filter env / sub / noise / drive / auto-vibrato). These
    // are as much "the live sound" as the FX above and must survive a reboot
    // the same way — without them a tweaked or character-heavy patch reverts
    // toward the neutral GLIDE tone on power-up. Seeded once from the current
    // patch below (the "schar" migration) for devices that predate this.
    s.fenvAtkS    = clampT<int>(gPrefs.getInt("fatk", (int)(d.synth.fenvAtkS * 1000)), 1, 2000) / 1000.f;
    s.fenvDecS    = clampT<int>(gPrefs.getInt("fdec", (int)(d.synth.fenvDecS * 1000)), 10, 2000) / 1000.f;
    s.fenvOct     = clampT<int>(gPrefs.getInt("fenv", (int)(d.synth.fenvOct * 100)), 0, 600) / 100.f;
    s.subLevel    = clampT<int>(gPrefs.getInt("sub", (int)(d.synth.subLevel * 100)), 0, 100) / 100.f;
    s.noiseLevel  = clampT<int>(gPrefs.getInt("noise", (int)(d.synth.noiseLevel * 100)), 0, 100) / 100.f;
    s.drive       = clampT<int>(gPrefs.getInt("drive", (int)(d.synth.drive * 100)), 100, 800) / 100.f;
    s.autoVibCents = (float)clampT<int>(gPrefs.getInt("avib", (int)d.synth.autoVibCents), 0, 100);

    // modulation: 2 LFOs + mod-env + the routing matrix. Absent keys default to
    // the neutral values, so existing devices load with the matrix inert (no
    // tone change) until the player assigns a slot. Rates as centi-Hz, env times
    // as ms, slot depths as ×100 (-100..100). Keys ≤15 chars.
    s.lfo1RateHz = clampT<int>(gPrefs.getInt("l1r", (int)(d.synth.lfo1RateHz * 100)), 1, 3000) / 100.f;
    s.lfo1Shape  = (uint8_t)clampT<int>(gPrefs.getUChar("l1sh", d.synth.lfo1Shape), 0, (int)dsp::LfoShape::Count - 1);
    s.lfo1Sync   = (uint8_t)clampT<int>(gPrefs.getUChar("l1sy", d.synth.lfo1Sync), 0, dsp::kDelaySyncCount - 1);
    s.lfo2RateHz = clampT<int>(gPrefs.getInt("l2r", (int)(d.synth.lfo2RateHz * 100)), 1, 3000) / 100.f;
    s.lfo2Shape  = (uint8_t)clampT<int>(gPrefs.getUChar("l2sh", d.synth.lfo2Shape), 0, (int)dsp::LfoShape::Count - 1);
    s.lfo2Sync   = (uint8_t)clampT<int>(gPrefs.getUChar("l2sy", d.synth.lfo2Sync), 0, dsp::kDelaySyncCount - 1);
    s.modEnvAtkS = clampT<int>(gPrefs.getInt("mea", (int)(d.synth.modEnvAtkS * 1000)), 1, 2000) / 1000.f;
    s.modEnvDecS = clampT<int>(gPrefs.getInt("med", (int)(d.synth.modEnvDecS * 1000)), 10, 4000) / 1000.f;
    for (int i = 0; i < dsp::kModSlots; ++i) {
        char ks[4] = {'m', (char)('0' + i), 's', '\0'};
        char kd[4] = {'m', (char)('0' + i), 'd', '\0'};
        char ka[4] = {'m', (char)('0' + i), 'a', '\0'};
        s.slots[i].src  = (uint8_t)clampT<int>(gPrefs.getUChar(ks, 0), 0, (int)dsp::ModSource::Count - 1);
        s.slots[i].dest = (uint8_t)clampT<int>(gPrefs.getUChar(kd, 0), 0, (int)dsp::ModDest::Count - 1);
        s.slots[i].depth = clampT<int>(gPrefs.getInt(ka, 0), -100, 100) / 100.f;
    }

    auto& l = gCfg.layout;
    l.rootSemis = clampT<int>(gPrefs.getUChar("root", d.layout.rootSemis), 0, 11);
    l.scaleIdx = clampT<int>(gPrefs.getUChar("scale", d.layout.scaleIdx), 0, dsp::kScaleCount - 1);
    l.octave = clampT<int>(gPrefs.getChar("oct", d.layout.octave), 1, 7);
    // one-time: the default base octave dropped to 3 — adopt it once even on
    // devices that saved the old 4 (the player's later octave shifts persist).
    if (!gPrefs.getBool("oct3", false)) {
        l.octave = 3;
        gPrefs.putChar("oct", 3);
        gPrefs.putBool("oct3", true);
    }
    l.rowIntervalSemis = clampT<int>(gPrefs.getUChar("rowint", d.layout.rowIntervalSemis), 1, 12);
    l.scaleLock = gPrefs.getBool("lock", d.layout.scaleLock);

    gCfg.stringMode = gPrefs.getBool("strmode", d.stringMode);
    gCfg.octaveGlide = gPrefs.getBool("octgl", d.octaveGlide);
    gCfg.tiltRoute = (TiltRoute)clampT<int>(gPrefs.getUChar("tiltrt", (uint8_t)d.tiltRoute), 0,
                                            (int)TiltRoute::Count - 1);
    gCfg.tiltDepth = clampT<int>(gPrefs.getInt("tiltdep", (int)(d.tiltDepth * 100)), 0, 100) / 100.f;
    gCfg.tiltCenter = clampT<int>(gPrefs.getInt("tiltctr", (int)(d.tiltCenter * 1000)), -1000, 1000) / 1000.f;
    gCfg.tiltRouteB = (TiltRoute)clampT<int>(gPrefs.getUChar("tiltrtb", (uint8_t)d.tiltRouteB), 0,
                                             (int)TiltRoute::Count - 1);
    gCfg.tiltDepthB = clampT<int>(gPrefs.getInt("tiltdepb", (int)(d.tiltDepthB * 100)), 0, 100) / 100.f;
    gCfg.tiltCenterB = clampT<int>(gPrefs.getInt("tiltctrb", 0), -1000, 1000) / 1000.f;
    gCfg.tiltOn = gPrefs.getBool("tilton", d.tiltOn);
    // one-time: gyro expression is on by default now — no need to press enter.
    // Adopt it once even on devices that saved tilt off (pre-2026-06-07).
    if (!gPrefs.getBool("tilton2", false)) {
        gCfg.tiltOn = true;
        gPrefs.putBool("tilton", true);
        gPrefs.putBool("tilton2", true);
    }
    gCfg.tiltDual = gPrefs.getBool("tiltdual", d.tiltDual);
    gCfg.currentPatch = clampT<int>(gPrefs.getUChar("cpatch", d.currentPatch), 0,
                                    dsp::kPatchCount - 1);

    // one-time: the patch-character fields above were never persisted before
    // this build, so on a pre-existing device they'd load as neutral defaults
    // (no filter env / sub / drive...) and then overwrite the real sound on the
    // next save. Seed them ONCE from the current patch (override if present,
    // else factory) — restoring correct character without touching the other
    // flat-key fields the player may have tweaked. New saves persist them.
    if (!gPrefs.getBool("schar", false)) {
        PatchData pd;  // seeded from factory, overlaid with the override if present
        loadPatchData(gCfg.currentPatch, pd);
        const dsp::SynthParams src = pd.synth;
        s.fenvAtkS = src.fenvAtkS;
        s.fenvDecS = src.fenvDecS;
        s.fenvOct = src.fenvOct;
        s.subLevel = src.subLevel;
        s.noiseLevel = src.noiseLevel;
        s.drive = src.drive;
        s.autoVibCents = src.autoVibCents;
        gPrefs.putInt("fatk", (int)(s.fenvAtkS * 1000));
        gPrefs.putInt("fdec", (int)(s.fenvDecS * 1000));
        gPrefs.putInt("fenv", (int)(s.fenvOct * 100));
        gPrefs.putInt("sub", (int)(s.subLevel * 100));
        gPrefs.putInt("noise", (int)(s.noiseLevel * 100));
        gPrefs.putInt("drive", (int)(s.drive * 100));
        gPrefs.putInt("avib", (int)s.autoVibCents);
        gPrefs.putBool("schar", true);
    }

    gCfg.jamRows = clampT<int>(gPrefs.getUChar("jamrows", d.jamRows), 0, 2);
    // one-time: the jam (backing) row is now on by default — adopt it once even
    // on devices that saved it off (the player's later choice still sticks).
    if (!gPrefs.getBool("jamrows2", false)) {
        gCfg.jamRows = 1;
        gPrefs.putUChar("jamrows", 1);
        gPrefs.putBool("jamrows2", true);
    }
    gCfg.droneVoicing = clampT<int>(gPrefs.getUChar("dvoice", d.droneVoicing), 0, 2);
    gCfg.jamMotion = clampT<int>(gPrefs.getUChar("jammot", d.jamMotion), 0, 3);
    // one-time: progression became the default jam motion — adopt it once even
    // on devices that saved the old "sustained" default (later choice sticks).
    if (!gPrefs.getBool("jammot2", false)) {
        gCfg.jamMotion = 3;
        gPrefs.putUChar("jammot", 3);
        gPrefs.putBool("jammot2", true);
    }
    gCfg.jamBpm = clampT<int>(gPrefs.getUShort("jambpm", d.jamBpm), 40, 240);
    gCfg.jamChordBeats = clampT<int>(gPrefs.getUChar("jamcbt", d.jamChordBeats), 1, 8);
    gCfg.bendMs = clampT<int>(gPrefs.getUShort("bendms", d.bendMs), 50, 1000);
    gCfg.bendRange = clampT<int>(gPrefs.getUChar("bendrg", d.bendRange), 1, 12);
    gCfg.scopeMode = clampT<int>(gPrefs.getUChar("scopemd", d.scopeMode), 0, 1);
    // one-time: pitch trail became the default — adopt it even on devices that
    // saved the old waveform default before the change (runs once; the player's
    // later choice still sticks).
    if (!gPrefs.getBool("dispv2", false)) {
        gCfg.scopeMode = 1;
        gPrefs.putUChar("scopemd", 1);
        gPrefs.putBool("dispv2", true);
    }
    gCfg.bootSound = gPrefs.getBool("boot", d.bootSound);
    gCfg.seenIntro = gPrefs.getBool("intro", d.seenIntro);

    // G0 trigger macro (absent on pre-existing devices -> the muffle default,
    // i.e. the original behaviour at the gentler default depth)
    gCfg.triggerAction = clampT<int>(gPrefs.getUChar("trigact", d.triggerAction), 0,
                                     (int)TriggerAction::Count - 1);
    gCfg.triggerDepth = clampT<int>(gPrefs.getInt("trigdep", (int)(d.triggerDepth * 100)), 0, 100) / 100.f;
    gCfg.triggerLatch = gPrefs.getBool("triglat", d.triggerLatch);
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
    gPrefs.putInt("fatk", (int)(s.fenvAtkS * 1000));
    gPrefs.putInt("fdec", (int)(s.fenvDecS * 1000));
    gPrefs.putInt("fenv", (int)(s.fenvOct * 100));
    gPrefs.putInt("sub", (int)(s.subLevel * 100));
    gPrefs.putInt("noise", (int)(s.noiseLevel * 100));
    gPrefs.putInt("drive", (int)(s.drive * 100));
    gPrefs.putInt("avib", (int)s.autoVibCents);
    gPrefs.putInt("l1r", (int)(s.lfo1RateHz * 100));
    gPrefs.putUChar("l1sh", s.lfo1Shape);
    gPrefs.putUChar("l1sy", s.lfo1Sync);
    gPrefs.putInt("l2r", (int)(s.lfo2RateHz * 100));
    gPrefs.putUChar("l2sh", s.lfo2Shape);
    gPrefs.putUChar("l2sy", s.lfo2Sync);
    gPrefs.putInt("mea", (int)(s.modEnvAtkS * 1000));
    gPrefs.putInt("med", (int)(s.modEnvDecS * 1000));
    for (int i = 0; i < dsp::kModSlots; ++i) {
        char ks[4] = {'m', (char)('0' + i), 's', '\0'};
        char kd[4] = {'m', (char)('0' + i), 'd', '\0'};
        char ka[4] = {'m', (char)('0' + i), 'a', '\0'};
        gPrefs.putUChar(ks, s.slots[i].src);
        gPrefs.putUChar(kd, s.slots[i].dest);
        gPrefs.putInt(ka, (int)(s.slots[i].depth * 100));
    }

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
    gPrefs.putUChar("trigact", gCfg.triggerAction);
    gPrefs.putInt("trigdep", (int)(gCfg.triggerDepth * 100));
    gPrefs.putBool("triglat", gCfg.triggerLatch);
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
    PatchData pd;
    loadPatchData(slot, pd);  // pd = the override if saved, else the factory seed
    applyPatchData(pd);
    gCfg.currentPatch = (uint8_t)slot;
    markDirty();
}

bool savePatch(int slot) {
    if (slot < 0 || slot >= dsp::kPatchCount) return false;
    PatchData pd;
    pd.synth = gCfg.synth;
    pd.synth.bendCents = 0.f;  // never bake live-mod into a saved sound
    pd.synth.vibratoCents = 0.f;
    pd.synth.cutoffModOct = 0.f;
    pd.synth.volMod = 1.f;
    pd.synth.tempoBpm = 120.f;  // tempo is live, not part of the saved sound
    pd.tiltRoute = (uint8_t)gCfg.tiltRoute;
    pd.tiltDepth = gCfg.tiltDepth;
    pd.tiltRouteB = (uint8_t)gCfg.tiltRouteB;
    pd.tiltDepthB = gCfg.tiltDepthB;

    uint8_t buf[512];
    const size_t n = encodePatch(pd, buf, sizeof buf);
    if (n == 0) return false;
    char key[3];
    patchKey(slot, key);
    const bool ok = gPrefs.putBytes(key, buf, n) == n;
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

// ---- solo/backing split -----------------------------------------------------
void lockBacking() {
    gCfg.backingSynth = gCfg.synth;       // freeze the sound now playing
    gCfg.backingSynth.bendCents = 0.f;    // a steady bed: no live mods baked in
    gCfg.backingSynth.vibratoCents = 0.f;
    gCfg.backingSynth.cutoffModOct = 0.f;
    gCfg.backingSynth.volMod = 1.f;
    gCfg.backingLocked = true;
}

void unlockBacking() { gCfg.backingLocked = false; }

bool backingLocked() { return gCfg.backingLocked; }

}  // namespace store
