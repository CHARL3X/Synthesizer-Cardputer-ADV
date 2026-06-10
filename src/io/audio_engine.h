// The hardware boundary for sound: owns the render task and the M5Unified
// playRaw streaming loop. Everything above this is portable dsp/; everything
// below is M5Stack-specific. If audio cannot start, begin() returns false
// and lastError() says why — the caller MUST show it. Never a silent
// dead instrument.
#pragma once
#include <cstdint>
#include "../dsp/params.h"

namespace audio {

// Call after M5Cardputer.begin(). Configures the speaker for low-latency
// streaming, probes the codec path, and starts the render task on core 0.
bool begin();
const char* lastError();

// UI thread -> audio thread (lock-free)
void pushEvent(const dsp::NoteEvent& ev);
void setParams(const dsp::SynthParams& p);

// audio thread -> UI thread
struct Lead {
    bool active;
    float pitchMidi;   // includes bend — tracks what you hear
    float glide01;     // 0..1 progress of the current slide
    uint8_t held;      // held voices (leads + drones)
    uint8_t leads;     // held lead voices — what the voice cap governs
    uint8_t sounding;  // held + release tails
};
Lead lead();

// Copies the most recent rendered samples (pre-quantize) for the scope.
// Returns the number of samples written (<= maxN).
int copyScope(float* dst, int maxN);

// Render-task health: blocks where the DMA queue ran dry (audible gap risk).
uint32_t starvedBlocks();

}  // namespace audio
