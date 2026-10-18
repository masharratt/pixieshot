// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>

enum class BackgroundMode
{
    None = 0,
    Solid = 1,
    Gradient = 2, // diagonal, top-left color1 to bottom-right color2
    // The shot itself scaled to cover the canvas, heavily blurred, more
    // saturated and a little darker (album-art look)
    FromScreenshot = 3,
};

// Settings for the after-capture effects. Sizes are logical pixels; the
// functions multiply them by the image devicePixelRatio.
struct EffectSettings
{
    int borderPx = 0;
    QColor borderColor = Qt::black;
    int cornerRadius = 0;
    bool shadow = false;
    int shadowBlur = 16;
    QPoint shadowOffset{ 0, 6 };
    QColor shadowColor = QColor(0, 0, 0, 110);
    int paddingPx = 0; // margin on each side of the shot (and its shadow)
    BackgroundMode backgroundMode = BackgroundMode::None;
    QColor backgroundColor1 = QColor(0x8B, 0x5C, 0xF6);
    QColor backgroundColor2 = QColor(0x3B, 0x82, 0xF6);
    // 0 off, 1 square 1:1, 2 landscape 16:9, 3 portrait 9:16, 4 social 4:5,
    // 5 classic 4:3. Grows the canvas (never crops) to the ratio.
    int aspect = 0;
    // Trim uneven near-uniform margins from the raw capture first
    bool autoBalance = false;
    // Gradient mode only: pick the two colors from the shot instead of
    // backgroundColor1/2
    bool autoColors = false;
    // Liquid Glass bezel of glassPx (logical) around the shot, between the
    // background and the shot. The shadow falls under the bezel.
    bool glass = false;
    int glassPx = 18;
    // 0..100 monochrome noise over the background only (kills banding)
    int grain = 0;
};

namespace imageeffects {

// Canvas grows by 2 * px (logical) on each axis
QImage addBorder(const QImage& src, int px, const QColor& color);

// Corner pixels become fully transparent
QImage roundCorners(const QImage& src, int radius);

// Canvas grows to fit the blur and offset; the original is drawn on top
QImage dropShadow(const QImage& src,
                  int blur,
                  const QPoint& offset,
                  const QColor& color);

// The shadow src's alpha casts, without src's own pixels, on the canvas
// beautifygeometry::shadowStep gives
QImage shadowLayer(const QImage& src,
                   int blur,
                   const QPoint& offset,
                   const QColor& color);

// The shadow the glass bezel casts: a plate of bezelSize physical pixels
// rounded by outerRadius, shadowed with the settings' shadow options
QImage glassShadow(QSize bezelSize,
                   qreal outerRadius,
                   const EffectSettings& settings,
                   qreal dpr);

// Canvas grows by 2 * padding (logical) on each axis, src is centered. With
// BackgroundMode::None the padding is transparent. A background fills the whole
// canvas, even when padding is 0. A non-zero aspect (see EffectSettings) then
// grows the canvas further to that ratio, src stays centered and unscaled.
QImage addBackground(const QImage& src,
                     int padding,
                     BackgroundMode mode,
                     const QColor& color1,
                     const QColor& color2,
                     int aspect = 0);

// Makes margins even: on each axis the wider of the two near-uniform edge
// margins is trimmed down to the narrower one (tolerance ~8 per channel, at
// most 40% of the size per side, always at least 1px of content left).
QImage autoBalance(const QImage& src);

// Order: auto-balance, border, round corners, glass bezel, shadow, padding +
// background + aspect. Identity when everything is off.
QImage applyEffects(const QImage& src, const EffectSettings& settings);

// Only border, rounded corners and shadow: what the "Apply effects"
// after-capture action always did. The Beautify extras (padding, background,
// aspect, glass, grain, auto-balance) are left out.
EffectSettings basicEffects(const EffectSettings& settings);

// True when the result can contain transparent pixels (a background fills
// the canvas, so it makes the result opaque)
bool needsAlpha(const EffectSettings& settings);

// True when the background is computed from the shot's pixels (From
// screenshot, or Gradient with auto colors)
bool backgroundUsesShot(const EffectSettings& settings);

} // namespace imageeffects
