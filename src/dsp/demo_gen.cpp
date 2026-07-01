#include "demo_gen.h"

namespace dsp {

namespace {
inline uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
}  // namespace

DemoNote DemoMelody::next(int scaleLen) {
    if (scaleLen < 1) scaleLen = 1;
    const int hi = 2 * scaleLen;  // two octaves of headroom

    if (left <= 0) {
        // phrase boundary: breathe, then start the next phrase near the middle
        left = 3 + (int)(xorshift(rng) % 5);  // 3..7 notes
        DemoNote r;
        r.type = DemoNote::Rest;
        r.degree = degree;
        r.beats4 = (uint8_t)(2 + xorshift(rng) % 5);  // half to 1.5 beats of air
        return r;
    }

    // melodic motion: mostly steps, some seconds, the odd leap, some repeats
    const uint32_t m = xorshift(rng) % 10;
    int step;
    if (m < 5)      step = (xorshift(rng) & 1) ? 1 : -1;
    else if (m < 7) step = (xorshift(rng) & 1) ? 2 : -2;
    else if (m < 8) step = (xorshift(rng) & 1) ? 4 : -4;  // the leap
    else            step = 0;                              // repeated note
    degree += step;
    if (degree < 0) degree = -degree;          // reflect off the range edges
    if (degree > hi) degree = 2 * hi - degree;
    if (degree < 0) degree = 0;                // (tiny scaleLen safety)

    DemoNote n;
    n.degree = degree;
    // inside a phrase ~half the moves SLIDE (legato retarget) — the whole point
    // of the instrument, on display. (After a rest nothing rings, so the driver
    // re-attacks a Slide anyway.)
    n.type = (xorshift(rng) % 10 < 5) ? DemoNote::Slide : DemoNote::Attack;
    if (step == 0) n.type = DemoNote::Attack;  // a repeat only reads re-struck
    // durations: 16ths and 8ths drive, quarters land, the odd long tone
    const uint32_t d = xorshift(rng) % 10;
    n.beats4 = d < 3 ? 1 : (d < 7 ? 2 : (d < 9 ? 4 : 8));
    --left;
    return n;
}

}  // namespace dsp
