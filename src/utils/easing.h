// SPDX-License-Identifier: GPL-3.0-or-later
// Timing curves for animations, solved the way CSS solves cubic-bezier():
// find the curve parameter whose x is the progress, return its y.

#pragma once

namespace easing {

// The CSS timing function cubic-bezier(x1, y1, x2, y2) at progress t.
// t is clamped to 0..1; the curve runs from (0, 0) to (1, 1). x1 and x2
// must lie in 0..1 (as in CSS), so x is monotonic and the solve is unique.
double cubicBezier(double x1, double y1, double x2, double y2, double t);

// Snap and settle: cubic-bezier(.25, 1, .5, 1). Leaves fast, lands softly,
// never overshoots. Every Animate motion uses it.
double snapSettle(double t);

} // namespace easing
