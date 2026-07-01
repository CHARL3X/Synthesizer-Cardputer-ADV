#include "sound_card.h"

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../storage/glide_config.h"
#include "sound_viz.h"
#include "theme.h"

namespace soundcard {

namespace {
uint32_t gUntil = 0;
uint32_t gShownAt = 0;

// card geometry — sized to sit inside the scope area, hint line stays clear
constexpr int kW = 204, kH = 88;
constexpr int kX = (240 - kW) / 2, kY = 22;
}  // namespace

void show(uint32_t holdMs) {
    gShownAt = millis();
    gUntil = gShownAt + holdMs;
}

void dismiss() { gUntil = 0; }

bool active(uint32_t nowMs) { return nowMs < gUntil; }

void draw(M5Canvas& c, uint32_t nowMs) {
    if (!active(nowMs)) return;

    // fade toward background over the last 250 ms, like the HUD
    const uint32_t remain = gUntil - nowMs;
    const uint8_t fade = remain < 250 ? (uint8_t)(255 - remain * 255 / 250) : 0;
    const uint16_t panel = theme::blend(theme::kPanel, theme::kBg, fade);
    const uint16_t frame = theme::blend(theme::kAmber, theme::kBg, fade);
    const uint16_t hot   = theme::blend(theme::kGreen, theme::kBg, fade);
    const uint16_t txt   = theme::blend(theme::kIdle, theme::kBg, fade);
    const uint16_t dim   = theme::blend(theme::kDim, theme::kBg, fade);

    const auto& g = store::get();
    const auto& s = g.synth;

    c.fillRoundRect(kX, kY, kW, kH, 5, panel);
    c.drawRoundRect(kX, kY, kW, kH, 5, frame);

    // header: the sound's name (exactly what Save writes) + unsaved-edit star
    char buf[32];
    snprintf(buf, sizeof buf, "%s%s", store::liveName(), store::liveDirty() ? "*" : "");
    c.setFont(&fonts::Font2);
    c.setTextDatum(top_left);
    while (buf[0] && c.textWidth(buf) > kW - 60) buf[strlen(buf) - 1] = '\0';
    c.setTextColor(frame, panel);
    c.drawString(buf, kX + 8, kY + 3);
    if (store::backingLocked()) {  // the bed holds its own sound — this is the solo
        c.setFont(&fonts::Font0);
        c.setTextDatum(top_right);
        c.setTextColor(theme::blend(theme::kSteel, theme::kBg, fade), panel);
        c.drawString("SOLO", kX + kW - 8, kY + 6);
        c.setTextDatum(top_left);
    }

    // the face: wave | envelope | filter, labels underneath
    const int vy = kY + 24, vh = 24;
    viz::drawWave(c, kX + 8, vy, 56, vh, s.wave, hot);
    viz::drawEnv(c, kX + 74, vy, 62, vh, s.attackS, s.decayS, s.sustain, s.releaseS, hot);
    viz::drawFilter(c, kX + 146, vy, 50, vh, (dsp::FilterMode)s.filterMode, s.cutoffHz,
                    s.resonance, hot);

    c.setFont(&fonts::Font0);
    c.setTextColor(dim, panel);
    c.drawString(dsp::waveformName(s.wave), kX + 8, vy + vh + 3);
    c.drawString("env", kX + 74, vy + vh + 3);
    static const char* kModeShort[4] = {"LP", "HP", "BP", "NT"};
    if (s.cutoffHz >= 1000.f)
        snprintf(buf, sizeof buf, "%s %.1fk", kModeShort[s.filterMode & 3], s.cutoffHz / 1000.f);
    else
        snprintf(buf, sizeof buf, "%s %d", kModeShort[s.filterMode & 3], (int)s.cutoffHz);
    c.drawString(buf, kX + 146, vy + vh + 3);

    // the signature parameter gets the widest gauge
    const int gy = kY + 64;
    c.setTextColor(txt, panel);
    c.drawString("GLIDE", kX + 8, gy);
    viz::drawGauge(c, kX + 44, gy + 1, 110, 6, s.glideS / 2.f, frame);
    snprintf(buf, sizeof buf, "%dms", (int)(s.glideS * 1000));
    c.setTextColor(dim, panel);
    c.drawString(buf, kX + 160, gy);

    // the space: chorus / delay / reverb sends
    const int sy = kY + 76;
    struct Send { const char* tag; float v; };
    const Send sends[3] = {{"CHO", s.chorusDepth}, {"DLY", s.delayMix}, {"REV", s.reverbMix}};
    for (int i = 0; i < 3; ++i) {
        const int sx = kX + 8 + i * 66;
        c.setTextColor(dim, panel);
        c.drawString(sends[i].tag, sx, sy);
        viz::drawGauge(c, sx + 22, sy + 1, 36, 6, sends[i].v, frame);
    }
}

}  // namespace soundcard
