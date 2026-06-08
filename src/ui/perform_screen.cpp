#include "perform_screen.h"

#include <M5Cardputer.h>
#include <cmath>
#include <cstdio>

#include "../config.h"
#include "../dsp/pitch.h"
#include "../dsp/scales.h"
#include "../io/audio_engine.h"
#include "../io/keys.h"
#include "../io/tilt.h"
#include "../storage/glide_config.h"
#include "hud.h"
#include "settings_screen.h"
#include "theme.h"

namespace perform {

namespace {

// screen regions
constexpr int kStatusH = 12;
constexpr int kScopeY = 13, kScopeH = 82;          // 13..94
constexpr int kScopeMid = kScopeY + kScopeH / 2;   // 54
constexpr int kBottomY = 98;
constexpr int kHintY = 125;

constexpr int kTraceX = 4, kTraceW = 232;
int16_t gPrevTrace[kTraceW];
bool gPrevValid = false;

float gScopeBuf[512];

// Add one tilt axis's contribution into the three mod accumulators. Cutoff is
// additive (octaves), vibrato additive (cents); volume is multiplicative (a
// swell), so two volume routes compound — floored by the caller. oneSided:
// axis A vibrato only leans one way ("forward to sing"); axis B (roll) is
// symmetric, so a roll either direction adds vibrato.
void accumTilt(store::TiltRoute route, float v, float depth, bool oneSided,
               float& cutOct, float& vibCents, float& volMul) {
    switch (route) {
        case store::TiltRoute::Cutoff:  // the wah
            cutOct += v * 2.f * depth;
            break;
        case store::TiltRoute::Vibrato:
            vibCents += (oneSided ? (v > 0.f ? v : 0.f) : fabsf(v)) * 80.f * depth;
            break;
        case store::TiltRoute::Volume:  // the swell pedal
            volMul *= 1.f - depth * 0.9f * (0.5f - v * 0.5f);
            break;
        default:
            break;
    }
}

void applyTilt() {
    auto& c = store::get();
    auto& s = c.synth;
    float cutOct = 0.f, vibCents = 0.f, volMul = 1.f;

    // Guard on enabled+available only — axis A may be Off while roll (B) is
    // active, so the per-route switch (not this guard) handles Off.
    if (c.tiltOn && tilt::available()) {
        tilt::poll();  // updates both axes in one IMU read

        // mod-latch: freeze the per-axis readings on the rising edge so the
        // player can set a timbre and then lay the device flat.
        static bool prevLatched = false;
        static float latchedA = 0.f, latchedB = 0.f;
        const bool latched = keys::tiltLatched();
        if (latched && !prevLatched) {
            latchedA = tilt::value();
            latchedB = tilt::valueB();
        }
        prevLatched = latched;

        const float vA = latched ? latchedA : tilt::value();
        accumTilt(c.tiltRoute, vA, c.tiltDepth, true, cutOct, vibCents, volMul);
        if (c.tiltDual) {
            const float vB = latched ? latchedB : tilt::valueB();
            accumTilt(c.tiltRouteB, vB, c.tiltDepthB, false, cutOct, vibCents, volMul);
        }
        if (cutOct > 3.f) cutOct = 3.f;
        if (cutOct < -3.f) cutOct = -3.f;
        if (volMul < 0.1f) volMul = 0.1f;  // two volume routes can't hit silence
    }
    s.cutoffModOct = cutOct;
    s.vibratoCents = vibCents;
    s.volMod = volMul;
}

void drawStatus(M5Canvas& c) {
    auto& cf = store::get();
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextDatum(top_left);

    if (keys::quickEditActive()) {
        c.fillRect(0, 0, cfg::kScreenW, kStatusH, theme::kAmberDim);
        c.setTextColor(theme::kBg, theme::kAmberDim);
        c.drawString("-- EDIT -- 1-0 param q-p sound [ ] adj", 4, 2);  // 38ch=228px
        return;
    }

    c.fillRect(0, 0, cfg::kScreenW, kStatusH, theme::kPanel);
    char buf[32];
    // the active sound owns the wordmark spot; * = user-saved over factory
    snprintf(buf, sizeof buf, "%s%s", store::patchName(cf.currentPatch),
             store::patchHasOverride(cf.currentPatch) ? "*" : "");
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString(buf, 4, 2);
    c.drawString(buf, 5, 2);  // faux bold

    c.setTextColor(theme::kIdle, theme::kPanel);
    snprintf(buf, sizeof buf, "%s %s%s", dsp::kNoteNames[cf.layout.rootSemis],
             dsp::kScales[cf.layout.scaleIdx].shortName, cf.layout.scaleLock ? "" : "*");
    c.drawString(buf, 56, 2);

    snprintf(buf, sizeof buf, "OCT%d", cf.layout.octave);
    c.drawString(buf, 108, 2);  // ends 132: clears HOLD (right edge 134-158)

    // annunciators, right side
    int x = cfg::kScreenW - 4;
    auto l = audio::lead();
    snprintf(buf, sizeof buf, "vox %d/%d", l.held, cf.synth.voiceCount);
    c.setTextDatum(top_right);
    c.setTextColor(l.held > 0 ? theme::kGreen : theme::kDim, theme::kPanel);
    c.drawString(buf, x, 2);
    x -= 48;
    // TILT annunciator carries mode + latch: dim=off, green=on, amber=mod-
    // latched; a trailing "2" means the roll axis (dual) is live too.
    const bool latched = cf.tiltOn && keys::tiltLatched();
    c.setTextColor(!cf.tiltOn ? theme::kLine : (latched ? theme::kAmber : theme::kGreen),
                   theme::kPanel);
    c.drawString(cf.tiltOn && cf.tiltDual ? "TILT2" : "TILT", x, 2);
    x -= 30;
    if (keys::holdLatched()) {
        c.setTextColor(theme::kAmber, theme::kPanel);
        c.drawString("HOLD", x, 2);
    }
    c.setTextDatum(top_left);
}

// While fn is held the scope yields to a map of the whole layer: all ten
// parameters with live values on the left, all ten sounds on the right.
// No more playing the edit layer blind.
void drawEditPanel(M5Canvas& c) {
    static const char* kShort[10] = {"GLIDE",  "ATTACK", "DECAY",  "SUSTAIN", "RELEASE",
                                     "WAVE",   "CUTOFF", "VOICES", "BEND",    "VOLUME"};
    static const char kParamKeys[11] = "1234567890";
    static const char kPatchKeys[11] = "qwertyuiop";
    auto& cf = store::get();
    const int sel = keys::quickEditParam();

    c.setFont(&fonts::Font0);
    char buf[24], val[10];
    for (int i = 0; i < 10; ++i) {
        const int y = kScopeY + 1 + i * 8;
        // params (left)
        keys::quickParamValue(i, val, sizeof val);
        snprintf(buf, sizeof buf, "%c %-7s %s", kParamKeys[i], kShort[i], val);
        if (i == sel) {
            c.fillRect(2, y - 1, 116, 8, theme::kPanel);
            c.setTextColor(theme::kAmber, theme::kPanel);
        } else {
            c.setTextColor(theme::kDim, theme::kBg);
        }
        c.drawString(buf, 6, y);
        // sounds (right)
        const bool cur = (i == cf.currentPatch);
        snprintf(buf, sizeof buf, "%c %s%s", kPatchKeys[i], store::patchName(i),
                 store::patchHasOverride(i) ? "*" : "");
        if (cur) {
            c.fillRect(126, y - 1, 112, 8, theme::kPanel);
            c.setTextColor(theme::kGreen, theme::kPanel);
        } else {
            c.setTextColor(theme::kDim, theme::kBg);
        }
        c.drawString(buf, 130, y);
    }
}

void drawScope(M5Canvas& c, uint32_t now) {
    if (keys::quickEditActive()) {
        drawEditPanel(c);
        return;
    }
    // graticule
    c.drawFastHLine(kTraceX, kScopeMid, kTraceW, theme::kLine);
    for (int x = kTraceX; x < kTraceX + kTraceW; x += 29)
        c.drawFastVLine(x, kScopeMid - 2, 5, theme::kLine);

    const int n = audio::copyScope(gScopeBuf, 512);
    if (n < kTraceW + 2) return;

    // rising zero-crossing trigger in the first half -> stable trace
    int trig = 0;
    for (int i = 1; i < n - kTraceW; ++i) {
        if (gScopeBuf[i - 1] <= 0.f && gScopeBuf[i] > 0.f) {
            trig = i;
            break;
        }
    }

    const uint16_t glow = theme::scale(theme::kGreen, 80);
    const uint16_t bright = theme::kGreen;
    const float gain = (kScopeH / 2 - 3) * 1.25f;

    // afterglow: last frame's trace lingers like phosphor
    if (gPrevValid) {
        for (int x = 1; x < kTraceW; ++x)
            c.drawLine(kTraceX + x - 1, gPrevTrace[x - 1], kTraceX + x, gPrevTrace[x], glow);
    }
    int prevY = 0;
    for (int x = 0; x < kTraceW; ++x) {
        float s = gScopeBuf[trig + x];
        if (s > 1.2f) s = 1.2f;
        if (s < -1.2f) s = -1.2f;
        int y = kScopeMid - (int)(s * gain);
        if (y < kScopeY) y = kScopeY;
        if (y > kScopeY + kScopeH - 1) y = kScopeY + kScopeH - 1;
        if (x > 0) c.drawLine(kTraceX + x - 1, prevY, kTraceX + x, y, bright);
        gPrevTrace[x] = (int16_t)y;
        prevY = y;
    }
    gPrevValid = true;
}

void drawReadout(M5Canvas& c) {
    auto l = audio::lead();
    if (!l.active) return;

    char name[8];
    int cents;
    dsp::midiToNoteCents(l.pitchMidi, name, sizeof name, cents);

    c.setTextDatum(top_right);
    c.setFont(&fonts::FreeMonoBold12pt7b);
    c.setTextColor(theme::kIdle);
    c.drawString(name, cfg::kScreenW - 30, kScopeY + 3);

    char cb[8];
    snprintf(cb, sizeof cb, "%+03dc", cents);
    c.setFont(&fonts::Font0);
    c.setTextColor(cents == 0 ? theme::kDim : theme::kAmber);
    c.drawString(cb, cfg::kScreenW - 5, kScopeY + 8);
    c.setTextDatum(top_left);

    // glide progress bar — fills as the slide arrives
    if (l.glide01 < 0.99f) {
        const int bx = cfg::kScreenW - 72, by = kScopeY + 24, bw = 66, bh = 3;
        c.drawRect(bx, by, bw, bh, theme::kLine);
        c.fillRect(bx + 1, by + 1, (int)((bw - 2) * l.glide01), bh - 2, theme::kGreen);
    }
}

void drawBottom(M5Canvas& c) {
    auto& cf = store::get();
    auto& s = cf.synth;
    char buf[44];

    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    snprintf(buf, sizeof buf, "GLD %dms  %s  %s", (int)(s.glideS * 1000),
             dsp::waveformName(s.wave),
             s.glideMode == dsp::GlideMode::LegatoOnly ? "legato" : "always");
    c.drawString(buf, 4, kBottomY);
    // compact: worst case "CUT 12.0k VOL 100 BND 12" = 24ch = 144px,
    // safely clear of the mini grid-map starting at x=166
    if (s.cutoffHz >= 1000.f)
        snprintf(buf, sizeof buf, "CUT %.1fk VOL %d BND %d", s.cutoffHz / 1000.f,
                 (int)(s.masterVol * 100), cf.bendRange);
    else
        snprintf(buf, sizeof buf, "CUT %d VOL %d BND %d", (int)s.cutoffHz,
                 (int)(s.masterVol * 100), cf.bendRange);
    c.drawString(buf, 4, kBottomY + 10);

    // mini grid-map: 4x10, top row = string 3. green = held, amber = root
    // degrees (lock on) or in-scale keys (lock off), faint = the rest.
    const int gx = 166, gy = kBottomY, cw = 7, ch = 6;
    const auto& sc = dsp::kScales[cf.layout.scaleIdx];
    const int rowDeg = dsp::rowDegrees(cf.layout);
    for (int str = 0; str < dsp::kGridStrings; ++str) {
        const int y = gy + (3 - str) * ch;
        for (int col = 0; col < dsp::kGridCols; ++col) {
            const int x = gx + col * cw;
            if (keys::noteHeld(str, col)) {
                c.fillRect(x, y, cw - 1, ch - 1, theme::kGreen);
                continue;
            }
            bool mark;
            if (cf.layout.scaleLock) {
                mark = ((str * rowDeg + col) % sc.len) == 0;  // root degrees
            } else {
                mark = dsp::chromaticInScale(cf.layout, str, col);
            }
            c.fillRect(x + 2, y + 2, 2, 2, mark ? theme::kAmber : theme::kLine);
        }
    }
}

void drawHint(M5Canvas& c) {
    c.setFont(&fonts::Font0);
    if (keys::quickEditActive()) {
        c.setTextColor(theme::kAmber, theme::kBg);
        c.drawString("release fn to play", 4, kHintY);
    } else {
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("fn edit  tab setup  shift chrom  ` exit", 2, kHintY);  // 40ch=240px @x2
    }
}

void drawIntro(M5Canvas& c) {
    const int w = 212, h = 92, x = (cfg::kScreenW - w) / 2, y = 20;
    c.fillRoundRect(x, y, w, h, 5, theme::kPanel);
    c.drawRoundRect(x, y, w, h, 5, theme::kAmber);
    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE", x + 8, y + 6);
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString("play  : letter + number keys", x + 8, y + 20);
    c.drawString("slide : new key on the same row", x + 8, y + 31);
    c.drawString("[ ]   : bend    shift : chromatic", x + 8, y + 42);
    c.drawString("fn+q-p: sounds  fn+1-0: edit", x + 8, y + 53);
    c.setTextColor(theme::kGreen, theme::kPanel);
    c.drawString("press any note key to play", x + 8, y + 72);
}

}  // namespace

void run() {
    M5Canvas canvas(&M5Cardputer.Display);
    if (!canvas.createSprite(cfg::kScreenW, cfg::kScreenH)) {
        // No RAM for the frame buffer is a visible failure, not a blank stare.
        M5Cardputer.Display.fillScreen(theme::kBg);
        M5Cardputer.Display.setTextColor(theme::kRed);
        M5Cardputer.Display.drawString("UI ALLOC FAILED", 10, 40);
        for (;;) delay(1000);
    }

    uint32_t introShownAt = millis();

    for (;;) {
        const uint32_t frameStart = millis();
        auto& cf = store::get();

        keys::Actions act = keys::poll(frameStart);

        if (act.exitApp) {
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::AllOff, 0));
            store::persistNow();
            delay(120);  // let the release tails fade
            ESP.restart();
        }
        if (act.openSettings) {
            settings::run(canvas);
            keys::resync();
            gPrevValid = false;
            introShownAt = millis();
            continue;
        }

        if (!cf.seenIntro && (act.gridPressed || millis() - introShownAt > cfg::kIntroMs)) {
            cf.seenIntro = true;
            store::markDirty();
        }

        applyTilt();
        audio::setParams(cf.synth);
        store::tick(frameStart);

        // ---- draw ----------------------------------------------------------
        canvas.fillScreen(theme::kBg);
        drawStatus(canvas);
        drawScope(canvas, frameStart);
        drawReadout(canvas);
        drawBottom(canvas);
        drawHint(canvas);
        hud::draw(canvas, frameStart);
        if (!cf.seenIntro) drawIntro(canvas);
        canvas.pushSprite(0, 0);

        const uint32_t spent = millis() - frameStart;
        if (spent < cfg::kFrameMs) delay(cfg::kFrameMs - spent);
    }
}

}  // namespace perform
