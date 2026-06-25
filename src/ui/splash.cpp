#include "splash.h"

#include <M5Cardputer.h>

#include "../config.h"
#include "../dsp/params.h"
#include "../io/audio_engine.h"
#include "../storage/glide_config.h"
#include "theme.h"

namespace splash {

bool run() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(theme::kBg);

    d.setTextDatum(middle_center);
    d.setFont(&fonts::FreeMonoBold18pt7b);
    d.setTextColor(theme::kAmber, theme::kBg);
    d.drawString("GLIDE", cfg::kScreenW / 2, 52);

    d.setFont(&fonts::Font0);
    d.setTextColor(theme::kDim, theme::kBg);
    d.drawString("a pocket slide synth", cfg::kScreenW / 2, 80);
    d.setTextDatum(top_left);

    // boot chime: one note sliding up an octave — the soul, in one second
    const bool chime = store::get().bootSound;
    constexpr uint8_t kChimeId = 250;
    if (chime) {
        dsp::SynthParams p = store::get().synth;  // user's own sound
        p.glideS = 0.22f;
        p.masterVol = p.masterVol * 0.6f;  // gentle: the chime rides your saved level
        audio::setParams(p);
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::On, kChimeId, 0xFF, false, 57.f));
    }

    // phosphor sweep under the wordmark, paced with the chime's glide
    const int y = 96, x0 = 40, x1 = cfg::kScreenW - 40;
    for (int step = 0; step <= 24; ++step) {
        const int x = x0 + (x1 - x0) * step / 24;
        d.drawFastHLine(x0, y, x - x0, theme::kGreen);
        if (chime && step == 6)
            audio::pushEvent(
                dsp::NoteEvent::make(dsp::NoteEvent::Retarget, kChimeId, 0xFF, false, 69.f));
        M5Cardputer.update();
        delay(34);
    }
    if (chime) audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kChimeId));
    delay(250);
    audio::setParams(store::get().synth);  // restore live params

    // Factory reset wipes settings AND saved sounds — it must be deliberate. A
    // stray backspace during boot was nuking people's sessions, so now it takes
    // BKSP held *now* and held through a confirm bar; releasing cancels. (The
    // TCA8418 is event-driven, so a key held from power-on never registers —
    // this is a press made during the splash, then sustained.)
    auto bkspHeld = [&]() {
        M5Cardputer.update();
        for (int i = 0; i < 4; ++i) M5Cardputer.Keyboard.updateKeyList();  // drain FIFO
        for (const auto& p : M5Cardputer.Keyboard.keyList())
            if (p.y == 0 && p.x == 13) return true;  // backspace
        return false;
    };
    if (!bkspHeld()) return false;
    for (int step = 0; step <= 36; ++step) {  // ~1.4 s sustained hold to confirm
        if (!bkspHeld()) return false;        // released -> cancel, boot normally
        d.fillScreen(theme::kBg);
        d.setTextDatum(middle_center);
        d.setFont(&fonts::Font2);
        d.setTextColor(theme::kRed, theme::kBg);
        d.drawString("FACTORY RESET", cfg::kScreenW / 2, 54);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kDim, theme::kBg);
        d.drawString("keep holding BKSP  (release = cancel)", cfg::kScreenW / 2, 74);
        const int bw = 168, bx = (cfg::kScreenW - bw) / 2, by = 86;
        d.drawRect(bx, by, bw, 6, theme::kDim);
        d.fillRect(bx + 1, by + 1, (bw - 2) * step / 36, 4, theme::kRed);
        d.setTextDatum(top_left);
        delay(40);
    }
    return true;  // held all the way -> confirmed
}

}  // namespace splash
