// GLIDE dsp core — parameter and event types shared between the UI thread
// and the audio render thread. PURE C++: no Arduino/M5/IDF includes anywhere
// under dsp/. This is the porting boundary for future dedicated hardware.
#pragma once
#include <cstdint>

namespace dsp {

constexpr int kMaxVoices = 8;

enum class Waveform : uint8_t { Sine, Triangle, Saw, Square, FatSaw, Pulse, Count };
enum class GlideMode : uint8_t {
    LegatoOnly,  // glide only when notes overlap — the skill-gap default
    Always,      // every transition portamentos (dreamier)
    Count
};

// Tilt is an assignable effects modulator — never pitch bend (rejected on
// tape: "fuck, I gotta lean it over again"). Lives in dsp so patches can
// carry a tilt personality and stay portable. Append-only (persisted in
// patches). Morph = lean into the synth-morph blend (the UI drives it; note
// sound_gen's randTiltRoute deliberately never rolls it — a generated patch
// must not depend on what the player happened to play previously).
enum class TiltRoute : uint8_t { Off, Cutoff, Vibrato, Volume, Morph, Count };

inline const char* tiltRouteName(TiltRoute r) {
    switch (r) {
        case TiltRoute::Cutoff:  return "cutoff";
        case TiltRoute::Vibrato: return "vibrato";
        case TiltRoute::Volume:  return "volume";
        case TiltRoute::Morph:   return "morph";
        default:                 return "off";
    }
}

// Tempo-synced delay divisions. 0 = free (the echo uses delayTimeS); 1.. lock
// the echo to a fraction of a quarter-note beat at SynthParams::tempoBpm, so a
// solo over the jam/progression sits in the pocket. The dotted-eighth (2) is
// the classic "lock the lead to the groove" repeat.
inline float delaySyncBeats(uint8_t d) {
    switch (d) {
        case 1: return 1.0f;        // 1/4
        case 2: return 0.75f;       // 1/8. (dotted eighth)
        case 3: return 0.5f;        // 1/8
        case 4: return 1.f / 3.f;   // 1/8 triplet
        case 5: return 0.25f;       // 1/16
        default: return 0.f;        // free
    }
}
constexpr uint8_t kDelaySyncCount = 6;  // free + five divisions
inline const char* delaySyncName(uint8_t d) {
    switch (d) {
        case 1: return "1/4";
        case 2: return "1/8.";
        case 3: return "1/8";
        case 4: return "1/8T";
        case 5: return "1/16";
        default: return "free";
    }
}

inline const char* waveformName(Waveform w) {
    switch (w) {
        case Waveform::Sine:     return "sine";
        case Waveform::Triangle: return "tri";
        case Waveform::Saw:      return "saw";
        case Waveform::Square:   return "sqr";
        case Waveform::FatSaw:   return "fat";
        case Waveform::Pulse:    return "pwm";
        default:                 return "?";
    }
}

// Filter response. The SVF computes all of these for free; LP is the original
// voice (default), the rest open up new timbres (HP = thin/airy, BP = vocal/
// telephone, notch = hollow/phasey).
enum class FilterMode : uint8_t { LP, HP, BP, Notch, Count };
inline const char* filterModeName(FilterMode m) {
    switch (m) {
        case FilterMode::HP:    return "highpass";
        case FilterMode::BP:    return "bandpass";
        case FilterMode::Notch: return "notch";
        default:                return "lowpass";
    }
}

// ---- modulation matrix ----------------------------------------------------
// A few mod SOURCES routed to DESTINATIONS by a depth — N×M sound-design space
// from a handful of primitives. Everything defaults neutral (every slot None,
// so the matrix sums to zero), which keeps a default SynthParams bit-for-bit
// the original GLIDE tone. Tilt keeps its own dedicated routing (TiltRoute) for
// now; these are NEW per-block sources layered on top, on the LEAD bus only.
// Append-only enums (values are persisted in patches): add at the end, never
// renumber. Tilt is intentionally NOT a Pitch-capable source (tilt is never
// pitch bend — rejected on tape).
// Append-only (values persist in patches): add new entries before Count, never
// renumber. TiltA/TiltB are the live gyro axes as routable sources; Random is a
// fresh value sampled at each note-on (per-note variation).
enum class ModSource : uint8_t { None, LFO1, LFO2, ModEnv, KeyTrack, Bend, TiltA, TiltB, Random, Count };
enum class ModDest   : uint8_t { None, Pitch, Cutoff, Resonance, Amp, FenvDepth, Drive, Chorus, Delay, Reverb, Count };
enum class LfoShape  : uint8_t { Sine, Tri, Saw, Square, SH, Count };

inline const char* modSourceName(ModSource s) {
    switch (s) {
        case ModSource::LFO1:     return "LFO1";
        case ModSource::LFO2:     return "LFO2";
        case ModSource::ModEnv:   return "mod env";
        case ModSource::KeyTrack: return "key trk";
        case ModSource::Bend:     return "bend";
        case ModSource::TiltA:    return "tilt f/b";
        case ModSource::TiltB:    return "tilt l/r";
        case ModSource::Random:   return "random";
        default:                  return "off";
    }
}
inline const char* modDestName(ModDest d) {
    switch (d) {
        case ModDest::Pitch:     return "pitch";
        case ModDest::Cutoff:    return "cutoff";
        case ModDest::Resonance: return "reso";
        case ModDest::Amp:       return "amp";
        case ModDest::FenvDepth: return "f.env";
        case ModDest::Drive:     return "drive";
        case ModDest::Chorus:    return "chorus";
        case ModDest::Delay:     return "delay";
        case ModDest::Reverb:    return "reverb";
        default:                 return "off";
    }
}
inline const char* lfoShapeName(LfoShape s) {
    switch (s) {
        case LfoShape::Tri:    return "tri";
        case LfoShape::Saw:    return "saw";
        case LfoShape::Square: return "sqr";
        case LfoShape::SH:     return "s&h";
        default:               return "sine";
    }
}

// One routing: source -> dest by a bipolar depth. C++11: it has default member
// initializers so it's NOT aggregate-initializable on the device std — build it
// with make() (mirrors NoteEvent::make()).
struct ModSlot {
    uint8_t src   = (uint8_t)ModSource::None;
    uint8_t dest  = (uint8_t)ModDest::None;
    float   depth = 0.f;  // -1..+1
    static ModSlot make(ModSource s, ModDest d, float depth) {
        ModSlot m;
        m.src = (uint8_t)s;
        m.dest = (uint8_t)d;
        m.depth = depth;
        return m;
    }
};
constexpr int kModSlots = 6;

// Everything the audio thread needs each block. POD and trivially copyable:
// the UI writes the inactive copy of a double buffer and flips an atomic
// index, so the render thread always sees a coherent set.
struct SynthParams {
    Waveform  wave       = Waveform::Saw;
    GlideMode glideMode  = GlideMode::LegatoOnly;
    float attackS  = 0.005f;
    float decayS   = 0.12f;
    float sustain  = 0.70f;   // 0..1
    float releaseS = 0.25f;
    float glideS   = 0.12f;   // THE core parameter — portamento time
    float cutoffHz = 4000.f;
    float resonance = 0.30f;  // 0..0.95
    uint8_t filterMode = 0;   // FilterMode: 0=LP (default, the original voice)
    float masterVol = 0.70f;  // 0..1
    float detuneCents = 12.f; // fat-saw spread
    uint8_t voiceCount = 6;   // held-voice cap (1..kMaxVoices)

