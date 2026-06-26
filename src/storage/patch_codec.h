// GLIDE patch serialization — a forward/backward-compatible TAGGED format.
//
// The whole point: adding new sound parameters must NEVER again invalidate a
// user's saved patches. The old fixed-struct blob discarded every save whenever
// dsp::SynthParams grew (the documented v3/v4 "revert to factory" behaviour).
// Here each field is a numbered record; the decoder overwrites only the tags it
// finds and leaves everything else at the caller-seeded default. A save that
// predates a field simply lacks that tag -> the field keeps its default. New
// firmware reading an old save, or old firmware reading a new save, both work.
//
// Lives in storage/ (may use Arduino), NOT in dsp/ (the pure-C++ porting
// boundary). The native test build does not compile storage/, so this is
// invisible to env:native / test_dsp.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

#include "../dsp/params.h"

namespace store {

// The full saveable unit: the sound plus its tilt "personality". The four tilt
// fields live OUTSIDE dsp::SynthParams (they're config, not synth params), so
// they're carried explicitly — exactly as the legacy PatchBlob did.
struct PatchData {
    dsp::SynthParams synth;
    uint8_t tiltRoute  = 0;
    float   tiltDepth  = 0.6f;
    uint8_t tiltRouteB = 0;
    float   tiltDepthB = 0.6f;
};

// Encode `in` into buf as a tagged stream. Returns bytes written, or 0 if the
// buffer is too small (caller should size generously — a few hundred bytes).
size_t encodePatch(const PatchData& in, uint8_t* buf, size_t cap);

// Decode a tagged stream into `out`. The caller MUST pre-seed `out` with sane
// defaults first (e.g. the factory patch for this slot): only tags present in
// the stream overwrite, so any field the stream predates stays at its seed.
// Returns false if the header/magic/version is unrecognized (out left seeded).
bool decodePatch(const uint8_t* buf, size_t len, PatchData& out);

}  // namespace store
