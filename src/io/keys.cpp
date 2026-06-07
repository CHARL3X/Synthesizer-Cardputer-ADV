#include "keys.h"

#include <M5Cardputer.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/pitch.h"
#include "../dsp/scales.h"
#include "../storage/glide_config.h"
#include "../ui/hud.h"
#include "audio_engine.h"

namespace keys {

namespace {

// ---- key codes: y*14 + x on the physical 4x14 matrix --------------------
constexpr int kCode = 14;
inline int code(int y, int x) { return y * kCode + x; }

constexpr int kKeyExit = 0;        // `
constexpr int kKeyOctDown = 11;    // -
constexpr int kKeyOctUp = 12;      // =
constexpr int kKeyPanic = 13;      // backspace
constexpr int kKeyTab = 14;        // tab
constexpr int kKeyBendDown = 25;   // [
constexpr int kKeyBendUp = 26;     // ]
constexpr int kKeyHold = 27;       // backslash
constexpr int kKeyFn = 28;         // fn
constexpr int kKeyShift = 29;      // shift
constexpr int kKeyScaleLock = 40;  // '
constexpr int kKeyTilt = 41;       // enter
constexpr int kKeyCtrl = 42;       // ctrl  (octave down, left thumb)
constexpr int kKeyOpt = 43;        // opt   (octave up, left thumb)
constexpr int kKeyAlt = 44;        // alt   (sustain, left thumb)
constexpr int kKeySpace = 55;      // space (sustain)

// note grid: code -> (string, col). string 0 = bottom row = lowest.
// Physical x offsets per row give the fretboard stagger:
//   y=0 (1..0) x1..10 | y=1 (q..p) x1..10 | y=2 (a..;) x2..11 | y=3 (z../) x3..12
int8_t gGridString[56];
int8_t gGridCol[56];

void buildGridTables() {
    memset(gGridString, -1, sizeof gGridString);
    memset(gGridCol, -1, sizeof gGridCol);
    const int xStart[4] = {1, 1, 2, 3};   // physical x of col 0, by matrix row y
    const int stringOf[4] = {3, 2, 1, 0}; // matrix row y -> musical string
    for (int y = 0; y < 4; ++y)
        for (int colI = 0; colI < dsp::kGridCols; ++colI) {
            const int cd = code(y, xStart[y] + colI);
            gGridString[cd] = (int8_t)stringOf[y];
            gGridCol[cd] = (int8_t)colI;
        }
}

// ---- held-note bookkeeping ----------------------------------------------
struct HeldNote {
    bool physical = false;   // finger still on the key
    bool sustained = false;  // released but held by sustain/latch
    bool drone = false;      // jam-row latch: rings until tapped again
    int8_t string = -1;
    int8_t col = -1;
    bool chromAtPress = false;  // captured at press: shift never repitches
    float pitch = 0.f;
};
HeldNote gNotes[56];

inline bool sounding(const HeldNote& n) {
    return n.string >= 0 && (n.physical || n.sustained || n.drone);
}

// per-lane press-order stack for hammer-on / pull-off (string mode)
uint8_t gLaneStack[4][16];
int gLaneDepth[4];

uint64_t gPrevMask = 0;
uint32_t gLastPollMs = 0;

float gBendCents = 0.f;
bool gHoldLatch = false;
bool gSustainHeld = false;

// quick-edit
bool gQuickEdit = false;
int gQuickParam = 0;  // 0..9 selected slot
uint32_t gRepeatStart = 0, gRepeatLast = 0;
int gRepeatDir = 0;
bool gRepeatCoarse = false;

inline bool held(uint64_t mask, int cd) { return (mask >> cd) & 1ULL; }

float pitchFor(const HeldNote& n) {
    // drones sit an octave under the lead — bass-pad territory
    return dsp::gridToMidi(store::get().layout, n.string, n.col, n.chromAtPress) -
           (n.drone ? 12.f : 0.f);
}

// ---- lane stack helpers ---------------------------------------------------
void laneRemove(int lane, uint8_t cd) {
    int d = gLaneDepth[lane];
    for (int i = 0; i < d; ++i) {
        if (gLaneStack[lane][i] == cd) {
            for (int j = i; j < d - 1; ++j) gLaneStack[lane][j] = gLaneStack[lane][j + 1];
            gLaneDepth[lane] = d - 1;
            return;
        }
    }
}

int laneTop(int lane) {
    return gLaneDepth[lane] > 0 ? gLaneStack[lane][gLaneDepth[lane] - 1] : -1;
}

void lanePush(int lane, uint8_t cd) {
    if (gLaneDepth[lane] < 16) gLaneStack[lane][gLaneDepth[lane]++] = cd;
}

// ---- note on/off -----------------------------------------------------------
void sendOff(int cd);

void notePress(int cd, bool shiftHeld) {
    auto& cfgr = store::get();
    HeldNote& n = gNotes[cd];

    // jam rows: tap to latch a drone, tap again to let it fade
    if (gGridString[cd] < cfgr.jamRows) {
        if (n.drone && n.string >= 0) {  // second tap: release the drone
            sendOff(cd);
            n.drone = false;
            return;
        }
        n.physical = true;
        n.sustained = false;
        n.drone = true;
        n.string = gGridString[cd];
        n.col = gGridCol[cd];
        n.chromAtPress = shiftHeld || !cfgr.layout.scaleLock;
        n.pitch = pitchFor(n);
        dsp::NoteEvent ev =
            dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd, 0xFF, false, n.pitch);
        ev.drone = true;  // exempt from the cap and from chord-slide stealing
        audio::pushEvent(ev);
        return;
    }

