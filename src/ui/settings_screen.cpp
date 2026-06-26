#include "settings_screen.h"

#include <cstdio>

#include "../config.h"
#include "../dsp/params.h"
#include "../dsp/scales.h"
#include "../io/audio_engine.h"
#include "../io/keys.h"
#include "../io/looper.h"
#include "../io/tilt.h"
#include "../storage/glide_config.h"
#include "theme.h"

namespace settings {

namespace {

// positional key codes (y*14+x) — same convention as keys.cpp
constexpr int kUp = 39;     // ;
constexpr int kDown = 53;   // .
constexpr int kLeft = 52;   // ,
constexpr int kRight = 54;  // /
constexpr int kDecAlt = 25; // [
constexpr int kIncAlt = 26; // ]
constexpr int kEnter = 41;
constexpr int kFn = 28;     // fn — held, jumps section to section
constexpr int kExit1 = 0;   // `
constexpr int kExit2 = 14;  // tab

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

struct Item {
    const char* name;
    void (*format)(char* out, int cap);
    void (*adjust)(int dir);
};

void fRoot(char* o, int c) { snprintf(o, c, "%s", dsp::kNoteNames[store::get().layout.rootSemis]); }
void aRoot(int d) {
    auto& l = store::get().layout;
    l.rootSemis = (uint8_t)(((int)l.rootSemis + d + 12) % 12);
}

void fScale(char* o, int c) { snprintf(o, c, "%s", dsp::kScales[store::get().layout.scaleIdx].name); }
void aScale(int d) {
    auto& l = store::get().layout;
    l.scaleIdx = (uint8_t)(((int)l.scaleIdx + d + dsp::kScaleCount) % dsp::kScaleCount);
}

void fRowInt(char* o, int c) { snprintf(o, c, "%d st", store::get().layout.rowIntervalSemis); }
void aRowInt(int d) {
    auto& l = store::get().layout;
    l.rowIntervalSemis = (uint8_t)clampT((int)l.rowIntervalSemis + d, 1, 12);
}

void fGlideMode(char* o, int c) {
    snprintf(o, c, "%s",
             store::get().synth.glideMode == dsp::GlideMode::LegatoOnly ? "legato only" : "always");
}
void aGlideMode(int) {
    auto& s = store::get().synth;
    s.glideMode = s.glideMode == dsp::GlideMode::LegatoOnly ? dsp::GlideMode::Always
                                                            : dsp::GlideMode::LegatoOnly;
}

void fStringMode(char* o, int c) {
    snprintf(o, c, "%s", store::get().stringMode ? "strings (mono rows)" : "free poly");
}
void aStringMode(int) { store::get().stringMode = !store::get().stringMode; }

void fJamRows(char* o, int c) {
    const uint8_t j = store::get().jamRows;
    snprintf(o, c, "%s", j == 0 ? "off" : (j == 1 ? "bottom row" : "bottom 2 rows"));
}
void aJamRows(int d) {
    auto& g = store::get();
    g.jamRows = (uint8_t)clampT((int)g.jamRows + d, 0, 2);
}

void fOctGlide(char* o, int c) {
    snprintf(o, c, "%s", store::get().octaveGlide ? "sweep (glide)" : "re-strike");
}
void aOctGlide(int) { store::get().octaveGlide = !store::get().octaveGlide; }

void fDroneVoice(char* o, int c) {
    const uint8_t v = store::get().droneVoicing;
    snprintf(o, c, "%s", v == 0 ? "single" : (v == 1 ? "+ octave" : "+ fifth"));
}
void aDroneVoice(int d) {
    auto& g = store::get();
    g.droneVoicing = (uint8_t)clampT((int)g.droneVoicing + d, 0, 2);
}

void fJamMotion(char* o, int c) {
    static const char* kNames[4] = {"sustained", "pulse", "arp", "progression"};
    const uint8_t m = store::get().jamMotion;
    snprintf(o, c, "%s", kNames[m < 4 ? m : 0]);
}
void aJamMotion(int d) {
    auto& g = store::get();
    g.jamMotion = (uint8_t)clampT((int)g.jamMotion + d, 0, 3);
}

void fJamBpm(char* o, int c) { snprintf(o, c, "%d bpm", store::get().jamBpm); }
void aJamBpm(int d) {
    auto& g = store::get();
    g.jamBpm = (uint16_t)clampT((int)g.jamBpm + d * 4, 40, 240);
}

void fJamChord(char* o, int c) { snprintf(o, c, "%d beats", store::get().jamChordBeats); }
void aJamChord(int d) {
    auto& g = store::get();
    g.jamChordBeats = (uint8_t)clampT((int)g.jamChordBeats + d, 1, 8);
}

void fTilt(char* o, int c) { snprintf(o, c, "%s", store::tiltRouteName(store::get().tiltRoute)); }
void aTilt(int d) {
    auto& g = store::get();
    g.tiltRoute = (store::TiltRoute)(((int)g.tiltRoute + d + (int)store::TiltRoute::Count) %
                                     (int)store::TiltRoute::Count);
}

void fTiltDepth(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().tiltDepth * 100)); }
void aTiltDepth(int d) {
    auto& g = store::get();
    g.tiltDepth = clampT(g.tiltDepth + d * 0.05f, 0.f, 1.f);
}

