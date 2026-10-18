// SPDX-License-Identifier: GPL-3.0-or-later
// Liquid Glass bezel: a translucent rounded frame around the shot that
// refracts a heavily blurred copy of the background behind it.

#pragma once

#include <QImage>
#include <QRect>

namespace glassframe {

// Paints the bezel into `canvas` (Format_RGB32 or ARGB32_Premultiplied):
// the ring between `bezel` (outer, rounded by outerRadius) and `shot`
// (inner, rounded by innerRadius), styled after macOS Liquid Glass. Inside
// the ring: `background` blurred more, made more vivid (vibrancy) and
// sampled displaced toward the ring's centre line (strongest at the outer
// edge, like a lens), a white sheen brightest at the top, a specular rim
// all round (strongest top-left), a soft glow inside the outer edge, a soft
// shade and 1 px dark hairline where the bezel meets the shot, and faint
// static noise. Outside the ring, up to outset() pixels away, a soft drop
// shadow (heavier below) lifts the pane. Pixels well inside `shot` are not
// touched; the caller draws the shot on top. `background` covers the whole
// canvas and may be smaller than it (it is sampled scaled).
void drawBezel(QImage& canvas,
               const QImage& background,
               const QRect& bezel,
               qreal outerRadius,
               const QRect& shot,
               qreal innerRadius,
               qreal dpr);

// How far outside `bezel` drawBezel paints (its soft drop shadow), in
// physical pixels. Callers that draw the bezel into a layer leave this much
// room around it.
int outset(const QRect& bezel, const QRect& shot, qreal dpr);

} // namespace glassframe
