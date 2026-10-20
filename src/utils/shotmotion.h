// SPDX-License-Identifier: GPL-3.0-or-later
// Motion of the shot in the "Animate" export: the timeline per loop and the
// transform it gives the shot layer (shot, border, glass bezel). Pure math,
// no pixels; AnimatedFrames draws with it.

#pragma once

#include <QPointF>
#include <QRectF>
#include <QTransform>

enum class ShotMotion
{
    None = 0,  // the shot stays put
    Tilt = 1,  // gentle 3D tilt that holds while a band of light sweeps by
    Float = 2, // slow vertical drift with a slight breathing scale
    Orbit = 3, // slow 3D turn one way, then the other, like a turntable
};
// Keep in step with the last value: config bounds and pickers use it
inline constexpr int kShotMotionCount = 4;

namespace shotmotion {

inline constexpr double kMaxYawDeg = 8.0;
inline constexpr double kMaxPitchDeg = 5.0;
inline constexpr double kMinHoldSec = 1.2;
inline constexpr double kEaseSec = 0.5;
inline constexpr double kFloatLiftPx = 8.0;  // logical, scaled by dpr
inline constexpr double kFloatScale = 0.012; // extra scale at the top
inline constexpr double kOrbitYawDeg = 10.0;  // turn each way at the peak
inline constexpr double kOrbitPitchDeg = 3.0; // lean back at the peak

// Tilt loop in seconds: rest, ease in, hold, ease out, rest. The two rests
// are equal and fill what is left of the loop.
struct Timeline
{
    double leadRest = 0;
    double easeIn = 0;
    double hold = 0;
    double easeOut = 0;
    double holdStart() const { return leadRest + easeIn; }
    double holdEnd() const { return holdStart() + hold; }
};
Timeline timeline(double durationSec);

// Where a motion is at one moment
struct Pose
{
    // 0 at rest, 1 fully tilted or lifted; Orbit runs -1..1, the sign is
    // the way it turns
    double amount = 0.0;
    double sweep = -1.0; // light band position 0..1 across the shot, < 0 none
    bool operator==(const Pose& o) const
    {
        return amount == o.amount && sweep == o.sweep;
    }
};

// The pose at phase 0..1 of a loop of durationSec. Phase 1 is phase 0, and
// every motion is at rest there, so the loop closes exactly.
Pose poseAt(ShotMotion motion, double phase, double durationSec);

// Canvas-to-canvas transform for the layer covering `rect` (canvas pixels)
// at `amount`. Identity at amount 0 and for None.
QTransform transformFor(ShotMotion motion,
                        double amount,
                        const QRectF& rect,
                        qreal dpr);

// How far the shadow moves (canvas pixels) at `amount`
QPointF shadowShift(ShotMotion motion, double amount, qreal dpr);

// Clamps a stored number to a motion
ShotMotion motionFrom(int value);

} // namespace shotmotion
