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

// The demo is a CURATED performance, not a random tour. The bed is Ethereal —
// the patch the README calls "a bed to solo over" — voiced in a register the
// 1 W speaker can actually speak (the progression pad sits an octave below
// the grid, so a low layout octave lands it in inaudible mud). The solo
// rotates through leads that are known to sing over a pad.
constexpr uint8_t kBedSlot = 4;                  // t = Ethereal
constexpr uint8_t kSoloSlots[4] = {3, 0, 1, 7};  // Solo, GLIDE, ACID, Drift

bool gPending = false, gActive = false;
dsp::DemoMelody gMel;
uint32_t gNextEv = 0, gSwapAt = 0;
bool gRinging = false;
bool gSoloEntered = false;
uint8_t gSoloIdx = 0;

uint32_t beatMs() {
    const int bpm = store::get().jamBpm;
    return 60000u / (uint32_t)(bpm < 40 ? 40 : bpm);
}

// Switch the SOLO voice over the running bed — the real hot-swap: the bed
// freezes on its own sound, the card shows the new face, the morph glides.
void switchSolo(uint8_t slot) {
    keys::soundSwitchBegin();
    store::applyPatch(slot);
    keys::soundSwitchEnd();
    soundcard::show();
    morph::kick();
}
}  // namespace

void requestStart() { gPending = true; }
bool pending() { return gPending; }

void start(uint32_t nowMs) {
    gPending = false;
    auto& g = store::get();
    if (g.jamRows == 0) g.jamRows = 1;  // the demo needs the progression bed
    g.jamMotion = 3;                    // progression

    if (keys::progLen() == 0) {  // no bed spelled? lay down OUR curated one
        // each chord spelled twice at 2 beats: the same I-IV-V-IV loop over
        // 4 bars, but the pad re-blooms every half bar — a pulse, not a wash
        g.jamChordBeats = 2;
        if (g.jamBpm < 70 || g.jamBpm > 130) g.jamBpm = 92;  // a groove tempo
        store::unlockBacking();  // a stale solo/backing split would trap the
                                 // bed on its frozen sound — Ethereal must land
        store::applyPatch(kBedSlot);         // nothing sounds yet — no lock
        const int8_t keepOct = g.layout.octave;
        g.layout.octave = 4;                 // bed = this minus an octave: audible
        static const int kCols[8] = {0, 0, 3, 3, 4, 4, 3, 3};  // I-IV-V-IV, pulsed
        for (int i = 0; i < 8; ++i) keys::progAppendStep(0, kCols[i], false);
        g.layout.octave = keepOct;           // the player's register, untouched
    }
    // (an existing player-built progression is reused as-is — their bed,
    // their sound, their tempo; the demo just solos over it)

    gMel.seed(esp_random());
    gRinging = false;
    gSoloEntered = false;
    gSoloIdx = 0;
    gNextEv = nowMs + 4 * beatMs();          // one bar for the bed to establish
    gSwapAt = nowMs + (4 + 32) * beatMs();   // then a new solo voice per 8 bars
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

    if (nowMs >= gSwapAt) {  // rotate the solo voice; the bed holds its ground
        gSwapAt = nowMs + 32 * beatMs();
        gSoloIdx = (uint8_t)((gSoloIdx + 1) % (sizeof kSoloSlots));
        switchSolo(kSoloSlots[gSoloIdx]);
    }

    if (nowMs < gNextEv) return;
    if (!gSoloEntered) {  // the solo voice arrives with the first phrase
        switchSolo(kSoloSlots[gSoloIdx]);
        gSoloEntered = true;
    }

    const dsp::DemoNote n = gMel.next(dsp::kScales[g.layout.scaleIdx].len);
    gNextEv = nowMs + n.steps16 * beatMs() / 4;  // a 16th = a quarter-beat

    if (n.type == dsp::DemoNote::Rest) {
        if (gRinging)
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kMelodyId));
        gRinging = false;
        return;
    }
    // melody rides "string 2" of the live layout math, register clamped so a
    // very low or high player octave never buries the line. Scale lock is
    // FORCED on for the melody: its degrees are scale steps — read chromatic
    // (lock off) they'd be atonal semitone runs.
    dsp::Layout lay = g.layout;
    if (lay.octave < 3) lay.octave = 3;
    if (lay.octave > 5) lay.octave = 5;
    lay.scaleLock = true;
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
