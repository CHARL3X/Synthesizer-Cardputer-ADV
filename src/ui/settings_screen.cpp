#include "settings_screen.h"

#include <esp_random.h>

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/params.h"
#include "../dsp/patches.h"
#include "../dsp/scales.h"
#include "../dsp/sound_gen.h"
#include "../io/audio_engine.h"
#include "../io/keys.h"
#include "../io/looper.h"
#include "../io/sd_store.h"
#include "../io/tilt.h"
#include "../storage/glide_config.h"
#include "help.h"
#include "sd_browser.h"
#include "theme.h"

namespace settings {

namespace {

bool gOpenHelp = false;   // set by the Help item; run() opens the modal (it owns the canvas)
bool gOpenSdLoad = false; // set by "Load from SD"; run() opens the browser modal
int gFlashRow = -1;       // a one-shot row blink confirming an action fired
uint32_t gFlashUntil = 0;
float gMutateAmt = 0.30f; // how far each Mutate roams (session pref; 0..1)
char gLastSaved[24] = ""; // name of the most recent Save to SD (shown in its row)

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
    bool repeatable;  // true = hold ,/ to ramp (continuous numerics); enums/
                      // toggles/actions stay tap-only (left {} = false by default)
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

// Blanket escape hatch: every saved slot back to factory in one tap, so you can
// experiment fearlessly and always return to stock. Settings (layout, tilt,
// jam...) are untouched — only the 10 sound overrides are dropped. The full
// nuke (sounds AND settings) is still the boot-time BKSP factory reset.
void fAllSoundsReset(char* o, int c) {
    int n = 0;
    for (int i = 0; i < dsp::kPatchCount; ++i) n += store::patchHasOverride(i) ? 1 : 0;
    if (n > 0) snprintf(o, c, "%d saved -> stock", n);
    else       snprintf(o, c, "all stock");
}
void aAllSoundsReset(int) {
    for (int i = 0; i < dsp::kPatchCount; ++i) store::clearOverride(i);
    store::applyPatch(store::get().currentPatch);  // reload the now-factory sound live
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

void fHelp(char* o, int c) { snprintf(o, c, "open ->"); }
void aHelp(int) { gOpenHelp = true; }  // run() does the actual modal open

// ---- sound-design starting points -----------------------------------------
void pushLiveSound() {  // apply the working sound to the engine + persist
    auto& g = store::get();
    g.synth.tempoBpm = (float)g.jamBpm;
    audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
    store::persistNow();
}

// --- randomize audition ---------------------------------------------------
// A short preview note fired right after Randomize so the player can keep
// hitting it and listening — no exit-play-and-dig-back-in dance. Scheduled
// non-blocking (like the boot chime): the run() loop fires the glide + release
// as they come due, so neither the UI nor the backing jam ever stalls. A fresh
// Randomize just re-articulates the same id. The note is a lead voice, so it
// auditions the live (just-randomized) lead sound, tails and all.
constexpr uint8_t kPreviewId = 251;  // distinct from the boot chime's 250

// A short phrase, not a single beep: a low note, glides up then back down, a
// couple of re-attacks across the range, then a sustained note left to ring on
// its own tail. Long enough (~2.5s + release) to actually hear the attack
// bloom, filter/LFO/mod-env movement, glide in BOTH directions, and the
// reverb/delay tail — so a roll of Randomize is genuinely auditionable. A fixed
// lick (same notes every roll) makes two random sounds easy to A/B.
struct PrevStep { uint16_t atMs; uint8_t type; float pitch; };
enum { kPrevOn = 0, kPrevReta = 1, kPrevOff = 2 };  // On = re-attack(+glide)
const PrevStep kPhrase[] = {
    {   0, kPrevOn,   52.f},  // low — hear the body
    { 300, kPrevReta, 59.f},  // slide up
    { 640, kPrevReta, 55.f},  // ...and back down: glide reads both ways
    {1000, kPrevOn,   64.f},  // re-attack, mid
    {1380, kPrevReta, 71.f},  // slide up high
    {1860, kPrevOn,   60.f},  // re-attack, settle into a sustain
    {2600, kPrevOff,   0.f},  // release — the patch's own tail rings on
};
constexpr int kPhraseLen = (int)(sizeof kPhrase / sizeof kPhrase[0]);

uint32_t gPreviewT0 = 0;  // phrase start time (0 = idle sentinel)
int gPreviewStep = 0;     // next step to fire

void startPreview() {
    gPreviewT0 = millis();
    if (gPreviewT0 == 0) gPreviewT0 = 1;  // 0 means idle; never let now() land there
    gPreviewStep = 0;  // a fresh roll re-articulates from the top
}

void tickPreview() {
    if (!gPreviewT0) return;
    // Fresh clock, NOT the run loop's cached `now`: that value is captured at the
    // top of the frame, before adjust()/startPreview() runs, so it predates
    // gPreviewT0 by a few ms — dt would underflow and fire the whole phrase
    // (including the final Off) in one silent frame.
    const uint32_t dt = millis() - gPreviewT0;
    // fire every step that has come due (catches up if a frame ran long)
    while (gPreviewStep < kPhraseLen && dt >= kPhrase[gPreviewStep].atMs) {
        const PrevStep& s = kPhrase[gPreviewStep];
        if (s.type == kPrevOff) {
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kPreviewId));
        } else {
            const auto t = s.type == kPrevReta ? dsp::NoteEvent::Retarget : dsp::NoteEvent::On;
            audio::pushEvent(dsp::NoteEvent::make(t, kPreviewId, 0xFF, false, s.pitch));
        }
        ++gPreviewStep;
    }
    if (gPreviewStep >= kPhraseLen) gPreviewT0 = 0;  // done — the tail is the engine's to finish
}

// Build a GenPatch snapshot of the live sound (for mutate / naming / saving).
dsp::GenPatch liveAsGen() {
    const auto& g = store::get();
    dsp::GenPatch gp;
    gp.synth = g.synth;
    gp.tiltRoute = (uint8_t)g.tiltRoute;
    gp.tiltDepth = g.tiltDepth;
    gp.tiltRouteB = (uint8_t)g.tiltRouteB;
    gp.tiltDepthB = g.tiltDepthB;
    return gp;
}

void fInitSound(char* o, int c) { snprintf(o, c, "blank slate ,/"); }
void aInitSound(int) {
    auto& g = store::get();
    store::historyCheckpoint();                // undoable — never trash a sound
    const float vol = g.synth.masterVol;       // keep the player's level
    g.synth = dsp::SynthParams();              // neutral: plain saw, no FX, no mod
    g.synth.masterVol = vol;
    pushLiveSound();
}

// THE button. Roll a whole new patch from a fresh hardware-random seed, land it
// live, and audition it on the spot — hit it until it sings. Nothing is saved
// until you shift-save onto a slot, so a roll can never wreck a sound you kept.
void fRandomize(char* o, int c) { snprintf(o, c, "surprise me ,/"); }
void aRandomize(int) {
    store::historyCheckpoint();
    store::applyGenerated(dsp::generateSound(esp_random()));
    startPreview();
}

// Evolve the CURRENT sound instead of rolling fresh — sculpt toward a vibe. The
// amount knob below sets how far it roams.
void fMutate(char* o, int c) { snprintf(o, c, "evolve this ,/"); }
void aMutate(int) {
    store::historyCheckpoint();
    store::applyGenerated(dsp::mutateSound(liveAsGen(), gMutateAmt, esp_random()));
    startPreview();
}

void fMutAmt(char* o, int c) { snprintf(o, c, "%d %%", (int)(gMutateAmt * 100 + 0.5f)); }
void aMutAmt(int d) { gMutateAmt = clampT(gMutateAmt + d * 0.05f, 0.05f, 1.f); }

// Non-destructive history: step back to a sound you had, or forward again.
void fUndo(char* o, int c) {
    const int d = store::historyUndoDepth();
    if (d > 0) snprintf(o, c, "step back (%d) ,/", d);
    else       snprintf(o, c, "(nothing back)");
}
void aUndo(int) { if (store::historyUndo()) startPreview(); }
void fRedo(char* o, int c) {
    snprintf(o, c, "%s", store::historyCanRedo() ? "step forward ,/" : "(nothing fwd)");
}
void aRedo(int) { if (store::historyRedo()) startPreview(); }

// Save the live sound to the SD library under an auto-generated, evocative name
// (e.g. "warm-haze-3f") derived from the sound itself — so it's reproducibly
// yours. Shows the name in the row on success.
void fSaveSd(char* o, int c) { snprintf(o, c, "%s", gLastSaved[0] ? gLastSaved : "to SD card ,/"); }
void aSaveSd(int) {
    const dsp::GenPatch gp = liveAsGen();
    char name[24];
    dsp::nameForSeed(dsp::patchHash(gp), name, sizeof name);
    store::PatchData pd;
    pd.synth = gp.synth;
    pd.synth.bendCents = 0.f; pd.synth.vibratoCents = 0.f;  // never bake live-mods
    pd.synth.cutoffModOct = 0.f; pd.synth.volMod = 1.f; pd.synth.tempoBpm = 120.f;
    pd.tiltRoute = gp.tiltRoute; pd.tiltDepth = gp.tiltDepth;
    pd.tiltRouteB = gp.tiltRouteB; pd.tiltDepthB = gp.tiltDepthB;
    if (sdstore::save(name, pd)) {
        strncpy(gLastSaved, name, sizeof gLastSaved - 1);
        gLastSaved[sizeof gLastSaved - 1] = '\0';
    } else {
        snprintf(gLastSaved, sizeof gLastSaved, "no SD");
    }
}

void fLoadSd(char* o, int c) { snprintf(o, c, "browse card ,/"); }
void aLoadSd(int) { gOpenSdLoad = true; }  // run() opens the browser modal

// A whole new instrument: regenerate the nine non-anchor slots (w..p) from a
// fresh seed. q stays GLIDE. Deliberate and destructive, like Reset all sounds.
void fReRoll(char* o, int c) { snprintf(o, c, "new bank w..p ,/"); }
void aReRoll(int) {
    store::reRollBank();   // reloads the current slot live
    startPreview();
}

const Item kItems[] = {
    // Sections (a null format = a non-selectable header the cursor skips).
    // fn+up/down jumps header-to-header so the deep list stays navigable.
    {"HELP", nullptr, nullptr},
    {"How to play", fHelp, aHelp},
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
    {"Jam tempo", fJamBpm, aJamBpm, true},
    {"Tap tempo", fTapTempo, aTapTempo},
    {"Chord length", fJamChord, aJamChord},
    {"TILT", nullptr, nullptr},
    {"Tilt f/b route", fTilt, aTilt},
    {"Tilt f/b depth", fTiltDepth, aTiltDepth, true},
    {"Tilt l/r route", fTiltB, aTiltB},
    {"Tilt l/r depth", fTiltDepthB, aTiltDepthB, true},
    {"Tilt center", fTiltCenter, aTiltCenter},
    {"SOUND (roll your own)", nullptr, nullptr},
    {"Randomize", fRandomize, aRandomize},
    {"Mutate", fMutate, aMutate},
    {"Mutate amt", fMutAmt, aMutAmt, true},
    {"Undo", fUndo, aUndo},
    {"Redo", fRedo, aRedo},
    {"Init sound", fInitSound, aInitSound},
    {"Save to SD", fSaveSd, aSaveSd},
    {"Load from SD", fLoadSd, aLoadSd},
    {"Re-roll bank", fReRoll, aReRoll},
    {"Sound reset", fPatchReset, aPatchReset},
    {"Reset all sounds", fAllSoundsReset, aAllSoundsReset},
    {"Bend time", fBendMs, aBendMs, true},
    {"Fat detune", fDetune, aDetune, true},
    {"Filter mode", fFilterMode, aFilterMode},
    {"Resonance", fRes, aRes, true},
    {"EFFECTS", nullptr, nullptr},
    {"Chorus", fChorus, aChorus, true},
    {"Delay send", fDelaySend, aDelaySend, true},
    {"Delay time", fDelayTime, aDelayTime, true},
    {"Delay sync", fDelaySync, aDelaySync},
    {"Delay fb", fDelayFb, aDelayFb, true},
    {"Reverb send", fReverbSend, aReverbSend, true},
    {"Reverb size", fReverbSize, aReverbSize, true},
    {"MOD SOURCES", nullptr, nullptr},
    {"LFO1 rate", fLfo1Rate, aLfo1Rate, true},
    {"LFO1 shape", fLfo1Shape, aLfo1Shape},
    {"LFO1 sync", fLfo1Sync, aLfo1Sync},
    {"LFO2 rate", fLfo2Rate, aLfo2Rate, true},
    {"LFO2 shape", fLfo2Shape, aLfo2Shape},
    {"LFO2 sync", fLfo2Sync, aLfo2Sync},
    {"Mod env atk", fModEnvAtk, aModEnvAtk, true},
    {"Mod env dec", fModEnvDec, aModEnvDec, true},
    {"MOD MATRIX", nullptr, nullptr},
    // Each slot reads as a routing: "Mod N = <source>" then indented "to <dest>"
    // and "amount". An unused slot (source off) collapses to its one line — set a
    // source and its `to`/`amount` rows appear.
    {"Mod 1", fSlot0Src, aSlot0Src},
    {"   to", fSlot0Dst, aSlot0Dst},
    {"   amount", fSlot0Amt, aSlot0Amt, true},
    {"Mod 2", fSlot1Src, aSlot1Src},
    {"   to", fSlot1Dst, aSlot1Dst},
    {"   amount", fSlot1Amt, aSlot1Amt, true},
    {"Mod 3", fSlot2Src, aSlot2Src},
    {"   to", fSlot2Dst, aSlot2Dst},
    {"   amount", fSlot2Amt, aSlot2Amt, true},
    {"Mod 4", fSlot3Src, aSlot3Src},
    {"   to", fSlot3Dst, aSlot3Dst},
    {"   amount", fSlot3Amt, aSlot3Amt, true},
    {"Mod 5", fSlot4Src, aSlot4Src},
    {"   to", fSlot4Dst, aSlot4Dst},
    {"   amount", fSlot4Amt, aSlot4Amt, true},
    {"Mod 6", fSlot5Src, aSlot5Src},
    {"   to", fSlot5Dst, aSlot5Dst},
    {"   amount", fSlot5Amt, aSlot5Amt, true},
    {"TRIGGER (G0 button)", nullptr, nullptr},
    {"Trigger action", fTrigAct, aTrigAct},
    {"Trigger depth", fTrigDepth, aTrigDepth, true},
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

// An unused mod slot collapses to one line: its `to`/`amount` rows are hidden
// until a source is chosen. Map a row's adjust fn back to its slot to decide.
void (*const kSlotDstFn[dsp::kModSlots])(int) = {aSlot0Dst, aSlot1Dst, aSlot2Dst,
                                                 aSlot3Dst, aSlot4Dst, aSlot5Dst};
void (*const kSlotAmtFn[dsp::kModSlots])(int) = {aSlot0Amt, aSlot1Amt, aSlot2Amt,
                                                 aSlot3Amt, aSlot4Amt, aSlot5Amt};
int slotOfSub(void (*adj)(int)) {  // slot index if adj is a dest/amount thunk, else -1
    for (int s = 0; s < dsp::kModSlots; ++s)
        if (adj == kSlotDstFn[s] || adj == kSlotAmtFn[s]) return s;
    return -1;
}
bool isHidden(int i) {
    if (isHeader(i)) return false;  // headers always show
    const int s = slotOfSub(kItems[i].adjust);
    return s >= 0 && store::get().synth.slots[s].src == (uint8_t)dsp::ModSource::None;
}
int buildVisible(int* vis) {  // indices of currently-shown rows (headers + non-collapsed)
    int nv = 0;
    for (int i = 0; i < kItemCount; ++i)
        if (!isHidden(i)) vis[nv++] = i;
    return nv;
}

// "Do something" rows that don't change a visible value — they get a one-shot
// row blink so a tap reads as confirmed (the sound changes but the row text doesn't).
bool isActionRow(int i) {
    if (isHeader(i)) return false;
    const auto a = kItems[i].adjust;
    return a == aInitSound || a == aRandomize || a == aMutate || a == aUndo ||
           a == aRedo || a == aSaveSd || a == aReRoll || a == aPatchReset ||
           a == aAllSoundsReset || a == aReset;
}

// Next selectable row in `dir`, skipping headers AND collapsed rows, wrapping.
int step(int from, int dir) {
    int i = from;
    for (int n = 0; n < kItemCount; ++n) {
        i = (i + dir + kItemCount) % kItemCount;
        if (!isHeader(i) && !isHidden(i)) return i;
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

    int vis[kItemCount];
    const int nv = buildVisible(vis);  // collapsed mod slots drop out of the list

    char val[28];
    for (int row = 0; row < kVisible; ++row) {
        const int vidx = top + row;
        if (vidx >= nv) break;
        const int i = vis[vidx];
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
        const bool flash = (i == gFlashRow) && (millis() < gFlashUntil);  // one-shot confirm
        const uint16_t rowBg = flash ? theme::kAmber : (isSel ? theme::kPanel : theme::kBg);
        const uint16_t nameCol = flash ? theme::kBg : (isSel ? theme::kAmber : theme::kDim);
        const uint16_t valCol = flash ? theme::kBg : (isSel ? theme::kIdle : theme::kDim);
        if (flash || isSel) c.fillRect(0, y - 1, cfg::kScreenW, 13, rowBg);
        c.setTextColor(nameCol, rowBg);
        c.drawString(kItems[i].name, 8, y);
        kItems[i].format(val, sizeof val);
        c.setTextDatum(top_right);
        c.setTextColor(valCol, rowBg);
        c.drawString(val, cfg::kScreenW - 8, y);
        c.setTextDatum(top_left);
    }

    // scroll position map (over the currently-visible rows)
    if (nv > kVisible) {
        int vsel = 0;
        for (int k = 0; k < nv; ++k)
            if (vis[k] == sel) { vsel = k; break; }
        const int trackY = 18, trackH = kVisible * 13 - 2;
        c.drawFastVLine(cfg::kScreenW - 2, trackY, trackH, theme::kLine);
        int thumbH = trackH * kVisible / nv;
        if (thumbH < 4) thumbH = 4;
        const int thumbY = trackY + (trackH - thumbH) * vsel / (nv - 1);
        c.fillRect(cfg::kScreenW - 3, thumbY, 2, thumbH, theme::kDim);
    }

    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString(";. move  ,/ change (hold)  fn jump  ` back", 4, 125);
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

    // hold-to-repeat (DAS/ARR, same feel as the perform-screen keys): nav always
    // repeats (fast scroll of a long list); adjust repeats only on `repeatable`
    // rows (continuous numerics — amounts, rates, %), so enums/toggles don't spin.
    int navRep = 0, adjRep = 0;
    uint32_t navStart = 0, navLast = 0, adjStart = 0, adjLast = 0;

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

        auto held = [&](int cd) { return (cur >> cd) & 1ULL; };
        const bool fnHeld = held(kFn);  // fn = jump section to section

        // --- navigation: ;/. move, fn+;/. jump section, hold to auto-scroll ---
        if (fnHeld && hit(kUp)) sel = jumpSection(sel, -1);
        else if (fnHeld && hit(kDown)) sel = jumpSection(sel, +1);
        else if (hit(kUp)) { sel = step(sel, -1); navRep = -1; navStart = navLast = now; }
        else if (hit(kDown)) { sel = step(sel, +1); navRep = +1; navStart = navLast = now; }
        else if (!fnHeld && navRep == -1 && held(kUp) &&
                 now - navStart >= cfg::kRepeatDelayMs && now - navLast >= cfg::kRepeatRateMs) {
            sel = step(sel, -1); navLast = now;
        } else if (!fnHeld && navRep == +1 && held(kDown) &&
                   now - navStart >= cfg::kRepeatDelayMs && now - navLast >= cfg::kRepeatRateMs) {
            sel = step(sel, +1); navLast = now;
        }
        if (!(held(kUp) || held(kDown)) || fnHeld) navRep = 0;  // released -> disarm

        // --- adjust: ,// change; hold repeats only on `repeatable` numeric rows ---
        int dir = 0;
        if (!fnHeld) {
            if (hit(kLeft) || hit(kDecAlt)) { dir = -1; adjRep = -1; adjStart = adjLast = now; }
            else if (hit(kRight) || hit(kIncAlt) || hit(kEnter)) { dir = +1; adjRep = +1; adjStart = adjLast = now; }
            else if (!isHeader(sel) && kItems[sel].repeatable && adjRep != 0 &&
                     now - adjStart >= cfg::kRepeatDelayMs && now - adjLast >= cfg::kRepeatRateMs) {
                const bool stillDown = adjRep < 0 ? (held(kLeft) || held(kDecAlt))
                                                  : (held(kRight) || held(kIncAlt) || held(kEnter));
                if (stillDown) { dir = adjRep; adjLast = now; }
            }
        }
        const bool anyAdj = held(kLeft) || held(kDecAlt) || held(kRight) || held(kIncAlt) || held(kEnter);
        if (!anyAdj || fnHeld) adjRep = 0;  // released -> disarm

        if (gOpenHelp) {  // the Help item asked to open the cheat-sheet modal
            gOpenHelp = false;
            help::run(canvas);
            prev = ~0ULL;  // treat keys held across the modal as already-down
            draw(canvas, sel, top);
            continue;
        }

        if (gOpenSdLoad) {  // "Load from SD" asked to open the library browser
            gOpenSdLoad = false;
            char nm[24] = {0};
            const bool loaded = sdbrowser::run(canvas, nm, sizeof nm);
            prev = ~0ULL;
            if (loaded) {  // the browser already applied it live + checkpointed
                auto& g = store::get();
                g.synth.tempoBpm = (float)g.jamBpm;
                audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
                store::persistNow();
                startPreview();  // audition the loaded sound on return
            }
            draw(canvas, sel, top);
            continue;
        }

        if (dir != 0 && !isHeader(sel)) {
            if (isActionRow(sel)) { gFlashRow = sel; gFlashUntil = now + 160; }  // confirm the tap
            kItems[sel].adjust(dir);
            auto& g = store::get();
            g.synth.tempoBpm = (float)g.jamBpm;  // synced-delay preview
            // Persist immediately, not just on exit: a pocket device gets its
            // power flicked mid-edit; a debounced write would be lost. NVS skips
            // unchanged keys so a write per step (incl. auto-repeat) is cheap.
            store::persistNow();
            audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
        }

        // scroll in visible-row space (collapsed mod slots aren't counted)
        int vis[kItemCount];
        const int nv = buildVisible(vis);
        int vsel = 0;
        for (int k = 0; k < nv; ++k)
            if (vis[k] == sel) { vsel = k; break; }
        if (vsel < top) top = vsel;
        if (vsel >= top + kVisible) top = vsel - kVisible + 1;
        if (vsel > 0 && isHeader(vis[vsel - 1]) && top > vsel - 1)
            top = vsel - 1;  // keep the section header in view atop its first item
        if (top < 0) top = 0;

        draw(canvas, sel, top);
        store::tick(now);
        looper::tick(now);   // the loop plays through settings, like the drones
        keys::tickBacking(now);  // ...and so does the jam/chord progression
        tickPreview();       // fire a Randomize audition's glide/release when due
        delay(16);
    }
    // never leave an audition note ringing if the player exits mid-preview
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kPreviewId));
    store::persistNow();
}

}  // namespace settings
