// GLIDE generative sound engine — the heart of "your instrument is yours."
//
// Rolls a complete, musically-bounded patch from a seed, or mutates an existing
// one to explore its neighbourhood. Seeded and DETERMINISTIC: the same seed
// always yields the same patch. Two payoffs fall out of that:
//   - a per-device seed gives every unit a unique-but-reproducible starting
//     bank (no two players' instruments sound alike out of the box), and
//   - the host tests can pin the behaviour exactly (env:native).
//
// PURE C++: no Arduino, no M5, no millis(), no global RNG state. All randomness
// flows from the `seed` argument through a local LCG. This lives under the
// dsp/ porting boundary and must keep compiling in env:native — so the soul of
// the instrument (its sound *generator*) ports to future hardware unchanged,
// exactly like the synth voice does.
#pragma once
#include <cstdint>

#include "params.h"

namespace dsp {

// A complete generated patch: the synth voice plus its tilt "personality."
// The four tilt fields live OUTSIDE SynthParams (they're config, not synth
// params) — this struct mirrors the shape of store::PatchData so storage can
// map a GenPatch across the dsp/storage boundary without dsp/ ever depending
// on storage/. (dsp/ defines TiltRoute, so it's free to suggest one here.)
struct GenPatch {
    SynthParams synth;
    uint8_t tiltRoute  = (uint8_t)TiltRoute::Vibrato;
    float   tiltDepth  = 0.55f;
    uint8_t tiltRouteB = (uint8_t)TiltRoute::Off;
    float   tiltDepthB = 0.60f;
};

// Roll a brand-new patch from `seed`. Deterministic: same seed -> same patch.
// Every field lands inside the engine's musical bounds (see the clamps in the
// .cpp), so a roll is always playable — never a dead or blown-out sound. The
// player's master volume is NOT touched here (the caller keeps it).
GenPatch generateSound(uint32_t seed);

// Evolve `base` by `amount` in [0,1]: small = a subtle variation that keeps the
// character (find the neighbour you almost had), large = a bold leap. Continuous
// params nudge by a bounded delta scaled to their range; categorical params
// (waveform, filter mode, LFO shapes, mod routings, tilt) flip with a
// probability that rises with `amount`. `seed` makes the mutation reproducible.
// amount == 0 returns `base` unchanged.
GenPatch mutateSound(const GenPatch& base, float amount, uint32_t seed);

// A stable hash of a patch's audible character. Deterministic and padding-safe
// (hashes named fields, not raw bytes): the same sound always hashes the same.
// Used to name a patch from its own contents.
uint32_t patchHash(const GenPatch& g);

// Build an evocative, deterministic name from `seed`, e.g. "warm-haze-3f"
// (adjective-noun-hex). Always null-terminated within `cap`. Pure, so the same
// sound — fed patchHash(g) — always names itself the same way. This is how a
// generated sound becomes "yours" rather than "patch-07".
void nameForSeed(uint32_t seed, char* out, int cap);

}  // namespace dsp
