#include "synth.h"
#include <cmath>
#include <cstring>
#include "wavetables.h"

namespace dsp {

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr float kVibratoHz = 5.5f;
}  // namespace

void Synth::init(float sampleRate) {
    sr_ = sampleRate;
    initWavetables();
    svf_.init(sr_);
    out_.init(sr_);
    for (auto& v : voices_) v.init(sr_);
    cutoffSm_ = p_.cutoffHz;
}

Voice* Synth::findActiveById(uint8_t id) {
    for (auto& v : voices_)
        if (v.active() && v.id() == id) return &v;
    return nullptr;
}

Voice* Synth::heldOnLane(uint8_t lane) {
    if (lane == 0xFF) return nullptr;
    for (auto& v : voices_)
        if (v.held() && v.lane() == lane) return &v;
    return nullptr;
}

Voice* Synth::nearestHeld(float pitch) {
    Voice* best = nullptr;
    float bestDist = 1e9f;
    for (auto& v : voices_) {
        if (!v.held() || v.isDrone()) continue;  // never steal the backing
        const float d = fabsf(v.currentPitch() - pitch);
        if (d < bestDist) {
            bestDist = d;
            best = &v;
        }
    }
    return best;
}

Voice* Synth::alloc() {
    // 1) a truly idle voice
    for (auto& v : voices_)
        if (!v.active()) return &v;
    // 2) the quietest releasing tail
    Voice* best = nullptr;
    float bestLvl = 1e9f;
    for (auto& v : voices_) {
        if (v.held()) continue;
        if (v.level() < bestLvl) {
            bestLvl = v.level();
            best = &v;
        }
    }
    if (best) return best;
    // 3) the oldest held voice (pool fully saturated)
    uint32_t oldest = 0xFFFFFFFF;
    for (auto& v : voices_) {
        if (v.seq() < oldest) {
            oldest = v.seq();
            best = &v;
        }
    }
    return best;
}

int Synth::heldVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += v.held() ? 1 : 0;
    return n;
}

int Synth::heldLeadVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += (v.held() && !v.isDrone()) ? 1 : 0;
    return n;
}

int Synth::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += v.active() ? 1 : 0;
    return n;
}

void Synth::noteOn(const NoteEvent& ev) {
    // Re-press of a key whose voice is still sounding (sustain pedal overlap,
    // release tail): retrigger that voice in place. The voice adopts the
    // event's role — a re-pressed ex-drone key becomes a normal lead voice.
    if (Voice* v = findActiveById(ev.id)) {
        v->legatoTo(ev.id, ev.lane, ev.pitchMidi);
        v->retrigger();
        v->setDrone(ev.drone);
        fenvStage_ = FEnv::Attack;  // a re-strike snaps the filter again
        if (!ev.drone) leadIdx_ = (int8_t)(v - voices_);
        return;
    }

    // String-mode hand-off: the lane already sings — glide it to the new key.
    if (ev.legato) {
        if (Voice* v = heldOnLane(ev.lane)) {
            v->legatoTo(ev.id, ev.lane, ev.pitchMidi);
            leadIdx_ = (int8_t)(v - voices_);
            return;
        }
    }

    // Voice cap reached -> nearest-pitch steal WITH glide: this is how a
    // chord shape slides in free allocation (press the new shape, each new
    // note grabs its nearest sounding neighbor and glides there).
    // Drones live outside the cap entirely: the backing layer neither
    // counts against the lead's polyphony nor gets robbed by it.
    const uint8_t cap = p_.voiceCount < 1 ? 1 : (p_.voiceCount > kMaxVoices ? kMaxVoices : p_.voiceCount);
    int heldLead = 0;
    for (const auto& v : voices_)
        if (v.held() && !v.isDrone()) ++heldLead;
    if (!ev.drone && heldLead >= cap) {
        if (Voice* v = nearestHeld(ev.pitchMidi)) {
            v->legatoTo(ev.id, ev.lane, ev.pitchMidi);
            leadIdx_ = (int8_t)(v - voices_);
            return;
        }
    }

    // Fresh voice. In Always-glide mode, slide in from the lead's pitch.
    float from = ev.pitchMidi;
    bool doGlide = false;
    if (p_.glideMode == GlideMode::Always && leadIdx_ >= 0 && voices_[leadIdx_].active()) {
        from = voices_[leadIdx_].currentPitch();
        doGlide = true;
    }
    Voice* v = alloc();
    v->noteOn(ev.id, ev.lane, ev.pitchMidi, from, doGlide, ++seq_);
    v->setDrone(ev.drone);
    fenvStage_ = FEnv::Attack;  // fresh attack retriggers the filter envelope
    if (!ev.drone) leadIdx_ = (int8_t)(v - voices_);  // readout tracks the solo hand
}

