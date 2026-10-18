// SPDX-License-Identifier: GPL-3.0-or-later
// Seamlessly looping animated backgrounds for the "Animate" export: pure
// CPU QImage work, deterministic for a seed, in physical pixels.

#pragma once

#include "utils/imageeffects.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <cstdint>
#include <utility>

enum class AnimStyle
{
    Aurora = 0,        // soft flowing light bands over a dark gradient
    GradientShift = 1, // two-colour linear gradient that turns once per loop
    Blobs = 2,         // 3 to 5 soft colour blobs orbiting
    Twinkle = 3,       // the still gradient with softly twinkling sparkles
};

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

// renderWork scaled smoothly up to `size`. No grain (the caller adds it once
// it is at full size).
QImage renderFrame(AnimStyle style,
                   const std::pair<QColor, QColor>& colors,
                   QSize size,
                   double phase,
                   uint32_t seed);

// The two colours an animation uses for these Beautify settings: the
// gradient colours, or the auto colours picked from the shot when auto
// colours or From screenshot is on. Solid uses its colour and a darker shade.
std::pair<QColor, QColor> colorsFor(const EffectSettings& settings,
                                    const QImage& shot);

// Clamps a stored style number to a style
AnimStyle styleFrom(int value);

} // namespace animatedbg
