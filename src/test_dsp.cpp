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
    Layout l;
    l.octave = 4;  // pin oct 4 here so the A4=69 reference math below stays put
                   // (the shipped default is oct 3; the math is the same shifted)
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

    // ---- diatonic chord builder (the auto-progression backing) ----------
    {
        float ch[3];
        // A min pent (default), lock on: triads come from the HARMONY PARENT
        // (natural minor), an octave under. base = 12*(4+1)+9-12 = 57 (A3).
        // deg 0 -> snap to minor degree 0 -> minor triad 0,3,7 = A3,C4,E4.
        const int nc = chordPitches(l, 0, 0, false, ch, 3);
        CHECK(nc == 3, "chord builds three tones");
        CHECK(fabsf(ch[0] - 57.f) < 1e-4, "chord root = A3 (an octave under A4)");
        CHECK(fabsf(ch[1] - 60.f) < 1e-4 && fabsf(ch[2] - 64.f) < 1e-4,
              "min pent backing = a real minor triad (not a quartal pile)");
        CHECK(ch[1] > ch[0] && ch[2] > ch[1], "chord tones ascend");
        // chromatic fallback (lock off / shift) = power voicing root+5th+8ve
        chordPitches(l, 0, 0, true, ch, 3);
        CHECK(fabsf(ch[1] - ch[0] - 7.f) < 1e-4, "chromatic chord has a fifth");
        CHECK(fabsf(ch[2] - ch[0] - 12.f) < 1e-4, "chromatic chord has an octave");
        CHECK(pitchClassName(57.f)[0] == 'A', "pitch-class label");

        // Blues: the ♭5 "blue note" must never end up a chord tone — every
        // backing triad is a consonant natural-minor triad (root, ♭3, 5), the
        // same notes a min-pent progression makes, so solo-in-blues lines up.
        Layout lb = l; lb.scaleIdx = SC_BLUES;
        for (int colq = 0; colq < kScales[SC_BLUES].len; ++colq) {
            const int bn = chordPitches(lb, 0, colq, false, ch, 3);
            CHECK(bn == 3, "blues chord builds three tones");
            const int third = (int)(ch[1] - ch[0] + 0.5f);
            const int fifth = (int)(ch[2] - ch[0] + 0.5f);
            CHECK((third == 3 || third == 4) && fifth == 7,
                  "blues backing is a plain major/minor triad, never the ♭5");
            for (int t = 0; t < 3; ++t) {
                const int pcb = (((int)(ch[t] + 0.5f) - lb.rootSemis) % 12 + 12) % 12;
                CHECK(pcb != 6, "no blue note (♭5) as a chord tone in the backing");
            }
        }
        // A 7-note scale is its own parent: diatonic triads are untouched.
        Layout lm = l; lm.scaleIdx = SC_MINOR;
        chordPitches(lm, 0, 0, false, ch, 3);
        CHECK(fabsf(ch[1] - ch[0] - 3.f) < 1e-4 && fabsf(ch[2] - ch[0] - 7.f) < 1e-4,
              "natural-minor i is still a minor triad");
    }

    // ---- every scale's harmony parent is a sane, consonant 7-note scale -----
    for (int si = 0; si < kScaleCount; ++si) {
        const Scale& hp = kScales[kScales[si].harm];
        CHECK(kScales[si].harm < kScaleCount, "harmony parent index in range");
        CHECK(hp.len == 7, "harmony parent is a full 7-note diatonic scale");
    }

    // ---- chord builder is well-formed for EVERY scale and EVERY grid cell ---
    // (edge sweep: no scale/position must ever produce garbage, a cluster, or a
    // non-triad — the backing's "you can't hit a wrong note" promise, total.)
    {
        float ch[3];
        bool sawDim = false;
        for (int si = 0; si < kScaleCount; ++si) {
            Layout ls = l; ls.scaleIdx = (uint8_t)si;
            for (int st = 0; st < kGridStrings; ++st)
                for (int co = 0; co < kGridCols; ++co) {
                    // scale-lock branch: always a stack of two diatonic thirds
                    const int nd = chordPitches(ls, st, co, false, ch, 3);
                    CHECK(nd == 3, "every cell builds a 3-note chord");
                    CHECK(ch[0] < ch[1] && ch[1] < ch[2], "chord tones strictly ascend");
                    CHECK(ch[0] > 0.f && ch[2] < 200.f, "chord pitches are finite & in range");
                    const int t1 = (int)(ch[1] - ch[0] + 0.5f);
                    const int t2 = (int)(ch[2] - ch[1] + 0.5f);
                    CHECK(t1 >= 3 && t1 <= 4 && t2 >= 3 && t2 <= 4,
                          "chord is a real triad: two stacked thirds, never a cluster");
                    if (t1 == 3 && t2 == 3) sawDim = true;  // diminished is allowed
                    // chromatic branch: a power voicing, every cell, every scale
                    chordPitches(ls, st, co, true, ch, 3);
                    CHECK(fabsf(ch[1] - ch[0] - 7.f) < 1e-4 &&
                          fabsf(ch[2] - ch[0] - 12.f) < 1e-4,
                          "chromatic voicing is root+5th+8ve everywhere");
                }
        }
        CHECK(sawDim, "the diatonic vii° still voices as a diminished triad");
        // maxOut is honored (a smaller voice budget never overruns)
        float two[2] = {-1.f, -1.f};
        CHECK(chordPitches(l, 0, 0, false, two, 2) == 2, "respects maxOut < 3");
        CHECK(chordPitches(l, 0, 0, false, two, 0) == 0, "maxOut 0 writes nothing");
    }

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

    // ---- tempo-synced delay: division math + bounded render ----------------
    CHECK(delaySyncBeats(2) > 0.74f && delaySyncBeats(2) < 0.76f, "dotted-eighth = 0.75 beat");
    CHECK(delaySyncBeats(0) == 0.f, "division 0 = free");
    CHECK(delaySyncName(3)[0] == '1' && delaySyncName(0)[0] == 'f', "division labels");
    {
        p = SynthParams();
        p.delayMix = 0.5f;
        p.delayFb = 0.5f;
        p.delaySync = 2;     // dotted-eighth
        p.tempoBpm = 120.f;  // beat 500 ms -> echo 375 ms, fits the 600 ms line
        s.setParams(p);
        s.handleEvent(NoteEvent::make(NoteEvent::On, 5, 0xFF, false, 64.f));
        const float pk = peakOf(s, 40);
        CHECK(pk > 0.01f && pk < 1.4f, "synced delay renders in range");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 20);
        p = SynthParams();
        s.setParams(p);
    }

    // ---- solo/backing split: each layer renders with its own sound ---------
    // Lead voices use the `lead` params, the backing layer uses `back`. Prove
    // the routing by silencing one bus and confirming only the other sounds.
    {
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
        SynthParams lead;
        SynthParams back = lead;
        back.masterVol = 0.f;            // silence the backing bus
        s.setParams(lead, back);
        peakOf(s, 30);                   // let the backing volume ramp settle to 0
        NoteEvent dr = NoteEvent::make(NoteEvent::On, 40, 0xFF, false, 60.f);
        dr.drone = true;
        s.handleEvent(dr);               // a drone -> backing bus (silenced)
        CHECK(peakOf(s, 20) < 1e-3f, "backing routed to its own (silenced) bus");
        s.handleEvent(NoteEvent::make(NoteEvent::On, 1, 0xFF, false, 64.f));  // lead bus
        CHECK(peakOf(s, 20) > 0.02f, "lead routed to the audible lead bus");

        // flip: silence lead, voice the backing, replay the drone fresh
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
        lead.masterVol = 0.f;
        back.masterVol = 0.7f;
        s.setParams(lead, back);
        peakOf(s, 30);                   // settle both ramps (no voices yet)
        s.handleEvent(dr);               // drone again, now on the audible backing bus
        CHECK(peakOf(s, 20) > 0.02f, "backing bus uses its own volume, not the lead's");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
        p = SynthParams();
        s.setParams(p);
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

    // ---- backing (loop-pedal playback): drone-grade protection, normal tail
    p = SynthParams();
    p.voiceCount = 1;
    p.releaseS = 0.05f;
    s.setParams(p);
    NoteEvent bk = NoteEvent::make(NoteEvent::On, 50, 4, false, 50.f);  // loop lane 4
    bk.backing = true;
    s.handleEvent(bk);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 1, 0, false, 69.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 2, 0xFF, false, 72.f));  // cap steal
    peakOf(s, 100);
    CHECK(s.heldVoices() == 2, "backing exempt from the cap (1 lead + 1 backing)");
    CHECK(s.heldLeadVoices() == 1, "lead counter ignores the backing");
    CHECK(fabsf(s.leadPitchMidi() - 72.f) < 0.2f, "steal hit the lead, not the loop");
    s.handleEvent(NoteEvent::make(NoteEvent::LeadsOff, 0, 0xFF, false, 0.f));
    peakOf(s, 4);
    CHECK(s.heldVoices() == 1, "LeadsOff keeps the loop playing");
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 50, 0xFF, false, 0.f));
    peakOf(s, 25);  // 100 ms — far past the 50 ms release, well short of a drone tail
    CHECK(s.activeVoices() == 0, "backing releases at the normal rate");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- backing survives a saturated pool (auto-progression under a solo) --
    // 3 drone-flagged backing voices + a full lead chord on the 8-voice pool
    // must not drop the backing: alloc evicts the oldest LEAD, never the
    // foundation. (The backing has the lowest seq, so the old policy would
    // have robbed it first — this guards the chord progression.)
    p = SynthParams();
    p.voiceCount = 8;
    p.releaseS = 0.05f;
    s.setParams(p);
    for (int i = 0; i < 3; ++i) {  // a 3-voice backing chord
        NoteEvent b = NoteEvent::make(NoteEvent::On, (uint8_t)(120 + i), 0xFF, false, 50.f + i * 4);
        b.drone = true;
        s.handleEvent(b);
    }
    for (int i = 0; i < 8; ++i)  // hammer 8 lead notes at the pool
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(i + 1), 0xFF, false, 60.f + i));
    peakOf(s, 8);
    CHECK(s.heldVoices() == 8, "pool saturated");
    CHECK(s.heldVoices() - s.heldLeadVoices() == 3, "all 3 backing voices survive the solo");
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
    // Exercises the engine paths: sub-osc (BASS/CELLO), noise (CELLO),
    // filter envelope (PLUCK/ACID/CELLO), drive (ACID), auto-vib (WHISTLE),
    // PWM pulse (STRINGS).
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

