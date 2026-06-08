// Optional motion modulation from the BMI270. Deliberately humble: tilt is
// an assignable effects modulator (cutoff / vibrato / volume) that defaults
// to OFF and can be ignored entirely. It is NEVER pitch bend — that was
// rejected out loud ("fuck, I gotta lean it over again").
#pragma once

namespace tilt {

void begin();
bool available();
void poll();      // call once per UI frame — updates both axes in one IMU read
float value();    // axis A (fwd/back): smoothed, center-calibrated, -1..+1
float raw();      // axis A uncalibrated smoothed reading (for center capture)
float valueB();   // axis B (left/right roll): center-calibrated, -1..+1
float rawB();     // axis B uncalibrated smoothed reading
}  // namespace tilt
