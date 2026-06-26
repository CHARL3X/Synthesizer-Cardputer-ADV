#include "help.h"

#include "../config.h"
#include "theme.h"

namespace help {

namespace {

// positional key codes (y*14+x) — same convention as keys.cpp / settings
constexpr int kUp = 39;     // ;
constexpr int kDown = 53;   // .
constexpr int kExit1 = 0;   // `
constexpr int kExit2 = 14;  // tab

struct Line {
    const char* text;
    bool header;  // amber section title vs dim body
};

// The whole manual. Kept terse — it's a reminder, not a tutorial. A leading
// space on body lines reads as a hanging indent under its header.
const Line kLines[] = {
    {"GLIDE - a pocket slide synth", true},
    {"4 rows = a scale. Press to play.", false},
    {"Hold a key, press another in the", false},
    {"same row = legato SLIDE (hammer-on).", false},
    {"Release back onto a held key = slide", false},
    {"down (pull-off). That glide is it.", false},

    {"PLAY KEYS", true},
    {"shift (hold): chromatic (off-scale)", false},
    {"'        : scale lock on/off", false},
    {"- / =    : octave down / up", false},
    {"[ / ]    : pitch bend down / up", false},
    {"\\        : hold (sustain, hands free)", false},
    {"space    : sustain pedal", false},
    {"ctrl/opt : volume - / + (hold to ramp)", false},
    {"enter    : tilt mode (tap cycle/hold lock)", false},
    {"` (hold) : exit", false},
    {"bksp     : panic (all notes off)", false},

    {"SOUNDS: 1 LIVE + 10 SLOTS (q..p)", true},
    {"You always play ONE live sound.", false},
    {"fn + q..p : load that slot's sound", false},
    {"  (unsaved live edits are dropped)", false},
    {"fn+shift+ q..p : save the live sound", false},
    {"  ONTO that key, overwriting its slot.", false},
    {"Tweak, then shift-save the SAME key", false},
    {"to keep it. A different key = a copy.", false},
    {"Saves it ALL: mods, LFOs, filter, FX.", false},
    {"* by a name = that slot was changed.", false},
    {"fn + number row : 10 live knobs,", false},
    {"   then [ ] fine, - = coarse.", false},
    {"tab : settings (everything else)", false},

    {"JAM / BACKING", true},
    {"Settings > Jam rows: bottom row(s)", false},
    {"become drones/chords. Tap them to", false},
    {"build a chord progression, then solo", false},
    {"over it on the rows above.", false},
    {"Settings > Jam motion = progression.", false},

    {"TILT", true},
    {"Lean the device to modulate. Route it", false},
    {"in Settings > Tilt, or send it anywhere", false},
    {"with the mod matrix.", false},

    {"MOD MATRIX (settings)", true},
    {"Pick a SOURCE (LFO, mod-env, tilt,", false},
    {"random, key, bend), a DEST (cutoff,", false},
    {"pitch, amp, drive, FX...), and an", false},
    {"amount. 6 slots. This is how a sound", false},
    {"becomes nobody else's but yours.", false},

    {"RANDOMIZE = INSTANT NEW SOUND", true},
    {"Settings > SOUND > Randomize rolls a", false},
    {"whole new patch in ONE tap, and plays", false},
    {"a preview. Roll till you love one,", false},
    {"then shift-save it onto a slot to keep", false},
    {"it (see SOUNDS). Nothing saves until", false},
    {"you do - it can't break a saved sound.", false},
    {"Init = a blank sound to build from.", false},
    {"Sound reset / Reset all sounds put", false},
    {"slots back to factory; both live here.", false},

    {"SAVES ARE FOREVER", true},
    {"Saved sounds survive reboots AND", false},
    {"firmware updates - never wiped.", false},
    {"Headphones: plug in (speaker mutes).", false},
};
constexpr int kLineCount = (int)(sizeof(kLines) / sizeof(kLines[0]));
constexpr int kVisible = 10;  // body lines fit ~10 at this size

}  // namespace

void run(M5Canvas& canvas) {
    int top = 0;
    uint64_t prev = ~0ULL;  // treat keys held on entry as already-down

    for (;;) {
        M5Cardputer.update();
        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;
        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };
        auto held = [&](int cd) { return (cur >> cd) & 1ULL; };

        if (hit(kExit1) || hit(kExit2)) break;

        // scroll: ;/. step, hold to run (a long page wants fast scroll)
        static uint32_t lastScroll = 0;
        const uint32_t nowMs = millis();
        int dir = 0;
        if (hit(kUp)) dir = -1;
        else if (hit(kDown)) dir = +1;
        else if (held(kUp) && nowMs - lastScroll > 90) dir = -1;
        else if (held(kDown) && nowMs - lastScroll > 90) dir = +1;
        if (dir) {
            top += dir;
            if (top < 0) top = 0;
            if (top > kLineCount - kVisible) top = kLineCount - kVisible;
            if (top < 0) top = 0;
            lastScroll = nowMs;
        }

        canvas.fillScreen(theme::kBg);
        canvas.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(top_left);
        canvas.setTextColor(theme::kAmber, theme::kPanel);
        canvas.drawString("HOW TO PLAY", 5, 3);
        canvas.setTextColor(theme::kDim, theme::kPanel);
        canvas.setTextDatum(top_right);
        canvas.drawString(top + kVisible < kLineCount ? ";/. scroll  ` back" : "` back",
                          cfg::kScreenW - 4, 3);
        canvas.setTextDatum(top_left);

        for (int row = 0; row < kVisible; ++row) {
            const int i = top + row;
            if (i >= kLineCount) break;
            const int y = 17 + row * 11;
            canvas.setTextColor(kLines[i].header ? theme::kAmber : theme::kIdle, theme::kBg);
            canvas.drawString(kLines[i].text, kLines[i].header ? 3 : 7, y);
            if (kLines[i].header)
                canvas.drawFastHLine(3, y + 9, cfg::kScreenW - 10, theme::kLine);
        }

        // scrollbar
        if (kLineCount > kVisible) {
            const int trackY = 17, trackH = kVisible * 11;
            int thumbH = trackH * kVisible / kLineCount;
            if (thumbH < 4) thumbH = 4;
            const int thumbY = trackY + (trackH - thumbH) * top / (kLineCount - kVisible);
            canvas.fillRect(cfg::kScreenW - 2, thumbY, 2, thumbH, theme::kDim);
        }

        canvas.pushSprite(0, 0);
        delay(16);
    }
}

}  // namespace help
