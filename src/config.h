// GLIDE — central tunables. Nothing musical is hardcoded anywhere else:
// if you want to change how the instrument behaves, start here or in settings.
#pragma once
#include <cstdint>

namespace cfg {

// ---- audio path -------------------------------------------------------
// 32 kHz / 128-sample blocks = 4 ms per block. With dma_buf_count=3 the
// output chain is ~12 ms; keyboard adds ~5-10 ms -> under the 25 ms target.
constexpr uint32_t kSampleRate   = 32000;
constexpr int      kBlockSamples = 128;
constexpr int      kNumBlockBufs = 3;   // > M5Unified's per-channel queue depth (2)
constexpr int      kAudioChannel = 0;   // M5 Speaker virtual channel we stream on
constexpr int      kDmaBufCount  = 3;
constexpr int      kRenderCore   = 0;   // UI/keyboard own core 1 (Arduino loop)
constexpr int      kRenderPrio   = 3;   // above M5's spk_task (2)
constexpr int      kSpkTaskPrio  = 2;
constexpr uint32_t kRenderStack  = 8192;

// ---- engine -----------------------------------------------------------
constexpr int kVoicePool = 8;  // sized above nominal polyphony: released voices
                               // occupy slots during their release tails
                               // (hard-won STRATA-1 lesson)

// ---- UI ---------------------------------------------------------------
constexpr int      kScreenW   = 240;
constexpr int      kScreenH   = 135;
constexpr uint32_t kFrameMs   = 33;    // ~30 fps
constexpr uint32_t kHudMs     = 1000;  // transient parameter HUD lifetime
constexpr uint32_t kHudErrMs  = 400;   // rejected-change red flash
constexpr uint32_t kIntroMs   = 6000;  // first-run gesture card auto-dismiss
constexpr uint32_t kRepeatDelayMs = 220;  // key auto-repeat (DAS)
constexpr uint32_t kRepeatRateMs  = 60;   // key auto-repeat (ARR)

// ---- storage ----------------------------------------------------------
constexpr uint32_t kPersistDebounceMs = 500;  // NVS write debounce after edits
constexpr const char* kNvsNamespace = "glide";

// ---- tilt (BMI270) ----------------------------------------------------
// Which accel axis maps to the mod value, and its sign. Set during bring-up;
// flip kTiltSign if "tilt toward you" feels backwards on the real device.
constexpr int   kTiltAxis  = 1;     // 0=x 1=y 2=z  — forward/back (axis A)
constexpr float kTiltSign  = 1.0f;
// Second axis: left/right roll (axis B). VERIFY on hardware — axis index and
// sign are best-guess (x, in-plane orthogonal to fwd/back); flip kTiltSignB
// if "roll right" drives the mod backwards, or change kTiltAxisB if it turns
// out the roll plane maps to a different accel channel.
constexpr int   kTiltAxisB = 0;     // 0=x 1=y 2=z  — left/right roll
constexpr float kTiltSignB = 1.0f;
constexpr float kTiltSmooth = 0.15f; // per-frame smoothing toward raw reading

}  // namespace cfg
