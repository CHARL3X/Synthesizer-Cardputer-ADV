#include "patches.h"

#include <cstring>

namespace dsp {

namespace {

void setName(Patch& p, const char* n) {
    strncpy(p.name, n, sizeof p.name - 1);
    p.name[sizeof p.name - 1] = '\0';
}

// The factory bank, rebuilt as ten genuine instruments rather than ten raw
// waveforms. Each leans on the engine's real character makers — fat-saw
// detune, the paraphonic filter envelope (motion = the difference between
// "alive" and "cheap"), tube-ish drive, and the chorus/delay/reverb send
// block — so every slot is something you'd actually reach for. ACID and ORGAN
// are kept verbatim (the player asked); the other eight are new.
void buildBank(Patch* P) {
    // q — GLIDE: the signature, AND the literal power-on sound. It is therefore
    // the engine's default tone, deliberately: a clean, dry saw — 4 kHz cutoff,
    // a touch of resonance, no send FX, no filter bloom, no drive. Raw and
    // immediate, the "original GLIDE tone." Selecting fn+q now lands you on
    // exactly what you booted into (and what a factory reset gives), so the
    // first sound and the boot sound can never diverge again. The trick: state
    // NO synth overrides — a default-constructed Patch already holds the
    // default SynthParams, so this slot == the engine default by construction.
    // Only the tilt personality (sing on lean, brighten on roll) is set.
    {
        Patch& p = P[0];
        setName(p, "GLIDE");
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.55f;
        p.tiltRouteB = TiltRoute::Cutoff;
        p.tiltDepthB = 0.6f;
    }
    // The lush "analog poly" remake of GLIDE that used to live in slot 0 — a
    // wide three-saw stack with a slow per-note filter bloom, tube warmth, deep
    // chorus and a real room. Genuinely nice, just not the raw default the
    // player reaches for. Parked here verbatim as a ready-made candidate for
    // the planned expanded preset bank (drop it into a new slot when the bank
    // grows past ten):
    //   s.wave = Waveform::FatSaw; s.detuneCents = 18.f; s.drive = 1.7f;
    //   s.cutoffHz = 3200.f; s.resonance = 0.14f;
    //   s.fenvOct = 1.2f; s.fenvDecS = 0.5f;
    //   s.attackS = 0.012f; s.decayS = 0.35f; s.sustain = 0.70f; s.releaseS = 0.5f;
    //   s.glideS = 0.12f;
    //   s.chorusDepth = 0.55f; s.delayMix = 0.12f; s.delayFb = 0.26f; s.delaySync = 3;
    //   s.reverbMix = 0.28f; s.reverbSize = 0.62f;
    //   tiltRoute = Vibrato (0.55), tiltRouteB = Cutoff (0.6)
    // w — EPIANO: a mellow electric piano. Triangle body (few harmonics =
    // soft), a light tube drive for the Rhodes "bark," a faint env-gated
    // noise knock on the attack, and a short bright filter ping that decays
    // into the body. The classic chorus + plate make it sing under chords.
    {
        Patch& p = P[1];
        setName(p, "EPIANO");
        auto& s = p.synth;
        s.wave = Waveform::Triangle;
        s.drive = 1.5f;
        s.cutoffHz = 2600.f;
        s.resonance = 0.08f;
        s.fenvOct = 1.6f;          // a soft "tine" sparkle on the strike
        s.fenvDecS = 0.22f;
        s.attackS = 0.004f;
        s.decayS = 0.6f;
        s.sustain = 0.28f;         // EPs ring down, they don't hold flat
        s.releaseS = 0.4f;
        s.noiseLevel = 0.035f;     // key knock
        s.glideS = 0.05f;
        s.chorusDepth = 0.45f;
        s.reverbMix = 0.24f;
        s.reverbSize = 0.55f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.4f;
        p.tiltRouteB = TiltRoute::Vibrato;  // roll = a Rhodes wobble
        p.tiltDepthB = 0.4f;
    }
    // e — HARP: a bright, articulate plucked string. Full pluck (sustain 0),
    // a snappy filter attack that decays to a round body, and a cascading
    // delay into a long hall — runs and arpeggios bloom, and the short glide
    // gives grace-note slides between strings.
    {
        Patch& p = P[2];
        setName(p, "HARP");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.drive = 1.3f;
        s.cutoffHz = 1400.f;
        s.resonance = 0.22f;
        s.fenvOct = 2.2f;
        s.fenvDecS = 0.30f;
        s.attackS = 0.001f;
        s.decayS = 0.5f;
        s.sustain = 0.f;
        s.releaseS = 0.35f;
        s.glideS = 0.05f;
        s.chorusDepth = 0.2f;
        s.delayMix = 0.30f;
        s.delayFb = 0.36f;
        s.delaySync = 2;           // dotted-eighth: runs and arpeggios lock up
        s.reverbMix = 0.28f;
        s.reverbSize = 0.7f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.4f;
        p.tiltRouteB = TiltRoute::Volume;  // roll = pluck dynamics / swell
        p.tiltDepthB = 0.4f;
    }
    // r — BASS: a fat, modern analog bass. Saw over a square sub for weight,
    // pushed into the clipper for growl, with a snappy filter-env pluck (the
    // Moog "tuumph"). Tight envelope, kept dry so the low end stays punchy,
    // and a slow glide for sliding basslines — exactly the GLIDE move.
    {
        Patch& p = P[3];
        setName(p, "BASS");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.subLevel = 0.6f;
        s.drive = 2.4f;
        s.cutoffHz = 700.f;
        s.resonance = 0.28f;
        s.fenvOct = 2.0f;          // the percussive filter pluck
        s.fenvDecS = 0.14f;
        s.attackS = 0.002f;
        s.decayS = 0.14f;
        s.sustain = 0.55f;
        s.releaseS = 0.12f;
        s.glideS = 0.08f;
        s.chorusDepth = 0.10f;     // a hair of width, no low-end smear
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.5f;
        p.tiltRouteB = TiltRoute::Volume;  // roll = subtle dynamics (no bass wobble)
        p.tiltDepthB = 0.3f;
    }
    // t — ACID: resonant squelch; tilt IS the wah. Lean into it. A dub delay
    // with heavy regen and a little room give the 303 line space to breathe
    // between squelches. (Kept — the player likes this one.)
    {
        Patch& p = P[4];
        setName(p, "ACID");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.cutoffHz = 480.f;
        s.resonance = 0.8f;
        s.fenvOct = 3.f;
        s.fenvDecS = 0.18f;
        s.drive = 2.8f;
        s.sustain = 0.6f;
        s.decayS = 0.18f;
        s.releaseS = 0.16f;
        s.glideS = 0.10f;
        s.delayMix = 0.30f;
        s.delayTimeS = 0.28f;
        s.delayFb = 0.42f;
        s.reverbMix = 0.12f;
        s.reverbSize = 0.5f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
    }
    // y — PAD: a big, slow-evolving warm wash. Wide detuned saws, a long bloom
    // in and out, and a very slow filter opening (fenv over ~1.2s) so the pad
    // keeps moving the whole time it's held. Deep chorus + a large hall + a
    // slow echo — the good kind of cinematic, warm not eerie. Latch and layer.
    {
        Patch& p = P[5];
        setName(p, "PAD");
        auto& s = p.synth;
        s.wave = Waveform::FatSaw;
        s.detuneCents = 20.f;
        s.drive = 1.3f;
        s.cutoffHz = 2000.f;
        s.resonance = 0.12f;
        s.fenvOct = 1.8f;
        s.fenvDecS = 1.2f;         // the slow swell that keeps a held pad alive
        s.attackS = 0.7f;
        s.decayS = 0.8f;
        s.sustain = 0.8f;
        s.releaseS = 1.4f;
        s.glideS = 0.25f;
        s.chorusDepth = 0.8f;
        s.delayMix = 0.2f;
        s.delayTimeS = 0.30f;
        s.delayFb = 0.36f;
        s.reverbMix = 0.45f;
        s.reverbSize = 0.85f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.35f;
        p.tiltRouteB = TiltRoute::Volume;  // lean = brighten, roll = swell the wash
        p.tiltDepthB = 0.5f;
    }
    // u — LEAD: the expressive solo voice. Fat driven saws with a touch of
    // filter bite on the attack, a subtle singing vibrato (kept small so it
    // breathes, never seasick), and a DOTTED-EIGHTH delay locked to the jam
    // tempo — the Edge/Gilmour trick — so the solo sits in the groove over a
    // progression or latched backing.
    {
        Patch& p = P[6];
        setName(p, "LEAD");
        auto& s = p.synth;
        s.wave = Waveform::FatSaw;
        s.detuneCents = 10.f;
        s.drive = 2.6f;
        s.cutoffHz = 3400.f;
        s.resonance = 0.18f;
        s.fenvOct = 1.0f;
        s.fenvDecS = 0.25f;
        s.attackS = 0.01f;
        s.decayS = 0.2f;
        s.sustain = 0.8f;
        s.releaseS = 0.3f;
        s.autoVibCents = 3.5f;
        s.glideS = 0.09f;
        s.chorusDepth = 0.3f;
        s.delayMix = 0.30f;
        s.delayFb = 0.36f;
        s.delaySync = 2;           // dotted-eighth, locked to the jam tempo
        s.reverbMix = 0.24f;
        s.reverbSize = 0.62f;
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.6f;
        p.tiltRouteB = TiltRoute::Cutoff;  // sing on f/b, brighten on roll
        p.tiltDepthB = 0.6f;
    }
    // i — ORGAN: square + sub, instant on/off — tilt is the swell pedal. A
    // heavy chorus stands in for a Leslie's rotary shimmer, with a little
    // chapel room behind it. (Kept — the player likes this one.)
    {
        Patch& p = P[7];
        setName(p, "ORGAN");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.subLevel = 0.6f;
        s.attackS = 0.004f;
        s.decayS = 0.02f;
        s.sustain = 1.f;
        s.releaseS = 0.10f;
        s.cutoffHz = 5500.f;
        s.glideS = 0.04f;
        s.chorusDepth = 0.55f;
        s.reverbMix = 0.20f;
        s.reverbSize = 0.55f;
        p.tiltRoute = TiltRoute::Volume;
        p.tiltDepth = 0.8f;
    }
    // o — BRASS: a bold synth-brass section (Jupiter/OB lineage). Detuned saws
    // with a deliberately slower FILTER attack (fenvAtk) so each note swells
    // up into the "blat," pushed with drive for body. Great for stabs, chords
    // and powerful leads; chorus + hall widen the section.
    {
        Patch& p = P[8];
        setName(p, "BRASS");
        auto& s = p.synth;
        s.wave = Waveform::FatSaw;
        s.detuneCents = 12.f;
        s.drive = 2.2f;
        s.cutoffHz = 1400.f;
        s.resonance = 0.18f;
        s.fenvAtkS = 0.03f;        // the brass swell: filter rises into the note
        s.fenvOct = 2.4f;
        s.fenvDecS = 0.35f;
        s.attackS = 0.04f;
        s.decayS = 0.3f;
        s.sustain = 0.85f;
        s.releaseS = 0.25f;
        s.autoVibCents = 2.f;
        s.glideS = 0.08f;
        s.chorusDepth = 0.45f;
        s.delayMix = 0.10f;
        s.delayTimeS = 0.30f;
        s.delayFb = 0.28f;
        s.reverbMix = 0.26f;
        s.reverbSize = 0.6f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.5f;
        p.tiltRouteB = TiltRoute::Vibrato;  // roll = section shake on the brass
        p.tiltDepthB = 0.4f;
    }
    // p — GLASS: a crystalline struck bell / mallet. Triangle struck hard
    // (sustain 0) with a fast bright ping, then a long shimmering delay + hall
    // tail it rings out into. Ethereal and very different from the rest —
    // celeste/glockenspiel territory; the slow glide bends the bell on slides.
    {
        Patch& p = P[9];
        setName(p, "GLASS");
        auto& s = p.synth;
        s.wave = Waveform::Triangle;
        s.cutoffHz = 4500.f;
        s.resonance = 0.15f;
        s.fenvOct = 1.0f;
        s.fenvDecS = 0.10f;
        s.attackS = 0.001f;
        s.decayS = 0.8f;
        s.sustain = 0.f;
        s.releaseS = 0.6f;
        s.glideS = 0.05f;
        s.chorusDepth = 0.5f;
        s.delayMix = 0.30f;
        s.delayFb = 0.40f;
        s.delaySync = 2;           // dotted-eighth: the bell shimmer in tempo
        s.reverbMix = 0.45f;
        s.reverbSize = 0.85f;
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.4f;
        p.tiltRouteB = TiltRoute::Cutoff;  // roll = ring the bell brighter
        p.tiltDepthB = 0.5f;
    }
}

}  // namespace

const Patch* factoryPatches() {
    static Patch bank[kPatchCount];
    static bool built = false;
    if (!built) {
        buildBank(bank);
        built = true;
    }
    return bank;
}

}  // namespace dsp
