#include "sd_browser.h"

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/patches.h"
#include "../io/keys.h"
#include "../io/looper.h"
#include "../io/sd_store.h"
#include "../storage/glide_config.h"
#include "audition.h"
#include "sound_card.h"
#include "text_entry.h"
#include "theme.h"

namespace sdbrowser {

namespace {
// positional key codes (y*14+x) — same convention as keys.cpp / settings
constexpr int kUp = 39;       // ;
constexpr int kDown = 53;     // .
constexpr int kEnter = 41;    // open the action sheet / confirm a sheet action
constexpr int kPreview = 55;  // space — hear the highlighted sound in place
constexpr int kExit1 = 0;     // `
constexpr int kExit2 = 14;    // tab
constexpr int kVisible = 7;
constexpr int kSlotQ = 15;    // q (y=1,x=1) = slot 0; p (y=1,x=10) = slot 9
constexpr int kSlotP = 24;

const char* const kSlotLetters = "qwertyuiop";

char gNames[sdstore::kMaxList][sdstore::kMaxNameLen + 1];
int gCount = 0;

void refresh() { gCount = sdstore::list(gNames, sdstore::kMaxList); }

// Seed a PatchData from the GLIDE factory voice (so any field the file predates
// keeps a sane default — the NVS-slot contract), then overlay the saved file.
bool loadFile(const char* name, store::PatchData& pd) {
    const dsp::Patch& fp = dsp::factoryPatches()[0];
    pd.synth = fp.synth;
    pd.tiltRoute = (uint8_t)fp.tiltRoute;
    pd.tiltDepth = fp.tiltDepth;
    pd.tiltRouteB = (uint8_t)fp.tiltRouteB;
    pd.tiltDepthB = fp.tiltDepthB;
    return sdstore::load(name, pd);
}

// the action sheet, opened with enter on a selected patch
enum class Mode { List, Sheet, SlotPrompt };
const char* const kSheet[] = {"Load (use it)", "Put on slot", "Rename", "Delete", "Back"};
constexpr int kSheetLen = (int)(sizeof kSheet / sizeof kSheet[0]);
enum { kActLoad = 0, kActSlot, kActRename, kActDelete, kActBack };
}  // namespace

bool run(M5Canvas& canvas, char* loadedName, int cap) {
    sdstore::begin();
    refresh();
    int sel = 0, top = 0;
    Mode mode = Mode::List;
    int sheetSel = 0;
    bool deleteArmed = false;
    bool previewed = false;   // changed the live sound to audition something
    bool committed = false;   // chose Load (use it) — keep the change
    bool weLocked = false;    // we froze the backing for the split this session
    char flash[28] = {};      // transient confirmation ("-> slot r")
    uint32_t flashUntil = 0;
    uint64_t prev = ~0ULL;

    for (;;) {
        M5Cardputer.update();
        const uint32_t now = millis();
        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;
        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };

        const bool sdOk = sdstore::available();
        const bool haveList = sdOk && gCount > 0;

        if (mode == Mode::List) {
            if (hit(kExit1) || hit(kExit2)) {
                // browsing was non-committal: if we previewed but didn't choose
                // Load, restore the sound AND the split state we had on entry
                if (previewed && !committed) {
                    store::historyUndo();                   // pre-browse solo sound
                    if (weLocked) store::unlockBacking();   // pre-browse split state
                }
                audition::stop();
                return false;
            }
            if (haveList) {
                if (hit(kUp)) { sel = (sel - 1 + gCount) % gCount; soundcard::dismiss(); }
                if (hit(kDown)) { sel = (sel + 1) % gCount; soundcard::dismiss(); }
                if (hit(kPreview)) {
                    store::PatchData pd;
                    if (loadFile(gNames[sel], pd)) {
                        if (!previewed) store::historyCheckpoint();  // one undo back to pre-browse
                        previewed = true;
                        // hold the backing on its current sound so previewing
                        // only swaps the SOLO — then push params so it's audible
                        if (keys::soundSwitchBegin()) weLocked = true;
                        store::applyStoredPatch(pd);
                        keys::soundSwitchEnd();
                        audition::start();
                        soundcard::show();  // the previewed sound's face, over the list
                    }
                }
                if (hit(kEnter)) { mode = Mode::Sheet; sheetSel = 0; deleteArmed = false; }
            }
        } else if (mode == Mode::Sheet) {
            if (hit(kExit1) || hit(kExit2)) { mode = Mode::List; }
            else if (hit(kUp)) { sheetSel = (sheetSel - 1 + kSheetLen) % kSheetLen; deleteArmed = false; }
            else if (hit(kDown)) { sheetSel = (sheetSel + 1) % kSheetLen; deleteArmed = false; }
            else if (hit(kEnter)) {
                switch (sheetSel) {
                    case kActLoad: {
                        store::PatchData pd;
                        if (loadFile(gNames[sel], pd)) {
                            if (!previewed) store::historyCheckpoint();
                            keys::soundSwitchBegin();   // freeze the backing if jamming
                            store::applyStoredPatch(pd);
                            keys::soundSwitchEnd();      // solo = the loaded sound
                            committed = true;
                            if (cap > 0) {
                                strncpy(loadedName, gNames[sel], cap - 1);
                                loadedName[cap - 1] = '\0';
                            }
                            audition::stop();
                            return true;
                        }
                        break;
                    }
                    case kActSlot: mode = Mode::SlotPrompt; break;
                    case kActRename: {
                        char nm[sdstore::kMaxNameLen + 1];
                        strncpy(nm, gNames[sel], sizeof nm - 1);
                        nm[sizeof nm - 1] = '\0';
                        if (textentry::run(canvas, "RENAME SOUND", nm, sizeof nm)) {
                            store::PatchData pd;
                            if (loadFile(gNames[sel], pd)) {
                                strncpy(pd.name, nm, sizeof pd.name - 1);
                                pd.name[sizeof pd.name - 1] = '\0';
                                char oldStem[sdstore::kMaxNameLen + 1];
                                char newStem[sdstore::kMaxNameLen + 1];
                                sdstore::sanitize(gNames[sel], oldStem, sizeof oldStem);
                                sdstore::sanitize(nm, newStem, sizeof newStem);
                                if (sdstore::save(nm, pd)) {
                                    if (strcmp(oldStem, newStem) != 0) sdstore::remove(gNames[sel]);
                                    refresh();
                                    snprintf(flash, sizeof flash, "renamed");
                                } else {
                                    snprintf(flash, sizeof flash, "rename failed");
                                }
                                flashUntil = now + 1300;
                            }
                        }
                        prev = ~0ULL;  // text-entry ate keys — rebuild edge state
                        mode = Mode::List;
                        break;
                    }
                    case kActDelete:
                        if (deleteArmed) {
                            sdstore::remove(gNames[sel]);
                            refresh();
                            if (sel >= gCount) sel = gCount > 0 ? gCount - 1 : 0;
                            deleteArmed = false;
                            mode = Mode::List;
                        } else {
                            deleteArmed = true;  // second enter confirms
                        }
                        break;
                    case kActBack: mode = Mode::List; break;
                }
            }
        } else {  // Mode::SlotPrompt — press q..p to assign the patch to a slot
            if (hit(kExit1) || hit(kExit2)) { mode = Mode::Sheet; }
            else {
                for (int cd = kSlotQ; cd <= kSlotP; ++cd) {
                    if (!hit(cd)) continue;
                    const int slot = cd - kSlotQ;
                    store::PatchData pd;
                    if (!loadFile(gNames[sel], pd))
                        snprintf(flash, sizeof flash, "load: %s", sdstore::lastError());
                    else if (!store::saveToSlot(slot, pd))
                        snprintf(flash, sizeof flash, "slot write failed (nvs?)");
                    else
                        snprintf(flash, sizeof flash, "put on slot %c", kSlotLetters[slot]);
                    flashUntil = now + 1800;
                    mode = Mode::List;
                    break;
                }
            }
        }

        if (sel < top) top = sel;
        if (sel >= top + kVisible) top = sel - kVisible + 1;
        if (top < 0) top = 0;

        looper::tick(now);        // keep a loop / chord progression playing while
        keys::tickBacking(now);   // browsing — the backing never freezes here
        audition::tick();         // fire a preview phrase's events when due

        // ---- draw ----
        if (mode == Mode::SlotPrompt && haveList) {
            // full-screen slot picker: the WHOLE rack, so you see exactly which
            // slot you'd replace before you do. Press the slot's key to assign.
            canvas.fillScreen(theme::kBg);
            canvas.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
            canvas.setFont(&fonts::Font0);
            canvas.setTextDatum(top_left);
            canvas.setTextColor(theme::kAmber, theme::kPanel);
            canvas.drawString("PUT ON WHICH SLOT?", 5, 3);
            canvas.setTextColor(theme::kDim, theme::kPanel);
            canvas.setTextDatum(top_right);
            canvas.drawString("press q..p  ` cancel", cfg::kScreenW - 4, 3);
            canvas.setTextDatum(top_left);
            canvas.setTextColor(theme::kGreen, theme::kBg);
            char hdr[40];
            snprintf(hdr, sizeof hdr, "load %s onto:", gNames[sel]);
            canvas.drawString(hdr, 6, 17);
            for (int i = 0; i < dsp::kPatchCount; ++i) {  // two columns of five
                const int x = 8 + (i / 5) * 118;
                const int y = 32 + (i % 5) * 18;
                char line[28];
                snprintf(line, sizeof line, "%c  %s", kSlotLetters[i], store::patchName(i));
                canvas.setTextColor(theme::kIdle, theme::kBg);
                canvas.drawString(line, x, y);
            }
        } else {
            // ---- the library list ----
            canvas.fillScreen(theme::kBg);
            canvas.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
            canvas.setFont(&fonts::Font0);
            canvas.setTextDatum(top_left);
            canvas.setTextColor(theme::kAmber, theme::kPanel);
            canvas.drawString("SD LIBRARY", 5, 3);
            canvas.setTextColor(theme::kDim, theme::kPanel);
            canvas.setTextDatum(top_right);
            canvas.drawString("space hear  enter actions  ` back", cfg::kScreenW - 4, 3);
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
                    const int y = 18 + row * 13;
                    const bool isSel = (i == sel);
                    if (isSel) canvas.fillRect(0, y - 1, cfg::kScreenW, 13, theme::kPanel);
                    canvas.setTextColor(isSel ? theme::kAmber : theme::kDim,
                                        isSel ? theme::kPanel : theme::kBg);
                    canvas.drawString(gNames[i], 8, y);
                }
                canvas.setFont(&fonts::Font0);
                canvas.setTextColor(theme::kDim, theme::kBg);
                canvas.setTextDatum(top_right);
                char cb[20];
                snprintf(cb, sizeof cb, "%d/%d", sel + 1, gCount);
                canvas.drawString(cb, cfg::kScreenW - 4, 122);
                canvas.setTextDatum(top_left);
            }

