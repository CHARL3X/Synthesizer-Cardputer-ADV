// GLIDE — a continuous-pitch polyphonic slide instrument for the
// M5Stack Cardputer ADV.
//
// Boot: M5 init -> config load -> audio engine (or a LOUD failure screen,
// never a silent dead instrument) -> splash with gliding chime -> play.
#include <M5Cardputer.h>

#include "config.h"
#include "dsp/patches.h"
#include "io/audio_engine.h"
#include "io/keys.h"
#include "io/tilt.h"
#include "storage/glide_config.h"
#include "ui/perform_screen.h"
#include "ui/splash.h"
#include "ui/theme.h"

namespace {

// Hard requirement from the build brief: any audio init failure must surface
// a visible error. The web prototype once shipped a silent dead power button
// and it cost a debugging round trip. Not again.
[[noreturn]] void fatalAudio(const char* reason) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(theme::kBg);
    d.drawRect(2, 2, cfg::kScreenW - 4, cfg::kScreenH - 4, theme::kRed);
    d.setTextDatum(top_left);
    d.setFont(&fonts::Font2);
    d.setTextColor(theme::kRed, theme::kBg);
    d.drawString("AUDIO INIT FAILED", 12, 14);
    d.setFont(&fonts::Font0);
    d.setTextColor(theme::kIdle, theme::kBg);
    d.drawString(reason, 12, 40);
    d.setTextColor(theme::kDim, theme::kBg);
    d.drawString("Is this a Cardputer ADV (ES8311)?", 12, 60);
    d.drawString("Check M5Cardputer/M5Unified versions", 12, 72);
    d.drawString("in platformio.ini, then rebuild.", 12, 84);
    Serial.printf("[glide] AUDIO INIT FAILED: %s\n", reason);
    bool on = true;
    for (;;) {
        d.fillCircle(cfg::kScreenW - 14, 14, 4, on ? theme::kRed : theme::kBg);
        on = !on;
        delay(500);
    }
}

}  // namespace

void setup() {
    auto mcfg = M5.config();
    mcfg.internal_spk = true;
    mcfg.internal_mic = false;  // half-duplex codec: never bring up the mic
    mcfg.internal_imu = true;   // optional tilt modulation
    M5Cardputer.begin(mcfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(120);
    M5Cardputer.Display.fillScreen(theme::kBg);

    Serial.begin(115200);
    Serial.println("[glide] boot");

    store::begin();

    if (!audio::begin()) fatalAudio(audio::lastError());
    audio::setParams(store::get().synth);

    // Escape hatch: press BACKSPACE during the boot splash -> full factory
    // reset (settings AND saved sound slots). Works even if stored state
    // ever wedges the UI. NOTE: it must be a press DURING the splash — the
    // ADV's TCA8418 keyboard is event-driven, so a key held from power-on
    // never produces an event (audited; a held-key gesture is dead on this
    // hardware).
    if (splash::run()) {
        store::resetDefaults();
        for (int i = 0; i < dsp::kPatchCount; ++i) store::clearOverride(i);
        audio::setParams(store::get().synth);
        auto& d = M5Cardputer.Display;
        d.fillScreen(theme::kBg);
        d.setFont(&fonts::Font2);
        d.setTextDatum(middle_center);
        d.setTextColor(theme::kAmber, theme::kBg);
        d.drawString("FACTORY RESET", cfg::kScreenW / 2, 58);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kDim, theme::kBg);
        d.drawString("settings + saved sounds cleared", cfg::kScreenW / 2, 78);
        d.setTextDatum(top_left);
        delay(1600);
    }

    keys::begin();
    tilt::begin();

    Serial.printf("[glide] ready  heap=%u  starved=%u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)audio::starvedBlocks());
}

void loop() {
    perform::run();  // never returns
}
