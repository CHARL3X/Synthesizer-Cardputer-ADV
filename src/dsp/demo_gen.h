// Demo-mode melody generator: seeded, deterministic phrases written to show
// off what GLIDE does — stepwise motion with slides (legato retargets), the
// occasional leap, breathing rests between phrases. Pure C++, host-tested.
// The driver maps degrees to pitch with the live Layout (always in key) and
// beats to ms with the jam tempo.
#pragma once
#include <cstdint>

namespace dsp {

struct DemoNote {
    enum Type : uint8_t { Rest, Attack, Slide };
    Type type;
    int degree;       // scale-degree index above the demo's base position
    uint8_t beats4;   // duration in quarter-beats (1 = a 16th, 4 = one beat)
};

struct DemoMelody {
    uint32_t rng = 1;
    int degree = 0;   // current melodic position
    int left = 0;     // notes remaining in the current phrase

    void seed(uint32_t s) {
        rng = s ? s : 1;
        degree = 0;
        left = 0;
    }
    // The next melodic event. scaleLen bounds the range (two octaves of degrees).
    DemoNote next(int scaleLen);
};

}  // namespace dsp