            // transient flash (slot-assign / rename confirmation or failure)
            if (flash[0] && now < flashUntil) {
                canvas.setFont(&fonts::Font0);
                canvas.setTextColor(theme::kGreen, theme::kBg);
                canvas.drawString(flash, 8, 122);
            }

            // ---- action sheet overlay ----
            if (mode == Mode::Sheet && haveList) {
                const int px = 44, py = 20, pw = 152, ph = 98;
                canvas.fillRect(px, py, pw, ph, theme::kPanel);
                canvas.drawRect(px, py, pw, ph, theme::kLine);
                canvas.setFont(&fonts::Font0);
                canvas.setTextDatum(top_left);
                canvas.setTextColor(theme::kAmber, theme::kPanel);
                canvas.drawString(gNames[sel], px + 8, py + 4);
                canvas.drawFastHLine(px + 6, py + 15, pw - 12, theme::kLine);
                canvas.setFont(&fonts::Font2);
                for (int i = 0; i < kSheetLen; ++i) {
                    const int y = py + 19 + i * 13;
                    const bool s = (i == sheetSel);
                    if (s) canvas.fillRect(px + 4, y - 1, pw - 8, 13, theme::kBg);
                    const char* label = (i == kActDelete && deleteArmed) ? "Delete? enter" : kSheet[i];
                    canvas.setTextColor(s ? theme::kAmber : theme::kDim,
                                        s ? theme::kBg : theme::kPanel);
                    canvas.drawString(label, px + 12, y);
                }
                canvas.setFont(&fonts::Font0);
                canvas.setTextColor(theme::kDim, theme::kPanel);
                canvas.drawString("\x1e\x1f pick  enter ok  ` back", px + 8, py + ph - 11);
            }
            if (mode == Mode::List) soundcard::draw(canvas, now);  // preview's face
        }

        canvas.pushSprite(0, 0);
        delay(16);
    }
}

}  // namespace sdbrowser
