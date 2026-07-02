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
// The screen-normal axis (gravity when flat) — the atan2 denominator that
// carries the quadrant, so tilt keeps counting PAST vertical instead of
// folding back. Flip kTiltSignZ if tilt reads ~full-scale while lying flat.
constexpr int   kTiltAxisZ = 2;
constexpr float kTiltSignZ = 1.0f;
constexpr float kTiltSmooth = 0.15f; // per-frame smoothing toward raw reading

// ---- microSD (SPI) — the personal patch library lives here ------------
// The SD card is where a player's generative collection grows past the ten
// fast NVS slots: roll a sound you love, save it here, browse them all back.
//
// HARDWARE-UNVERIFIED (Phase 0 spirit): these are the standard M5Cardputer
// SD/SPI pins. The Cardputer ADV's pinout may differ — CONFIRM against your
// unit before trusting SD. If SD.begin() fails the instrument runs fully on
// its built-in NVS slots and the library simply reports "no SD"; nothing about
// playing the instrument depends on the card. Set kSdEnabled=false to compile
// the SD path out entirely. If saves don't appear, the CS/clock pins are the
// first suspects — fix them here.
constexpr bool     kSdEnabled = true;
constexpr int      kSdSckPin  = 40;   // SPI clock   — VERIFY on ADV
constexpr int      kSdMisoPin = 39;   // SPI MISO    — VERIFY on ADV
constexpr int      kSdMosiPin = 14;   // SPI MOSI    — VERIFY on ADV
constexpr int      kSdCsPin   = 12;   // SD chip-sel — VERIFY on ADV
constexpr uint32_t kSdFreqHz  = 25000000;
constexpr const char* kSdDir  = "/glide";        // patch-library folder on the card
constexpr const char* kSdExt  = ".gpat";         // one patch per file (tagged codec)

// ---- onboard RGB LED (WS2812 / SK6812) --------------------------------
// A second display: the lead voice's pitch picks the hue (chromatic color
// wheel — C is red and it rotates up by semitone), note activity drives the
// brightness, and bends/new attacks throw a white sparkle. Driven once per UI
// frame off audio::lead(), never from the audio thread, via the core's
// built-in neopixelWrite() (no FastLED).
//
// Hardware: M5Stamp S3A core. Its single SK6812 RGB LED is on GPIO 21 (data,
// GRB wire order handled by neopixelWrite) AND needs its power rail enabled
// first — GPIO 38 driven HIGH, or the LED stays dark no matter what data you
// send. If the color comes out wrong (e.g. red shows as green) the panel isn't
// GRB — tell me and I'll remap. kLedPowerPin = 255 means "no enable pin".
constexpr bool    kLedEnabled   = true;
constexpr uint8_t kLedPin       = 21;   // SK6812 data
constexpr uint8_t kLedPowerPin  = 38;   // LED power-enable (HIGH = on); 255 = none
constexpr uint8_t kLedMaxBright = 90;   // 0..255 ceiling — the LED is searing at full

}  // namespace cfg
