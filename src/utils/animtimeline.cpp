// SPDX-License-Identifier: GPL-3.0-or-later

#include "animtimeline.h"
#include "easing.h"

#include <algorithm>
#include <cmath>

namespace animtimeline {

Plan plan(double durationSec, int count)
{
    Plan p;
    p.durationSec = std::max(0.0, durationSec);
    p.count = std::max(0, count);
    if (p.count == 0) {
        return p;
    }
    p.lead = kLeadSec;
    p.reveal = kRevealSec;
    p.fadeOut = kFadeOutSec;
    // Every reveal ends by kRevealShare of the loop, and the hold keeps at
    // least kMinHoldSec; many objects stagger closer to fit
    const double revealBy = std::min(kRevealShare * p.durationSec,
                                     p.durationSec - p.fadeOut - kMinHoldSec);
    const double room = std::max(0.0, revealBy - p.lead - p.reveal);
    p.stagger = p.count > 1 ? std::min(kStaggerSec, room / (p.count - 1)) : 0.0;
    return p;
}

double progress(const Plan& plan, int index, double tSec)
{
    if (!plan.drawsIn() || plan.reveal <= 0.0) {
        return 1.0;
    }
    const double u = (tSec - plan.revealStart(index)) / plan.reveal;
    if (u <= 0.0) {
        return 0.0;
    }
    return u >= 1.0 ? 1.0 : easing::snapSettle(u);
}

double opacity(const Plan& plan, int index, double tSec)
{
    if (!plan.drawsIn()) {
        return 1.0;
    }
    const double fadeStart = plan.fadeStart();
    if (tSec >= plan.durationSec) {
        return 0.0;
    }
    if (tSec > fadeStart) {
        return 1.0 - easing::snapSettle((tSec - fadeStart) / plan.fadeOut);
    }
    const double holdStart = plan.revealEnd();
    if (index != plan.count - 1 || tSec <= holdStart) {
        return 1.0;
    }
    // A whole number of slow breaths over the hold, starting and ending at 1
    const double hold = fadeStart - holdStart;
    const double cycles = std::max(1.0, std::round(hold / kPulsePeriodSec));
    const double u = (tSec - holdStart) / hold;
    return 1.0 - (1.0 - kPulseLow) * 0.5 * (1.0 - std::cos(2.0 * M_PI * cycles * u));
}

double motionPhase(const Plan& plan, double phase)
{
    if (!plan.drawsIn()) {
        return phase;
    }
    double p = phase - std::floor(phase);
    const double t = p * plan.durationSec;
    const double start = plan.revealEnd();
    const double end = plan.fadeStart();
    if (t <= start || t >= end || end <= start) {
        return 0.0;
    }
    return (t - start) / (end - start);
}

double motionDurationSec(const Plan& plan)
{
    if (!plan.drawsIn()) {
        return plan.durationSec;
    }
    return std::max(0.0, plan.fadeStart() - plan.revealEnd());
}

} // namespace animtimeline