    n.physical = true;
    n.sustained = false;
    n.drone = false;
    n.string = gGridString[cd];
    n.col = gGridCol[cd];
    n.chromAtPress = shiftHeld || !cfgr.layout.scaleLock;
    n.pitch = pitchFor(n);

    bool legato = false;
    if (cfgr.stringMode) {
        // the lane already sings if anything is on its stack
        legato = gLaneDepth[n.string] > 0;
        lanePush(n.string, (uint8_t)cd);
    }
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd, (uint8_t)n.string,
                                          legato, n.pitch));
}

void sendOff(int cd) {
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)cd));
    gNotes[cd].physical = false;
    gNotes[cd].sustained = false;
    gNotes[cd].drone = false;
    gNotes[cd].string = -1;
}

void noteRelease(int cd) {
    auto& cfgr = store::get();
    HeldNote& n = gNotes[cd];
    if (n.string < 0) return;
    n.physical = false;
    if (n.drone) return;  // drones ignore the finger leaving — tap-to-stop

    if (gSustainHeld || gHoldLatch) {
        n.sustained = true;
        if (cfgr.stringMode) laneRemove(n.string, (uint8_t)cd);
        return;
    }

    if (cfgr.stringMode) {
        const int lane = n.string;
        const bool wasOwner = (laneTop(lane) == cd);
        laneRemove(lane, (uint8_t)cd);
        if (wasOwner) {
            const int back = laneTop(lane);
            if (back >= 0) {
                // pull-off: glide the lane's voice back to the still-held key
                HeldNote& b = gNotes[back];
                audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)back,
                                                      (uint8_t)lane, true, b.pitch));
                n.string = -1;
                return;
            }
            sendOff(cd);
            return;
        }
        // non-owner key never owned the sounding voice — just forget it
        n.string = -1;
        return;
    }
    sendOff(cd);
}

void flushSustained() {
    for (int cd = 0; cd < 56; ++cd)
        if (gNotes[cd].sustained && !gNotes[cd].physical) sendOff(cd);
}

void clearAllNotes() {
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::AllOff, 0));
    for (auto& n : gNotes) {
        n.physical = n.sustained = n.drone = false;
        n.string = -1;
    }
    for (int l = 0; l < 4; ++l) gLaneDepth[l] = 0;
    gBendCents = 0.f;
    store::get().synth.bendCents = 0.f;
}

void panic() {
    clearAllNotes();
    hud::show("PANIC", "all notes off", -1.f);
}

// ---- octave / layout controls ----------------------------------------------
void retuneHeldNotes() {
    for (int cd = 0; cd < 56; ++cd) {
        HeldNote& n = gNotes[cd];
        if (!sounding(n)) continue;
        n.pitch = pitchFor(n);
        audio::pushEvent(
            dsp::NoteEvent::make(dsp::NoteEvent::Retarget, (uint8_t)cd, (uint8_t)n.string,
                                 false, n.pitch));
    }
}

