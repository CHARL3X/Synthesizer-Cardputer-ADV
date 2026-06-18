// The playing surface. Reads the key matrix POSITIONALLY (keyList(), not
// the char word — chars mutate under shift, positions don't), and turns
// finger motion into note events:
//
//   string 3 (hi) |  1 2 3 4 5 6 7 8 9 0  | -:oct-  =:oct+  bksp:panic
//   string 2      |  q w e r t y u i o p  | [:bend- ]:bend+ \:hold
//   string 1      |  a s d f g h j k l ;  | ':scale-lock    enter:tilt
//   string 0 (lo) |  z x c v b n m , . /  | space:sustain
//
//   ` exit   tab settings   fn(hold) quick-edit   shift(hold) chromatic
//   ctrl/opt volume-/+ (left thumb)   - = octave-/+   alt loop pedal
//
// In string mode each row is a mono lane: a new press while the lane sings
// hands the voice off legato (hammer-on); releasing back onto a still-held
// key glides back (pull-off). That is the slide.
#pragma once
#include <cstdint>

namespace keys {

struct Actions {
    bool openSettings = false;
    bool exitApp = false;
    bool gridPressed = false;  // any note key went down (dismisses the intro)
};

void begin();
Actions poll(uint32_t nowMs);

// view state for the perform screen
bool noteHeld(int string, int col);
// Richer grid-map state: 0 = silent, 1 = held lead, 2 = latched drone,
// 3 = drone flashing on the jam-motion beat (the backing made visible).
int noteState(int string, int col, uint32_t nowMs);
bool quickEditActive();
int quickEditParam();          // selected slot 0..9
const char* quickParamName(int idx);
void quickParamValue(int idx, char* out, int cap);  // formatted current value
float quickParamFill(int idx);  // 0..1 gauge for the edit panel, <0 = no bar
float bendCentsNow();
bool holdLatched();
bool sustainActive();
bool tiltLatched();  // mod-latch: tilt values frozen (long-press enter)

// auto chord progression (jam motion = progression): is the mode live, how
// many chord steps spelled, which is playing now (-1 = idle), each step's
// root-note label, and the grid cell of the chord sounding now — the perform
// screen's progression annunciator and grid blink.
bool progActive();
int progLen();
int progIndex();
void progStepName(int i, char* out, int cap);
bool progCurrentCell(int& string, int& col);

// re-sync edge state after a blocking screen (settings) ate the keyboard
void resync();

}  // namespace keys