void Synth::handleEvent(const NoteEvent& ev) {
    switch (ev.type) {
        case NoteEvent::On:
            noteOn(ev);
            break;
        case NoteEvent::Off:
            if (Voice* v = findActiveById(ev.id))
                // drones let go with a drawn-out tail — the backing fades,
                // it never stops dead under a solo
                v->noteOff(v->isDrone() ? p_.releaseS * 4.f + 0.4f : p_.releaseS);
            break;
        case NoteEvent::Retarget:
            if (Voice* v = findActiveById(ev.id)) {
                v->retarget(ev.pitchMidi);
                // never let a retuned drone hijack the note readout
                if (!v->isDrone()) leadIdx_ = (int8_t)(v - voices_);
            }
            break;
        case NoteEvent::AllOff:
            for (auto& v : voices_) v.kill();
            break;
        case NoteEvent::LeadsOff:
            // the backing layer plays through sound switches and settings
            // trips — only the solo hand resets
            for (auto& v : voices_)
                if (v.active() && !v.isDrone()) v.kill();
            break;
    }
}

void Synth::render(float* out, int n) {
    memset(out, 0, sizeof(float) * n);

    // vibrato LFO, evaluated per block (250 Hz update of a 5.5 Hz LFO);
    // patch vibrato (autoVibCents) and tilt vibrato sum
    lfoPhase_ += kTwoPi * kVibratoHz * n / sr_;
    if (lfoPhase_ > kTwoPi) lfoPhase_ -= kTwoPi;
    const float lfo = sinf(lfoPhase_);
    const float cents = p_.bendCents + (p_.vibratoCents + p_.autoVibCents) * lfo;
    // the backing stays put: drones ignore bend keys and tilt vibrato (only
    // the solo hand bends strings), keeping just the patch's own vibrato
    const float droneCents = p_.autoVibCents * lfo;

    for (auto& v : voices_)
        if (v.active()) v.render(out, n, p_, v.isDrone() ? droneCents : cents);

    // paraphonic filter envelope, advanced at block rate (4 ms)
    const float blockDur = n / sr_;
    if (fenvStage_ == FEnv::Attack) {
        const float aS = p_.fenvAtkS < 0.001f ? 0.001f : p_.fenvAtkS;
        fenv_ += blockDur / aS;
        if (fenv_ >= 1.f) {
            fenv_ = 1.f;
            fenvStage_ = FEnv::Decay;
        }
    } else if (fenvStage_ == FEnv::Decay) {
        const float dS = p_.fenvDecS < 0.01f ? 0.01f : p_.fenvDecS;
        fenv_ -= blockDur / dS;
        if (fenv_ <= 0.f) {
            fenv_ = 0.f;
            fenvStage_ = FEnv::Idle;
        }
    }

    // smoothed cutoff: base * tilt octaves * filter-env octaves
    float cutTarget = p_.cutoffHz * exp2f(p_.cutoffModOct + p_.fenvOct * fenv_);
    if (cutTarget < 60.f) cutTarget = 60.f;
    if (cutTarget > 14000.f) cutTarget = 14000.f;
    cutoffSm_ += (cutTarget - cutoffSm_) * 0.2f;
    svf_.set(cutoffSm_, p_.resonance);

    // master volume ramped linearly across the block (no zipper)
    float volTarget = p_.masterVol * p_.volMod;
    if (volTarget < 0.f) volTarget = 0.f;
    if (volTarget > 1.f) volTarget = 1.f;
    const float v1 = volSm_ + (volTarget - volSm_) * 0.3f;
    float vol = volSm_;
    const float dv = (v1 - volSm_) / n;
    volSm_ = v1;

    // drive: push harder into the filter+clipper, compensate loudness after
    const float drive = p_.drive < 1.f ? 1.f : (p_.drive > 8.f ? 8.f : p_.drive);
    const float makeup = 1.f / (0.55f + 0.45f * drive);
    for (int i = 0; i < n; ++i) {
        out[i] = out_.process(svf_.process(out[i] * drive)) * makeup * vol;
        vol += dv;
    }

    // once-per-block NaN/denormal guard: a poisoned filter would otherwise
    // stay silent forever — reset loudly visible (silence) but recoverable
    if (!std::isfinite(out[n - 1])) {
        svf_.reset();
        out_.reset();
        memset(out, 0, sizeof(float) * n);
    }
}

}  // namespace dsp