void octaveShift(int dir) {
    auto& cfgr = store::get();
    const int next = cfgr.layout.octave + dir;
    if (next < 1 || next > 7) {
        hud::showError("OCTAVE", dir > 0 ? "ceiling (7)" : "floor (1)");
        return;
    }
    cfgr.layout.octave = (int8_t)next;
    if (cfgr.octaveGlide) {
        retuneHeldNotes();  // Retarget glides: a fat-finger becomes a sweep
    } else {
        // instant: re-strike held notes at the new octave
        for (int cd = 0; cd < 56; ++cd) {
            HeldNote& n = gNotes[cd];
            if (!sounding(n)) continue;
            n.pitch = pitchFor(n);
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)cd));
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd,
                                                  (uint8_t)n.string, false, n.pitch));
        }
    }
    char v[16];
    snprintf(v, sizeof v, "%d", next);
    hud::show("OCTAVE", v, (next - 1) / 6.f);
    store::markDirty();
}

// ---- quick-edit layer (hold fn, top row selects, [ ] adjusts) ---------------
struct QuickParam {
    const char* name;
};
const QuickParam kQuick[10] = {
    {"GLIDE"},   {"ATTACK"}, {"DECAY"},      {"SUSTAIN"}, {"RELEASE"},
    {"WAVEFORM"}, {"CUTOFF"}, {"VOICES"},     {"BEND RANGE"}, {"VOLUME"},
};

void hudQuickValue(int idx) {
    auto& cfgr = store::get();
    auto& s = cfgr.synth;
    char v[20];
    float fill = -1.f;
    switch (idx) {
        case 0: snprintf(v, sizeof v, "%d ms", (int)(s.glideS * 1000)); fill = s.glideS / 2.f; break;
        case 1: snprintf(v, sizeof v, "%d ms", (int)(s.attackS * 1000)); fill = s.attackS / 2.f; break;
        case 2: snprintf(v, sizeof v, "%d ms", (int)(s.decayS * 1000)); fill = s.decayS / 2.f; break;
        case 3: snprintf(v, sizeof v, "%d %%", (int)(s.sustain * 100)); fill = s.sustain; break;
        case 4: snprintf(v, sizeof v, "%d ms", (int)(s.releaseS * 1000)); fill = s.releaseS / 3.f; break;
        case 5: snprintf(v, sizeof v, "%s", dsp::waveformName(s.wave)); break;
        case 6:
            if (s.cutoffHz >= 1000.f) snprintf(v, sizeof v, "%.1f kHz", s.cutoffHz / 1000.f);
            else snprintf(v, sizeof v, "%d Hz", (int)s.cutoffHz);
            fill = log10f(s.cutoffHz / 80.f) / log10f(12000.f / 80.f);
            break;
        case 7: snprintf(v, sizeof v, "%d", s.voiceCount); fill = s.voiceCount / 8.f; break;
        case 8: snprintf(v, sizeof v, "%d st", cfgr.bendRange); fill = cfgr.bendRange / 12.f; break;
        case 9: snprintf(v, sizeof v, "%d %%", (int)(s.masterVol * 100)); fill = s.masterVol; break;
        default: v[0] = '\0'; break;
    }
    hud::show(kQuick[idx].name, v, fill);
}

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void adjustQuickParam(int idx, int dir, bool coarse) {
    auto& cfgr = store::get();
    auto& s = cfgr.synth;
    switch (idx) {
        case 0: s.glideS = clampT(s.glideS + dir * (coarse ? 0.1f : 0.01f), 0.f, 2.f); break;
        case 1: s.attackS = clampT(s.attackS + dir * (coarse ? 0.05f : 0.005f), 0.f, 2.f); break;
        case 2: s.decayS = clampT(s.decayS + dir * (coarse ? 0.1f : 0.01f), 0.001f, 2.f); break;
        case 3: s.sustain = clampT(s.sustain + dir * (coarse ? 0.2f : 0.05f), 0.f, 1.f); break;
        case 4: s.releaseS = clampT(s.releaseS + dir * (coarse ? 0.1f : 0.01f), 0.001f, 3.f); break;
        case 5: {
            int w = ((int)s.wave + dir + (int)dsp::Waveform::Count) % (int)dsp::Waveform::Count;
            s.wave = (dsp::Waveform)w;
            break;
        }
        case 6: s.cutoffHz = clampT(s.cutoffHz * (dir > 0 ? (coarse ? 1.3f : 1.06f)
                                                          : (coarse ? 1.f / 1.3f : 1.f / 1.06f)),
                                    80.f, 12000.f); break;
        case 7: s.voiceCount = (uint8_t)clampT((int)s.voiceCount + dir, 1, (int)dsp::kMaxVoices); break;
        case 8: cfgr.bendRange = (uint8_t)clampT((int)cfgr.bendRange + dir, 1, 12); break;
        case 9: s.masterVol = clampT(s.masterVol + dir * (coarse ? 0.2f : 0.05f), 0.f, 1.f); break;
        default: break;
    }
    store::markDirty();
    hudQuickValue(idx);
}

}  // namespace

