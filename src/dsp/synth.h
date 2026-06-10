// The synth: voice pool + allocation + global chain. Owns the chord-slide
// behavior: legato lane hand-offs (string mode), nearest-pitch stealing at
// the voice cap (free mode), retargets for octave sweeps. Pure C++ —
// everything here ports unchanged to dedicated hardware later.
#pragma once
#include "params.h"
#include "saturator.h"
#include "svf.h"
#include "voice.h"

namespace dsp {

class Synth {
public:
    void init(float sampleRate);
    void setParams(const SynthParams& p) { p_ = p; }
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

    Voice voices_[kMaxVoices];
    SynthParams p_;
    Svf svf_;
    OutputStage out_;
    float sr_ = 32000.f;
    float lfoPhase_ = 0.f;
    float cutoffSm_ = 4000.f;
    float volSm_ = 0.f;
    uint32_t seq_ = 0;
    int8_t leadIdx_ = -1;

    // paraphonic filter envelope: retriggered by fresh attacks only —
    // legato hand-offs and slides never re-snap the filter
    enum class FEnv : uint8_t { Idle, Attack, Decay };
    FEnv fenvStage_ = FEnv::Idle;
    float fenv_ = 0.f;
};

}  // namespace dsp
