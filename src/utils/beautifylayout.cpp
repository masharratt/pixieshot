// SPDX-License-Identifier: GPL-3.0-or-later

#include "beautifylayout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// Ratio width:height for the aspect presets, {0, 0} when off or unknown
QSize aspectRatio(int aspect)
{
    switch (aspect) {
        case 1:
            return { 1, 1 };
        case 2:
            return { 16, 9 };
        case 3:
            return { 9, 16 };
        case 4:
            return { 4, 5 };
        case 5:
            return { 4, 3 };
        default:
            return {};
    }
}

} // namespace

namespace beautifygeometry {

int scaledPx(int logical, qreal dpr)
{
    return static_cast<int>(std::lround(logical * dpr));
}

Step borderStep(QSize input, int borderPx, qreal dpr)
{
    const int b = std::max(0, scaledPx(borderPx, dpr));
    return { input + QSize(2 * b, 2 * b), QPoint(b, b) };
}

Step glassStep(QSize input, int glassPx, qreal dpr)
{
    return borderStep(input, glassPx, dpr);
}

int shadowBlurRadius(int blur, qreal dpr)
{
    return std::max(0,
                    (scaledPx(blur, dpr) + kShadowBlurPasses - 1) /
                      kShadowBlurPasses);
}

Step shadowStep(QSize input, int blur, QPoint offset, qreal dpr)
{
    const int pad = shadowBlurRadius(blur, dpr) * kShadowBlurPasses;
    const int dx = scaledPx(offset.x(), dpr);
    const int dy = scaledPx(offset.y(), dpr);
    return { QSize(input.width() + 2 * pad + std::abs(dx),
                   input.height() + 2 * pad + std::abs(dy)),
             QPoint(pad + (dx < 0 ? -dx : 0), pad + (dy < 0 ? -dy : 0)) };
}

Step backgroundStep(QSize input, int padding, int aspect, qreal dpr)
{
    const int pad = std::max(0, scaledPx(padding, dpr));
    const QSize ratio = aspectRatio(aspect);
    int w = input.width() + 2 * pad;
    int h = input.height() + 2 * pad;
    if (!ratio.isEmpty()) {
        // Grow the shorter axis, rounding up so the shot always fits
        if (qint64(w) * ratio.height() < qint64(h) * ratio.width()) {
            w = int((qint64(h) * ratio.width() + ratio.height() - 1) /
                    ratio.height());
        } else {
            h = int((qint64(w) * ratio.height() + ratio.width() - 1) /
                    ratio.width());
        }
    }
    return { QSize(w, h),
             QPoint((w - input.width()) / 2, (h - input.height()) / 2) };
}

} // namespace beautifygeometry

bool BeautifyLayout::isIdentity() const
{
    return frame == QRect(QPoint(0, 0), canvas) && shot == frame &&
           cornerRadius <= 0.0;
}

QSize videoCanvasSize(QSize canvas)
{
    auto even = [](int v) { return std::max(2, v + (v & 1)); };
    return { even(canvas.width()), even(canvas.height()) };
}

BeautifyLayout beautifyLayout(QSize sourcePx,
                              const EffectSettings& settings,
                              qreal dpr)
{
    using namespace beautifygeometry;
    // Same order as imageeffects::applyEffects; each step moves what is
    // already placed by the origin of its input on its new canvas
    BeautifyLayout layout;
    layout.canvas = sourcePx;
    layout.frame = QRect(QPoint(0, 0), sourcePx);
    if (sourcePx.isEmpty()) {
        layout.shot = layout.frame;
        return layout;
    }
    if (settings.borderPx > 0) {
        const Step s = borderStep(layout.canvas, settings.borderPx, dpr);
        layout.canvas = s.canvas;
        layout.frame.translate(s.origin);
    }
    layout.shot = QRect(QPoint(0, 0), layout.canvas);
    if (settings.cornerRadius > 0) {
        layout.cornerRadius = settings.cornerRadius * dpr;
    }
    layout.bezel = layout.shot;
    layout.bezelRadius = layout.cornerRadius;
    auto place = [&layout](const Step& s) {
        layout.canvas = s.canvas;
        layout.frame.translate(s.origin);
        layout.shot.translate(s.origin);
        layout.bezel.translate(s.origin);
    };
    if (settings.glass && scaledPx(settings.glassPx, dpr) > 0) {
        const Step s = glassStep(layout.canvas, settings.glassPx, dpr);
        place(s);
        layout.bezel = QRect(QPoint(0, 0), layout.canvas);
        layout.bezelRadius = layout.cornerRadius + s.origin.x();
    }
    if (settings.shadow) {
        place(shadowStep(
          layout.canvas, settings.shadowBlur, settings.shadowOffset, dpr));
    }
    place(backgroundStep(
      layout.canvas, settings.paddingPx, settings.aspect, dpr));
    return layout;
}
