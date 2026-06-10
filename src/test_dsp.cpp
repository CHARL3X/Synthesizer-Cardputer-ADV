// Host-side sanity tests for the pure DSP core (pio run -e native).
// This compiling and passing on a PC is the proof that dsp/ has no hardware
// dependencies -- the porting boundary the whole architecture promises.
#ifdef GLIDE_HOST_BUILD

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dsp/patches.h"
#include "dsp/pitch.h"
#include "dsp/synth.h"

using namespace dsp;

static int failures = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

static constexpr float kSr = 32000.f;
static constexpr int kBlock = 128;

static float peakOf(Synth& s, int blocks) {
    float buf[kBlock];
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        s.render(buf, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            const float a = fabsf(buf[i]);
            if (!std::isfinite(buf[i])) {
                CHECK(false, "non-finite sample");
                return -1.f;
            }
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main() {
    // ---- pitch / layout math -------------------------------------------
    Layout l;  // defaults: A minor pent, oct 4, row interval 5, lock on
    CHECK(fabsf(gridToMidi(l, 0, 0, false) - 69.f) < 1e-4, "string0 col0 = A4 (69)");
    CHECK(fabsf(gridToMidi(l, 0, 1, false) - 72.f) < 1e-4, "A min pent degree 1 = C5");
    CHECK(fabsf(gridToMidi(l, 0, 5, false) - 81.f) < 1e-4, "degree 5 wraps the octave = A5");
    CHECK(rowDegrees(l) == 2, "pentatonic row interval = 2 degrees (a fourth)");
    CHECK(fabsf(gridToMidi(l, 1, 0, false) - gridToMidi(l, 0, 2, false)) < 1e-4,
          "string 1 starts 2 degrees up");
    CHECK(fabsf(gridToMidi(l, 0, 1, true) - 70.f) < 1e-4, "chromatic override = semitones");
    CHECK(fabsf(midiToFreq(69.f) - 440.f) < 0.01f, "A4 = 440 Hz");

    char name[8];
    int cents;
    midiToNoteCents(69.3f, name, sizeof name, cents);
    CHECK(name[0] == 'A' && cents == 30, "note+cents readout");

    // ---- scale tables are well-formed (incl. the v0.5 additions) ---------
    for (int si = 0; si < kScaleCount; ++si) {
        const Scale& sc = kScales[si];
        CHECK(sc.len >= 5 && sc.len <= 12, "scale length sane");
        CHECK(sc.steps[0] == 0, "scale starts at the root");
        for (int j = 1; j < sc.len; ++j)
            CHECK(sc.steps[j] > sc.steps[j - 1] && sc.steps[j] < 12,
                  "scale steps ascend within the octave");
    }

    // ---- synth: silence -> sound -> silence ------------------------------
    Synth s;
    s.init(kSr);
    SynthParams p;
    p.glideS = 0.05f;
    s.setParams(p);

    CHECK(peakOf(s, 4) < 1e-6f, "silent before any note");

    NoteEvent on = NoteEvent::make(NoteEvent::On, 10, 0, false, 69.f);
    s.handleEvent(on);
    CHECK(peakOf(s, 30) > 0.02f, "audible after noteOn");
    CHECK(s.leadActive(), "lead active");
    CHECK(fabsf(s.leadPitchMidi() - 69.f) < 0.01f, "lead pitch settled at target");

    // ---- glide: legato hand-off slews monotonically ----------------------
    NoteEvent leg = NoteEvent::make(NoteEvent::On, 11, 0, true, 75.f);
    s.handleEvent(leg);
    float buf[kBlock];
    float prev = s.leadPitchMidi();
    bool monotonic = true;
    bool moved = false;
    // glideS is the slew time constant; run ~8 tau so the exponential lands
    for (int b = 0; b < 100; ++b) {
        s.render(buf, kBlock);
        const float cur = s.leadPitchMidi();
        if (cur < prev - 1e-4f) monotonic = false;
        if (cur > prev + 1e-4f) moved = true;
        prev = cur;
    }
    CHECK(monotonic && moved, "glide slews upward without overshoot");
    CHECK(fabsf(s.leadPitchMidi() - 75.f) < 0.05f, "glide arrives at target");
    CHECK(s.heldVoices() == 1, "legato hand-off reuses the lane voice");

    // ---- release decays to silence ---------------------------------------
    NoteEvent off = NoteEvent::make(NoteEvent::Off, 11, 0, false, 0.f);
    s.handleEvent(off);
    peakOf(s, 90);  // run well past the 250 ms release tail (90 blocks = 360 ms)
    CHECK(peakOf(s, 4) < 1e-4f, "silent after release tail");

    // ---- voice-cap nearest steal (the free-mode chord slide) -------------
    p.voiceCount = 2;
    s.setParams(p);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 1, 0xFF, false, 60.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 2, 0xFF, false, 67.f));
    peakOf(s, 8);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 3, 0xFF, false, 69.f));  // steals 67
    CHECK(s.heldVoices() == 2, "voice cap respected via steal");
    peakOf(s, 60);
    CHECK(fabsf(s.leadPitchMidi() - 69.f) < 0.05f, "stolen voice glided to new pitch");

    // ---- staccato never squats in the pool (constant-rate release) -------
    // A note released early in its attack must free its voice in millis,
    // not sit silently for the full releaseS exhausting the pool.
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 20);  // drain the previous section's voices completely
    p = SynthParams();
    p.attackS = 2.f;     // long attack: 1 block in -> level ~0.002
    p.releaseS = 0.25f;
    s.setParams(p);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 20, 0xFF, false, 69.f));
    peakOf(s, 1);        // 4 ms of attack
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 20, 0xFF, false, 0.f));
    peakOf(s, 3);        // 12 ms — proportional release frees it here
    CHECK(s.activeVoices() == 0, "staccato release frees the voice quickly");
    p = SynthParams();   // restore defaults for the sections below
    s.setParams(p);

    // ---- panic ------------------------------------------------------------
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 20);
    CHECK(peakOf(s, 4) < 1e-4f, "panic silences everything");

    // ---- every waveform renders finite ------------------------------------
    for (int w = 0; w < (int)Waveform::Count; ++w) {
        p.wave = (Waveform)w;
        p.voiceCount = 6;
        s.setParams(p);
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(50 + w), 0xFF, false, 64.f));
        const float pk = peakOf(s, 20);
        CHECK(pk > 0.01f && pk < 1.3f, "waveform renders in range");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
    }

    // ---- drones: the layering jam ------------------------------------------
    // A latched drone must survive the lead's voice-cap stealing and let go
    // with a drawn-out tail.
    p = SynthParams();
    p.voiceCount = 2;
    p.releaseS = 0.05f;
    s.setParams(p);
    NoteEvent dr = NoteEvent::make(NoteEvent::On, 40, 0xFF, false, 45.f);
    dr.drone = true;
    s.handleEvent(dr);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 41, 0xFF, false, 69.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 42, 0xFF, false, 72.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 43, 0xFF, false, 76.f));  // cap steal
    peakOf(s, 100);  // let the stolen voice finish its glide to the target
    CHECK(s.heldVoices() == 3, "drone exempt from the lead voice cap (2 lead + 1 drone)");
    CHECK(fabsf(s.leadPitchMidi() - 76.f) < 0.2f, "steal hit a lead voice, not the drone");
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 40, 0xFF, false, 0.f));
    peakOf(s, 25);  // 100 ms: lead release (50 ms) done, drone tail (4x+0.4s) still alive
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 41, 0xFF, false, 0.f));
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 42, 0xFF, false, 0.f));
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 43, 0xFF, false, 0.f));
    peakOf(s, 25);
    CHECK(s.activeVoices() >= 1, "drone release is drawn out");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- LeadsOff: sound switches / settings clear the solo, not the jam --
    dr = NoteEvent::make(NoteEvent::On, 45, 0xFF, false, 45.f);
    dr.drone = true;
    s.handleEvent(dr);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 46, 0xFF, false, 69.f));
    peakOf(s, 4);
    s.handleEvent(NoteEvent::make(NoteEvent::LeadsOff, 0, 0xFF, false, 0.f));
    peakOf(s, 4);
    CHECK(s.heldVoices() == 1, "LeadsOff keeps the drone ringing");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- factory sound bank: every patch is alive and sane ----------------
    // Exercises the new engine paths: sub-osc (BASS), noise (GHOST/PERC),
    // filter envelope (PLUCK/ACID/PERC), drive (ACID), auto-vib (WHISTLE).
    const Patch* bank = factoryPatches();
    for (int i = 0; i < kPatchCount; ++i) {
        CHECK(bank[i].name[0] != '\0', "patch has a name");
        CHECK(bank[i].tiltDepth >= 0.f && bank[i].tiltDepth <= 1.f, "tilt depth in range");
        CHECK(bank[i].tiltDepthB >= 0.f && bank[i].tiltDepthB <= 1.f, "tilt depth B in range");
        for (int j = 0; j < i; ++j)
            CHECK(strcmp(bank[i].name, bank[j].name) != 0, "patch names unique");

        s.setParams(bank[i].synth);
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(100 + i), 0, false, 69.f));
        // slide it too — every sound must survive a glide
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(120 + i), 0, true, 72.f));
        const float pk = peakOf(s, 60);  // 240 ms covers slow PAD/GHOST attacks
        CHECK(pk > 0.005f && pk < 1.4f, "patch renders audible and bounded");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 80);  // drain long releases before the next patch
    }

    if (failures == 0) {
        printf("GLIDE dsp: all checks passed\n");
        return 0;
    }
    printf("GLIDE dsp: %d FAILURES\n", failures);
    return 1;
}

#endif  // GLIDE_HOST_BUILD