void fTiltB(char* o, int c) { snprintf(o, c, "%s", store::tiltRouteName(store::get().tiltRouteB)); }
void aTiltB(int d) {
    auto& g = store::get();
    g.tiltRouteB = (store::TiltRoute)(((int)g.tiltRouteB + d + (int)store::TiltRoute::Count) %
                                      (int)store::TiltRoute::Count);
}

void fTiltDepthB(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().tiltDepthB * 100)); }
void aTiltDepthB(int d) {
    auto& g = store::get();
    g.tiltDepthB = clampT(g.tiltDepthB + d * 0.05f, 0.f, 1.f);
}

void fTiltCenter(char* o, int c) {
    snprintf(o, c, "%+d (set: , /)", (int)(store::get().tiltCenter * 100));
}
void aTiltCenter(int) {
    // capture how you're holding it RIGHT NOW as "flat" — BOTH axes at once,
    // one gesture. The reading is one-pole smoothed (15%/step), so converge
    // before trusting it; a single poll would capture stale history.
    for (int i = 0; i < 30; ++i) {
        tilt::poll();
        delay(2);
    }
    auto& g = store::get();
    g.tiltCenter = tilt::raw();
    g.tiltCenterB = tilt::rawB();
}

void fPatchReset(char* o, int c) {
    const int slot = store::get().currentPatch;
    snprintf(o, c, "%s%s", store::patchName(slot),
             store::patchHasOverride(slot) ? "* -> factory" : " (factory)");
}
void aPatchReset(int) {
    const int slot = store::get().currentPatch;
    store::clearOverride(slot);
    store::applyPatch(slot);  // reload the factory sound immediately
}

void fBendMs(char* o, int c) { snprintf(o, c, "%d ms", store::get().bendMs); }
void aBendMs(int d) { store::get().bendMs = (uint16_t)clampT((int)store::get().bendMs + d * 50, 50, 1000); }

void fDetune(char* o, int c) { snprintf(o, c, "%d cents", (int)store::get().synth.detuneCents); }
void aDetune(int d) {
    auto& s = store::get().synth;
    s.detuneCents = (float)clampT((int)s.detuneCents + d * 2, 0, 50);
}

void fFilterMode(char* o, int c) {
    snprintf(o, c, "%s", dsp::filterModeName((dsp::FilterMode)store::get().synth.filterMode));
}
void aFilterMode(int d) {
    auto& s = store::get().synth;
    s.filterMode = (uint8_t)(((int)s.filterMode + d + (int)dsp::FilterMode::Count) % (int)dsp::FilterMode::Count);
}

void fRes(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.resonance * 100)); }
void aRes(int d) {
    auto& s = store::get().synth;
    s.resonance = clampT(s.resonance + d * 0.05f, 0.f, 0.95f);
}

