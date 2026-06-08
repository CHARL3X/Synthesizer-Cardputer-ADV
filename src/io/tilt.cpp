#include "tilt.h"

#include <M5Cardputer.h>

#include "../config.h"
#include "../storage/glide_config.h"

namespace tilt {

namespace {
bool gAvailable = false;
float gValue = 0.f;   // axis A (fwd/back), smoothed
float gValueB = 0.f;  // axis B (left/right roll), smoothed
}  // namespace

void begin() {
    gAvailable = M5.Imu.isEnabled();
}

bool available() {
    return gAvailable;
}

void poll() {
    if (!gAvailable) return;
    M5.Imu.update();
    float a[3] = {0.f, 0.f, 0.f};
    M5.Imu.getAccel(&a[0], &a[1], &a[2]);  // one read feeds both axes
    float v = a[cfg::kTiltAxis] * cfg::kTiltSign;
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    gValue += (v - gValue) * cfg::kTiltSmooth;

    float vb = a[cfg::kTiltAxisB] * cfg::kTiltSignB;
    if (vb > 1.f) vb = 1.f;
    if (vb < -1.f) vb = -1.f;
    gValueB += (vb - gValueB) * cfg::kTiltSmooth;
}

float value() {
    // "flat" is wherever the player calibrated it (settings: Tilt center)
    float v = gValue - store::get().tiltCenter;
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return v;
}

float raw() {
    return gValue;
}

float valueB() {
    float v = gValueB - store::get().tiltCenterB;
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return v;
}

float rawB() {
    return gValueB;
}

}  // namespace tilt