    // ---- patch character (defaults = neutral: the original GLIDE tone) --
    float fenvAtkS = 0.003f;  // filter envelope attack
    float fenvDecS = 0.20f;   // filter envelope decay
    float fenvOct  = 0.f;     // filter env depth in octaves (0 = off);
                              // paraphonic: retriggers on fresh attacks only,
                              // never on legato hand-offs — slides stay smooth
    float subLevel = 0.f;     // square sub-osc one octave down, 0..1
    float noiseLevel = 0.f;   // per-voice white noise, env-gated, 0..1
    float drive = 1.f;        // pre-filter gain into the soft clipper, 1..8
    float autoVibCents = 0.f; // built-in vibrato depth (tilt adds on top)

    // ---- send effects (global block, post-filter; 0 = dry/off) -----------
    // The "space" a bare mono voice lacks. Defaults are fully dry so the
    // neutral SynthParams is bit-for-bit the old engine (and the native
    // tests' default-param paths are unchanged).
    float chorusDepth = 0.f;   // 0..1 ensemble shimmer (dual-tap mod delay)
    float delayMix    = 0.f;   // 0..1 echo send level
    float delayTimeS  = 0.28f; // echo time, seconds (clamped to the fx buffer)
    float delayFb     = 0.35f; // 0..0.9 echo regeneration
    float reverbMix   = 0.f;   // 0..1 reverb send level
    float reverbSize  = 0.6f;  // 0..1 tail length / room size
    uint8_t delaySync = 0;     // 0 = free (delayTimeS); 1.. = tempo division
                               // (locks the echo to tempoBpm — see delaySync*)

