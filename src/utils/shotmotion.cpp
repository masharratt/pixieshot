// SPDX-License-Identifier: GPL-3.0-or-later

#include "shotmotion.h"
#include "easing.h"

#include <algorithm>
#include <cmath>

namespace {

// Share of the hold at each end where the light band waits off the shot
constexpr double kSweepMargin = 0.1;
// Hold as a share of the loop, before the kMinHoldSec floor
constexpr double kHoldShare = 0.5;
// Camera distance for the tilt, in multiples of the layer's long side:
// further away is flatter
constexpr double kFocal = 2.0;
// Shadow travel at full motion, logical pixels
constexpr double kTiltShadowX = 3.0;
constexpr double kTiltShadowY = 4.0;

double wrap(double phase)
{
    double p = phase - std::floor(phase);
    return p >= 1.0 ? 0.0 : p;
}

QTransform aroundCentre(const QTransform& t, const QPointF& c, const QPointF& move = {})
{
    return QTransform::fromTranslate(-c.x(), -c.y()) * t *
           QTransform::fromTranslate(c.x() + move.x(), c.y() + move.y());
}

} // namespace

namespace shotmotion {

Timeline timeline(double durationSec)
{
    const double d = std::max(0.0, durationSec);
    Timeline tl;
    // Short loops keep the hold and shorten the eases
    tl.easeIn = std::clamp((d - kMinHoldSec) / 2.0, 0.0, kEaseSec);
    tl.easeOut = tl.easeIn;
    tl.hold = std::min(std::max(kMinHoldSec, kHoldShare * d), d - 2.0 * tl.easeIn);
    tl.hold = std::max(0.0, tl.hold);
    const double rest = std::max(0.0, d - tl.easeIn - tl.hold - tl.easeOut);
    tl.leadRest = rest / 2.0;
    return tl;
}

Pose poseAt(ShotMotion motion, double phase, double durationSec)
{
    Pose pose;
    const double p = wrap(phase);
    switch (motion) {
        case ShotMotion::None:
            return pose;
        case ShotMotion::Float:
            // Up over the first half, back down over the second
            pose.amount = p < 0.5 ? easing::snapSettle(p / 0.5)
                                  : 1.0 - easing::snapSettle((p - 0.5) / 0.5);
            return pose;
        case ShotMotion::Tilt:
            break;
    }
    const Timeline tl = timeline(durationSec);
    const double t = p * durationSec;
    if (t < tl.leadRest) {
        return pose;
    }
    if (t < tl.holdStart()) {
        pose.amount = easing::snapSettle((t - tl.leadRest) / tl.easeIn);
        return pose;
    }
    if (t < tl.holdEnd()) {
        pose.amount = 1.0;
        const double s0 = tl.holdStart() + kSweepMargin * tl.hold;
        const double s1 = tl.holdEnd() - kSweepMargin * tl.hold;
        pose.sweep = easing::snapSettle((t - s0) / std::max(1e-9, s1 - s0));
        return pose;
    }
    if (t < tl.holdEnd() + tl.easeOut) {
        pose.amount = 1.0 - easing::snapSettle((t - tl.holdEnd()) / tl.easeOut);
    }
    return pose;
}

QTransform transformFor(ShotMotion motion,
                        double amount,
                        const QRectF& rect,
                        qreal dpr)
{
    if (amount == 0.0 || rect.isEmpty()) {
        return {};
    }
    const QPointF c = rect.center();
    switch (motion) {
        case ShotMotion::None:
            return {};
        case ShotMotion::Float: {
            const double s = 1.0 + kFloatScale * amount;
            return aroundCentre(QTransform::fromScale(s, s),
                                c,
                                QPointF(0, -kFloatLiftPx * dpr * amount));
        }
        case ShotMotion::Tilt:
            break;
    }
    // Turn about the vertical axis (the right side goes back) and the
    // horizontal one (the top goes back), then project with a camera
    // kFocal long sides away. The plane z = 0 maps by a homography.
    const double yaw = -kMaxYawDeg * amount * M_PI / 180.0;
    const double pitch = -kMaxPitchDeg * amount * M_PI / 180.0;
    const double ca = std::cos(yaw);
    const double sa = std::sin(yaw);
    const double cb = std::cos(pitch);
    const double sb = std::sin(pitch);
    const double focal = kFocal * std::max(rect.width(), rect.height());
    // R = Ry(yaw) * Rx(pitch) applied to (x, y, 0)
    const double r11 = ca;
    const double r12 = sa * sb;
    const double r22 = cb;
    const double r31 = -sa;
    const double r32 = ca * sb;
    const QTransform h(r11, 0.0, r31 / focal, r12, r22, r32 / focal, 0.0, 0.0, 1.0);
    return aroundCentre(h, c);
}

QPointF shadowShift(ShotMotion motion, double amount, qreal dpr)
{
    switch (motion) {
        case ShotMotion::None:
            return {};
        case ShotMotion::Tilt:
            return { kTiltShadowX * dpr * amount, kTiltShadowY * dpr * amount };
        case ShotMotion::Float:
            // The shot rises, its shadow stays nearer the ground
            return { 0.0, 0.5 * kFloatLiftPx * dpr * amount };
    }
    return {};
}

ShotMotion motionFrom(int value)
{
    return ShotMotion(std::clamp(value, 0, 2));
}

} // namespace shotmotion
