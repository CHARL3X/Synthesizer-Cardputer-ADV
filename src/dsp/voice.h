// One voice: wavetable oscillator (x3 phases for fat-saw), ADSR, and the
// exponential pitch slew that makes the whole instrument — every glide,
// chord slide, hammer-on and octave sweep is this one slew. Pure C++.
#pragma once
#include <cstdint>
#include "params.h"

namespace dsp {

class Voice {
public:
    enum class Env : uint8_t { Idle, Attack, Decay, Sustain, Release };

    void init(float sr) { sr_ = sr; }

    // Fresh note. If doGlide, start sounding at fromPitch and slew to pitch.
    void noteOn(uint8_t id, uint8_t lane, float pitch, float fromPitch, bool doGlide,
                uint32_t seq);

    // Legato hand-off: this sounding voice now answers to `id` and glides to
    // `pitch`; the envelope keeps running (no re-attack) — hammer-on feel.
    void legatoTo(uint8_t id, uint8_t lane, float pitch);

    // Re-aim the pitch with an explicit glide (octave sweeps, pull-offs).
    void retarget(float pitch);

    void retrigger();                       // attack again from current level
    void noteOff(float releaseS);
    void kill() { noteOff(0.008f); }        // panic: 8 ms fade, no hard click

    bool active() const { return env_ != Env::Idle; }
    bool held() const { return active() && env_ != Env::Release; }
    bool isDrone() const { return drone_; }
    void setDrone(bool d) { drone_ = d; }
    uint8_t id() const { return id_; }
    uint8_t lane() const { return lane_; }
    void setLane(uint8_t lane) { lane_ = lane; }
    uint32_t seq() const { return seq_; }
    float level() const { return lvl_; }
    float currentPitch() const { return curPitch_; }
    float targetPitch() const { return tgtPitch_; }

    // 0..1 progress of the current glide (1 = arrived).
    float glide01() const;

    // Adds into out[]. centsOffset = global bend + vibrato for this block.
    void render(float* out, int n, const SynthParams& p, float centsOffset);

private:
    float sr_ = 32000.f;
    Env env_ = Env::Idle;
    float lvl_ = 0.f;
    float relRate_ = 0.f;
    uint8_t id_ = 0;
    uint8_t lane_ = 0xFF;
    uint32_t seq_ = 0;
    float curPitch_ = 60.f, tgtPitch_ = 60.f, startPitch_ = 60.f;
    uint32_t ph_[4] = {0, 0, 0, 0};  // main, fat up, fat down, sub (-1 oct)
    uint32_t rng_ = 0x9E3779B9u;     // per-voice noise state
    float pwmPhase_ = 0.f;           // slow pulse-width LFO (Pulse wave)
    bool drone_ = false;             // jam-row backing voice
};

}  // namespace dsp
