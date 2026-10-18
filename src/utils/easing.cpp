// SPDX-License-Identifier: GPL-3.0-or-later

#include "easing.h"

#include <algorithm>
#include <cmath>

namespace {

// One coordinate of the curve with control values p1, p2 (ends 0 and 1),
// in polynomial form: ((a s + b) s + c) s
struct Axis
{
    double a, b, c;
    Axis(double p1, double p2)
      : a(1.0 - 3.0 * p2 + 3.0 * p1)
      , b(3.0 * p2 - 6.0 * p1)
      , c(3.0 * p1)
    {}
    double at(double s) const { return ((a * s + b) * s + c) * s; }
    double slope(double s) const { return (3.0 * a * s + 2.0 * b) * s + c; }
};

constexpr double kEpsilon = 1e-12;

} // namespace

namespace easing {

double cubicBezier(double x1, double y1, double x2, double y2, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0) {
        return 0.0;
    }
    if (t >= 1.0) {
        return 1.0;
    }
    const Axis x(std::clamp(x1, 0.0, 1.0), std::clamp(x2, 0.0, 1.0));
    const Axis y(y1, y2);
    // Newton from s = t converges in a few steps where the slope is healthy
    double s = t;
    for (int i = 0; i < 8; ++i) {
        const double err = x.at(s) - t;
        if (std::abs(err) < kEpsilon) {
            return y.at(s);
        }
        const double d = x.slope(s);
        if (std::abs(d) < 1e-6) {
            break;
        }
        s -= err / d;
        if (s < 0.0 || s > 1.0) {
            break;
        }
    }
    // Bisection always works: x is monotonic for x1, x2 in 0..1
    double lo = 0.0;
    double hi = 1.0;
    s = t;
    for (int i = 0; i < 100; ++i) {
        s = 0.5 * (lo + hi);
        const double v = x.at(s);
        if (std::abs(v - t) < kEpsilon) {
            break;
        }
        (v < t ? lo : hi) = s;
    }
    return y.at(s);
}

double snapSettle(double t)
{
    return cubicBezier(0.25, 1.0, 0.5, 1.0, t);
}

} // namespace easing