// ---- the send-FX rack, now live-editable (and saved per slot like every
// other sound param). Dial the space, fn+shift+letter to keep it.
void fChorus(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.chorusDepth * 100)); }
void aChorus(int d) {
    auto& s = store::get().synth;
    s.chorusDepth = clampT(s.chorusDepth + d * 0.05f, 0.f, 1.f);
}

void fDelaySend(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.delayMix * 100)); }
void aDelaySend(int d) {
    auto& s = store::get().synth;
    s.delayMix = clampT(s.delayMix + d * 0.05f, 0.f, 1.f);
}

void fDelayTime(char* o, int c) {
    auto& s = store::get().synth;
    if (s.delaySync) snprintf(o, c, "%s (synced)", dsp::delaySyncName(s.delaySync));
    else snprintf(o, c, "%d ms", (int)(s.delayTimeS * 1000));
}
void aDelayTime(int d) {
    auto& s = store::get().synth;
    s.delayTimeS = (float)clampT((int)(s.delayTimeS * 1000) + d * 10, 10, 600) / 1000.f;
}

void fDelaySync(char* o, int c) { snprintf(o, c, "%s", dsp::delaySyncName(store::get().synth.delaySync)); }
void aDelaySync(int d) {
    auto& s = store::get().synth;
    s.delaySync = (uint8_t)(((int)s.delaySync + d + dsp::kDelaySyncCount) % dsp::kDelaySyncCount);
}

void fDelayFb(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.delayFb * 100)); }
void aDelayFb(int d) {
    auto& s = store::get().synth;
    s.delayFb = clampT(s.delayFb + d * 0.05f, 0.f, 0.9f);
}

void fReverbSend(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.reverbMix * 100)); }
void aReverbSend(int d) {
    auto& s = store::get().synth;
    s.reverbMix = clampT(s.reverbMix + d * 0.05f, 0.f, 1.f);
}

void fReverbSize(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.reverbSize * 100)); }
void aReverbSize(int d) {
    auto& s = store::get().synth;
    s.reverbSize = clampT(s.reverbSize + d * 0.05f, 0.f, 1.f);
}

// Tap tempo: each press here is a beat — two or more in time set the jam BPM,
// which drives the progression AND the tempo-synced delay.
void fTapTempo(char* o, int c) { snprintf(o, c, "%d bpm  (tap , /)", store::get().jamBpm); }
void aTapTempo(int) {
    static uint32_t lastTap = 0;
    static float avgInt = 0.f;
    const uint32_t now = millis();
    if (lastTap != 0 && now - lastTap < 2000) {
        const float interval = (float)(now - lastTap);
        avgInt = avgInt > 0.f ? avgInt * 0.5f + interval * 0.5f : interval;
        store::get().jamBpm = (uint16_t)clampT((int)(60000.f / avgInt + 0.5f), 40, 240);
    } else {
        avgInt = 0.f;  // first tap of a fresh series
    }
    lastTap = now;
}

void fScopeMode(char* o, int c) {
    snprintf(o, c, "%s", store::get().scopeMode == 0 ? "waveform" : "pitch trail");
}
void aScopeMode(int) {
    auto& g = store::get();
    g.scopeMode = g.scopeMode ? 0 : 1;
}

void fBoot(char* o, int c) { snprintf(o, c, "%s", store::get().bootSound ? "on" : "off"); }
void aBoot(int) { store::get().bootSound = !store::get().bootSound; }

void fIntro(char* o, int c) { snprintf(o, c, "%s", store::get().seenIntro ? "hidden" : "will show"); }
void aIntro(int) { store::get().seenIntro = !store::get().seenIntro; }

void fReset(char* o, int c) { snprintf(o, c, "press , or /"); }
void aReset(int) { store::resetDefaults(); }

// G0 top-button macro: pick the action, how hard it drives, and whether it's
// held (momentary) or tap-to-toggle (latch).
void fTrigAct(char* o, int c) { snprintf(o, c, "%s", store::triggerActionName(store::get().triggerAction)); }
void aTrigAct(int d) {
    auto& g = store::get();
    g.triggerAction = (uint8_t)(((int)g.triggerAction + d + (int)store::TriggerAction::Count) %
                                (int)store::TriggerAction::Count);
}

