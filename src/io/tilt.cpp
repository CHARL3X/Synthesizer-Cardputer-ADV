#include "tilt.h"

#include <M5Cardputer.h>
#include <cmath>

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

    // ANGLES, not raw axis components. The raw component is sin(angle): it
    // maxed at exactly 90° and FOLDED back past vertical (tilting to 110° read
    // like 70°, so the effect "reset toward flat"), it compressed motion near
    // vertical, and a calibrated center permanently ate its offset off the top
    // of the range (sin can't exceed 1, so center +0.2 capped travel at 0.8).
    // atan2 against the screen-normal axis carries the quadrant: monotonic
    // through vertical, linear across the swing, and the center subtraction
    // keeps the full ±90° of authority around wherever "flat" is calibrated.
    // Normalized so 1.0 = 90°; value() clamps AFTER the center is subtracted,
    // so past-vertical holds at full effect instead of retracing.
    constexpr float kInvHalfPi = 1.f / 1.5707963f;
    const float az = a[cfg::kTiltAxisZ] * cfg::kTiltSignZ;
    const float v =
        atan2f(a[cfg::kTiltAxis] * cfg::kTiltSign, az) * kInvHalfPi;
    gValue += (v - gValue) * cfg::kTiltSmooth;

    // Roll measures against the in-plane remainder of gravity, so it stays
    // stable when the device is pitched steeply (az alone goes to 0 there,
    // which would make roll hair-trigger). It folds past 90° of ROLL — the
    // screen facing away sideways is not a playing position.
    const float ap = a[cfg::kTiltAxis];
    const float vb = atan2f(a[cfg::kTiltAxisB] * cfg::kTiltSignB,
                            sqrtf(ap * ap + az * az)) * kInvHalfPi;
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
