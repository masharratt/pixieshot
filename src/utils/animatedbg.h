// SPDX-License-Identifier: GPL-3.0-or-later
// Seamlessly looping animated backgrounds for the "Animate" export: pure
// CPU QImage work, deterministic for a seed, in physical pixels.

#pragma once

#include "utils/imageeffects.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QVector>
#include <cstdint>
#include <utility>

enum class AnimStyle
{
    Aurora = 0,        // soft flowing light bands over a dark gradient
    GradientShift = 1, // two-colour linear gradient that turns once per loop
    Blobs = 2,         // 3 to 5 soft colour blobs orbiting
    Twinkle = 3,       // the still gradient with softly twinkling sparkles
    Flow = 4,          // bold slanted gradient with liquid ribbons flowing through
    Silk = 5,          // fine twisting strands, front and back lit, on a deep ground
    Mesh = 6,          // a mesh gradient: colour points drifting and blending
    Bokeh = 7,         // soft out-of-focus discs drifting over a dim ground
    Waves = 8,         // layered rolling hills of colour, lit along each crest
    Grid = 9,          // a perspective floor grid with a glow travelling over it
    Prism = 10,        // light caustics: rippling bright veins over a glow
    Contours = 11,     // topographic contour lines over rainbow hills
};
// Keep in step with the last value: config bounds, pickers and packs use it
inline constexpr int kAnimStyleCount = 12;

namespace animatedbg {

// Backgrounds are drawn at most this long (long side) and scaled up: the
// content is soft, so detail above it is never seen and a frame stays cheap
inline constexpr int kWorkLongSide = 480;
// Seed used by exports and the settings preview, so both look the same
inline constexpr uint32_t kDefaultSeed = 0x5eedu;

// Size the background is drawn at before scaling up to `size`
QSize workSize(QSize size);

// The background for `phase` at workSize(size). phase wraps into [0, 1), so
// 0 and 1 give identical pixels, and every motion completes whole cycles per
// loop, so the last frames run smoothly into the first. Opaque Format_RGB32.
QImage renderWork(AnimStyle style,
                  const std::pair<QColor, QColor>& colors,
                  QSize size,
                  double phase,
                  uint32_t seed);

// The same with a multi-stop palette (two or more colours, first to last).
// With two stops it is exactly renderWork with that pair.
QImage renderWorkStops(AnimStyle style,
                  const QVector<QColor>& stops,
                  QSize size,
                  double phase,
                  uint32_t seed);

// renderWork scaled smoothly up to `size`. No grain (the caller adds it once
// it is at full size).
QImage renderFrame(AnimStyle style,
                   const std::pair<QColor, QColor>& colors,
                   QSize size,
                   double phase,
                   uint32_t seed);

QImage renderFrameStops(AnimStyle style,
                   const QVector<QColor>& stops,
                   QSize size,
                   double phase,
                   uint32_t seed);

// The two colours an animation uses for these Beautify settings: the
// gradient colours, or the auto colours picked from the shot when auto
// colours or From screenshot is on. Solid uses its colour and a darker shade.
std::pair<QColor, QColor> colorsFor(const EffectSettings& settings,
                                    const QImage& shot);

// All the stops an animation uses: the Beautify palette when one is set
// (Gradient mode, no auto colours), otherwise colorsFor() as two stops
QVector<QColor> stopsFor(const EffectSettings& settings, const QImage& shot);

// Clamps a stored style number to a style
AnimStyle styleFrom(int value);

} // namespace animatedbg
