// GLIDE Phase 0 hardware risk probe (pio run -e phase0-probe -t upload).
//
// Validates the two make-or-break assumptions on the real Cardputer ADV
// BEFORE trusting the full instrument:
//
//   1. AUDIO: gapless streaming through M5Unified playRaw with three
//      rotating 128-sample blocks at 32 kHz. Counts starved blocks.
//      A clean 440 Hz tone with starved=0 over minutes = pass.
//   2. KEYBOARD: rollover/ghosting ceiling. Mash chords; every key the
//      TCA8418 reports lights up on a 4x14 grid. The max-held counter
//      tells you how many simultaneous notes the hardware honors.
//
// Controls: G0 button (BtnA) toggles the tone. SPACE injects a one-off
// 6 ms render stall to prove the headroom margin is real (you should hear
// nothing if dma_buf_count covers it; starved increments if not).
#include <M5Cardputer.h>
#include <cmath>

namespace {

constexpr uint32_t kSampleRate = 32000;
constexpr int kBlock = 128;
constexpr int kBufs = 3;
constexpr int kCh = 0;

int16_t gBlocks[kBufs][kBlock];
uint32_t gPhase = 0;
uint32_t gStarved = 0;
uint32_t gBlocksDone = 0;
bool gTone = true;
bool gAudioOk = false;
volatile bool gInjectStall = false;

M5Canvas gCanvas(&M5Cardputer.Display);

void audioTask(void*) {
    auto& spk = M5Cardputer.Speaker;
    const uint32_t inc = (uint32_t)(440.f / kSampleRate * 4294967296.f);

    for (int b = 0; b < kBufs; ++b) {
        memset(gBlocks[b], 0, sizeof(gBlocks[b]));
        spk.playRaw(gBlocks[b], kBlock, kSampleRate, false, 1, kCh, false);
    }
    uint8_t b = 0;
    for (;;) {
        while (spk.isPlaying(kCh) >= 2) vTaskDelay(1);
        // queue fully drained after warm-up = we were late = audible gap
        if (gBlocksDone > 16 && spk.isPlaying(kCh) == 0) gStarved++;

        if (gInjectStall) {
            gInjectStall = false;
            delayMicroseconds(6000);
        }

        int16_t* blk = gBlocks[b];
        if (gTone) {
            for (int i = 0; i < kBlock; ++i) {
                // 2^32 phase -> radians: phase * 2pi / 2^32
                blk[i] = (int16_t)(sinf((float)gPhase * 1.46291808e-9f) * 9000.f);
                gPhase += inc;
            }
        } else {
            memset(blk, 0, sizeof(int16_t) * kBlock);
        }
        // playRaw's return value is not a health signal (lies on failure
        // paths, blocks on full queue) — isPlaying()==0 above is the metric
        spk.playRaw(blk, kBlock, kSampleRate, false, 1, kCh, false);
        gBlocksDone++;
        b = (b + 1) % kBufs;
    }
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    Serial.begin(115200);

    auto& spk = M5Cardputer.Speaker;
    auto sc = spk.config();
    sc.sample_rate = kSampleRate;
    sc.dma_buf_len = kBlock;
    sc.dma_buf_count = 3;
    sc.task_priority = 2;
    sc.task_pinned_core = 0;
    spk.config(sc);

    gAudioOk = spk.begin() && spk.isEnabled();
    if (gAudioOk) {
        spk.setVolume(255);
        xTaskCreatePinnedToCore(audioTask, "probe_audio", 8192, nullptr, 3, nullptr, 0);
    }
    gCanvas.createSprite(240, 135);
}

void loop() {
    static uint32_t maxHeld = 0;
    M5Cardputer.update();

    if (M5Cardputer.BtnA.wasPressed()) gTone = !gTone;

    const auto& list = M5Cardputer.Keyboard.keyList();
    uint32_t heldNow = (uint32_t)list.size();
    if (heldNow > maxHeld) maxHeld = heldNow;
    bool spaceHeld = false;
    for (const auto& p : list)
        if (p.y == 3 && p.x == 13) spaceHeld = true;
    static bool prevSpace = false;
    if (spaceHeld && !prevSpace) gInjectStall = true;
    prevSpace = spaceHeld;

    gCanvas.fillScreen(TFT_BLACK);
    gCanvas.setFont(&fonts::Font0);
    gCanvas.setTextColor(0xFD60, TFT_BLACK);
    gCanvas.drawString("GLIDE PHASE 0 PROBE", 4, 3);

    char buf[64];
    if (!gAudioOk) {
        gCanvas.setTextColor(TFT_RED, TFT_BLACK);
        gCanvas.setFont(&fonts::Font2);
        gCanvas.drawString("SPK INIT FAIL (ES8311)", 4, 18);
        gCanvas.setFont(&fonts::Font0);
    } else {
        gCanvas.setTextColor(0xEF7D, TFT_BLACK);
        snprintf(buf, sizeof buf, "tone:%s  blocks:%lu  4ms cadence", gTone ? "ON " : "off",
                 (unsigned long)gBlocksDone);
        gCanvas.drawString(buf, 4, 16);
        snprintf(buf, sizeof buf, "STARVED: %lu   heap: %u", (unsigned long)gStarved,
                 (unsigned)ESP.getFreeHeap());
        gCanvas.setTextColor(gStarved == 0 ? 0x07E0 : TFT_RED, TFT_BLACK);
        gCanvas.drawString(buf, 4, 27);
    }

    snprintf(buf, sizeof buf, "held now: %lu   max seen: %lu", (unsigned long)heldNow,
             (unsigned long)maxHeld);
    gCanvas.setTextColor(0xEF7D, TFT_BLACK);
    gCanvas.drawString(buf, 4, 40);

    // 4x14 key grid: every reported key lights green
    const int gx = 8, gy = 56, cw = 16, ch = 16;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 14; ++x)
            gCanvas.drawRect(gx + x * cw, gy + y * ch, cw - 2, ch - 2, 0x2104);
    for (const auto& p : list)
        gCanvas.fillRect(gx + p.x * cw + 2, gy + p.y * ch + 2, cw - 6, ch - 6, 0x07E0);

    gCanvas.setTextColor(0x6B4D, TFT_BLACK);
    gCanvas.drawString("G0: tone on/off   space: inject 6ms stall", 4, 124);
    gCanvas.pushSprite(0, 0);

    static uint32_t lastLog = 0;
    if (millis() - lastLog > 1000) {
        lastLog = millis();
        Serial.printf("[probe] blocks=%lu starved=%lu heldMax=%lu heap=%u\n",
                      (unsigned long)gBlocksDone, (unsigned long)gStarved,
                      (unsigned long)maxHeld, (unsigned)ESP.getFreeHeap());
    }
    delay(33);
}