void fTrigDepth(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().triggerDepth * 100)); }
void aTrigDepth(int d) {
    auto& g = store::get();
    g.triggerDepth = clampT(g.triggerDepth + d * 0.05f, 0.f, 1.f);
}

void fTrigMode(char* o, int c) { snprintf(o, c, "%s", store::get().triggerLatch ? "latch (tap)" : "momentary"); }
void aTrigMode(int) { store::get().triggerLatch = !store::get().triggerLatch; }

// ---- modulation: LFOs, mod-env, and the routing matrix --------------------
void fmtHz(char* o, int c, float hz) {
    const int h = (int)(hz * 100 + 0.5f);
    snprintf(o, c, "%d.%02d Hz", h / 100, h % 100);
}
void adjRate(float& hz, int d) {
    const float step = hz < 2.f ? 0.1f : (hz < 8.f ? 0.5f : 1.f);  // fine low, coarse high
    hz = clampT(hz + d * step, 0.05f, 30.f);
}
void fLfo1Rate(char* o, int c) { fmtHz(o, c, store::get().synth.lfo1RateHz); }
void aLfo1Rate(int d) { adjRate(store::get().synth.lfo1RateHz, d); }
void fLfo1Shape(char* o, int c) {
    snprintf(o, c, "%s", dsp::lfoShapeName((dsp::LfoShape)store::get().synth.lfo1Shape));
}
void aLfo1Shape(int d) {
    auto& s = store::get().synth;
    s.lfo1Shape = (uint8_t)(((int)s.lfo1Shape + d + (int)dsp::LfoShape::Count) % (int)dsp::LfoShape::Count);
}
void fLfo1Sync(char* o, int c) { snprintf(o, c, "%s", dsp::delaySyncName(store::get().synth.lfo1Sync)); }
void aLfo1Sync(int d) {
    auto& s = store::get().synth;
    s.lfo1Sync = (uint8_t)(((int)s.lfo1Sync + d + dsp::kDelaySyncCount) % dsp::kDelaySyncCount);
}
void fLfo2Rate(char* o, int c) { fmtHz(o, c, store::get().synth.lfo2RateHz); }
void aLfo2Rate(int d) { adjRate(store::get().synth.lfo2RateHz, d); }
void fLfo2Shape(char* o, int c) {
    snprintf(o, c, "%s", dsp::lfoShapeName((dsp::LfoShape)store::get().synth.lfo2Shape));
}
void aLfo2Shape(int d) {
    auto& s = store::get().synth;
    s.lfo2Shape = (uint8_t)(((int)s.lfo2Shape + d + (int)dsp::LfoShape::Count) % (int)dsp::LfoShape::Count);
}
void fLfo2Sync(char* o, int c) { snprintf(o, c, "%s", dsp::delaySyncName(store::get().synth.lfo2Sync)); }
void aLfo2Sync(int d) {
    auto& s = store::get().synth;
    s.lfo2Sync = (uint8_t)(((int)s.lfo2Sync + d + dsp::kDelaySyncCount) % dsp::kDelaySyncCount);
}
void fModEnvAtk(char* o, int c) { snprintf(o, c, "%d ms", (int)(store::get().synth.modEnvAtkS * 1000)); }
void aModEnvAtk(int d) {
    auto& s = store::get().synth;
    s.modEnvAtkS = clampT(s.modEnvAtkS + d * 0.005f, 0.001f, 2.f);
}
void fModEnvDec(char* o, int c) { snprintf(o, c, "%d ms", (int)(store::get().synth.modEnvDecS * 1000)); }
void aModEnvDec(int d) {
    auto& s = store::get().synth;
    s.modEnvDecS = clampT(s.modEnvDecS + d * 0.02f, 0.01f, 4.f);
}

