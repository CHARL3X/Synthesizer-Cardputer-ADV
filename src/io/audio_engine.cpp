#include "audio_engine.h"

#include <M5Cardputer.h>
#include <atomic>
#include <cstring>

#include "../config.h"
#include "../dsp/spsc_queue.h"
#include "../dsp/synth.h"

namespace audio {

namespace {

dsp::Synth gSynth;
dsp::SpscQueue<dsp::NoteEvent, 64> gEvents;

// Scheduled events (loop-pedal playback): held until due, fired at block
// rate. The generation counter makes flushes race-free: a stale-gen event
// is dropped on the render thread instead of being fished out of the queue.
struct TimedEvent {
    dsp::NoteEvent ev;
    uint32_t dueMs;
    uint16_t gen;
};
dsp::SpscQueue<TimedEvent, 128> gTimed;
std::atomic<uint16_t> gGen{0};

// Double-buffered params: UI writes the inactive copy, flips the index.
// The render thread copies the active struct once per block — always a
// coherent set, never a torn cutoff/resonance combo. Two sounds travel
// together (lead + backing) so the split is always a coherent pair too.
dsp::SynthParams gParams[2];
dsp::SynthParams gParamsBack[2];
std::atomic<uint8_t> gParamIdx{0};

// 3 rotating output buffers vs M5Unified's per-channel queue depth of 2:
// playRaw stores our POINTER (no copy), so the buffer being refilled must
// never be one of the two still in flight.
int16_t gBlocks[cfg::kNumBlockBufs][cfg::kBlockSamples];
float gMix[cfg::kBlockSamples];

// Scope tap: power-of-two float ring, written per block on the audio
// thread, snapshotted by the UI at ~30 fps. A torn read is one frame of
// visual noise at worst — no lock needed.
constexpr int kScopeSize = 1024;  // power of two
float gScope[kScopeSize];
std::atomic<uint32_t> gScopeW{0};

std::atomic<uint32_t> gStarved{0};
std::atomic<bool> gLeadActive{false};
std::atomic<float> gLeadPitch{0.f};
std::atomic<float> gLeadGlide{1.f};
std::atomic<float> gLeadLevel{0.f};
std::atomic<uint8_t> gHeld{0};
std::atomic<uint8_t> gHeldLeads{0};
std::atomic<uint8_t> gSounding{0};

const char* gError = nullptr;
bool gRunning = false;

void renderTask(void*) {
    auto& spk = M5Cardputer.Speaker;

    // Prime the queue with silence so the stream starts gapless.
    for (int b = 0; b < cfg::kNumBlockBufs; ++b) {
        memset(gBlocks[b], 0, sizeof(gBlocks[b]));
        spk.playRaw(gBlocks[b], cfg::kBlockSamples, cfg::kSampleRate, false, 1,
                    cfg::kAudioChannel, false);
    }

    uint8_t b = 0;
    uint32_t blocksDone = 0;
    for (;;) {
        // Backpressure pacing: render exactly as fast as the DMA drains.
        while (spk.isPlaying(cfg::kAudioChannel) >= 2) vTaskDelay(1);
        // queue fully drained after warm-up = we were late = audible gap risk
        if (blocksDone > 16 && spk.isPlaying(cfg::kAudioChannel) == 0) gStarved.fetch_add(1);
        ++blocksDone;

        {
            const uint8_t pi = gParamIdx.load(std::memory_order_acquire);
            gSynth.setParams(gParams[pi], gParamsBack[pi]);
        }

        // scheduled (loop playback) events that have come due
        const uint32_t nowMs = millis();
        const uint16_t gen = gGen.load(std::memory_order_acquire);
        TimedEvent te;
        while (gTimed.peek(te)) {
            if (te.gen == gen && (int32_t)(nowMs - te.dueMs) < 0) break;  // not due yet
            gTimed.pop(te);
            if (te.gen == gen) gSynth.handleEvent(te.ev);  // stale gen: dropped
        }

        dsp::NoteEvent ev;
        while (gEvents.pop(ev)) gSynth.handleEvent(ev);

        gSynth.render(gMix, cfg::kBlockSamples);

        // scope tap (pre-quantize)
        uint32_t w = gScopeW.load(std::memory_order_relaxed);
        for (int i = 0; i < cfg::kBlockSamples; ++i)
            gScope[(w + i) & (kScopeSize - 1)] = gMix[i];
        gScopeW.store(w + cfg::kBlockSamples, std::memory_order_release);

        // lead-voice feedback for the readout
        gLeadActive.store(gSynth.leadActive());
        gLeadPitch.store(gSynth.leadPitchMidi());
        gLeadGlide.store(gSynth.leadGlide01());
        gLeadLevel.store(gSynth.leadLevel());
        gHeld.store((uint8_t)gSynth.heldVoices());
        gHeldLeads.store((uint8_t)gSynth.heldLeadVoices());
        gSounding.store((uint8_t)gSynth.activeVoices());

        int16_t* blk = gBlocks[b];
        for (int i = 0; i < cfg::kBlockSamples; ++i) {
            float s = gMix[i] * 32767.f;
            if (s > 32767.f) s = 32767.f;
            else if (s < -32768.f) s = -32768.f;
            blk[i] = (int16_t)s;
        }

        // NOTE: playRaw returns true even on its internal early-outs and
        // blocks (not fails) on a full queue — its return value is not a
        // health signal. The isPlaying()==0 check above is.
        spk.playRaw(blk, cfg::kBlockSamples, cfg::kSampleRate, false, 1,
                    cfg::kAudioChannel, false);
        b = (b + 1) % cfg::kNumBlockBufs;
    }
}

}  // namespace

bool begin() {
    if (gRunning) return true;

    gSynth.init((float)cfg::kSampleRate);
    gParams[0] = dsp::SynthParams();
    gParams[1] = gParams[0];
    gParamsBack[0] = gParams[0];
    gParamsBack[1] = gParams[0];

    auto& spk = M5Cardputer.Speaker;

    // Mutate the speaker config while its task is not yet running, then
    // lock it in with begin(). M5Unified owns the undocumented ES8311
    // power-up sequence — this is exactly why we never touch raw I2S.
    auto sc = spk.config();
    sc.sample_rate = cfg::kSampleRate;
    sc.dma_buf_len = cfg::kBlockSamples;
    sc.dma_buf_count = cfg::kDmaBufCount;
    sc.task_priority = cfg::kSpkTaskPrio;
    sc.task_pinned_core = cfg::kRenderCore;
    spk.config(sc);

    if (!spk.begin()) {
        gError = "Speaker.begin() failed (ES8311/I2S init)";
        return false;
    }
    if (!spk.isEnabled()) {
        gError = "speaker not enabled (board detect: not a Cardputer ADV?)";
        return false;
    }
    spk.setVolume(255);  // gain lives in DSP; keep the M5 mixer at unity

    // Probe the actual playRaw path before claiming success. playRaw's
    // return value lies on failure paths (verified in Speaker_Class.cpp),
    // so the real assertion is isRunning(): the spk task spun up and took
    // the wav. Both checked.
    static int16_t probe[cfg::kBlockSamples] = {0};
    if (!spk.playRaw(probe, cfg::kBlockSamples, cfg::kSampleRate, false, 1,
                     cfg::kAudioChannel, false)) {
        gError = "probe playRaw() rejected";
        return false;
    }
    delay(10);  // give the lazily-created spk task a beat to start
    if (!spk.isRunning()) {
        gError = "speaker task did not start (isRunning false)";
        return false;
    }

    TaskHandle_t h = nullptr;
    xTaskCreatePinnedToCore(renderTask, "glide_audio", cfg::kRenderStack, nullptr,
                            cfg::kRenderPrio, &h, cfg::kRenderCore);
    if (!h) {
        gError = "render task creation failed";
        return false;
    }
    gRunning = true;
    return true;
}

const char* lastError() {
    return gError ? gError : "(no error)";
}

void pushEvent(const dsp::NoteEvent& ev) {
    gEvents.push(ev);
}

void pushEventAt(const dsp::NoteEvent& ev, uint32_t dueMs) {
    TimedEvent te;
    te.ev = ev;
    te.dueMs = dueMs;
    te.gen = gGen.load(std::memory_order_relaxed);
    if (!gTimed.push(te)) gEvents.push(ev);  // full: fire now rather than drop
}

void flushScheduled() {
    gGen.fetch_add(1, std::memory_order_release);
}

void setParams(const dsp::SynthParams& lead, const dsp::SynthParams& back) {
    const uint8_t cur = gParamIdx.load(std::memory_order_relaxed);
    const uint8_t next = cur ^ 1;
    gParams[next] = lead;
    gParamsBack[next] = back;
    gParamIdx.store(next, std::memory_order_release);
}

void setParams(const dsp::SynthParams& p) { setParams(p, p); }

Lead lead() {
    Lead l;
    l.active = gLeadActive.load();
    l.pitchMidi = gLeadPitch.load();
    l.glide01 = gLeadGlide.load();
    l.level = gLeadLevel.load();
    l.held = gHeld.load();
    l.leads = gHeldLeads.load();
    l.sounding = gSounding.load();
    return l;
}

int copyScope(float* dst, int maxN) {
    int n = maxN > kScopeSize ? kScopeSize : maxN;
    const uint32_t w = gScopeW.load(std::memory_order_acquire);
    const uint32_t start = w - (uint32_t)n;
    for (int i = 0; i < n; ++i) dst[i] = gScope[(start + i) & (kScopeSize - 1)];
    return n;
}

uint32_t starvedBlocks() {
    return gStarved.load();
}

}  // namespace audio
