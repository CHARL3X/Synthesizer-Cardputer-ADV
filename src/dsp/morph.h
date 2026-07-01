// Synth morph: interpolate between two complete sounds. The instrument glides
// between notes; this is the same idea for timbre — a G0 hold leans the live
// sound toward the previous one, and a sound switch is just a morph that
// completes. Pure C++, host-tested.
#pragma once
#include "params.h"

namespace dsp {

// Blend a -> b by t (0 = exactly a, 1 = exactly b, clamped). Perceptual
// interpolation: Hz and time-shaped params lerp geometrically (a cutoff sweep
// sounds linear), 0..1 mixes lerp linearly, enums/discretes switch at the
// midpoint, mod-matrix slots crossfade depth out / swap routing / fade in.
// The live-mod fields (bend, tilt, tempo...) always come from `a` — they are
// per-frame performance state the caller owns, never part of the blend.
SynthParams morphParams(const SynthParams& a, const SynthParams& b, float t);

}  // namespace dsp
