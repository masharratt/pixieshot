// SPDX-License-Identifier: GPL-3.0-or-later
// Liquid Glass bezel: a translucent rounded frame around the shot that
// refracts the background behind it and the shot's own edge.

#pragma once

#include <QImage>
#include <QRect>

namespace glassframe {

// Paints the bezel into `canvas` (Format_RGB32 or ARGB32_Premultiplied):
// the ring between `bezel` (outer, rounded by outerRadius) and `shot`
// (inner, rounded by innerRadius), styled after macOS Liquid Glass. Inside
// the ring: `background` lightly blurred and made more vivid (vibrancy),
// always a clear step lighter than a dark backdrop or darker than a light
// one; an outer squircle bevel that bends the background inward and, when
// `content` (the shot's pixels, covering `shot`) is given, mirrors the
// shot's edge into view as a thick lens does; a white sheen at the top; a
// screen-blended specular rim (strongest top-left, a weaker lobe at the
// bottom-right); a soft glow inside the outer edge; and where the bezel
// meets the shot a dark hairline, a thin lit lip and a soft shade; plus
// faint static noise. Outside the ring, up to outset() pixels away, a tight
// contact shadow and a broad soft one (both heavier below) lift the pane.
// Pixels well inside `shot` are not touched; the caller draws the shot on
// top. `background` covers the whole canvas and may be smaller than it (it
// is sampled scaled).
void drawBezel(QImage& canvas,
               const QImage& background,
               const QRect& bezel,
               qreal outerRadius,
               const QRect& shot,
               qreal innerRadius,
               qreal dpr,
               const QImage& content = QImage());

// How far outside `bezel` drawBezel paints (its soft drop shadow), in
// physical pixels. Callers that draw the bezel into a layer leave this much
// room around it.
int outset(const QRect& bezel, const QRect& shot, qreal dpr);

} // namespace glassframe