void begin() {
    buildGridTables();
    memset(gLaneDepth, 0, sizeof gLaneDepth);
}

void resync() {
    // a blocking screen consumed key events; rebuild edge state from scratch
    M5Cardputer.update();
    gPrevMask = 0;
    for (const auto& p : M5Cardputer.Keyboard.keyList()) gPrevMask |= 1ULL << code(p.y, p.x);
    clearAllNotes();
}

Actions poll(uint32_t nowMs) {
    Actions act;
    auto& cfgr = store::get();

    M5Cardputer.update();
    uint64_t cur = 0;
    for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << code(p.y, p.x);

    const uint64_t pressed = cur & ~gPrevMask;
    const uint64_t released = gPrevMask & ~cur;
    const float dtMs = gLastPollMs ? (float)(nowMs - gLastPollMs) : 16.f;
    gLastPollMs = nowMs;

    const bool shiftHeld = held(cur, kKeyShift);
    gSustainHeld = held(cur, kKeySpace) || held(cur, kKeyAlt);
    const bool wasQuickEdit = gQuickEdit;
    gQuickEdit = held(cur, kKeyFn);
    if (wasQuickEdit && !gQuickEdit) store::markDirty();  // persist on fn release

    // ---- presses ---------------------------------------------------------
    for (int cd = 0; cd < 56 && pressed; ++cd) {
        if (!held(pressed, cd)) continue;

        if (gGridString[cd] >= 0) {
            if (gQuickEdit) {
                if (gGridString[cd] == 3) {
                    // top row selects the quick-edit parameter
                    gQuickParam = gGridCol[cd];
                    hudQuickValue(gQuickParam);
                } else if (gGridString[cd] == 2) {
                    // q..p row = the ten sounds; +shift saves over the slot
                    const int slot = gGridCol[cd];
                    char v[20];
                    if (shiftHeld) {
                        if (store::savePatch(slot)) {
                            snprintf(v, sizeof v, "saved -> %s", store::patchName(slot));
                            hud::show("SOUND", v, -1.f);
                        } else {
                            hud::showError("SOUND", "save failed");
                        }
                    } else {
                        clearAllNotes();  // a new instrument starts clean
                        store::applyPatch(slot);
                        audio::setParams(store::get().synth);
                        snprintf(v, sizeof v, "%s%s", store::patchName(slot),
                                 store::patchHasOverride(slot) ? "*" : "");
                        hud::show("SOUND", v, (slot + 1) / 10.f);
                    }
                }
                continue;  // grid is muted while editing
            }
            act.gridPressed = true;
            notePress(cd, shiftHeld);
            continue;
        }

        switch (cd) {
            case kKeyExit:
                act.exitApp = true;
                break;
            case kKeyTab:
                act.openSettings = true;
                break;
            case kKeyPanic:
                panic();
                break;
            case kKeyOctDown:
                if (gQuickEdit) adjustQuickParam(gQuickParam, -1, true);
                else octaveShift(-1);
                break;
            case kKeyOctUp:
                if (gQuickEdit) adjustQuickParam(gQuickParam, +1, true);
                else octaveShift(+1);
                break;
            case kKeyCtrl:
                octaveShift(-1);
                break;
            case kKeyOpt:
                octaveShift(+1);
                break;
            case kKeyBendDown:
                if (gQuickEdit) {
                    adjustQuickParam(gQuickParam, -1, false);
                    gRepeatDir = -1;
                    gRepeatCoarse = false;
                    gRepeatStart = gRepeatLast = nowMs;
                }
                break;
            case kKeyBendUp:
                if (gQuickEdit) {
                    adjustQuickParam(gQuickParam, +1, false);
                    gRepeatDir = +1;
                    gRepeatCoarse = false;
                    gRepeatStart = gRepeatLast = nowMs;
                }
                break;
            case kKeyHold:
                gHoldLatch = !gHoldLatch;
                if (!gHoldLatch) flushSustained();
                hud::show("HOLD", gHoldLatch ? "latched" : "off", -1.f);
                break;
            case kKeyScaleLock:
                cfgr.layout.scaleLock = !cfgr.layout.scaleLock;
                hud::show("SCALE LOCK", cfgr.layout.scaleLock ? "on (degrees)" : "off (chromatic)",
                          -1.f);
                store::markDirty();
                break;
            case kKeyTilt: {
                cfgr.tiltOn = !cfgr.tiltOn;
                // never a dead toggle: turning tilt ON with no route (or no
                // depth) self-heals to a sensible wah instead of doing
                // nothing until a settings trip
                if (cfgr.tiltOn) {
                    if (cfgr.tiltRoute == store::TiltRoute::Off)
                        cfgr.tiltRoute = store::TiltRoute::Cutoff;
                    if (cfgr.tiltDepth < 0.05f) cfgr.tiltDepth = 0.6f;
                }
                char v[20];
                if (cfgr.tiltOn)
                    snprintf(v, sizeof v, "%s %d%%", store::tiltRouteName(cfgr.tiltRoute),
                             (int)(cfgr.tiltDepth * 100));
                else
                    snprintf(v, sizeof v, "off");
                hud::show("TILT", v, cfgr.tiltOn ? cfgr.tiltDepth : -1.f);
                store::markDirty();
                break;
            }
            default:
                break;
        }
    }

    // ---- releases ----------------------------------------------------------
    for (int cd = 0; cd < 56 && released; ++cd) {
        if (!held(released, cd)) continue;
        if (gGridString[cd] >= 0) {
            if (gNotes[cd].string >= 0) noteRelease(cd);
            continue;
        }
        if (cd == kKeyBendDown || cd == kKeyBendUp) gRepeatDir = 0;
    }

    // sustain pedal lifted -> let go of everything not physically held
    static bool prevSustain = false;
    if (prevSustain && !gSustainHeld && !gHoldLatch) flushSustained();
    prevSustain = gSustainHeld;

    // ---- quick-edit auto-repeat ---------------------------------------------
    if (gQuickEdit && gRepeatDir != 0 &&
        (held(cur, kKeyBendDown) || held(cur, kKeyBendUp))) {
        if (nowMs - gRepeatStart >= cfg::kRepeatDelayMs &&
            nowMs - gRepeatLast >= cfg::kRepeatRateMs) {
            gRepeatLast = nowMs;
            adjustQuickParam(gQuickParam, gRepeatDir, gRepeatCoarse);
        }
    } else if (!gQuickEdit) {
        gRepeatDir = 0;
    }

    // ---- bend ramp (level-sensitive: sampled every frame) --------------------
    float bendTarget = 0.f;
    if (!gQuickEdit) {
        if (held(cur, kKeyBendUp)) bendTarget += cfgr.bendRange * 100.f;
        if (held(cur, kKeyBendDown)) bendTarget -= cfgr.bendRange * 100.f;
    }
    const float rate = (cfgr.bendRange * 100.f) / (float)cfgr.bendMs;  // cents per ms
    const float step = rate * dtMs;
    if (gBendCents < bendTarget) {
        gBendCents += step;
        if (gBendCents > bendTarget) gBendCents = bendTarget;
    } else if (gBendCents > bendTarget) {
        gBendCents -= step;
        if (gBendCents < bendTarget) gBendCents = bendTarget;
    }
    cfgr.synth.bendCents = gBendCents;

    gPrevMask = cur;
    return act;
}

// Known limit: if the synth steals a sustained note's voice at the cap
// (free mode), the key layer isn't told — that key stays lit on the grid
// map until its Off/panic. Display-only; audio is always correct.
bool noteHeld(int string, int col) {
    for (int cd = 0; cd < 56; ++cd) {
        const HeldNote& n = gNotes[cd];
        if (sounding(n) && n.string == string && n.col == col) return true;
    }
    return false;
}

bool quickEditActive() { return gQuickEdit; }
int quickEditParam() { return gQuickParam; }
float bendCentsNow() { return gBendCents; }
bool holdLatched() { return gHoldLatch; }
bool sustainActive() { return gSustainHeld || gHoldLatch; }

}  // namespace keys
