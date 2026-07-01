// Host-side sanity tests for the pure DSP core (pio run -e native).
// This compiling and passing on a PC is the proof that dsp/ has no hardware
// dependencies -- the porting boundary the whole architecture promises.
#ifdef GLIDE_HOST_BUILD

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dsp/demo_gen.h"
#include "dsp/morph.h"
#include "dsp/patches.h"
#include "dsp/pitch.h"
#include "dsp/sound_gen.h"
#include "dsp/synth.h"
#include "storage/patch_codec.h"  // host-safe codec (in env:native build_src_filter)

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

    // ---- regression: two notes on one row struck twice in quick succession --
    // A lane carries exactly ONE voice, and its release sends a note-off only
    // for the stack owner. A legato re-press must therefore GLIDE that lane
    // voice, never resurrect a still-fading same-id tail as a SECOND held voice
    // — otherwise the non-owner key's release sends no off and that voice is
    // stranded droning. This is the real "G+H, then G+H rapidly, one sticks"
    // bug; round 2 fires while round 1's release tail is still ringing.
    {
        p = SynthParams();
        p.glideS = 0.03f;
        p.releaseS = 0.3f;  // a long-ish tail so round 2 overlaps it
        s.setParams(p);
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 100);  // start from a truly empty pool
        CHECK(s.activeVoices() == 0, "pool empty before the row re-strike test");

        const uint8_t kG = 30, kH = 31;  // two keys on one row -> same lane 0
        // round 1: press G, then H legato; release H (G is a non-owner: no off)
        s.handleEvent(NoteEvent::make(NoteEvent::On, kG, 0, false, 67.f));
        s.handleEvent(NoteEvent::make(NoteEvent::On, kH, 0, true, 69.f));
        peakOf(s, 4);
        s.handleEvent(NoteEvent::make(NoteEvent::Off, kH, 0, false, 0.f));
        peakOf(s, 1);  // barely into H's release tail — still active
        // round 2: same gesture while round 1's tail still rings
        s.handleEvent(NoteEvent::make(NoteEvent::On, kG, 0, false, 67.f));
        s.handleEvent(NoteEvent::make(NoteEvent::On, kH, 0, true, 69.f));
        CHECK(s.heldLeadVoices() == 1, "row re-strike keeps one held voice on the lane");
        peakOf(s, 4);
        s.handleEvent(NoteEvent::make(NoteEvent::Off, kH, 0, false, 0.f));
        peakOf(s, 150);  // run past every release tail
        CHECK(s.activeVoices() == 0, "no voice stranded after the row re-strike");
        // restore the fast glide the sections below were written against
        p = SynthParams();
        p.glideS = 0.05f;
        s.setParams(p);
    }

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

    // ---- modulation matrix ------------------------------------------------
    {
        ModSlot ms = ModSlot::make(ModSource::LFO1, ModDest::Amp, 0.5f);
        CHECK(ms.src == (uint8_t)ModSource::LFO1 && ms.dest == (uint8_t)ModDest::Amp &&
                  fabsf(ms.depth - 0.5f) < 1e-6f,
              "ModSlot::make round-trips its fields");

        // windowed peak swing over a sustained note: steady => mn≈mx, tremolo => mx>>mn
        auto windowSwing = [](Synth& syn, float& mn, float& mx) {
            mn = 1e9f;
            mx = 0.f;
            float b[kBlock];
            for (int w = 0; w < 50; ++w) {
                float wp = 0.f;
                for (int k = 0; k < 4; ++k) {  // 16 ms window
                    syn.render(b, kBlock);
                    for (int i = 0; i < kBlock; ++i) {
                        const float a = fabsf(b[i]);
                        if (a > wp) wp = a;
                    }
                }
                if (wp < mn) mn = wp;
                if (wp > mx) mx = wp;
            }
        };

        SynthParams base;  // a steady sustained tone to watch the matrix act on
        base.attackS = 0.005f;
        base.decayS = 0.01f;
        base.sustain = 1.f;
        base.releaseS = 0.2f;
        base.wave = Waveform::Saw;

        // neutral (no routing) -> amplitude is steady
        Synth sn;
        sn.init(kSr);
        sn.setParams(base);
        sn.handleEvent(NoteEvent::make(NoteEvent::On, 5, 0xFF, false, 69.f));
        peakOf(sn, 20);  // settle into sustain
        float mn0, mx0;
        windowSwing(sn, mn0, mx0);
        CHECK(mx0 > 1e-4f && mx0 < mn0 * 1.5f, "neutral sustain is steady (matrix inert)");

        // deep LFO1 -> Amp -> obvious tremolo
        Synth sm;
        sm.init(kSr);
        SynthParams mod = base;
        mod.lfo1RateHz = 8.f;
        mod.lfo1Shape = (uint8_t)LfoShape::Sine;
        mod.slots[0] = ModSlot::make(ModSource::LFO1, ModDest::Amp, 0.9f);
        sm.setParams(mod);
        sm.handleEvent(NoteEvent::make(NoteEvent::On, 5, 0xFF, false, 69.f));
        peakOf(sm, 20);
        float mn1, mx1;
        windowSwing(sm, mn1, mx1);
        CHECK(mx1 > mn1 * 3.f, "LFO1->Amp produces deep tremolo");

        // ModEnv -> Cutoff stays audible and bounded after a fresh attack
        SynthParams me = base;
        me.modEnvAtkS = 0.01f;
        me.modEnvDecS = 0.4f;
        me.slots[0] = ModSlot::make(ModSource::ModEnv, ModDest::Cutoff, 0.8f);
        sm.setParams(me);
        sm.handleEvent(NoteEvent::make(NoteEvent::On, 6, 0xFF, false, 64.f));
        const float pk = peakOf(sm, 60);
        CHECK(pk > 0.005f && pk < 1.4f, "ModEnv->Cutoff renders audible & bounded");

        // new sources/dests render finite & bounded: tilt->drive, random->reverb,
        // LFO->delay, and the tilt->pitch guard (must not crash / blow up).
        SynthParams nx = base;
        nx.tiltAVal = 0.8f;  // pretend the device is leaned over
        nx.slots[0] = ModSlot::make(ModSource::TiltA, ModDest::Drive, 0.7f);
        nx.slots[1] = ModSlot::make(ModSource::Random, ModDest::Reverb, 0.5f);
        nx.slots[2] = ModSlot::make(ModSource::LFO1, ModDest::Delay, 0.4f);
        nx.slots[3] = ModSlot::make(ModSource::TiltA, ModDest::Pitch, 1.0f);  // refused (no-op)
        sm.setParams(nx);
        sm.handleEvent(NoteEvent::make(NoteEvent::On, 7, 0xFF, false, 69.f));
        const float pk2 = peakOf(sm, 40);
        CHECK(pk2 > 0.005f && pk2 < 1.6f, "new sources/dests render bounded");
        sm.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(sm, 80);
    }

    // ---- filter modes (LP/HP/BP/notch) render finite & bounded ------------
    {
        Synth sf;
        sf.init(kSr);
        for (int m = 0; m < (int)FilterMode::Count; ++m) {
            SynthParams fp;
            fp.glideS = 0.05f;
            fp.cutoffHz = 1200.f;
            fp.resonance = 0.5f;
            fp.filterMode = (uint8_t)m;
            sf.setParams(fp);
            sf.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(80 + m), 0xFF, false, 69.f));
            const float pk = peakOf(sf, 30);
            CHECK(pk > 0.f && pk < 1.5f, "filter mode renders bounded");
            sf.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
            peakOf(sf, 60);
        }
    }

    // ---- generative sound engine (sound_gen) ------------------------------
    // The soul of the "your instrument is yours" feature: every field a roll
    // produces must be in range and playable, the roll must be deterministic
    // (so a per-device seed gives a stable unique bank), and a mutate must stay
    // in bounds while amount==0 is an exact identity.
    {
        // validate every field of a generated patch is inside the engine's
        // musical/persisted bounds — a roll must NEVER make a dead or blown sound.
        auto validParams = [](const SynthParams& s) -> bool {
            return s.cutoffHz >= 80.f && s.cutoffHz <= 12000.f &&
                   s.resonance >= 0.f && s.resonance <= 0.95f &&
                   s.attackS >= 0.f && s.attackS <= 2.f &&
                   s.decayS >= 0.f && s.decayS <= 2.f &&
                   s.sustain >= 0.f && s.sustain <= 1.f &&
                   s.releaseS >= 0.f && s.releaseS <= 3.f &&
                   s.glideS >= 0.f && s.glideS <= 2.f &&
                   s.detuneCents >= 0.f && s.detuneCents <= 50.f &&
                   s.fenvOct >= 0.f && s.fenvOct <= 6.f &&
                   s.fenvDecS >= 0.01f && s.fenvDecS <= 2.f &&
                   s.subLevel >= 0.f && s.subLevel <= 1.f &&
                   s.noiseLevel >= 0.f && s.noiseLevel <= 1.f &&
                   s.drive >= 1.f && s.drive <= 8.f &&
                   s.reverbMix >= 0.f && s.reverbMix <= 1.f &&
                   s.delayMix >= 0.f && s.delayMix <= 1.f &&
                   s.chorusDepth >= 0.f && s.chorusDepth <= 1.f &&
                   s.delaySync < kDelaySyncCount &&
                   s.lfo1RateHz > 0.f && s.lfo2RateHz > 0.f &&
                   (int)s.wave < (int)Waveform::Count &&
                   s.filterMode < (uint8_t)FilterMode::Count &&
                   s.lfo1Shape < (uint8_t)LfoShape::Count &&
                   s.lfo2Shape < (uint8_t)LfoShape::Count;
        };
        auto validSlots = [](const SynthParams& s) -> bool {
            for (int i = 0; i < kModSlots; ++i) {
                if (s.slots[i].src >= (uint8_t)ModSource::Count) return false;
                if (s.slots[i].dest >= (uint8_t)ModDest::Count) return false;
                if (s.slots[i].depth < -1.f || s.slots[i].depth > 1.f) return false;
            }
            return true;
        };
        auto validTilt = [](const GenPatch& g) -> bool {
            return g.tiltRoute < (uint8_t)TiltRoute::Count &&
                   g.tiltRouteB < (uint8_t)TiltRoute::Count &&
                   g.tiltDepth >= 0.f && g.tiltDepth <= 1.f &&
                   g.tiltDepthB >= 0.f && g.tiltDepthB <= 1.f;
        };
        // field-wise equality (NOT memcmp — GenPatch has padding bytes between
        // its uint8/float members that two separate constructions needn't match)
        auto genEq = [](const GenPatch& a, const GenPatch& b) -> bool {
            const SynthParams& x = a.synth;
            const SynthParams& y = b.synth;
            bool eq = x.wave == y.wave && x.glideMode == y.glideMode &&
                      x.attackS == y.attackS && x.decayS == y.decayS && x.sustain == y.sustain &&
                      x.releaseS == y.releaseS && x.glideS == y.glideS && x.cutoffHz == y.cutoffHz &&
                      x.resonance == y.resonance && x.filterMode == y.filterMode &&
                      x.detuneCents == y.detuneCents && x.fenvOct == y.fenvOct && x.fenvDecS == y.fenvDecS &&
                      x.subLevel == y.subLevel && x.noiseLevel == y.noiseLevel && x.drive == y.drive &&
                      x.chorusDepth == y.chorusDepth && x.delayMix == y.delayMix && x.delayFb == y.delayFb &&
                      x.delaySync == y.delaySync && x.reverbMix == y.reverbMix && x.reverbSize == y.reverbSize &&
                      x.lfo1RateHz == y.lfo1RateHz && x.lfo1Shape == y.lfo1Shape &&
                      x.lfo2RateHz == y.lfo2RateHz && x.lfo2Shape == y.lfo2Shape &&
                      x.modEnvAtkS == y.modEnvAtkS && x.modEnvDecS == y.modEnvDecS &&
                      a.tiltRoute == b.tiltRoute && a.tiltDepth == b.tiltDepth &&
                      a.tiltRouteB == b.tiltRouteB && a.tiltDepthB == b.tiltDepthB;
            for (int i = 0; i < kModSlots; ++i)
                eq = eq && x.slots[i].src == y.slots[i].src && x.slots[i].dest == y.slots[i].dest &&
                     x.slots[i].depth == y.slots[i].depth;
            return eq;
        };

        // determinism: same seed -> identical patch (the per-device-bank
        // contract — a unit's slots are reproducible from its stored seed).
        GenPatch a1 = generateSound(0xC0FFEEu);
        GenPatch a2 = generateSound(0xC0FFEEu);
        CHECK(genEq(a1, a2), "generateSound is deterministic");
        GenPatch b1 = generateSound(0xC0FFEFu);
        CHECK(!genEq(a1, b1), "different seeds -> different patch");

        // sweep many seeds: every roll is in range and renders finite & bounded
        Synth sg;
        sg.init(kSr);
        bool sawGlideAlways = false, sawMod = false;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            GenPatch g = generateSound(seed * 2654435761u + 1u);
            CHECK(validParams(g.synth), "generated params in range");
            CHECK(validSlots(g.synth), "generated mod slots in range");
            CHECK(validTilt(g), "generated tilt in range");
            if (g.synth.glideMode == GlideMode::Always) sawGlideAlways = true;
            for (int i = 0; i < kModSlots; ++i)
                if (g.synth.slots[i].src != (uint8_t)ModSource::None) sawMod = true;
            sg.setParams(g.synth);
            sg.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(seed & 0x7F), 0, false, 64.f));
            const float pk = peakOf(sg, 24);
            CHECK(pk >= 0.f && pk < 1.6f, "generated patch renders finite & bounded");
            sg.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
            peakOf(sg, 40);
        }
        CHECK(sawGlideAlways, "some rolls are always-glide");
        CHECK(sawMod, "some rolls wire the mod matrix");

        // mutate: amount 0 is an exact identity
        GenPatch base = generateSound(42u);
        GenPatch m0 = mutateSound(base, 0.f, 123u);
        CHECK(genEq(base, m0), "mutate amount=0 is identity");

        // mutate stays in range + renders bounded, at gentle and wild amounts
        for (uint32_t seed = 1; seed <= 100; ++seed) {
            const float amt = (seed % 2) ? 0.15f : 0.9f;  // gentle vs wild
            GenPatch m = mutateSound(base, amt, seed * 40503u + 7u);
            CHECK(validParams(m.synth), "mutated params in range");
            CHECK(validSlots(m.synth), "mutated mod slots in range");
            CHECK(validTilt(m), "mutated tilt in range");
            sg.setParams(m.synth);
            sg.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(seed & 0x7F), 0, false, 62.f));
            const float pk = peakOf(sg, 24);
            CHECK(pk >= 0.f && pk < 1.6f, "mutated patch renders finite & bounded");
            sg.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
            peakOf(sg, 40);
        }

        // a gentle mutate keeps the patch's character: most continuous params
        // should stay close to the base (it's a neighbour, not a new roll).
        GenPatch gentle = mutateSound(base, 0.12f, 999u);
        int near = 0, tot = 0;
        auto rel = [&](float a, float b, float span) { ++tot; if (fabsf(a - b) <= 0.35f * span) ++near; };
        rel(gentle.synth.cutoffHz, base.synth.cutoffHz, 11000.f);
        rel(gentle.synth.resonance, base.synth.resonance, 0.9f);
        rel(gentle.synth.sustain, base.synth.sustain, 1.f);
        rel(gentle.synth.drive, base.synth.drive, 5.f);
        rel(gentle.synth.releaseS, base.synth.releaseS, 2.f);
        CHECK(near >= tot - 1, "a gentle mutate preserves character (neighbour, not a new roll)");

        // ---- patch naming: deterministic, terminated, content-sensitive -----
        CHECK(patchHash(base) == patchHash(base), "patchHash deterministic");
        CHECK(patchHash(base) != patchHash(generateSound(7u)), "patchHash separates patches");
        char nm[24], nm2[24];
        nameForSeed(patchHash(base), nm, sizeof nm);
        nameForSeed(patchHash(base), nm2, sizeof nm2);
        CHECK(strcmp(nm, nm2) == 0, "nameForSeed deterministic");
        CHECK(strlen(nm) > 0 && strlen(nm) < sizeof nm, "name fits and is non-empty");
        bool hasDash = false;
        for (const char* c = nm; *c; ++c) {
            const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-';
            CHECK(ok, "name uses only filename-safe chars");
            if (*c == '-') hasDash = true;
        }
        CHECK(hasDash, "name has the adj-noun-hex shape");
        // a tiny cap must still be safely terminated, never overrun
        char tiny[4];
        nameForSeed(0xABCDEF12u, tiny, sizeof tiny);
        CHECK(tiny[3] == '\0' || strlen(tiny) < 4, "name respects a small cap");

        // short label (slot display): one word, fits the cramped status bar,
        // shares its noun with the full name so they read as the same sound.
        char sh[8];
        shortNameForSeed(patchHash(base), sh, sizeof sh);
        CHECK(strlen(sh) > 0 && strlen(sh) <= 6, "short name is a compact (<=6) label");
        for (const char* c = sh; *c; ++c)
            CHECK(*c >= 'a' && *c <= 'z', "short name is a single lowercase word");
        CHECK(strstr(nm, sh) != nullptr, "short name's noun appears in the full name");
    }

    // ---- patch codec: the tagged save format (names + forward-compat) -------
    {
        using store::PatchData;
        uint8_t buf[512];

        // (a) a human name and a scalar field both round-trip
        PatchData a;
        a.synth.cutoffHz = 1234.f;
        std::strcpy(a.name, "my-bass");
        const size_t na = store::encodePatch(a, buf, sizeof buf);
        CHECK(na > 0, "encode produced a stream");
        PatchData out;
        out.synth.cutoffHz = 999.f;  // seeded different -> proves overwrite
        CHECK(store::decodePatch(buf, na, out), "decode accepts the stream");
        CHECK(fabsf(out.synth.cutoffHz - 1234.f) < 0.5f, "scalar field round-trips");
        CHECK(strcmp(out.name, "my-bass") == 0, "name round-trips");

        // (b) an empty name emits NO extra bytes and decodes back empty
        PatchData b;
        b.synth.cutoffHz = 1234.f;
        const size_t nb = store::encodePatch(b, buf, sizeof buf);
        CHECK(nb > 0 && nb < na, "empty name adds no record (shorter stream)");
        PatchData out2;
        CHECK(store::decodePatch(buf, nb, out2) && out2.name[0] == '\0',
              "empty name decodes empty (status quo)");

        // (c) an over-long name truncates to the field cap, never overruns
        PatchData c;
        memset(c.name, 'x', sizeof c.name - 1);
        c.name[sizeof c.name - 1] = '\0';
        const size_t nc = store::encodePatch(c, buf, sizeof buf);
        PatchData out3;
        CHECK(store::decodePatch(buf, nc, out3) && strlen(out3.name) <= 20,
              "long name truncated to <=20");

        // (d) forward-compat: an UNKNOWN string record is skipped, scalars before
        // it survive. The name is emitted last, so flipping its tag to an unknown
        // value models an OLD firmware meeting a NEW string field.
        PatchData d;
        d.synth.cutoffHz = 1234.f;
        const size_t nd0 = store::encodePatch(d, buf, sizeof buf);  // no name yet
        std::strcpy(d.name, "test");
        const size_t nd1 = store::encodePatch(d, buf, sizeof buf);  // name record at [nd0..]
        CHECK(nd1 > nd0 && buf[nd0] == 110, "name record is last (tag 110)");
        buf[nd0] = 200;  // unknown tag, still the T_STR type byte
        PatchData out4;
        out4.synth.cutoffHz = 999.f;
        CHECK(store::decodePatch(buf, nd1, out4), "stream with an unknown string tag decodes");
        CHECK(fabsf(out4.synth.cutoffHz - 1234.f) < 0.5f, "scalars before the unknown tag applied");
        CHECK(out4.name[0] == '\0', "unknown string tag skipped, name left unset");
    }

    // ---- synth morph: perceptual interpolation between two sounds ----------
    {
        SynthParams a;  // neutral defaults
        SynthParams b;
        b.wave = Waveform::Square;
        b.cutoffHz = 400.f;
        b.attackS = 0.5f;
        b.sustain = 0.2f;
        b.resonance = 0.9f;
        b.chorusDepth = 1.f;
        b.voiceCount = 2;
        b.slots[0] = ModSlot::make(ModSource::LFO1, ModDest::Cutoff, 0.8f);

        const SynthParams m0 = morphParams(a, b, 0.f);
        CHECK(m0.cutoffHz == a.cutoffHz && m0.wave == a.wave && m0.sustain == a.sustain,
              "morph t=0 is exactly a");
        const SynthParams m1 = morphParams(a, b, 1.f);
        CHECK(m1.cutoffHz == b.cutoffHz && m1.wave == b.wave && m1.sustain == b.sustain,
              "morph t=1 is b's sound");
        CHECK(m1.voiceCount == a.voiceCount, "voiceCount never morphs (no voice yanking)");

        const SynthParams mh = morphParams(a, b, 0.5f);
        const float gm = sqrtf((a.cutoffHz + 1e-3f) * (b.cutoffHz + 1e-3f)) - 1e-3f;
        CHECK(fabsf(mh.cutoffHz - gm) < 1.f, "cutoff lerps geometrically");
        CHECK(fabsf(mh.sustain - 0.45f) < 0.01f, "sustain lerps linearly");
        CHECK(morphParams(a, b, 0.49f).wave == a.wave && mh.wave == b.wave,
              "discretes switch at the midpoint");
        CHECK(mh.resonance >= a.resonance && mh.resonance <= b.resonance,
              "linear lerp stays inside the endpoints");
        CHECK(mh.attackS > a.attackS && mh.attackS < b.attackS, "times move monotonically");
        // mismatched mod routing: depth breathes out, swaps, breathes in
        CHECK(fabsf(mh.slots[0].depth) < 0.05f, "mismatched slot depth ~0 at midpoint");
        CHECK(morphParams(a, b, 0.75f).slots[0].depth > 0.3f, "b's slot fades in past midpoint");
        // live-mod fields ride from a, never blended
        SynthParams a2 = a;
        a2.bendCents = 123.f;
        CHECK(morphParams(a2, b, 1.f).bendCents == 123.f, "live-mod fields stay the caller's");
    }

    // ---- demo melody generator: seeded, bounded, musical --------------------
    {
        DemoMelody m1, m2;
        m1.seed(42);
        m2.seed(42);
        int slides = 0, attacks = 0, rests = 0;
        bool inRange = true, durOk = true, sameSeq = true;
        for (int i = 0; i < 300; ++i) {
            const DemoNote a = m1.next(5);  // pentatonic-sized scale
            const DemoNote b = m2.next(5);
            sameSeq = sameSeq && a.type == b.type && a.degree == b.degree &&
                      a.beats4 == b.beats4;
            inRange = inRange && a.degree >= 0 && a.degree <= 10;
            durOk = durOk && a.beats4 >= 1 && a.beats4 <= 8;
            if (a.type == DemoNote::Slide) ++slides;
            else if (a.type == DemoNote::Attack) ++attacks;
            else ++rests;
        }
        CHECK(sameSeq, "demo melody is deterministic per seed");
        CHECK(inRange, "demo degrees stay inside two octaves");
        CHECK(durOk, "demo durations are 1..8 quarter-beats");
        CHECK(slides > 30, "the demo actually slides (glide on display)");
        CHECK(rests > 10 && attacks > 30, "phrases breathe and re-attack");
        DemoMelody m3;
        m3.seed(7);
        bool ok7 = true;  // a 7-note scale widens the range accordingly
        for (int i = 0; i < 300; ++i) {
            const DemoNote a = m3.next(7);
            ok7 = ok7 && a.degree >= 0 && a.degree <= 14;
        }
        CHECK(ok7, "range follows the scale length");
    }

    if (failures == 0) {
        printf("GLIDE dsp: all checks passed\n");
        return 0;
    }
    printf("GLIDE dsp: %d FAILURES\n", failures);
    return 1;
}

#endif  // GLIDE_HOST_BUILD

