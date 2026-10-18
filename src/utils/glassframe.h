// SPDX-License-Identifier: GPL-3.0-or-later
// Liquid Glass bezel: a translucent rounded frame around the shot that
// refracts a heavily blurred copy of the background behind it.

#pragma once

#include <QImage>
#include <QRect>

namespace glassframe {

// Paints the bezel into `canvas` (Format_RGB32 or ARGB32_Premultiplied):
// the ring between `bezel` (outer, rounded by outerRadius) and `shot`
// (inner, rounded by innerRadius). Inside the ring: `background` blurred
// more and sampled displaced toward the ring's centre line (strongest at the
// edges, like a lens), an 18% white tint, a specular rim along the top-left
// inner and outer edges fading to the bottom-right, and a 1 px dark hairline
// where the bezel meets the shot. Pixels well inside `shot` are not touched;
// the caller draws the shot on top. `background` covers the whole canvas
// and may be smaller than it (it is sampled scaled); at the canvas size the
// result is the same as before that was allowed.
void drawBezel(QImage& canvas,
               const QImage& background,
               const QRect& bezel,
               qreal outerRadius,
               const QRect& shot,
               qreal innerRadius,
               qreal dpr);

} // namespace glassframe