// Per-slot src/dest/amount thunks (6 slots). A macro keeps the 18 functions
// honest — each binds to one slot by index. (C++11: no template thunk table.)
#define MOD_SLOT_THUNKS(N)                                                                 \
    void fSlot##N##Src(char* o, int c) {                                                   \
        snprintf(o, c, "%s", dsp::modSourceName((dsp::ModSource)store::get().synth.slots[N].src)); \
    }                                                                                      \
    void aSlot##N##Src(int d) {                                                            \
        auto& s = store::get().synth.slots[N];                                             \
        s.src = (uint8_t)(((int)s.src + d + (int)dsp::ModSource::Count) % (int)dsp::ModSource::Count); \
    }                                                                                      \
    void fSlot##N##Dst(char* o, int c) {                                                   \
        snprintf(o, c, "%s", dsp::modDestName((dsp::ModDest)store::get().synth.slots[N].dest)); \
    }                                                                                      \
    void aSlot##N##Dst(int d) {                                                            \
        auto& s = store::get().synth.slots[N];                                             \
        s.dest = (uint8_t)(((int)s.dest + d + (int)dsp::ModDest::Count) % (int)dsp::ModDest::Count); \
    }                                                                                      \
    void fSlot##N##Amt(char* o, int c) {                                                   \
        snprintf(o, c, "%+d %%", (int)(store::get().synth.slots[N].depth * 100));          \
    }                                                                                      \
    void aSlot##N##Amt(int d) {                                                            \
        auto& s = store::get().synth.slots[N];                                             \
        s.depth = clampT(s.depth + d * 0.05f, -1.f, 1.f);                                  \
    }
MOD_SLOT_THUNKS(0)
MOD_SLOT_THUNKS(1)
MOD_SLOT_THUNKS(2)
MOD_SLOT_THUNKS(3)
MOD_SLOT_THUNKS(4)
MOD_SLOT_THUNKS(5)
#undef MOD_SLOT_THUNKS

const Item kItems[] = {
    // Sections (a null format = a non-selectable header the cursor skips).
    // fn+up/down jumps header-to-header so the deep list stays navigable.
    {"LAYOUT", nullptr, nullptr},
    {"Root key", fRoot, aRoot},
    {"Scale", fScale, aScale},
    {"Row interval", fRowInt, aRowInt},
    {"Glide mode", fGlideMode, aGlideMode},
    {"Allocation", fStringMode, aStringMode},
    {"Octave keys", fOctGlide, aOctGlide},
    {"JAM / BACKING", nullptr, nullptr},
    {"Jam rows (drones)", fJamRows, aJamRows},
    {"Drone voicing", fDroneVoice, aDroneVoice},
    {"Jam motion", fJamMotion, aJamMotion},
    {"Jam tempo", fJamBpm, aJamBpm},
    {"Tap tempo", fTapTempo, aTapTempo},
    {"Chord length", fJamChord, aJamChord},
    {"TILT", nullptr, nullptr},
    {"Tilt f/b route", fTilt, aTilt},
    {"Tilt f/b depth", fTiltDepth, aTiltDepth},
    {"Tilt l/r route", fTiltB, aTiltB},
    {"Tilt l/r depth", fTiltDepthB, aTiltDepthB},
    {"Tilt center", fTiltCenter, aTiltCenter},
    {"SOUND", nullptr, nullptr},
    {"Sound reset", fPatchReset, aPatchReset},
    {"Bend time", fBendMs, aBendMs},
    {"Fat detune", fDetune, aDetune},
    {"Filter mode", fFilterMode, aFilterMode},
    {"Resonance", fRes, aRes},
    {"EFFECTS", nullptr, nullptr},
    {"Chorus", fChorus, aChorus},
    {"Delay send", fDelaySend, aDelaySend},
    {"Delay time", fDelayTime, aDelayTime},
    {"Delay sync", fDelaySync, aDelaySync},
    {"Delay fb", fDelayFb, aDelayFb},
    {"Reverb send", fReverbSend, aReverbSend},
    {"Reverb size", fReverbSize, aReverbSize},
    {"MOD SOURCES", nullptr, nullptr},
    {"LFO1 rate", fLfo1Rate, aLfo1Rate},
    {"LFO1 shape", fLfo1Shape, aLfo1Shape},
    {"LFO1 sync", fLfo1Sync, aLfo1Sync},
    {"LFO2 rate", fLfo2Rate, aLfo2Rate},
    {"LFO2 shape", fLfo2Shape, aLfo2Shape},
    {"LFO2 sync", fLfo2Sync, aLfo2Sync},
    {"Mod env atk", fModEnvAtk, aModEnvAtk},
    {"Mod env dec", fModEnvDec, aModEnvDec},
    {"MOD MATRIX (src>dest)", nullptr, nullptr},
    {"Slot 1 source", fSlot0Src, aSlot0Src},
    {"Slot 1 dest", fSlot0Dst, aSlot0Dst},
    {"Slot 1 amount", fSlot0Amt, aSlot0Amt},
    {"Slot 2 source", fSlot1Src, aSlot1Src},
    {"Slot 2 dest", fSlot1Dst, aSlot1Dst},
    {"Slot 2 amount", fSlot1Amt, aSlot1Amt},
    {"Slot 3 source", fSlot2Src, aSlot2Src},
    {"Slot 3 dest", fSlot2Dst, aSlot2Dst},
    {"Slot 3 amount", fSlot2Amt, aSlot2Amt},
    {"Slot 4 source", fSlot3Src, aSlot3Src},
    {"Slot 4 dest", fSlot3Dst, aSlot3Dst},
    {"Slot 4 amount", fSlot3Amt, aSlot3Amt},
    {"Slot 5 source", fSlot4Src, aSlot4Src},
    {"Slot 5 dest", fSlot4Dst, aSlot4Dst},
    {"Slot 5 amount", fSlot4Amt, aSlot4Amt},
    {"Slot 6 source", fSlot5Src, aSlot5Src},
    {"Slot 6 dest", fSlot5Dst, aSlot5Dst},
    {"Slot 6 amount", fSlot5Amt, aSlot5Amt},
    {"TRIGGER (G0 button)", nullptr, nullptr},
    {"Trigger action", fTrigAct, aTrigAct},
    {"Trigger depth", fTrigDepth, aTrigDepth},
    {"Trigger mode", fTrigMode, aTrigMode},
    {"SYSTEM", nullptr, nullptr},
    {"Display", fScopeMode, aScopeMode},
    {"Boot sound", fBoot, aBoot},
    {"Intro card", fIntro, aIntro},
    {"Reset defaults", fReset, aReset},
};
constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kVisible = 8;

