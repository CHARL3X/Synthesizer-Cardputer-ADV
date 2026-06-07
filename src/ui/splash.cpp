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
        p.masterVol = p.masterVol * 0.8f;
        audio::setParams(p);
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::On, kChimeId, 0xFF, false, 57.f));
    }

    // phosphor sweep under the wordmark, paced with the chime's glide;
    // doubles as the factory-reset window (backspace press = reset request)
    bool resetRequested = false;
    const int y = 96, x0 = 40, x1 = cfg::kScreenW - 40;
    for (int step = 0; step <= 24; ++step) {
        const int x = x0 + (x1 - x0) * step / 24;
        d.drawFastHLine(x0, y, x - x0, theme::kGreen);
        if (chime && step == 6)
            audio::pushEvent(
                dsp::NoteEvent::make(dsp::NoteEvent::Retarget, kChimeId, 0xFF, false, 69.f));
        M5Cardputer.update();
        for (const auto& p : M5Cardputer.Keyboard.keyList())
            if (p.y == 0 && p.x == 13) resetRequested = true;  // backspace
        delay(34);
    }
    if (chime) audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kChimeId));
    delay(250);
    audio::setParams(store::get().synth);  // restore live params
    return resetRequested;
}

}  // namespace splash
