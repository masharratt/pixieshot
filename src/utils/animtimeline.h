// SPDX-License-Identifier: GPL-3.0-or-later
// When annotations draw themselves in the "Animate" export: one reveal per
// object in creation order, a hold where everything is shown (the shot
// motion plays there), then a fade so the loop ends where it began. Pure
// math in seconds, no pixels; AnimatedFrames draws with it.

#pragma once

namespace animtimeline {

inline constexpr double kLeadSec = 0.15;    // nothing moves at the very start
inline constexpr double kRevealSec = 0.35;  // one object drawing itself
inline constexpr double kStaggerSec = 0.25; // between two objects' starts
inline constexpr double kRevealShare = 0.45; // every reveal done by this share
inline constexpr double kFadeOutSec = 0.4;  // all objects fade out together
inline constexpr double kMinHoldSec = 1.2;  // everything shown at least this long
inline constexpr double kPulseLow = 0.85;   // opacity floor of the last object's pulse
inline constexpr double kPulsePeriodSec = 1.6;

struct Plan
{
    double durationSec = 0.0;
    int count = 0; // objects that draw in; 0 means no timeline
    double lead = 0.0;
    double reveal = 0.0;
    double stagger = 0.0;
    double fadeOut = 0.0;

    bool drawsIn() const { return count > 0; }
    double revealStart(int index) const { return lead + index * stagger; }
    // The last reveal is done (the hold starts)
    double revealEnd() const
    {
        return count > 0 ? revealStart(count - 1) + reveal : 0.0;
    }
    // Everything starts to fade out (the hold ends)
    double fadeStart() const { return durationSec - fadeOut; }
};

// The plan for `count` objects in a loop of durationSec. Many objects
// stagger closer so every reveal still ends by kRevealShare of the loop.
Plan plan(double durationSec, int count);

// How far object `index` has drawn itself at tSec, eased 0..1
double progress(const Plan& plan, int index, double tSec);

// Opacity of object `index` at tSec: 1 while shown, falling to 0 over the
// fade, and a slow pulse (kPulseLow..1) on the last object during the hold
double opacity(const Plan& plan, int index, double tSec);

// Phase (0..1) and loop length the shot motion runs on. Without a
// timeline it is the whole loop; with one the motion plays only during the
// hold, at rest while objects draw in and while they fade out.
double motionPhase(const Plan& plan, double phase);
double motionDurationSec(const Plan& plan);

} // namespace animtimeline
