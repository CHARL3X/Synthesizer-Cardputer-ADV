#include "settings_screen.h"

#include <cstdio>

#include "../config.h"
#include "../dsp/params.h"
#include "../dsp/scales.h"
#include "../io/audio_engine.h"
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
    const uint8_t m = store::get().jamMotion;
    snprintf(o, c, "%s", m == 0 ? "sustained" : (m == 1 ? "pulse" : "arp"));
}
void aJamMotion(int d) {
    auto& g = store::get();
    g.jamMotion = (uint8_t)clampT((int)g.jamMotion + d, 0, 2);
}

void fJamBpm(char* o, int c) { snprintf(o, c, "%d bpm", store::get().jamBpm); }
void aJamBpm(int d) {
    auto& g = store::get();
    g.jamBpm = (uint16_t)clampT((int)g.jamBpm + d * 4, 40, 240);
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

void fRes(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.resonance * 100)); }
void aRes(int d) {
    auto& s = store::get().synth;
    s.resonance = clampT(s.resonance + d * 0.05f, 0.f, 0.95f);
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

const Item kItems[] = {
    {"Root key", fRoot, aRoot},
    {"Scale", fScale, aScale},
    {"Row interval", fRowInt, aRowInt},
    {"Glide mode", fGlideMode, aGlideMode},
    {"Allocation", fStringMode, aStringMode},
    {"Jam rows (drones)", fJamRows, aJamRows},
    {"Drone voicing", fDroneVoice, aDroneVoice},
    {"Jam motion", fJamMotion, aJamMotion},
    {"Jam tempo", fJamBpm, aJamBpm},
    {"Octave keys", fOctGlide, aOctGlide},
    {"Tilt f/b route", fTilt, aTilt},
    {"Tilt f/b depth", fTiltDepth, aTiltDepth},
    {"Tilt l/r route", fTiltB, aTiltB},
    {"Tilt l/r depth", fTiltDepthB, aTiltDepthB},
    {"Tilt center", fTiltCenter, aTiltCenter},
    {"Sound reset", fPatchReset, aPatchReset},
    {"Bend time", fBendMs, aBendMs},
    {"Fat detune", fDetune, aDetune},
    {"Resonance", fRes, aRes},
    {"Display", fScopeMode, aScopeMode},
    {"Boot sound", fBoot, aBoot},
    {"Intro card", fIntro, aIntro},
    {"Reset defaults", fReset, aReset},
};
constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kVisible = 8;

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

    c.setFont(&fonts::Font2);
    char val[28];
    for (int row = 0; row < kVisible; ++row) {
        const int i = top + row;
        if (i >= kItemCount) break;
        const int y = 18 + row * 13;
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

    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("; . move    , / or [ ] change    ` tab back", 4, 125);
    c.pushSprite(0, 0);
}

}  // namespace

void run(M5Canvas& canvas) {
    // quiet the solo layer; latched drones keep ringing so every edit is
    // heard live against the backing — sound design with your ears on
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::LeadsOff, 0));

    int sel = 0, top = 0;
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
        if (hit(kUp)) sel = (sel + kItemCount - 1) % kItemCount;
        if (hit(kDown)) sel = (sel + 1) % kItemCount;
        int dir = 0;
        if (hit(kLeft) || hit(kDecAlt)) dir = -1;
        if (hit(kRight) || hit(kIncAlt) || hit(kEnter)) dir = +1;
        if (dir != 0) {
            kItems[sel].adjust(dir);
            store::markDirty();
            audio::setParams(store::get().synth);
        }

        if (sel < top) top = sel;
        if (sel >= top + kVisible) top = sel - kVisible + 1;

        draw(canvas, sel, top);
        store::tick(now);
        delay(16);
    }
    store::persistNow();
}

}  // namespace settings
