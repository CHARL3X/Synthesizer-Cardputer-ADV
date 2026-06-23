// All user-adjustable state, with NVS persistence. "Nothing hardcoded":
// every parameter the synth or layout consumes lives here, is editable
// on-device, and survives reboot. Writes are debounced so a knob sweep
// doesn't hammer flash.
#pragma once
#include <cstdint>
#include "../dsp/params.h"
#include "../dsp/patches.h"
#include "../dsp/pitch.h"

namespace store {

using TiltRoute = dsp::TiltRoute;  // moved into dsp so patches carry it
using dsp::tiltRouteName;

// The G0 top button is an assignable momentary performance macro (the "trigger
// throw"). Like the tilt route, it picks one destination and a depth — but it's
// a global gesture, not a per-patch personality, so it lives in GlideConfig.
// Every action writes only into the per-frame live-mod fields (cutoffModOct,
// bendCents) or a local param copy (drive), never into the saved sound.
enum class TriggerAction : uint8_t { Muffle, Brighten, PitchDive, Drive, Count };

inline const char* triggerActionName(uint8_t a) {
    switch ((TriggerAction)a) {
        case TriggerAction::Muffle:    return "muffle (filter dn)";
        case TriggerAction::Brighten:  return "brighten (filter up)";
        case TriggerAction::PitchDive: return "pitch dive";
        case TriggerAction::Drive:     return "drive grit";
        default:                       return "?";
    }
}

// Short tag for the on-scope badge (≤6 chars to sit beside the loop status).
inline const char* triggerActionTag(uint8_t a) {
    switch ((TriggerAction)a) {
        case TriggerAction::Muffle:    return "MUFFLE";
        case TriggerAction::Brighten:  return "BRIGHT";
        case TriggerAction::PitchDive: return "DIVE";
        case TriggerAction::Drive:     return "GRIT";
        default:                       return "TRIG";
    }
}

struct GlideConfig {
    dsp::SynthParams synth;   // engine params (ADSR, glide, wave, filter...)
    dsp::Layout layout;       // key, scale, octave, row interval, lock

    bool stringMode = true;   // rows are mono "strings" with legato hand-off
                              // (guitar feel); off = free poly allocation
    bool octaveGlide = true;  // octave keys sweep held notes instead of jumping
    // Tilt is NEVER pitch bend. Two simultaneous axes: A = forward/back,
    // B = left/right roll. Each routes to its own destination (per patch).
    TiltRoute tiltRoute = TiltRoute::Vibrato;   // axis A route — on by default
    float tiltDepth = 0.6f;   // axis A depth, 0..1
    float tiltCenter = 0.f;   // axis A calibrated "flat" — wherever YOU hold it
    TiltRoute tiltRouteB = TiltRoute::Off;      // axis B (roll) route
    float tiltDepthB = 0.6f;  // axis B depth, 0..1
    float tiltCenterB = 0.f;  // axis B calibrated "flat"
    bool tiltOn = true;       // tilt expression on by default (single-axis)
    bool tiltDual = false;    // also use axis B (roll) — the 2D body, opt-in
    uint8_t currentPatch = 0; // active sound slot (fn+q..p)
    uint8_t jamRows = 1;      // 0=off, 1..2 bottom rows become tap-to-latch
                              // drones (-1 oct): the layering jam — backing
                              // rings underneath while you solo above. On by
                              // default (bottom row) so the backing is ready.
    uint8_t droneVoicing = 2; // 0=single note, 1=+octave, 2=+fifth (power
                              // chord) — one drone key voices a fuller backing
    uint8_t jamMotion = 3;    // 0=sustained, 1=pulse (re-strike together),
                              // 2=arp (re-strike one per beat), 3=progression
                              // (default) — turn jam rows on and you're ready
                              // to tap a chord loop
                              // (tap chords on the jam row — no timing — and
                              // they loop one diatonic chord per bar in tempo,
                              // gliding between changes; you solo on top)
    uint16_t jamBpm = 100;    // jam-motion / progression tempo
    uint8_t jamChordBeats = 4;// progression: beats each chord holds (1 bar)
    uint16_t bendMs = 250;    // time to reach full bend range
    uint8_t bendRange = 2;    // semitones
    uint8_t scopeMode = 1;    // 0=waveform scope, 1=pitch trail (default — the
                              // glide drawn over time, the instrument's whole
                              // point; watch a slide curve between the notes)
    bool bootSound = true;
    bool seenIntro = false;

    // ---- G0 trigger macro ("filter throw" and friends) ---------------------
    // Default reproduces the original behaviour (muffle, momentary) but at a
    // gentler depth — the old fixed throw dove a touch too far.
    uint8_t triggerAction = (uint8_t)TriggerAction::Muffle;
    float   triggerDepth  = 0.70f;  // 0..1 — how hard the action drives
    bool    triggerLatch  = false;  // false = momentary (hold), true = tap-latch

    // ---- solo/backing split (transient performance state, never persisted) --
    // When you switch sound (or shift octave) over a running jam, the backing
    // freezes onto the sound it was playing so the solo gets its own voice and
    // register. The backing keeps its own oscillator/filter/envelope; the two
    // layers share one reverb/delay "room" (the live patch's FX).
    dsp::SynthParams backingSynth;   // the frozen backing sound (when locked)
    bool backingLocked = false;      // true once the backing is held apart
};

GlideConfig& get();

void begin();                 // load from NVS (or defaults on first boot)
void markDirty();             // schedule a debounced persist
void tick(uint32_t nowMs);    // call each frame; performs the deferred write
void persistNow();
void resetDefaults();         // restore + persist

// ---- sound slots (fn+q..p) ----------------------------------------------
// Each of the 10 slots is a factory patch plus an optional user override
// saved over it (fn+shift+q..p). Overrides are versioned NVS blobs: a
// firmware that changes SynthParams silently falls back to factory.
void applyPatch(int slot);             // load slot -> working sound + tilt
bool savePatch(int slot);              // working sound -> slot override

// ---- solo/backing split -------------------------------------------------
// Freeze the current sound as the backing (called when the player switches
// sound over a running jam); unlock when the backing is gone. The live mods
// are neutralised in the frozen copy so the bed stays steady.
void lockBacking();
void unlockBacking();
bool backingLocked();
void clearOverride(int slot);          // back to factory
bool patchHasOverride(int slot);
const char* patchName(int slot);       // factory name

}  // namespace store
