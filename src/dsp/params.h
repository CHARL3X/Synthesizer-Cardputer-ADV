// GLIDE dsp core — parameter and event types shared between the UI thread
// and the audio render thread. PURE C++: no Arduino/M5/IDF includes anywhere
// under dsp/. This is the porting boundary for future dedicated hardware.
#pragma once
#include <cstdint>

namespace dsp {

constexpr int kMaxVoices = 8;

enum class Waveform : uint8_t { Sine, Triangle, Saw, Square, FatSaw, Count };
enum class GlideMode : uint8_t {
    LegatoOnly,  // glide only when notes overlap — the skill-gap default
    Always,      // every transition portamentos (dreamier)
    Count
};

// Tilt is an assignable effects modulator — never pitch bend (rejected on
// tape: "fuck, I gotta lean it over again"). Lives in dsp so patches can
// carry a tilt personality and stay portable.
enum class TiltRoute : uint8_t { Off, Cutoff, Vibrato, Volume, Count };

inline const char* tiltRouteName(TiltRoute r) {
    switch (r) {
        case TiltRoute::Cutoff:  return "cutoff";
        case TiltRoute::Vibrato: return "vibrato";
        case TiltRoute::Volume:  return "volume";
        default:                 return "off";
    }
}

inline const char* waveformName(Waveform w) {
    switch (w) {
        case Waveform::Sine:     return "sine";
        case Waveform::Triangle: return "tri";
        case Waveform::Saw:      return "saw";
        case Waveform::Square:   return "sqr";
        case Waveform::FatSaw:   return "fat";
        default:                 return "?";
    }
}

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

    // live modulation, pre-summed by the UI thread each frame
    float bendCents    = 0.f;  // bend keys, ramped by UI
    float vibratoCents = 0.f;  // tilt->vibrato depth (0 = off)
    float cutoffModOct = 0.f;  // tilt->cutoff offset in octaves (-2..+2)
    float volMod       = 1.f;  // tilt->volume multiplier (0.25..1)
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