inline bool isHeader(int i) { return kItems[i].format == nullptr; }

// Next selectable row in `dir`, skipping headers, wrapping around the list.
int step(int from, int dir) {
    int i = from;
    for (int n = 0; n < kItemCount; ++n) {
        i = (i + dir + kItemCount) % kItemCount;
        if (!isHeader(i)) return i;
    }
    return from;
}

// First selectable item of the previous/next section (fn+up/down).
int jumpSection(int sel, int dir) {
    int h = sel;  // find this item's header
    while (h >= 0 && !isHeader(h)) --h;
    for (int j = h + dir; j >= 0 && j < kItemCount; j += dir)
        if (isHeader(j)) return step(j, +1);
    // wrapped past an end: land on the first item of the last/first section
    if (dir > 0) return step(0, +1);
    int last = 0;
    for (int j = 0; j < kItemCount; ++j)
        if (isHeader(j)) last = j;
    return step(last, +1);
}

void draw(M5Canvas& c, int sel, int top) {
    c.fillScreen(theme::kBg);
    c.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE", 4, 3);
    c.drawString("GLIDE", 5, 3);
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString("SETTINGS", 44, 3);

    // battery, polled lazily — it's the natural place to check before a set
    static int bat = -1;
    static uint32_t batAt = 0;
    if (bat < 0 || millis() - batAt > 2000) {
        batAt = millis();
        bat = M5.Power.getBatteryLevel();
    }
    if (bat >= 0) {
        char bb[12];
        snprintf(bb, sizeof bb, "BAT %d%%", bat);
        c.setTextDatum(top_right);
        c.setTextColor(bat <= 20 ? (bat <= 10 ? theme::kRed : theme::kAmber) : theme::kDim,
                       theme::kPanel);
        c.drawString(bb, cfg::kScreenW - 4, 3);
        c.setTextDatum(top_left);
    }

    char val[28];
    for (int row = 0; row < kVisible; ++row) {
        const int i = top + row;
        if (i >= kItemCount) break;
        const int y = 18 + row * 13;

        if (isHeader(i)) {  // section label + a divider rule under it
            c.setFont(&fonts::Font0);
            c.setTextColor(theme::kAmber, theme::kBg);
            c.drawString(kItems[i].name, 6, y + 3);
            c.drawFastHLine(6, y + 12, cfg::kScreenW - 16, theme::kLine);
            continue;
        }

        c.setFont(&fonts::Font2);
        const bool isSel = (i == sel);
        if (isSel) c.fillRect(0, y - 1, cfg::kScreenW, 13, theme::kPanel);
        c.setTextColor(isSel ? theme::kAmber : theme::kDim, isSel ? theme::kPanel : theme::kBg);
        c.drawString(kItems[i].name, 8, y);
        kItems[i].format(val, sizeof val);
        c.setTextDatum(top_right);
        c.setTextColor(isSel ? theme::kIdle : theme::kDim, isSel ? theme::kPanel : theme::kBg);
        c.drawString(val, cfg::kScreenW - 8, y);
        c.setTextDatum(top_left);
    }

    // scroll position: 22 items behind an 8-row window deserve a map
    const int trackY = 18, trackH = kVisible * 13 - 2;
    c.drawFastVLine(cfg::kScreenW - 2, trackY, trackH, theme::kLine);
    const int thumbH = trackH * kVisible / kItemCount;
    const int thumbY = trackY + (trackH - thumbH) * sel / (kItemCount - 1);
    c.fillRect(cfg::kScreenW - 3, thumbY, 2, thumbH, theme::kDim);

    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString(";. move  fn+;. section  ,/ change  ` back", 4, 125);
    c.pushSprite(0, 0);
}

}  // namespace

