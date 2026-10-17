// SPDX-License-Identifier: GPL-3.0-or-later
// Beautify backgrounds that need more than a fill: the blurred "From
// screenshot" backdrop, auto gradient colors picked from the shot, and film
// grain. All CPU QImage work, deterministic, in physical pixels.

#pragma once

#include "utils/imageeffects.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <utility>

namespace backgroundfx {

// Two gradient colors picked from the shot: k-means (k = 4, fixed seed) on a
// ~64 px copy in OKLab, the two most vivid well-separated clusters (greys,
// near-white and near-black are skipped unless nothing else exists), nudged
// to a designed saturation and lightness, and pushed away from the shot's
// edge lightness so the shot stands out. first goes top-left.
std::pair<QColor, QColor> autoGradientColors(const QImage& shot);

// The shot scaled to cover `canvas`, blurred (~4% of the long side),
// saturation x1.4 and slightly darkened. Opaque, Format_RGB32.
QImage fromScreenshot(QSize canvas, const QImage& shot);

// Adds seeded monochrome noise in place (amount 0..100, 0 does nothing).
// Same image and amount always give the same pixels.
void addGrain(QImage& image, int amount);

// The full background for a canvas of `canvas` physical pixels: fill by
// mode (auto colors resolved from `shot`), then grain. Opaque Format_RGB32
// unless the mode is None (transparent ARGB32_Premultiplied).
QImage renderBackground(QSize canvas,
                        const EffectSettings& settings,
                        const QImage& shot);

// Clamped-edge box blur, `passes` times per axis, in place. Works on
// Format_RGB32 and Format_ARGB32_Premultiplied.
void boxBlur(QImage& image, int radius, int passes);

} // namespace backgroundfx
