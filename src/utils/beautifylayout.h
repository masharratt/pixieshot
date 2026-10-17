// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry of the Beautify effects without the pixels. imageeffects uses the
// per-step helpers to size its canvases, and video export uses
// beautifyLayout() to place each frame, so stills and recordings match.

#pragma once

#include "utils/imageeffects.h"

#include <QPoint>
#include <QRect>
#include <QSize>

// Where things land on the Beautify canvas, all in physical pixels
struct BeautifyLayout
{
    QSize canvas;             // output size
    QRect shot;               // the shot including its border
    QRect frame;              // the untouched source pixels
    qreal cornerRadius = 0.0; // rounding applied to `shot`
    QRect bezel;              // glass bezel around `shot` (== shot when off)
    qreal bezelRadius = 0.0;  // rounding applied to `bezel`

    // True when the effects change nothing
    bool isIdentity() const;
};

// Layout of imageeffects::applyEffects for a source of sourcePx physical
// pixels shown at dpr. Auto-balance is left out: it depends on the pixels.
BeautifyLayout beautifyLayout(QSize sourcePx,
                              const EffectSettings& settings,
                              qreal dpr = 1.0);

// Canvas size for a video: H.264 needs even sizes, so an odd axis grows by
// one pixel at the right or bottom edge (the background is extended)
QSize videoCanvasSize(QSize canvas);

namespace beautifygeometry {

// Logical size at dpr, rounded to whole pixels
int scaledPx(int logical, qreal dpr);

// One step of the effect chain: the canvas it produces and where its input
// is drawn on that canvas
struct Step
{
    QSize canvas;
    QPoint origin;
};

Step borderStep(QSize input, int borderPx, qreal dpr);

// Glass bezel of glassPx logical pixels on each side
Step glassStep(QSize input, int glassPx, qreal dpr);

// Box blur radius per pass for a shadow blur of `blur` logical pixels
int shadowBlurRadius(int blur, qreal dpr);
inline constexpr int kShadowBlurPasses = 3;
Step shadowStep(QSize input, int blur, QPoint offset, qreal dpr);

// Padding on each side, then growth to the aspect preset (see
// EffectSettings::aspect). The input is centered.
Step backgroundStep(QSize input, int padding, int aspect, qreal dpr);

} // namespace beautifygeometry
