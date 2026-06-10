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
    uint8_t jamRows = 0;      // 0=off, 1..2 bottom rows become tap-to-latch
                              // drones (-1 oct): the layering jam — backing
                              // rings underneath while you solo above
    uint8_t droneVoicing = 2; // 0=single note, 1=+octave, 2=+fifth (power
                              // chord) — one drone key voices a fuller backing
    uint8_t jamMotion = 0;    // 0=sustained, 1=pulse (re-strike together),
                              // 2=arp (re-strike one per beat) — living backing
    uint16_t jamBpm = 100;    // jam-motion tempo
    uint16_t bendMs = 250;    // time to reach full bend range
    uint8_t bendRange = 2;    // semitones
    uint8_t scopeMode = 0;    // 0=waveform scope, 1=pitch trail (the glide,
                              // drawn over time — watch a slide curve between
                              // the notes)
    bool bootSound = true;
    bool seenIntro = false;
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
void clearOverride(int slot);          // back to factory
bool patchHasOverride(int slot);
const char* patchName(int slot);       // factory name

}  // namespace store
