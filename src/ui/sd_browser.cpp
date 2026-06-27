#include "sd_browser.h"

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/patches.h"
#include "../io/sd_store.h"
#include "../storage/glide_config.h"
#include "theme.h"

namespace sdbrowser {

namespace {
// positional key codes (y*14+x) — same convention as keys.cpp / settings
constexpr int kUp = 39;     // ;
constexpr int kDown = 53;   // .
constexpr int kEnter = 41;
constexpr int kLoad = 54;   // /  (also loads — mirrors settings' "change")
constexpr int kDel = 13;    // backspace
constexpr int kExit1 = 0;   // `
constexpr int kExit2 = 14;  // tab
constexpr int kVisible = 8;

char gNames[sdstore::kMaxList][sdstore::kMaxNameLen + 1];
int gCount = 0;

void refresh() { gCount = sdstore::list(gNames, sdstore::kMaxList); }

// Load the named patch into the live sound, seeded from the GLIDE factory voice
// so any field the file predates keeps a sane default (the NVS-slot contract).
// Checkpoints history first, so an SD load is undoable like a roll.
bool loadInto(const char* name) {
    store::PatchData pd;
    const dsp::Patch& fp = dsp::factoryPatches()[0];  // GLIDE = the neutral seed
    pd.synth = fp.synth;
    pd.tiltRoute = (uint8_t)fp.tiltRoute;
    pd.tiltDepth = fp.tiltDepth;
    pd.tiltRouteB = (uint8_t)fp.tiltRouteB;
    pd.tiltDepthB = fp.tiltDepthB;
    if (!sdstore::load(name, pd)) return false;
    store::historyCheckpoint();
    store::applyStoredPatch(pd);
    return true;
}
}  // namespace

bool run(M5Canvas& canvas, char* loadedName, int cap) {
    sdstore::begin();
    refresh();
    int sel = 0, top = 0;
    int armedDelete = -1;  // index awaiting a confirming second bksp
    uint64_t prev = ~0ULL;

    for (;;) {
        M5Cardputer.update();
        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;
        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };

        if (hit(kExit1) || hit(kExit2)) return false;

        const bool sdOk = sdstore::available();
        if (sdOk && gCount > 0) {
            if (hit(kUp)) { sel = (sel - 1 + gCount) % gCount; armedDelete = -1; }
            if (hit(kDown)) { sel = (sel + 1) % gCount; armedDelete = -1; }
            if (hit(kEnter) || hit(kLoad)) {
                if (loadInto(gNames[sel])) {
                    if (cap > 0) { strncpy(loadedName, gNames[sel], cap - 1); loadedName[cap - 1] = '\0'; }
                    return true;
                }
            }
            if (hit(kDel)) {
                if (armedDelete == sel) {  // second tap confirms
                    sdstore::remove(gNames[sel]);
                    refresh();
                    armedDelete = -1;
                    if (sel >= gCount) sel = gCount > 0 ? gCount - 1 : 0;
                } else {
                    armedDelete = sel;  // first tap arms
                }
            }
        }

        if (sel < top) top = sel;
        if (sel >= top + kVisible) top = sel - kVisible + 1;
        if (top < 0) top = 0;

        // ---- draw ----
        canvas.fillScreen(theme::kBg);
        canvas.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(top_left);
        canvas.setTextColor(theme::kAmber, theme::kPanel);
        canvas.drawString("SD LIBRARY", 5, 3);
        canvas.setTextColor(theme::kDim, theme::kPanel);
        canvas.setTextDatum(top_right);
        canvas.drawString(";/. pick  / load  bksp del  ` back", cfg::kScreenW - 4, 3);
        canvas.setTextDatum(top_left);

        if (!sdOk) {
            canvas.setTextColor(theme::kRed, theme::kBg);
            canvas.drawString("No SD card.", 8, 40);
            canvas.setTextColor(theme::kDim, theme::kBg);
            canvas.drawString(sdstore::lastError(), 8, 56);
            canvas.drawString("Saved sounds live on the microSD.", 8, 72);
            canvas.drawString("The 10 slots (q..p) work without it.", 8, 84);
        } else if (gCount <= 0) {
            canvas.setTextColor(theme::kIdle, theme::kBg);
            canvas.drawString("No saved sounds yet.", 8, 44);
            canvas.setTextColor(theme::kDim, theme::kBg);
            canvas.drawString("Roll one you love, then", 8, 62);
            canvas.drawString("SOUND > Save to SD.", 8, 74);
        } else {
            canvas.setFont(&fonts::Font2);
            for (int row = 0; row < kVisible; ++row) {
                const int i = top + row;
                if (i >= gCount) break;
                const int y = 20 + row * 13;
                const bool isSel = (i == sel);
                const bool arm = (i == armedDelete);
                const uint16_t bg = arm ? theme::kRed : (isSel ? theme::kPanel : theme::kBg);
                const uint16_t fg = arm ? theme::kBg : (isSel ? theme::kAmber : theme::kDim);
                if (arm || isSel) canvas.fillRect(0, y - 1, cfg::kScreenW, 13, bg);
                canvas.setTextColor(fg, bg);
                canvas.drawString(arm ? "delete? bksp again" : gNames[i], 8, y);
            }
            // count badge bottom-right
            canvas.setFont(&fonts::Font0);
            char cb[20];
            snprintf(cb, sizeof cb, "%d/%d", sel + 1, gCount);
            canvas.setTextColor(theme::kDim, theme::kBg);
            canvas.setTextDatum(top_right);
            canvas.drawString(cb, cfg::kScreenW - 4, 124);
            canvas.setTextDatum(top_left);
        }

        canvas.pushSprite(0, 0);
        delay(16);
    }
}

}  // namespace sdbrowser
