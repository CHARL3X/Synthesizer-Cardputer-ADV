#include "patches.h"

#include <cstring>

namespace dsp {

namespace {

void setName(Patch& p, const char* n) {
    strncpy(p.name, n, sizeof p.name - 1);
    p.name[sizeof p.name - 1] = '\0';
}

void buildBank(Patch* P) {
    // q — GLIDE: the signature. The sound the instrument booted with.
    {
        Patch& p = P[0];
        setName(p, "GLIDE");
        // The default sound leads with vibrato (lean forward to sing), and its
        // roll axis is a wah — so enabling dual mode on GLIDE gives the 2D
        // body (vibrato + filter) immediately, the showcase for the feature.
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.7f;
        p.tiltRouteB = TiltRoute::Cutoff;
        p.tiltDepthB = 0.6f;
        // synth = neutral defaults (saw, 4k cutoff, glide 120ms)
    }
    // w — WHISTLE: the digital slide whistle. Where this all started.
    {
        Patch& p = P[1];
        setName(p, "WHISTLE");
        auto& s = p.synth;
        s.wave = Waveform::Sine;
        s.glideMode = GlideMode::Always;
        s.glideS = 0.16f;
        s.attackS = 0.012f;
        s.decayS = 0.05f;
        s.sustain = 0.92f;
        s.releaseS = 0.15f;
        s.cutoffHz = 9000.f;
        s.autoVibCents = 10.f;
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.8f;
    }
    // e — PLUCK: filter-envelope pluck; sustain 0, slides on overlap.
    {
        Patch& p = P[2];
        setName(p, "PLUCK");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.attackS = 0.001f;
        s.decayS = 0.35f;
        s.sustain = 0.f;
        s.releaseS = 0.18f;
        s.glideS = 0.06f;
        s.cutoffHz = 650.f;
        s.resonance = 0.45f;
        s.fenvOct = 2.6f;
        s.fenvDecS = 0.16f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.4f;
    }
    // r — BASS: square + heavy sub, dark and slow-gliding.
    {
        Patch& p = P[3];
        setName(p, "BASS");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.subLevel = 0.9f;
        s.cutoffHz = 900.f;
        s.resonance = 0.25f;
        s.drive = 2.f;
        s.attackS = 0.002f;
        s.decayS = 0.10f;
        s.sustain = 0.8f;
        s.releaseS = 0.12f;
        s.glideS = 0.09f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.5f;
    }
    // t — ACID: resonant squelch; tilt IS the wah. Lean into it.
    {
        Patch& p = P[4];
        setName(p, "ACID");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.cutoffHz = 480.f;
        s.resonance = 0.85f;
        s.fenvOct = 3.f;
        s.fenvDecS = 0.18f;
        s.drive = 3.f;
        s.sustain = 0.6f;
        s.decayS = 0.18f;
        s.releaseS = 0.14f;
        s.glideS = 0.10f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
    }
    // y — PAD: fat detuned wash, slow bloom, long tails.
    {
        Patch& p = P[5];
        setName(p, "PAD");
        auto& s = p.synth;
        s.wave = Waveform::FatSaw;
        s.detuneCents = 14.f;
        s.attackS = 0.4f;
        s.decayS = 0.5f;
        s.sustain = 0.85f;
        s.releaseS = 0.9f;
        s.cutoffHz = 2200.f;
        s.resonance = 0.2f;
        s.glideS = 0.22f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.3f;
    }
    // u — LEAD: fat, driven, singing vibrato. The solo voice.
    {
        Patch& p = P[6];
        setName(p, "LEAD");
        auto& s = p.synth;
        s.wave = Waveform::FatSaw;
        s.detuneCents = 8.f;
        s.drive = 2.5f;
        s.cutoffHz = 3500.f;
        s.attackS = 0.008f;
        s.decayS = 0.15f;
        s.sustain = 0.75f;
        s.releaseS = 0.2f;
        s.autoVibCents = 6.f;
        s.glideS = 0.08f;
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.7f;
    }
    // i — ORGAN: square + sub, instant on/off — and tilt is the swell pedal.
    {
        Patch& p = P[7];
        setName(p, "ORGAN");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.subLevel = 0.6f;
        s.attackS = 0.004f;
        s.decayS = 0.02f;
        s.sustain = 1.f;
        s.releaseS = 0.08f;
        s.cutoffHz = 5000.f;
        s.glideS = 0.04f;
        p.tiltRoute = TiltRoute::Volume;
        p.tiltDepth = 0.8f;
    }
    // o — STRINGS: PWM ensemble wash (v0.5 Pulse wave's factory home).
    // Replaced GHOST: built FOR the drone workflow — latch it as a bed and it
    // breathes; solo on it and every slide blooms. Tilt is the section swell.
    {
        Patch& p = P[8];
        setName(p, "STRINGS");
        auto& s = p.synth;
        s.wave = Waveform::Pulse;
        s.glideMode = GlideMode::Always;
        s.glideS = 0.2f;
        s.attackS = 0.15f;
        s.decayS = 0.4f;
        s.sustain = 0.85f;
        s.releaseS = 0.6f;
        s.cutoffHz = 3200.f;
        s.resonance = 0.15f;
        s.subLevel = 0.2f;
        s.autoVibCents = 4.f;
        p.tiltRoute = TiltRoute::Volume;
        p.tiltDepth = 0.7f;
    }
    // p — CELLO: the bowed voice — hurdy-gurdy lineage, like the drones.
    // Replaced PERC: driven saw + sub for the body, env-gated noise as bow
    // rosin, a small filter "dig" on fresh bow strokes (paraphonic fenv never
    // re-snaps legato slides), and a singing tilt vibrato. Made for bends.
    {
        Patch& p = P[9];
        setName(p, "CELLO");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.drive = 2.2f;
        s.cutoffHz = 1300.f;
        s.resonance = 0.35f;
        s.attackS = 0.05f;
        s.decayS = 0.25f;
        s.sustain = 0.8f;
        s.releaseS = 0.3f;
        s.glideS = 0.12f;
        s.subLevel = 0.35f;
        s.noiseLevel = 0.07f;
        s.fenvOct = 0.7f;
        s.fenvDecS = 0.3f;
        s.autoVibCents = 5.f;
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.7f;
        p.tiltRouteB = TiltRoute::Cutoff;  // dual mode: roll = bow pressure
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
