#include "voice.h"
#include <cmath>
#include "pitch.h"
#include "wavetables.h"

namespace dsp {

namespace {
// Per-voice gain leaves headroom for 8 voices into the soft clipper.
constexpr float kVoiceGain = 0.22f;
constexpr float kFatGain = 0.45f;   // 3 oscillators summed
constexpr float kPulseGain = 0.7f;  // saw-difference pulse peaks ~1.8 at thin widths
constexpr float kMinAttackS = 0.001f;
constexpr float kMinSegS = 0.002f;
constexpr float kTwoPi = 6.28318530718f;
// Envelopes are analog-style (RC charge/discharge), not linear ramps — the
// single biggest "real synth vs cheap keyboard" difference. The attack chases a
// target ABOVE 1 and clamps at 1, giving the front-loaded capacitor-charge
// curve; decay/release are true exponential falls. kEnvReach ≈ ln(100): a
// segment lands within ~1% of its destination in its set time.
constexpr float kAttackTarget = 1.3f;
constexpr float kEnvReach = 4.6f;
// attack reaches 1.0 (not the 1.3 target) in attackS -> this shorter constant
constexpr float kAtkReach = 1.465f;
constexpr float kPwmRateHz = 0.6f;  // slow width sweep — the classic PWM shimmer

inline uint32_t freqToInc(float freq, float sr) {
    return (uint32_t)(freq / sr * 4294967296.f);
}
}  // namespace

void Voice::noteOn(uint8_t id, uint8_t lane, float pitch, float fromPitch, bool doGlide,
                   uint32_t seq) {
    id_ = id;
    lane_ = lane;
    seq_ = seq;
    drone_ = false;    // reused voice: caller re-flags via setDrone
    backing_ = false;  // same: caller re-flags via setBacking
    tgtPitch_ = pitch;
    curPitch_ = doGlide ? fromPitch : pitch;
    startPitch_ = curPitch_;
    env_ = Env::Attack;  // attack resumes from current lvl_ (0 if idle) — no click
    // free-running phases: deliberately NOT reset, so re-attacks don't phase-lock
}

void Voice::legatoTo(uint8_t id, uint8_t lane, float pitch) {
    id_ = id;
    lane_ = lane;
    startPitch_ = curPitch_;
    tgtPitch_ = pitch;
    if (env_ == Env::Release) env_ = Env::Attack;  // grabbed during its tail
}

void Voice::retarget(float pitch) {
    startPitch_ = curPitch_;
    tgtPitch_ = pitch;
}

void Voice::retrigger() {
    if (env_ != Env::Idle) env_ = Env::Attack;
}

void Voice::noteOff(float releaseS) {
    if (env_ == Env::Idle) return;
    if (lvl_ < 0.01f) {  // released while still inaudible (<1%, e.g. early in a
        lvl_ = 0.f;      // long attack): just free the slot, no pointless tail
        env_ = Env::Idle;
        return;
    }
    if (releaseS < kMinSegS) releaseS = kMinSegS;
    // exponential release: lvl reaches ~-60 dB in releaseS from full scale, so
    // quieter notes finish proportionally sooner — the pool never starves
    relCoef_ = expf(-kEnvReach * 1.5f / (releaseS * sr_));
    env_ = Env::Release;
}

float Voice::glide01() const {
    const float span = fabsf(tgtPitch_ - startPitch_);
    if (span < 0.01f) return 1.f;
    return 1.f - fabsf(tgtPitch_ - curPitch_) / span;
}

void Voice::render(float* out, int n, const SynthParams& p, float centsOffset) {
    if (env_ == Env::Idle) return;

    // ---- pitch slew (the soul) -----------------------------------------
    const float blockDur = n / sr_;
    float newPitch = tgtPitch_;
    if (p.glideS > 0.0005f) {
        const float alpha = 1.f - expf(-blockDur / p.glideS);
        newPitch = curPitch_ + (tgtPitch_ - curPitch_) * alpha;
        if (fabsf(tgtPitch_ - newPitch) < 0.002f) newPitch = tgtPitch_;
    }
    const float off = centsOffset * 0.01f;
    const float f0 = midiToFreq(curPitch_ + off);
    const float f1 = midiToFreq(newPitch + off);
    curPitch_ = newPitch;

    const bool fat = (p.wave == Waveform::FatSaw);
    const bool pulse = (p.wave == Waveform::Pulse);
    const float* tbl = tableFor(p.wave, f1);

    // Pulse = saw(ph) - saw(ph + width): band-limited PWM from the existing
    // saw tables, zero DC at any width. The width breathes under a slow LFO.
    uint32_t pwOff = 0;
    if (pulse) {
        pwmPhase_ += kTwoPi * kPwmRateHz * blockDur;
        if (pwmPhase_ > kTwoPi) pwmPhase_ -= kTwoPi;
        const float pw = 0.5f + 0.4f * sinf(pwmPhase_);  // 10%..90%
        pwOff = (uint32_t)(pw * 4294967296.f);
    }

    // linear phase-increment ramp across the block = smooth intra-block glide
    float inc0 = (float)freqToInc(f0, sr_);
    float inc1 = (float)freqToInc(f1, sr_);
    float incStep = (inc1 - inc0) / n;

    // fat-saw detune multipliers (computed once per block)
    float mUp = 1.f, mDn = 1.f;
    if (fat) {
        mUp = exp2f(p.detuneCents / 1200.f);
        mDn = 1.f / mUp;
    }

    // ---- envelope coefficients (analog RC curves; live edits apply) -----
    const float aS = p.attackS < kMinAttackS ? kMinAttackS : p.attackS;
    const float dS = p.decayS < kMinSegS ? kMinSegS : p.decayS;
    const float ka = 1.f - expf(-kAtkReach / (aS * sr_));   // charge toward target
    const float kd = 1.f - expf(-kEnvReach / (dS * sr_));    // fall toward sustain

    float inc = inc0;
    const float gain = fat ? kVoiceGain * kFatGain : kVoiceGain;

    const bool sub = p.subLevel > 0.001f;
    const bool noise = p.noiseLevel > 0.001f;
    const float* subTbl = gTables[TblSqrLo];  // sub lives an octave down: always low

    for (int i = 0; i < n; ++i) {
        // envelope
        switch (env_) {
            case Env::Attack:
                lvl_ += (kAttackTarget - lvl_) * ka;  // front-loaded RC charge
                if (lvl_ >= 1.f) {
                    lvl_ = 1.f;
                    env_ = Env::Decay;
                }
                break;
            case Env::Decay:
                lvl_ += (p.sustain - lvl_) * kd;       // exponential fall to sustain
                if (lvl_ - p.sustain <= 0.002f) env_ = Env::Sustain;
                break;
            case Env::Sustain:
                // slew toward the live sustain value so mid-note edits don't click
                lvl_ += (p.sustain - lvl_) * 0.0008f;
                break;
            case Env::Release:
                lvl_ *= relCoef_;                      // exponential discharge
                if (lvl_ <= 1e-3f) {
                    lvl_ = 0.f;
                    env_ = Env::Idle;
                    return;  // rest of block is silence
                }
                break;
            default:
                return;
        }

        const uint32_t ui = (uint32_t)inc;
        float s = tableRead(tbl, ph_[0]);
        if (pulse) s = (s - tableRead(tbl, ph_[0] + pwOff)) * kPulseGain;
        ph_[0] += ui;
        if (fat) {
            s += tableRead(tbl, ph_[1]) + tableRead(tbl, ph_[2]);
            ph_[1] += (uint32_t)(inc * mUp);
            ph_[2] += (uint32_t)(inc * mDn);
        }
        if (sub) {  // square one octave down, glides with the voice
            s += tableRead(subTbl, ph_[3]) * p.subLevel;
            ph_[3] += ui >> 1;
        }
        if (noise) {  // env-gated white noise (LCG), articulates per note
            rng_ = rng_ * 1664525u + 1013904223u;
            s += (float)(int32_t)rng_ * 4.6566129e-10f * p.noiseLevel;
        }
        out[i] += s * lvl_ * gain;
        inc += incStep;
    }
}

}  // namespace dsp
