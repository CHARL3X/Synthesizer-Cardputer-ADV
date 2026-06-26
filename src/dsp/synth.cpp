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
    svfBack_.init(sr_);
    out_.init(sr_);
    outBack_.init(sr_);
    fx_.init(sr_);
    for (auto& v : voices_) v.init(sr_);
    cutoffSm_ = p_.cutoffHz;
    cutoffSmBack_ = pBack_.cutoffHz;
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
        if (!v.held() || v.isDrone() || v.isBacking()) continue;  // never steal the backing
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
    // 3) pool fully saturated: evict the oldest held LEAD voice. The backing
    // (drones, loop playback, the auto-progression) is the foundation a solo
    // rides on, so it is the last thing to drop — never robbed by a dense
    // chord on top of it. Falls back to the oldest of anything only if every
    // voice is backing.
    uint32_t oldest = 0xFFFFFFFF;
    for (auto& v : voices_) {
        if (v.isDrone() || v.isBacking()) continue;
        if (v.seq() < oldest) {
            oldest = v.seq();
            best = &v;
        }
    }
    if (best) return best;
    oldest = 0xFFFFFFFF;
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
    for (const auto& v : voices_) n += (v.held() && !v.isDrone() && !v.isBacking()) ? 1 : 0;
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
        v->setBacking(ev.backing);
        // snap the filter env of the layer this voice belongs to — a backing
        // chord re-strike no longer pumps the solo's filter, and vice versa
        ((ev.drone || ev.backing) ? fenvBackStage_ : fenvStage_) = FEnv::Attack;
        if (!ev.drone && !ev.backing) {
            modEnvStage_ = FEnv::Attack;  // 2nd env retriggers on fresh lead attacks
            leadIdx_ = (int8_t)(v - voices_);
        }
        return;
    }

    // String-mode hand-off: the lane already sings — glide it to the new key.
    // (Loop playback uses lanes 4..7, so its hand-offs can only ever grab
    // its own voices, never the live player's.)
    if (ev.legato) {
        if (Voice* v = heldOnLane(ev.lane)) {
            v->legatoTo(ev.id, ev.lane, ev.pitchMidi);
            if (!ev.backing) leadIdx_ = (int8_t)(v - voices_);
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
        if (v.held() && !v.isDrone() && !v.isBacking()) ++heldLead;
    if (!ev.drone && !ev.backing && heldLead >= cap) {
        if (Voice* v = nearestHeld(ev.pitchMidi)) {
            v->legatoTo(ev.id, ev.lane, ev.pitchMidi);
            leadIdx_ = (int8_t)(v - voices_);
            return;
        }
    }

    // Fresh voice. In Always-glide mode, slide in from the lead's pitch —
    // live notes only: the backing layers replay their own recorded slides.
    float from = ev.pitchMidi;
    bool doGlide = false;
    if (p_.glideMode == GlideMode::Always && !ev.drone && !ev.backing && leadIdx_ >= 0 &&
        voices_[leadIdx_].active()) {
        from = voices_[leadIdx_].currentPitch();
        doGlide = true;
    }
    Voice* v = alloc();
    v->noteOn(ev.id, ev.lane, ev.pitchMidi, from, doGlide, ++seq_);
    v->setDrone(ev.drone);
    v->setBacking(ev.backing);
    ((ev.drone || ev.backing) ? fenvBackStage_ : fenvStage_) = FEnv::Attack;  // its layer's filter
    if (!ev.drone && !ev.backing) {
        modEnvStage_ = FEnv::Attack;  // 2nd env retriggers on fresh lead attacks
        leadIdx_ = (int8_t)(v - voices_);  // readout = the solo hand
    }
}

void Synth::handleEvent(const NoteEvent& ev) {
    switch (ev.type) {
        case NoteEvent::On:
            noteOn(ev);
            break;
        case NoteEvent::Off:
            if (Voice* v = findActiveById(ev.id)) {
                // each layer releases on its own envelope: a drone fades with a
                // long drawn-out tail, the loop at its backing rate, the lead
                // on the live patch
                float rel = p_.releaseS;
                if (v->isDrone())        rel = pBack_.releaseS * 4.f + 0.4f;
                else if (v->isBacking()) rel = pBack_.releaseS;
                v->noteOff(rel);
            }
            break;
        case NoteEvent::Retarget:
            if (Voice* v = findActiveById(ev.id)) {
                v->retarget(ev.pitchMidi);
                // never let a retuned backing layer hijack the note readout
                if (!v->isDrone() && !v->isBacking()) leadIdx_ = (int8_t)(v - voices_);
            }
            break;
        case NoteEvent::AllOff:
            for (auto& v : voices_) v.kill();
            fx_.reset();  // panic kills the tails too — no reverb ringing on
            break;
        case NoteEvent::LeadsOff:
            // the backing layers (drones AND the loop) play through sound
            // switches and settings trips — only the solo hand resets
            for (auto& v : voices_)
                if (v.active() && !v.isDrone() && !v.isBacking()) v.kill();
            break;
    }
}

// Generic AD envelope (linear rise, exponential fall) — the filter env and the
// 2nd mod-env both run on this.
void Synth::advanceAD(FEnv& stage, float& env, float atkS, float decS, float blockDur) {
    if (stage == FEnv::Attack) {
        const float aS = atkS < 0.001f ? 0.001f : atkS;
        env += blockDur / aS;
        if (env >= 1.f) {
            env = 1.f;
            stage = FEnv::Decay;
        }
    } else if (stage == FEnv::Decay) {
        const float dS = decS < 0.01f ? 0.01f : decS;
        env *= expf(-4.6f * blockDur / dS);  // exponential fall: a natural filter
        if (env <= 0.002f) {                 // sweep closing, not a linear ramp
            env = 0.f;
            stage = FEnv::Idle;
        }
    }
}

void Synth::advanceFenv(FEnv& stage, float& env, const SynthParams& p, float blockDur) {
    advanceAD(stage, env, p.fenvAtkS, p.fenvDecS, blockDur);
}

// One mod-matrix LFO, evaluated per block. Returns -1..+1 (ModEnv is the only
// unipolar source). sync==0 = free-run at rateHz; sync>=1 locks to tempoBpm via
// the same divisions the delay uses (delaySyncBeats), so LFO and delay share one
// tempo vocabulary. S&H re-samples white noise on each phase wrap.
float Synth::evalLfo(float& phase, float rateHz, uint8_t shape, uint8_t sync,
                     uint32_t& rng, float& hold, int n) {
    float hz = rateHz;
    if (sync) {
        const float beats = delaySyncBeats(sync);
        if (beats > 0.f) hz = (p_.tempoBpm / 60.f) / beats;
    }
    if (hz < 0.01f) hz = 0.01f;
    phase += kTwoPi * hz * n / sr_;
    bool wrapped = false;
    if (phase >= kTwoPi) {
        phase -= kTwoPi;
        wrapped = true;
    }
    switch ((LfoShape)shape) {
        case LfoShape::Tri:    return 1.f - 4.f * fabsf(phase * (1.f / kTwoPi) - 0.5f);
        case LfoShape::Saw:    return phase * (2.f / kTwoPi) - 1.f;
        case LfoShape::Square: return phase < kTwoPi * 0.5f ? 1.f : -1.f;
        case LfoShape::SH:
            if (wrapped) {  // step to a fresh random value once per cycle
                rng = rng * 1664525u + 1013904223u;
                hold = (float)(int32_t)rng * (1.f / 2147483648.f);
            }
            return hold;
        case LfoShape::Sine:
        default:               return sinf(phase);
    }
}

void Synth::render(float* out, int n) {
    if (n > kBlockMax) n = kBlockMax;  // member sub-mix buffer ceiling
    memset(out, 0, sizeof(float) * n);          // lead bus
    memset(backBuf_, 0, sizeof(float) * n);     // backing bus

    const float blockDur = n / sr_;

    // dedicated 5.5 Hz auto-vibrato LFO (unchanged): the patch's own vibrato.
    // The lead sums patch + tilt vibrato, the backing only the patch's own —
    // drones/loop ignore the bend keys and tilt.
    lfoPhase_ += kTwoPi * kVibratoHz * n / sr_;
    if (lfoPhase_ > kTwoPi) lfoPhase_ -= kTwoPi;
    const float lfo = sinf(lfoPhase_);
    const float backCents = pBack_.autoVibCents * lfo;

    // ---- modulation matrix (LEAD bus only), all per-block ------------------
    // Advance the 2nd env first — it can modulate pitch, consumed by the voice
    // render below. Then evaluate both LFOs and gather every source value. When
    // no slot routes anything (the default), all accumulators stay neutral and
    // the lead path is bit-for-bit unchanged.
    advanceAD(modEnvStage_, modEnv_, p_.modEnvAtkS, p_.modEnvDecS, blockDur);
    const float lfo1 = evalLfo(lfo1Phase_, p_.lfo1RateHz, p_.lfo1Shape, p_.lfo1Sync, shRng1_, shHold1_, n);
    const float lfo2 = evalLfo(lfo2Phase_, p_.lfo2RateHz, p_.lfo2Shape, p_.lfo2Sync, shRng2_, shHold2_, n);
    const float keyTrk = (leadIdx_ >= 0 && voices_[leadIdx_].active())
                             ? (voices_[leadIdx_].currentPitch() - 60.f) * (1.f / 24.f) : 0.f;
    float src[(int)ModSource::Count] = {0.f};
    src[(int)ModSource::LFO1]     = lfo1;
    src[(int)ModSource::LFO2]     = lfo2;
    src[(int)ModSource::ModEnv]   = modEnv_;  // unipolar 0..1
    src[(int)ModSource::KeyTrack] = keyTrk;
    src[(int)ModSource::Bend]     = p_.bendCents * (1.f / 1200.f);

    float modPitchCents = 0.f, modCutOct = 0.f, modRes = 0.f, modFenvOct = 0.f, modAmpMul = 1.f;
    for (int i = 0; i < kModSlots; ++i) {
        const ModSlot& m = p_.slots[i];
        if (m.src == 0 || m.dest == 0 || m.depth == 0.f) continue;  // None / inert
        if (m.src >= (uint8_t)ModSource::Count || m.dest >= (uint8_t)ModDest::Count) continue;
        const float v = src[m.src] * m.depth;
        switch ((ModDest)m.dest) {
            case ModDest::Pitch:     modPitchCents += v * 1200.f; break;  // ±1 oct at depth 1
            case ModDest::Cutoff:    modCutOct     += v * 4.f;    break;  // ±4 oct
            case ModDest::Resonance: modRes        += v;          break;
            case ModDest::Amp:       modAmpMul     *= (1.f + v);  break;  // tremolo
            case ModDest::FenvDepth: modFenvOct    += v * 4.f;    break;
            default: break;
        }
    }

    const float leadCents =
        p_.bendCents + (p_.vibratoCents + p_.autoVibCents) * lfo + modPitchCents;

    // Lead voices render with the live sound; the backing layer (drones, loop
    // playback, the auto-progression) renders into its own bus with the frozen
    // backing sound — so switching the solo's patch/octave leaves the bed alone.
    for (auto& v : voices_)
        if (v.active()) {
            if (v.isDrone() || v.isBacking()) v.render(backBuf_, n, pBack_, backCents);
            else                              v.render(out, n, p_, leadCents);
        }

    advanceFenv(fenvStage_, fenv_, p_, blockDur);             // lead filter env
    advanceFenv(fenvBackStage_, fenvBack_, pBack_, blockDur);  // backing filter env

    // lead filter: base * (tilt + matrix) octaves * (env + matrix) env octaves
    float cutL = p_.cutoffHz * exp2f(p_.cutoffModOct + modCutOct + (p_.fenvOct + modFenvOct) * fenv_);
    if (cutL < 60.f) cutL = 60.f;
    if (cutL > 14000.f) cutL = 14000.f;
    cutoffSm_ += (cutL - cutoffSm_) * 0.2f;
    float resL = p_.resonance + modRes;  // matrix can push resonance
    if (resL < 0.f) resL = 0.f;
    if (resL > 0.95f) resL = 0.95f;
    svf_.set(cutoffSm_, resL);

    // backing filter: its own env, NO tilt (the bed stays put under the solo)
    float cutB = pBack_.cutoffHz * exp2f(pBack_.fenvOct * fenvBack_);
    if (cutB < 60.f) cutB = 60.f;
    if (cutB > 14000.f) cutB = 14000.f;
    cutoffSmBack_ += (cutB - cutoffSmBack_) * 0.2f;
    svfBack_.set(cutoffSmBack_, pBack_.resonance);

    // per-bus volume ramps (no zipper). Tilt swell only touches the lead.
    auto rampVol = [n](float target, float& sm, float& step) {
        if (target < 0.f) target = 0.f;
        if (target > 1.f) target = 1.f;
        const float v1 = sm + (target - sm) * 0.3f;
        step = (v1 - sm) / n;
        const float start = sm;
        sm = v1;
        return start;
    };
    float dvL = 0.f, dvB = 0.f;
    float volL = rampVol(p_.masterVol * p_.volMod * modAmpMul, volSm_, dvL);
    float volB = rampVol(pBack_.masterVol * pBack_.volMod, volSmBack_, dvB);

    const float driveL = p_.drive < 1.f ? 1.f : (p_.drive > 8.f ? 8.f : p_.drive);
    const float driveB = pBack_.drive < 1.f ? 1.f : (pBack_.drive > 8.f ? 8.f : pBack_.drive);
    const float makeupL = 1.f / (0.55f + 0.45f * driveL);
    const float makeupB = 1.f / (0.55f + 0.45f * driveB);

    for (int i = 0; i < n; ++i) {
        const float l = out_.process(svf_.process(out[i] * driveL)) * makeupL * volL;
        const float b = outBack_.process(svfBack_.process(backBuf_[i] * driveB)) * makeupB * volB;
        out[i] = l + b;
        volL += dvL;
        volB += dvB;
    }

    // one shared FX "room": both layers wash into the live patch's
    // chorus/delay/reverb. Self-bypasses when all three sends are 0.
    fx_.process(out, n, p_);

    // once-per-block NaN/denormal guard: a poisoned filter or a runaway
    // reverb tail would otherwise stay broken forever — reset loudly visible
    // (silence) but recoverable
    if (!std::isfinite(out[n - 1])) {
        svf_.reset();
        svfBack_.reset();
        out_.reset();
        outBack_.reset();
        fx_.reset();
        memset(out, 0, sizeof(float) * n);
    }
}

}  // namespace dsp
