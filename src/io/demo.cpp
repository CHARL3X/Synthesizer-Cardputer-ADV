#include "demo.h"

#include <esp_random.h>

#include "../dsp/demo_gen.h"
#include "../dsp/params.h"
#include "../dsp/patches.h"
#include "../dsp/pitch.h"
#include "../dsp/scales.h"
#include "../storage/glide_config.h"
#include "../ui/morph.h"
#include "../ui/sound_card.h"
#include "audio_engine.h"
#include "keys.h"

namespace demo {

namespace {
// Melody voice id: clear of leads (0..55), drone partners (64..119), backing
// (120..122), loop playback (128..183) and the chime/preview (250/251).
constexpr uint8_t kMelodyId = 200;

bool gPending = false, gActive = false;
dsp::DemoMelody gMel;
uint32_t gNextEv = 0, gSwapAt = 0;
bool gRinging = false;
uint8_t gSlot = 0;

uint32_t beatMs() {
    const int bpm = store::get().jamBpm;
    return 60000u / (uint32_t)(bpm < 40 ? 40 : bpm);
}
}  // namespace

void requestStart() { gPending = true; }
bool pending() { return gPending; }

void start(uint32_t nowMs) {
    gPending = false;
    auto& g = store::get();
    // the demo needs the progression bed: force it in RAM (not persisted here;
    // it's also the factory default, so usually nothing changes at all)
    if (g.jamRows == 0) g.jamRows = 1;
    g.jamMotion = 3;  // progression
    if (keys::progLen() == 0) {  // no bed spelled? lay down I-IV-V-IV
        static const int kCols[4] = {0, 3, 4, 3};
        for (int i = 0; i < 4; ++i) keys::progAppendStep(0, kCols[i], false);
    }
    gMel.seed(esp_random());
    gRinging = false;
    gSlot = g.currentPatch;
    gNextEv = nowMs + 2 * beatMs();  // let the bed establish before the solo
    gSwapAt = nowMs + 16 * beatMs();
    gActive = true;
}

void stop() {
    if (!gActive) return;
    gActive = false;
    if (gRinging) audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kMelodyId));
    gRinging = false;
    // the bed keeps looping — stopping the demo IS the takeover
}

bool active() { return gActive; }

void tick(uint32_t nowMs) {
    if (!gActive) return;
    auto& g = store::get();

    // every 4 bars, wander to the next curated sound — the bed holds its own
    // voice (the real hot-swap), the card shows the new face, the morph glides
    if (nowMs >= gSwapAt) {
        gSwapAt = nowMs + 16 * beatMs();
        gSlot = (uint8_t)((gSlot + 1) % dsp::kFirstGenSlot);  // q..i
        keys::soundSwitchBegin();
        store::applyPatch(gSlot);
        keys::soundSwitchEnd();
        soundcard::show();
        morph::kick();
    }

    if (nowMs < gNextEv) return;
    const auto& lay = g.layout;
    const dsp::DemoNote n = gMel.next(dsp::kScales[lay.scaleIdx].len);
    gNextEv = nowMs + n.beats4 * beatMs() / 4;

    if (n.type == dsp::DemoNote::Rest) {
        if (gRinging)
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kMelodyId));
        gRinging = false;
        return;
    }
    // melody rides "string 2" of the live layout math — above the bass pad,
    // always in the player's key and scale
    const float pitch = dsp::gridToMidi(lay, 2, n.degree, false);
    if (n.type == dsp::DemoNote::Slide && gRinging) {
        audio::pushEvent(
            dsp::NoteEvent::make(dsp::NoteEvent::Retarget, kMelodyId, 0xFF, false, pitch));
    } else {
        if (gRinging)  // release before the re-strike, like a real re-fingering
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kMelodyId));
        audio::pushEvent(
            dsp::NoteEvent::make(dsp::NoteEvent::On, kMelodyId, 0xFF, false, pitch));
    }
    gRinging = true;
}

}  // namespace demo
