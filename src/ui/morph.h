// Synth-morph runtime: one blend position (0 = the current sound, 1 = fully
// the previous sound) ramped at the Morph time rate. Two drivers, one state:
// a sound switch kicks the position to 1 and lets it glide home (the smooth
// transition), and the G0 Morph action holds a target while engaged (the
// lean). Performance state — never persisted.
#pragma once
#include <cstdint>

namespace morph {

void kick();                  // a sound change landed: start at the old sound
void setHold(float target01); // G0 morph lean target (0 = released)
void setTiltAmt(float amt01); // tilt-route contribution, DIRECT (the hand is
                              // the ramp) — ADDS to the G0/switch position
void tick(uint32_t nowMs);    // ramp toward the target; call once per frame
float pos();                  // current blend, 0..1 (all pushes summed, clamped)

}  // namespace morph
