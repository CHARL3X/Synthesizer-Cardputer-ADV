#include "perform_screen.h"

#include <M5Cardputer.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/pitch.h"
#include "../dsp/scales.h"
#include "../io/audio_engine.h"
#include "../io/keys.h"
#include "../io/led.h"
#include "../io/looper.h"
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

// pitch trail: lead pitch sampled once per frame, scrolling right-to-left
// (~7.5 s across the screen at 30 fps). NAN = silence gap.
float gTrail[kTraceW];
uint8_t gTrailBend[kTraceW];  // frames where the bend keys were deforming it
uint8_t gTrailLev[kTraceW];   // lead envelope level per frame (0..255): drives the
                              // trace's brightness + glow, so loudness is visible
uint8_t gTrailBri[kTraceW];   // lead filter openness per frame (0..255): green (dark)
                              // -> gold (open), so timbre/filter motion is visible
int gTrailPos = 0;
bool gTrailInit = false;
float gTrailCenter = 69.f;  // view center in MIDI, follows the lead slowly
bool gTrailCenterSet = false;
constexpr float kVibVisGain = 2.5f;  // visual exaggeration of the vibrato wobble

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
    float rawA = 0.f, rawB = 0.f;  // raw axes, exposed as mod-matrix sources

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

        rawA = latched ? latchedA : tilt::value();
        rawB = latched ? latchedB : tilt::valueB();  // read both, even if dual is off,
                                                      // so the matrix can route roll
        accumTilt(c.tiltRoute, rawA, c.tiltDepth, true, cutOct, vibCents, volMul);
        if (c.tiltDual)
            accumTilt(c.tiltRouteB, rawB, c.tiltDepthB, false, cutOct, vibCents, volMul);
        if (cutOct > 3.f) cutOct = 3.f;
        if (cutOct < -3.f) cutOct = -3.f;
        if (volMul < 0.1f) volMul = 0.1f;  // two volume routes can't hit silence
    }
    s.cutoffModOct = cutOct;
    s.vibratoCents = vibCents;
    s.volMod = volMul;
    s.tiltAVal = rawA;  // matrix sources (separate from the hardwired routes above)
    s.tiltBVal = rawB;
    s.tempoBpm = (float)c.jamBpm;  // publish the jam tempo for the synced delay
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The G0 trigger macro. Engaged is decided by the caller (momentary vs latch);
// here we just drive the chosen action into the live param copies by `depth`
// (0..1). Filter routes move both lead and backing; pitch/drive touch the lead
// only, so the backing stays a steady bed (matching the bend/tilt rule).
void applyTrigger(dsp::SynthParams& lead, dsp::SynthParams& back, uint8_t action, float depth) {
    switch ((store::TriggerAction)action) {
        case store::TriggerAction::Muffle: {  // lowpass dives shut — the classic throw
            const float oct = -5.f * depth;
            lead.cutoffModOct += oct;
            back.cutoffHz = clampf(back.cutoffHz * exp2f(oct), 60.f, 12000.f);
            break;
        }
        case store::TriggerAction::Brighten: {  // opposite throw: a resonant lift
            // Just raising the corner is nearly inaudible when the patch is
            // already open (a saw barely has top harmonics, and the 1 W speaker
            // barely reproduces them). So push the cutoff up AND sweep resonance
            // up with it — a singing peak as it opens is what you actually hear.
            const float oct = 5.f * depth;
            lead.cutoffModOct += oct;
            lead.resonance = clampf(lead.resonance + depth * 0.6f, 0.f, 0.92f);
            back.cutoffHz = clampf(back.cutoffHz * exp2f(oct), 60.f, 15000.f);
            back.resonance = clampf(back.resonance + depth * 0.6f, 0.f, 0.92f);
            break;
        }
        case store::TriggerAction::PitchDive:   // dive-bomb the lead, up to ~2 oct
            lead.bendCents -= depth * 2400.f;
            break;
        case store::TriggerAction::Drive:       // shove the lead into the soft clipper
            lead.drive = clampf(lead.drive + depth * 6.f, 1.f, 8.f);
            break;
        default: break;
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

    // solo/backing split engaged: the backing is held on its own sound while
    // this patch/octave is the solo. Small amber "LK" so you know why a sound
    // switch isn't changing the bed.
    if (store::backingLocked()) {
        c.setTextColor(theme::kAmber, theme::kPanel);
        c.drawString("LK", 138, 2);
    }

    // annunciators, right side
    int x = cfg::kScreenW - 4;
    auto l = audio::lead();
    // leads vs the cap — what the cap actually governs. Latched drones live
    // outside it (their count sits by the grid map); no more "vox 5/4".
    snprintf(buf, sizeof buf, "vox %d/%d", l.leads, cf.synth.voiceCount);
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
        // params (left): a value gauge behind each row — the whole sound
        // reads at a glance, like channel strips on a mixer
        keys::quickParamValue(i, val, sizeof val);
        snprintf(buf, sizeof buf, "%c %-7s %s", kParamKeys[i], kShort[i], val);
        if (i == sel) c.fillRect(2, y - 1, 116, 8, theme::kPanel);
        const float fill = keys::quickParamFill(i);
        if (fill >= 0.f) {
            const int bw = (int)(114.f * (fill > 1.f ? 1.f : fill));
            if (bw > 0)
                c.fillRect(3, y - 1, bw, 8,
                           i == sel ? theme::scale(theme::kAmber, 70) : theme::kLine);
        }
        c.setTextColor(i == sel ? theme::kAmber : theme::kDim);  // bg shows through
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

// The pitch trail: the lead voice's pitch drawn over time. On an instrument
// whose whole point is the space between the notes, this is the oscilloscope
// for the *other* axis — every glide, hammer-on and bend is a visible curve.
void drawPitchTrail(M5Canvas& c) {
    if (!gTrailInit) {
        for (int i = 0; i < kTraceW; ++i) gTrail[i] = NAN;
        memset(gTrailBend, 0, sizeof gTrailBend);
        memset(gTrailLev, 0, sizeof gTrailLev);
        memset(gTrailBri, 0, sizeof gTrailBri);
        gTrailPos = 0;
        gTrailCenterSet = false;
        gTrailInit = true;
    }

    auto l = audio::lead();
    const float base = l.active ? l.pitchMidi : NAN;
    float v = base;
    if (l.active) {
        if (!gTrailCenterSet) {
            gTrailCenter = base;
            gTrailCenterSet = true;
        }
        // the view drifts after the lead; snaps faster if it ran off-screen
        const float d = base - gTrailCenter;
        gTrailCenter += d * (fabsf(d) > 12.f ? 0.3f : 0.05f);
        // vibrato shimmer: a display-only LFO at the synth's ~5.5 Hz vibrato
        // rate, scaled by the live vibrato depth (tilt + patch), so the trail
        // visibly wobbles when tilt-vibrato is on. The note readout stays on
        // `base` (no wobble) so the name doesn't flicker.
        static float vibPhase = 0.f;
        vibPhase += 6.2831853f * 5.5f / 30.f;
        if (vibPhase > 6.2831853f) vibPhase -= 6.2831853f;
        const auto& s = store::get().synth;
        const float depthCents = s.vibratoCents + s.autoVibCents;
        v = base + sinf(vibPhase) * depthCents * 0.01f * kVibVisGain;
    }
    gTrail[gTrailPos] = v;
    gTrailBend[gTrailPos] = fabsf(keys::bendCentsNow()) > 2.f ? 1 : 0;
    gTrailLev[gTrailPos] = (uint8_t)(l.active ? clampf(l.level * 180.f, 0.f, 255.f) : 0.f);
    gTrailBri[gTrailPos] = (uint8_t)(l.active ? clampf(l.brightness * 255.f, 0.f, 255.f) : 0.f);
    gTrailPos = (gTrailPos + 1) % kTraceW;

    // 30-semitone window: ~2.5 octaves visible
    const float pxPerSemi = (kScopeH - 4) / 30.f;
    const int yTop = kScopeY, yBot = kScopeY + kScopeH - 1;
    auto yOf = [&](float midi) {
        int y = kScopeMid - (int)((midi - gTrailCenter) * pxPerSemi + 0.5f);
        return y < yTop ? yTop : (y > yBot ? yBot : y);
    };

    // gridlines at every root pitch — the fret markers of the time axis
    auto& cf = store::get();
    const int root = cf.layout.rootSemis;
    char nm[8];
    c.setFont(&fonts::Font0);
    for (int m = (int)gTrailCenter - 16; m <= (int)gTrailCenter + 16; ++m) {
        if (((m % 12) + 12) % 12 != root) continue;
        const int y = kScopeMid - (int)((m - gTrailCenter) * pxPerSemi + 0.5f);
        if (y < yTop + 2 || y > yBot - 2) continue;
        c.drawFastHLine(kTraceX, y, kTraceW, theme::kLine);
        if (y + 10 <= yBot) {  // label sits under its line, inside the scope
            snprintf(nm, sizeof nm, "%s%d", dsp::kNoteNames[root], m / 12 - 1);
            c.setTextColor(theme::kDim, theme::kBg);
            c.drawString(nm, kTraceX + 2, y + 2);
        }
    }

    // the trace, oldest at the left edge; segments drawn while the bend keys
    // were pulling the pitch go amber — earned notes, marked. Phosphor decay:
    // the line fades from dim at the old (left) end to full at the new end, and
    // the leading ~18px blend white-hot — it reads like a real CRT trace
    // decaying behind the beam instead of a flat painted line.
    int prevY = 0;
    bool prevValid = false;
    for (int x = 0; x < kTraceW; ++x) {
        const int idx = (gTrailPos + x) % kTraceW;
        const float m = gTrail[idx];
        if (m != m) {  // NAN: a rest — break the line
            prevValid = false;
            continue;
        }
        const int y = yOf(m);
        // hue = timbre: a dark filter reads green, an open one warms toward gold
        // (capped ~60% so it stays in the green-gold family). Bend overrides to
        // full amber, since the deflection + colour together mark a pulled note.
        const uint16_t baseCol = gTrailBend[idx]
                                     ? theme::kAmber
                                     : theme::blend(theme::kGreen, theme::kAmber,
                                                    (uint8_t)((uint32_t)gTrailBri[idx] * 153 / 255));
        const uint8_t age = (uint8_t)(40 + (uint32_t)x * 215 / kTraceW);  // old->new
        const uint8_t lev = gTrailLev[idx];                              // loudness here
        // loudness shapes brightness on top of the age fade: a loud note glows,
        // a releasing/quiet one dims even when recent — the envelope made visible.
        const uint8_t bright = (uint8_t)((uint32_t)age * (90 + (uint32_t)lev * 165 / 255) / 255);
        uint16_t col = theme::scale(baseCol, bright);
        const int fromHead = kTraceW - 1 - x;
        if (fromHead < 18)  // white-hot tip, where the beam is now
            col = theme::blend(col, theme::kIdle, (uint8_t)((18 - fromHead) * 8));
        if (prevValid)
            c.drawLine(kTraceX + x - 1, prevY, kTraceX + x, y, col);
        else
            c.drawPixel(kTraceX + x, y, col);
        // bloom: loud + recent segments bleed a faint halo above/below, so the
        // bright part reads thick and glowing, then thins as the note releases.
        if (lev > 110 && fromHead < 48) {
            const uint16_t hcol = theme::scale(baseCol, (uint8_t)((uint32_t)bright * (lev - 110) / 220));
            c.drawPixel(kTraceX + x, y - 1, hcol);
            c.drawPixel(kTraceX + x, y + 1, hcol);
        }
        prevY = y;
        prevValid = true;
    }
    // the beam head: a white core inside a layered green bloom (wide soft halo ->
    // mid glow -> hot inner), so the live end glows before the trail fades behind it
    if (prevValid) {
        const int hx = kTraceX + kTraceW - 2;
        // the white core warms toward amber as drive climbs — a gritty patch
        // glows hot, a clean one stays cool white.
        const float drv = cf.synth.drive;
        const float warm = drv <= 2.f ? 0.f : (drv >= 7.f ? 1.f : (drv - 2.f) / 5.f);
        const uint16_t core = theme::blend(theme::kIdle, theme::kAmber, (uint8_t)(warm * 150.f));
        c.drawFastVLine(hx, prevY - 5, 11, theme::scale(theme::kGreen, 22));
        c.drawFastVLine(hx, prevY - 3, 7, theme::scale(theme::kGreen, 70));
        c.drawFastVLine(hx, prevY - 1, 3, theme::kGreen);
        c.fillRect(hx - 1, prevY - 1, 3, 3, core);
    }
}

// CRT depth: bevelled glass corners so the scope seats in its bezel like a
// little tube screen. (The drifting scanline sheen was removed — too busy.)
// Overlaid on top of the scope, skipped during the quick-edit panel.
void drawCrt(M5Canvas& c) {
    const int x0 = kTraceX, x1 = kTraceX + kTraceW - 1;
    const int y0 = kScopeY, y1 = kScopeY + kScopeH - 1;

    // bevelled glass corners — a 45° chamfer masked back to black
    const int r = 7;
    for (int i = 0; i < r; ++i) {
        const int w = r - i;
        c.drawFastHLine(x0, y0 + i, w, theme::kBg);              // top-left
        c.drawFastHLine(x1 - w + 1, y0 + i, w, theme::kBg);      // top-right
        c.drawFastHLine(x0, y1 - i, w, theme::kBg);              // bottom-left
        c.drawFastHLine(x1 - w + 1, y1 - i, w, theme::kBg);      // bottom-right
    }
}

void drawScope(M5Canvas& c, uint32_t now) {
    if (keys::quickEditActive()) {
        drawEditPanel(c);
        return;
    }
    if (store::get().scopeMode == 1) {
        drawPitchTrail(c);
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

void drawBottom(M5Canvas& c, uint32_t now) {
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

    // mini grid-map: 4x10, top row = string 3. green = held lead, amber =
    // latched drone (blinking white on the jam-motion beat — you can SEE the
    // arp walk), small amber dot = root degrees (lock on) or in-scale keys
    // (lock off), faint = the rest.
    const int gx = 166, gy = kBottomY, cw = 7, ch = 6;
    const auto& sc = dsp::kScales[cf.layout.scaleIdx];
    const int rowDeg = dsp::rowDegrees(cf.layout);
    int droneCount = 0;
    for (int str = 0; str < dsp::kGridStrings; ++str) {
        const int y = gy + (3 - str) * ch;
        for (int col = 0; col < dsp::kGridCols; ++col) {
            const int x = gx + col * cw;
            const int st = keys::noteState(str, col, now);
            if (st > 0) {
                if (st >= 2) ++droneCount;
                const uint16_t fill = st == 1   ? theme::kGreen
                                      : st == 2 ? theme::kAmber
                                                : theme::kIdle;  // beat blink
                c.fillRect(x, y, cw - 1, ch - 1, fill);
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
    // progression: outline the chord root sounding now, so the walking
    // backing is visible on the grid map even though no key is held for it
    int ps, pc;
    if (keys::progCurrentCell(ps, pc)) {
        const int x = gx + pc * cw, y = gy + (3 - ps) * ch;
        c.drawRect(x, y, cw - 1, ch - 1, theme::kAmber);
    }

    // the backing, counted where it lives — drones sit outside the lead cap
    if (droneCount > 0) {
        snprintf(buf, sizeof buf, "+%d", droneCount);
        c.setTextColor(theme::kAmber, theme::kBg);
        c.setTextDatum(top_right);
        c.drawString(buf, gx - 3, gy + 9);
        c.setTextDatum(top_left);
    }
}

// Loop pedal state, top-left of the scope: REC blinks red while the take
// rolls, LOOP green with a cycle-progress bar while it plays, OVR amber
// while layering. Dim LOOP = a stopped take waiting on the pedal.
void drawLoop(M5Canvas& c, uint32_t now) {
    const looper::State st = looper::state();
    if (st == looper::State::Empty || keys::quickEditActive()) return;

    const int x = kTraceX + 2, y = kScopeY + 3;
    char buf[16];
    c.setFont(&fonts::Font0);
    switch (st) {
        case looper::State::Recording:
            if ((now >> 8) & 1) c.fillCircle(x + 3, y + 3, 3, theme::kRed);
            snprintf(buf, sizeof buf, "REC %lu.%lus", (unsigned long)(looper::positionMs(now) / 1000),
                     (unsigned long)(looper::positionMs(now) % 1000 / 100));
            c.setTextColor(theme::kRed, theme::kBg);
            c.drawString(buf, x + 10, y);
            break;
        case looper::State::Playing:
        case looper::State::Overdub: {
            const bool ovr = st == looper::State::Overdub;
            c.setTextColor(ovr ? theme::kAmber : theme::kGreen, theme::kBg);
            c.drawString(ovr ? "OVR" : "LOOP", x, y);
            const uint32_t len = looper::lengthMs();
            if (len > 0) {  // cycle progress — see the downbeat coming
                const int bx = x + 28, bw = 44;
                c.drawRect(bx, y + 1, bw, 5, theme::kLine);
                c.fillRect(bx + 1, y + 2, (int)((bw - 2) * looper::positionMs(now) / len), 3,
                           ovr ? theme::kAmber : theme::kGreenDim);
            }
            // layer count: amber while a peel is in effect (live < total)
            if (looper::overflowed()) {
                c.setTextColor(theme::kRed, theme::kBg);
                c.drawString("FULL", x + 76, y);
            } else if (looper::topLayers() > 0) {
                const int live = looper::liveLayers() + 1, top = looper::topLayers() + 1;
                char lc[12];
                if (live < top) snprintf(lc, sizeof lc, "x%d/%d", live, top);
                else            snprintf(lc, sizeof lc, "x%d", live);
                c.setTextColor(live < top ? theme::kAmber : theme::kDim, theme::kBg);
                c.drawString(lc, x + 76, y);
            }
            break;
        }
        case looper::State::Stopped:
            c.setTextColor(theme::kDim, theme::kBg);
            c.drawString("LOOP --", x, y);
            break;
        default:
            break;
    }
}

// The auto-progression annunciator: the chord sequence as note-name chips with
// the chord sounding now boxed amber. You can read where you are in the loop at
// a glance, and "PROG: tap chords" prompts the setup when it's still empty.
void drawProg(M5Canvas& c, uint32_t now) {
    if (!keys::progActive() || keys::quickEditActive()) return;
    int y = kScopeY + 3;
    if (looper::state() != looper::State::Empty) y += 10;  // yield the top line
    const int x0 = kTraceX + 2;
    c.setFont(&fonts::Font0);
    const int len = keys::progLen();
    if (len == 0) {
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("PROG: tap chords", x0, y);
        return;
    }
    c.setTextColor(theme::kAmber, theme::kBg);
    c.drawString("PROG", x0, y);
    const int cur = keys::progIndex();
    int x = x0 + 28;
    char nm[6];
    for (int i = 0; i < len && x < kTraceX + kTraceW - 14; ++i) {
        keys::progStepName(i, nm, sizeof nm);
        const int w = (int)strlen(nm) * 6 + 3;
        if (i == cur) {
            c.fillRect(x - 1, y - 1, w, 9, theme::kAmber);
            c.setTextColor(theme::kBg, theme::kAmber);
        } else {
            c.setTextColor(theme::kDim, theme::kBg);
        }
        c.drawString(nm, x + 1, y);
        x += w + 2;
    }
}

// Low-battery warning: a pocket instrument that dies mid-jam without telling
// you is a broken promise. Quiet until 20%, blinking red at 10%.
void drawBattery(M5Canvas& c, uint32_t now) {
    static int level = -1;
    static uint32_t lastPoll = 0;
    if (level < 0 || now - lastPoll > 5000) {
        lastPoll = now;
        level = M5.Power.getBatteryLevel();
    }
    if (level < 0 || level > 20 || keys::quickEditActive()) return;
    if (level <= 10 && (now >> 9) & 1) return;  // blink when critical
    char buf[12];
    snprintf(buf, sizeof buf, "BAT %d%%", level);
    c.setFont(&fonts::Font0);
    c.setTextColor(level <= 10 ? theme::kRed : theme::kAmber, theme::kBg);
    // drop below whatever owns the top-left corner (loop and/or progression)
    int y = kScopeY + 3;
    if (looper::state() != looper::State::Empty) y += 10;
    if (keys::progActive()) y += 10;
    c.drawString(buf, kTraceX + 2, y);
}

void drawHint(M5Canvas& c) {
    c.setFont(&fonts::Font0);
    if (keys::quickEditActive()) {
        c.setTextColor(theme::kAmber, theme::kBg);
        c.drawString("release fn to play", 4, kHintY);
        return;
    }
    // the hint line turns loop-aware while a take exists — the gestures live
    // on screen exactly when they're relevant (no more hunting for clear)
    const looper::State ls = looper::state();
    c.setTextColor(theme::kDim, theme::kBg);
    if (ls == looper::State::Recording)
        c.drawString("recording...  alt: close the loop", 2, kHintY);
    else if (keys::progActive())
        c.drawString("tap row = chord progression   bksp clear", 2, kHintY);
    else if (ls != looper::State::Empty)
        c.drawString("alt dub   hold undo   fn+alt clear", 2, kHintY);
    else
        c.drawString("fn edit  tab setup  shift chrom  ` exit", 2, kHintY);  // 40ch=240px @x2
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
    c.drawString("fn+q-p: sounds  ctrl/opt: volume", x + 8, y + 53);
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
        looper::tick(frameStart);  // schedule due loop-playback events

        if (act.exitApp) {
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::AllOff, 0));
            led::off();
            store::persistNow();
            delay(120);  // let the release tails fade
            ESP.restart();
        }
        if (act.openSettings) {
            settings::run(canvas);
            keys::resync();
            gPrevValid = false;
            gTrailInit = false;  // restart the pitch trail clean
            introShownAt = millis();
            continue;
        }

        if (!cf.seenIntro && (act.gridPressed || millis() - introShownAt > cfg::kIntroMs)) {
            cf.seenIntro = true;
            store::markDirty();
        }

        applyTilt();
        // lead = live sound; backing = its frozen sound when the jam is locked.
        // Both are local copies so the G0 trigger macro never bakes into the
        // saved sound (drive especially is a real param, not a live-mod field).
        dsp::SynthParams leadParams = cf.synth;
        dsp::SynthParams backParams = cf.backingLocked ? cf.backingSynth : cf.synth;

        // G0 trigger macro: momentary reads the level; latch toggles on each
        // press (rising edge), so a tap arms it and a second tap releases.
        const bool trigRaw = keys::triggerHeld();
        static bool trigPrev = false, trigLatched = false;
        if (trigRaw && !trigPrev) trigLatched = !trigLatched;
        trigPrev = trigRaw;
        const bool trigEngaged = cf.triggerLatch ? trigLatched : trigRaw;
        if (trigEngaged)
            applyTrigger(leadParams, backParams, cf.triggerAction, cf.triggerDepth);
        audio::setParams(leadParams, backParams);
        store::tick(frameStart);

        // onboard LED mirrors the lead voice: pitch -> hue, activity ->
        // brightness, fresh attacks and bends throw a white sparkle
        {
            static bool prevLedActive = false;
            auto ld = audio::lead();
            const bool bending = fabsf(keys::bendCentsNow()) > 2.f;
            const bool accent = (ld.active && !prevLedActive) || bending;
            prevLedActive = ld.active;
            led::update(ld.active, ld.pitchMidi, 1.f, accent);
        }

        // ---- draw ----------------------------------------------------------
        canvas.fillScreen(theme::kBg);
        drawStatus(canvas);
        drawScope(canvas, frameStart);
        if (!keys::quickEditActive()) drawCrt(canvas);  // CRT bezel over the trace
        drawReadout(canvas);
        drawLoop(canvas, frameStart);
        drawProg(canvas, frameStart);
        drawBattery(canvas, frameStart);
        drawBottom(canvas, frameStart);
        drawHint(canvas);
        hud::draw(canvas, frameStart);
        if (!cf.seenIntro) drawIntro(canvas);
        if (trigEngaged) {  // G0 trigger macro engaged — name the action
            char tb[12];
            snprintf(tb, sizeof tb, "%s%s", store::triggerActionTag(cf.triggerAction),
                     cf.triggerLatch ? "*" : "");
            canvas.setFont(&fonts::Font0);
            canvas.setTextColor(theme::kAmber, theme::kBg);
            canvas.setTextDatum(top_center);
            canvas.drawString(tb, cfg::kScreenW / 2, kScopeY + 4);
            canvas.setTextDatum(top_left);
        }
        canvas.pushSprite(0, 0);

        const uint32_t spent = millis() - frameStart;
        if (spent < cfg::kFrameMs) delay(cfg::kFrameMs - spent);
    }
}

}  // namespace perform
