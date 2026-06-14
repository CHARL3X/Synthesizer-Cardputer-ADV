#include "fx.h"

#include <cmath>
#include <cstring>

namespace dsp {

namespace {
constexpr float kTwoPi = 6.28318530718f;

// Chorus: a slow dual-tap modulated delay. Base ~11 ms with a ±5 ms swing at
// ~0.5 Hz; the second tap runs in antiphase so the two detuned copies pan
// against each other into a wide ensemble — the classic "string machine"
// thickness, on one mono oscillator.
constexpr float kChorusRateHz = 0.45f;
constexpr float kChorusBaseS  = 0.011f;
constexpr float kChorusDepthS = 0.005f;

// Reverb tunings, scaled from Freeverb's 44.1k originals to 32 kHz (×0.726).
// Mutually-prime lengths keep the comb modes from piling onto the same
// frequencies (the metallic-box sound a naive reverb makes).
constexpr int kCombLen[4] = {810, 926, 1032, 1130};
constexpr int kApLen[2]   = {403, 320};
constexpr float kRvbDamp  = 0.25f;   // HF absorption in the comb feedback
constexpr float kRvbInput = 0.22f;   // drive into the comb bank (4 combs sum)

inline float readLerp(const float* buf, int len, int writePos, float delaySamp) {
    // fractional read `delaySamp` samples behind the write head (linear interp)
    float rp = (float)writePos - delaySamp;
    while (rp < 0.f) rp += (float)len;
    int i0 = (int)rp;
    float frac = rp - (float)i0;
    int i1 = i0 + 1;
    if (i1 >= len) i1 -= len;
    return buf[i0] + (buf[i1] - buf[i0]) * frac;
}
}  // namespace

void Fx::init(float sr) {
    sr_ = sr;
    // Carve the packed reverb buffer into its comb and allpass lines.
    float* cur = rvBuf_;
    for (int i = 0; i < kNComb; ++i) {
        combBuf_[i] = cur;
        cur += kCombLen[i];
    }
    for (int i = 0; i < kNAllpass; ++i) {
        apBuf_[i] = cur;
        cur += kApLen[i];
    }
    rvReady_ = true;
    reset();
}

void Fx::reset() {
    memset(chBuf_, 0, sizeof chBuf_);
    memset(dlBuf_, 0, sizeof dlBuf_);
    memset(rvBuf_, 0, sizeof rvBuf_);
    chW_ = dlW_ = 0;
    chLfo_ = 0.f;
    dlDamp_ = 0.f;
    for (int i = 0; i < kNComb; ++i) {
        combIdx_[i] = 0;
        combLp_[i] = 0.f;
    }
    for (int i = 0; i < kNAllpass; ++i) apIdx_[i] = 0;
}

void Fx::process(float* buf, int n, const SynthParams& p) {
    const bool doChorus = p.chorusDepth > 0.001f;
    const bool doDelay  = p.delayMix   > 0.001f;
    const bool doReverb = p.reverbMix  > 0.001f;
    if (!doChorus && !doDelay && !doReverb) return;  // fully dry: leave untouched

    // ---- chorus: per-block LFO, intra-block interpolated tap offsets -----
    float tapAStart = 0.f, tapBStart = 0.f, tapAStep = 0.f, tapBStep = 0.f;
    float chMixWet = 0.f, chMixDry = 1.f;
    if (doChorus) {
        const float base = kChorusBaseS * sr_;
        const float depth = kChorusDepthS * sr_;
        const float p0 = chLfo_;
        const float p1 = p0 + kTwoPi * kChorusRateHz * n / sr_;  // phase at block end
        chLfo_ = p1 >= kTwoPi ? p1 - kTwoPi : p1;
        // tap A on the LFO, tap B a quarter-cycle ahead -> the two detuned
        // copies move against each other instead of in lockstep
        const float qtr = 1.5707963f;
        tapAStart = base + depth * sinf(p0);
        tapBStart = base + depth * sinf(p0 + qtr);
        tapAStep = (depth * sinf(p1)       - (tapAStart - base)) / n;  // glide A across block
        tapBStep = (depth * sinf(p1 + qtr) - (tapBStart - base)) / n;  // glide B across block
        chMixWet = 0.5f * p.chorusDepth;
        chMixDry = 1.f - 0.5f * p.chorusDepth;
    }

    // ---- delay setup -----------------------------------------------------
    float dlSamp = 0.f, dlFb = 0.f;
    if (doDelay) {
        // tempo-synced: lock the echo time to a beat fraction of tempoBpm so a
        // solo over the jam sits in the pocket. Fold long divisions down an
        // octave at a time until they fit the line — still on the grid.
        float t = p.delayTimeS;
        const float beats = delaySyncBeats(p.delaySync);
        if (beats > 0.f) {
            const float bpm = p.tempoBpm < 20.f ? 20.f : (p.tempoBpm > 300.f ? 300.f : p.tempoBpm);
            t = beats * 60.f / bpm;
            const float maxT = (float)(kDelayMax - 1) / sr_;
            while (t > maxT) t *= 0.5f;
        }
        dlSamp = t * sr_;
        if (dlSamp < 1.f) dlSamp = 1.f;
        if (dlSamp > (float)(kDelayMax - 1)) dlSamp = (float)(kDelayMax - 1);
        dlFb = p.delayFb < 0.f ? 0.f : (p.delayFb > 0.9f ? 0.9f : p.delayFb);
    }

    // ---- reverb setup ----------------------------------------------------
    float combFb = 0.f;
    if (doReverb && rvReady_) {
        const float room = p.reverbSize < 0.f ? 0.f : (p.reverbSize > 1.f ? 1.f : p.reverbSize);
        combFb = 0.72f + 0.26f * room;       // 0.72 (short) .. 0.98 (long hall)
    }

    float tapA = tapAStart;
    float tapB = tapBStart;
    for (int i = 0; i < n; ++i) {
        float x = buf[i];          // dry input to the send block
        float wet = x;             // running signal the sends chain onto

        // chorus -----------------------------------------------------------
        if (doChorus) {
            chBuf_[chW_] = x;
            const float a = readLerp(chBuf_, kChorusMax, chW_, tapA);
            const float b = readLerp(chBuf_, kChorusMax, chW_, tapB);
            if (++chW_ >= kChorusMax) chW_ = 0;
            tapA += tapAStep;
            tapB += tapBStep;
            wet = chMixDry * x + chMixWet * (a + b);
        }

        // delay ------------------------------------------------------------
        if (doDelay) {
            const float echo = readLerp(dlBuf_, kDelayMax, dlW_, dlSamp);
            // darken the feedback so repeats melt instead of building harshly
            dlDamp_ += (echo - dlDamp_) * 0.30f;
            dlBuf_[dlW_] = wet + dlDamp_ * dlFb;
            if (++dlW_ >= kDelayMax) dlW_ = 0;
            wet += echo * p.delayMix;
        }

        // reverb -----------------------------------------------------------
        if (doReverb && rvReady_) {
            const float in = wet * kRvbInput;
            float acc = 0.f;
            for (int c = 0; c < kNComb; ++c) {
                const int ci = combIdx_[c];
                const float y = combBuf_[c][ci];
                combLp_[c] = y * (1.f - kRvbDamp) + combLp_[c] * kRvbDamp;
                combBuf_[c][ci] = in + combLp_[c] * combFb;
                if (++combIdx_[c] >= kCombLen[c]) combIdx_[c] = 0;
                acc += y;
            }
            // series allpass diffusers smear the comb sum into a dense tail
            for (int ap = 0; ap < kNAllpass; ++ap) {
                const int ai = apIdx_[ap];
                const float bufout = apBuf_[ap][ai];
                const float out = -acc + bufout;
                apBuf_[ap][ai] = acc + bufout * 0.5f;
                if (++apIdx_[ap] >= kApLen[ap]) apIdx_[ap] = 0;
                acc = out;
            }
            wet += acc * (0.6f * p.reverbMix);
        }

        // safety clamp: the dry mix already passed the soft clipper near ±1;
        // the sends can only add, so cap the sum just under the test ceiling.
        if (wet > 1.3f) wet = 1.3f;
        else if (wet < -1.3f) wet = -1.3f;
        buf[i] = wet;
    }
}

}  // namespace dsp
