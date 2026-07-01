// Demo mode: the instrument plays itself. Spells a chord progression on the
// REAL backing engine, improvises over it with the seeded dsp phrase
// generator (slides on display), and wanders the curated bank — identity card
// and synth morph included. The exit is the feature: any grid key stops the
// melody but leaves the bed looping, and you're just... playing over it.
#pragma once
#include <cstdint>

namespace demo {

void requestStart();        // set by the settings row; perform starts it
bool pending();
void start(uint32_t nowMs);
void stop();                // melody off; the bed keeps playing (the takeover)
bool active();
void tick(uint32_t nowMs);  // schedule melody events + periodic sound swaps

}  // namespace demo
