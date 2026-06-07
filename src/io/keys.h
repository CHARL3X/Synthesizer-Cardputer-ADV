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
//   ctrl/opt octave-/+ (left thumb)   alt sustain (left thumb)
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
bool quickEditActive();
int quickEditParam();          // selected slot 0..9
const char* quickParamName(int idx);
void quickParamValue(int idx, char* out, int cap);  // formatted current value
float bendCentsNow();
bool holdLatched();
bool sustainActive();

// re-sync edge state after a blocking screen (settings) ate the keyboard
void resync();

}  // namespace keys
