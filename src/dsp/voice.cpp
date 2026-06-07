#include "voice.h"
#include <cmath>
#include "pitch.h"
#include "wavetables.h"

namespace dsp {

namespace {
// Per-voice gain leaves headroom for 8 voices into the soft clipper.
constexpr float kVoiceGain = 0.22f;
constexpr float kFatGain = 0.45f;  // 3 oscillators summed
constexpr float kMinAttackS = 0.001f;
constexpr float kMinSegS = 0.002f;

inline uint32_t freqToInc(float freq, float sr) {
    return (uint32_t)(freq / sr * 4294967296.f);
}
}  // namespace

void Voice::noteOn(uint8_t id, uint8_t lane, float pitch, float fromPitch, bool doGlide,
                   uint32_t seq) {
    id_ = id;
    lane_ = lane;
    seq_ = seq;
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
    if (lvl_ < 1e-3f) {  // released during the first ms of attack: inaudible —
        lvl_ = 0.f;      // free the slot now instead of squatting for releaseS
        env_ = Env::Idle;
        return;
    }
    if (releaseS < kMinSegS) releaseS = kMinSegS;
    // constant-rate release: full scale takes releaseS, quieter notes finish
    // proportionally sooner — so staccato tails never exhaust the voice pool
    relRate_ = 1.f / (releaseS * sr_);
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
    const float* tbl = tableFor(p.wave, f1);

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

    // ---- envelope rates (live: param edits apply mid-note) --------------
    const float aS = p.attackS < kMinAttackS ? kMinAttackS : p.attackS;
    const float dS = p.decayS < kMinSegS ? kMinSegS : p.decayS;
    const float aInc = 1.f / (aS * sr_);
    const float dInc = (1.f - p.sustain) / (dS * sr_);

    float inc = inc0;
    const float gain = fat ? kVoiceGain * kFatGain : kVoiceGain;

    for (int i = 0; i < n; ++i) {
        // envelope
        switch (env_) {
            case Env::Attack:
                lvl_ += aInc;
                if (lvl_ >= 1.f) {
                    lvl_ = 1.f;
                    env_ = Env::Decay;
                }
                break;
            case Env::Decay:
                lvl_ -= dInc;
                if (lvl_ <= p.sustain) env_ = Env::Sustain;  // no snap: slew below
                break;
            case Env::Sustain:
                // slew toward the live sustain value so mid-note edits don't click
                lvl_ += (p.sustain - lvl_) * 0.0008f;
                break;
            case Env::Release:
                lvl_ -= relRate_;
                if (lvl_ <= 0.f) {
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
        ph_[0] += ui;
        if (fat) {
            s += tableRead(tbl, ph_[1]) + tableRead(tbl, ph_[2]);
            ph_[1] += (uint32_t)(inc * mUp);
            ph_[2] += (uint32_t)(inc * mDn);
        }
        out[i] += s * lvl_ * gain;
        inc += incStep;
    }
}

}  // namespace dsp