void run(M5Canvas& canvas) {
    // quiet the solo layer; latched drones keep ringing so every edit is
    // heard live against the backing — sound design with your ears on
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::LeadsOff, 0));

    int sel = step(kItemCount - 1, +1);  // first selectable item (skip the header)
    int top = 0;
    uint64_t prev = ~0ULL;  // force first frame to treat keys as already-held

    // wait for the tab press that opened us to clear
    for (;;) {
        const uint32_t now = millis();
        M5Cardputer.update();
        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;

        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };

        if (hit(kExit1) || hit(kExit2)) break;

        const bool fnHeld = (cur >> kFn) & 1ULL;  // fn = jump section to section
        if (fnHeld && hit(kUp)) sel = jumpSection(sel, -1);
        else if (fnHeld && hit(kDown)) sel = jumpSection(sel, +1);
        else {
            if (hit(kUp)) sel = step(sel, -1);
            if (hit(kDown)) sel = step(sel, +1);
        }

        int dir = 0;
        if (!fnHeld) {
            if (hit(kLeft) || hit(kDecAlt)) dir = -1;
            if (hit(kRight) || hit(kIncAlt) || hit(kEnter)) dir = +1;
        }
        if (dir != 0 && !isHeader(sel)) {
            kItems[sel].adjust(dir);
            auto& g = store::get();
            g.synth.tempoBpm = (float)g.jamBpm;  // synced-delay preview
            // Persist immediately, not just on exit: this is a pocket device and
            // people change a setting then flick the hardware power switch — a
            // debounced write would be lost. Settings edits are user-paced (no
            // auto-repeat) and NVS skips unchanged keys, so a full write per
            // press is cheap. (Perform-screen tweaks stay debounced; they can
            // auto-repeat.)
            store::persistNow();
            audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
        }

        if (sel < top) top = sel;
        if (sel >= top + kVisible) top = sel - kVisible + 1;
        if (sel > 0 && isHeader(sel - 1) && top > sel - 1)
            top = sel - 1;  // keep the section header in view atop its first item

        draw(canvas, sel, top);
        store::tick(now);
        looper::tick(now);   // the loop plays through settings, like the drones
        keys::tickBacking(now);  // ...and so does the jam/chord progression
        delay(16);
    }
    store::persistNow();
}

}  // namespace settings
