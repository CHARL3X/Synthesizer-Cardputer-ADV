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

void applyTilt() {
    auto& c = store::get();
    auto& s = c.synth;
    if (c.tiltOn && c.tiltRoute != store::TiltRoute::Off && tilt::available()) {
        tilt::poll();
        const float v = tilt::value();
        switch (c.tiltRoute) {
            case store::TiltRoute::Cutoff:
                s.cutoffModOct = v * 1.5f;
                s.vibratoCents = 0.f;
                s.volMod = 1.f;
                break;
            case store::TiltRoute::Vibrato:
                s.vibratoCents = (v > 0.f ? v : 0.f) * 60.f;
                s.cutoffModOct = 0.f;
                s.volMod = 1.f;
                break;
            case store::TiltRoute::Volume:
                s.volMod = 0.55f + 0.45f * (v * 0.5f + 0.5f);
                s.cutoffModOct = 0.f;
                s.vibratoCents = 0.f;
                break;
            default:
                break;
        }
    } else {
        s.cutoffModOct = 0.f;
        s.vibratoCents = 0.f;
        s.volMod = 1.f;
    }
}

void drawStatus(M5Canvas& c) {
    auto& cf = store::get();
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextDatum(top_left);

    if (keys::quickEditActive()) {
        c.fillRect(0, 0, cfg::kScreenW, kStatusH, theme::kAmberDim);
        c.setTextColor(theme::kBg, theme::kAmberDim);
        c.drawString("-- EDIT --  1-0 select  [ ] adjust", 4, 2);  // 35ch = 210px
        return;
    }

    c.fillRect(0, 0, cfg::kScreenW, kStatusH, theme::kPanel);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE", 4, 2);
    c.drawString("GLIDE", 5, 2);  // faux bold

    char buf[32];
    c.setTextColor(theme::kIdle, theme::kPanel);
    snprintf(buf, sizeof buf, "%s %s%s", dsp::kNoteNames[cf.layout.rootSemis],
             dsp::kScales[cf.layout.scaleIdx].shortName, cf.layout.scaleLock ? "" : "*");
    c.drawString(buf, 44, 2);

    snprintf(buf, sizeof buf, "OCT%d", cf.layout.octave);
    c.drawString(buf, 100, 2);  // ends at 124: clears HOLD (right edge 134-158)

    // annunciators, right side
    int x = cfg::kScreenW - 4;
    auto l = audio::lead();
    snprintf(buf, sizeof buf, "vox %d/%d", l.held, cf.synth.voiceCount);
    c.setTextDatum(top_right);
    c.setTextColor(l.held > 0 ? theme::kGreen : theme::kDim, theme::kPanel);
    c.drawString(buf, x, 2);
    x -= 48;
    c.setTextColor(cf.tiltOn ? theme::kGreen : theme::kLine, theme::kPanel);
    c.drawString("TILT", x, 2);
    x -= 30;
    if (keys::holdLatched()) {
        c.setTextColor(theme::kAmber, theme::kPanel);
        c.drawString("HOLD", x, 2);
    }
    c.setTextDatum(top_left);
}

void drawScope(M5Canvas& c, uint32_t now) {
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

    const bool dim = keys::quickEditActive();
    const uint16_t glow = theme::scale(theme::kGreen, dim ? 30 : 80);
    const uint16_t bright = dim ? theme::scale(theme::kGreen, 90) : theme::kGreen;
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
    c.drawString("fn    : edit    tab   : settings", x + 8, y + 53);
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