    // ---- modulation: 2 LFOs + a 2nd (AD) envelope + the routing matrix -----
    // All neutral by default (slots None) so the tone is unchanged until the
    // player assigns a slot. LFO sync reuses the delaySync* tempo divisions
    // (0 = free-run at the Hz rate; 1.. = lock to tempoBpm).
    float   lfo1RateHz = 2.0f;   uint8_t lfo1Shape = 0;  uint8_t lfo1Sync = 0;
    float   lfo2RateHz = 0.5f;   uint8_t lfo2Shape = 0;  uint8_t lfo2Sync = 0;
    float   modEnvAtkS = 0.01f;  float   modEnvDecS = 0.30f;
    ModSlot slots[kModSlots];

    // live modulation, pre-summed by the UI thread each frame (never persisted
    // as sound state, reset by the patch-save hygiene)
    float bendCents    = 0.f;  // bend keys, ramped by UI
    float vibratoCents = 0.f;  // tilt->vibrato depth (0 = off)
    float cutoffModOct = 0.f;  // tilt->cutoff offset in octaves (-2..+2)
    float volMod       = 1.f;  // tilt->volume multiplier (0.25..1)
    float tempoBpm     = 120.f;// the jam tempo, published each frame so a
                               // synced delay locks to it
    float tiltAVal     = 0.f;  // raw calibrated fwd/back axis (-1..1) — the mod
    float tiltBVal     = 0.f;  // raw calibrated roll axis — matrix source inputs
};

struct NoteEvent {
    enum Type : uint8_t {
        On,        // fresh note (legato=false) or lane hand-off (legato=true)
        Off,       // release by id
        Retarget,  // re-aim a sounding note's pitch with an explicit glide
        AllOff,    // panic: everything dies, drones included
        LeadsOff   // clear the solo layer; latched drones keep ringing
    };
    Type    type   = On;
    uint8_t id     = 0;     // physical key code — identity for Off/Retarget
    uint8_t lane   = 0xFF;  // grid row ("string") 0..3, 0xFF = no lane
    bool    legato = false; // On: hand the lane's sounding voice this id+pitch
    bool    drone  = false; // jam-row note: latched backing layer — exempt
                            // from the voice cap and from nearest-pitch
                            // stealing, released with a drawn-out tail
    bool    backing = false; // loop-pedal playback note: cap-exempt and
                             // steal-proof like a drone, survives LeadsOff,
                             // ignores live bend/tilt vibrato — but releases
                             // normally (it's a recorded performance, not a
                             // drone bed)
    float   pitchMidi = 60.f;  // fractional MIDI note number

    // C++11-safe construction helper (NoteEvent has default member
    // initializers, so aggregate init isn't portable to the device std)
    static NoteEvent make(Type t, uint8_t id, uint8_t lane = 0xFF, bool legato = false,
                          float pitch = 60.f) {
        NoteEvent e;
        e.type = t;
        e.id = id;
        e.lane = lane;
        e.legato = legato;
        e.pitchMidi = pitch;
        return e;
    }
};

}  // namespace dsp
