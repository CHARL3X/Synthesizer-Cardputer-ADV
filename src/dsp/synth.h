// The synth: voice pool + allocation + global chain. Owns the chord-slide
// behavior: legato lane hand-offs (string mode), nearest-pitch stealing at
// the voice cap (free mode), retargets for octave sweeps. Pure C++ —
// everything here ports unchanged to dedicated hardware later.
#pragma once
#include "fx.h"
#include "params.h"
#include "saturator.h"
#include "svf.h"
#include "voice.h"

namespace dsp {

class Synth {
public:
    static constexpr int kBlockMax = 256;  // render block ceiling (device uses 128)

    void init(float sampleRate);
    // Two sounds at once: lead voices render with `lead`, the backing layer
    // (drones + loop + auto-progression) with `back`. They get separate
    // filters/envelopes but share one FX "room" (the lead's). Pass the same
    // struct for both (the convenience overload) when there's no split.
    void setParams(const SynthParams& lead, const SynthParams& back) {
        p_ = lead;
        pBack_ = back;
    }
    void setParams(const SynthParams& p) { setParams(p, p); }
    void handleEvent(const NoteEvent& ev);
    void render(float* out, int n);

    // feedback for the UI (read on the audio thread, published by the engine)
    bool leadActive() const { return leadIdx_ >= 0 && voices_[leadIdx_].active(); }
    float leadPitchMidi() const {
        // include bend so the readout tracks what you actually hear
        return leadIdx_ >= 0 ? voices_[leadIdx_].currentPitch() + p_.bendCents * 0.01f : 0.f;
    }
    float leadGlide01() const { return leadIdx_ >= 0 ? voices_[leadIdx_].glide01() : 1.f; }
    int heldVoices() const;
    int heldLeadVoices() const;  // held minus drones — what the cap governs
    int activeVoices() const;

private:
    Voice* findActiveById(uint8_t id);
    Voice* heldOnLane(uint8_t lane);
    Voice* nearestHeld(float pitch);
    Voice* alloc();
    void noteOn(const NoteEvent& ev);

    // paraphonic filter envelope: retriggered by fresh attacks only —
    // legato hand-offs and slides never re-snap the filter
    enum class FEnv : uint8_t { Idle, Attack, Decay };
    void advanceAD(FEnv& stage, float& env, float atkS, float decS, float blockDur);
    void advanceFenv(FEnv& stage, float& env, const SynthParams& p, float blockDur);
    // one mod source: an LFO with selectable shape and optional tempo-sync.
    float evalLfo(float& phase, float rateHz, uint8_t shape, uint8_t sync,
                  uint32_t& rng, float& hold, int n);

    Voice voices_[kMaxVoices];
    SynthParams p_;      // lead (the live, solo sound — carries the tilt mods)
    SynthParams pBack_;  // backing (drones/loop/progression; steady, no tilt)
    Svf svf_;            // lead filter
    Svf svfBack_;        // backing filter (own cutoff/env, no tilt)
    OutputStage out_;    // lead output stage
    OutputStage outBack_;// backing output stage
    Fx fx_;              // one shared "room" (reverb/delay/chorus), lead-driven
    float backBuf_[kBlockMax] = {0.f};  // backing sub-mix before it joins the lead
    float sr_ = 32000.f;
    float lfoPhase_ = 0.f;       // the dedicated 5.5 Hz auto-vibrato LFO
    float lfo1Phase_ = 0.f;      // mod-matrix LFO1
    float lfo2Phase_ = 0.f;      // mod-matrix LFO2
    uint32_t shRng1_ = 0x12345678u, shRng2_ = 0x9E3779B9u;  // per-LFO S&H noise
    float shHold1_ = 0.f, shHold2_ = 0.f;                   // held S&H values
    FEnv modEnvStage_ = FEnv::Idle;  // 2nd (mod) envelope — a routable AD source
    float modEnv_ = 0.f;
    uint32_t randRng_ = 0x2545F491u;  // per-note Random source: re-sampled on each
    float randHold_ = 0.f;            // fresh lead attack, held until the next
    float cutoffSm_ = 4000.f;
    float cutoffSmBack_ = 4000.f;
    float volSm_ = 0.f;
    float volSmBack_ = 0.f;
    uint32_t seq_ = 0;
    int8_t leadIdx_ = -1;

    FEnv fenvStage_ = FEnv::Idle;       // lead filter env (was the only one)
    float fenv_ = 0.f;
    FEnv fenvBackStage_ = FEnv::Idle;   // backing filter env — fixes the old quirk
    float fenvBack_ = 0.f;              // where a chord re-strike pumped the solo
};

}  // namespace dsp
