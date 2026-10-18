// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageeffects.h"
#include "backgroundfx.h"
#include "beautifylayout.h"
#include "glassframe.h"

#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {

using beautifygeometry::kShadowBlurPasses;
using beautifygeometry::scaledPx;

// All painting happens in physical pixels, so work on a DPR 1 view of the image
QImage physical(const QImage& src)
{
    QImage plain = src;
    plain.setDevicePixelRatio(1.0);
    return plain;
}

// One box blur pass along rows or columns, treating pixels outside as zero
void boxBlur(QVector<float>& data, int w, int h, int radius, bool horizontal)
{
    if (radius <= 0) {
        return;
    }
    const int lines = horizontal ? h : w;
    const int length = horizontal ? w : h;
    const int stride = horizontal ? 1 : w;
    const int lineStep = horizontal ? w : 1;
    const float norm = 1.0f / float(2 * radius + 1);
    QVector<float> out(length);
    for (int line = 0; line < lines; ++line) {
        float* base = data.data() + line * lineStep;
        float sum = 0;
        for (int i = 0; i <= std::min(radius, length - 1); ++i) {
            sum += base[i * stride];
        }
        for (int i = 0; i < length; ++i) {
            out[i] = sum * norm;
            const int add = i + radius + 1;
            const int sub = i - radius;
            if (add < length) {
                sum += base[add * stride];
            }
            if (sub >= 0) {
                sum -= base[sub * stride];
            }
        }
        for (int i = 0; i < length; ++i) {
            base[i * stride] = out[i];
        }
    }
}

constexpr int kBalanceTolerance = 8;
constexpr double kMaxTrimFraction = 0.4;

bool near(QRgb a, QRgb b)
{
    return std::abs(qRed(a) - qRed(b)) <= kBalanceTolerance &&
           std::abs(qGreen(a) - qGreen(b)) <= kBalanceTolerance &&
           std::abs(qBlue(a) - qBlue(b)) <= kBalanceTolerance &&
           std::abs(qAlpha(a) - qAlpha(b)) <= kBalanceTolerance;
}

// Number of leading uniform lines (rows or columns) from one edge, limited
// to maxCount. A line is uniform when every pixel is near the edge's first
// pixel.
int uniformMargin(const QImage& img, bool rows, bool fromStart, int maxCount)
{
    const int lines = rows ? img.height() : img.width();
    const int length = rows ? img.width() : img.height();
    auto pixelAt = [&](int line, int i) {
        return rows ? img.pixel(i, line) : img.pixel(line, i);
    };
    const int first = fromStart ? 0 : lines - 1;
    const QRgb ref = pixelAt(first, 0);
    int count = 0;
    while (count < maxCount) {
        const int line = fromStart ? count : lines - 1 - count;
        for (int i = 0; i < length; ++i) {
            if (!near(pixelAt(line, i), ref)) {
                return count;
            }
        }
        ++count;
    }
    return count;
}

} // namespace

namespace imageeffects {

QImage addBorder(const QImage& src, int px, const QColor& color)
{
    const qreal dpr = src.devicePixelRatio();
    const beautifygeometry::Step step =
      beautifygeometry::borderStep(src.size(), px, dpr);
    if (src.isNull() || step.origin.x() <= 0) {
        return src;
    }
    QImage out(step.canvas,
               src.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                     : QImage::Format_RGB32);
    out.fill(color);
    {
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(step.origin, physical(src));
    }
    out.setDevicePixelRatio(dpr);
    return out;
}

QImage roundCorners(const QImage& src, int radius)
{
    const qreal dpr = src.devicePixelRatio();
    const qreal r = radius * dpr;
    if (src.isNull() || r <= 0) {
        return src;
    }
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(src.size())), r, r);
        p.setClipPath(path);
        p.drawImage(0, 0, physical(src));
    }
    out.setDevicePixelRatio(dpr);
    return out;
}

} // namespace imageeffects

namespace {

bool glassActive(const EffectSettings& settings, qreal dpr)
{
    return settings.glass && scaledPx(settings.glassPx, dpr) > 0;
}

// The shadow of src alone, on the canvas shadowStep gives (src's own
// pixels are not drawn)
QImage shadowLayer(const QImage& src,
                   int blur,
                   const QPoint& offset,
                   const QColor& color)
{
    const qreal dpr = src.devicePixelRatio();
    const int radius = beautifygeometry::shadowBlurRadius(blur, dpr);
    const beautifygeometry::Step step =
      beautifygeometry::shadowStep(src.size(), blur, offset, dpr);
    const int dx = scaledPx(offset.x(), dpr);
    const int dy = scaledPx(offset.y(), dpr);
    const int w = step.canvas.width();
    const int h = step.canvas.height();
    const QImage plain = physical(src);

    // Alpha mask of the original at the shadow position
    QImage mask(w, h, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(step.origin.x() + dx, step.origin.y() + dy, plain);
    }
    QVector<float> alpha(w * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            alpha[y * w + x] = qAlpha(line[x]) / 255.0f;
        }
    }
    for (int i = 0; i < kShadowBlurPasses; ++i) {
        boxBlur(alpha, w, h, radius, true);
        boxBlur(alpha, w, h, radius, false);
    }

    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    const float colorAlpha = color.alphaF();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float a = std::clamp(alpha[y * w + x] * colorAlpha, 0.f, 1.f);
            line[x] = qPremultiply(qRgba(color.red(),
                                         color.green(),
                                         color.blue(),
                                         int(a * 255.f + 0.5f)));
        }
    }
    return out;
}

// src on a background canvas of its padding/aspect step. `shot` feeds the
// backgrounds made from the shot's pixels.
QImage composeOnBackground(const QImage& src,
                           const EffectSettings& settings,
                           const QImage& shot)
{
    const qreal dpr = src.devicePixelRatio();
    const beautifygeometry::Step step = beautifygeometry::backgroundStep(
      src.size(), settings.paddingPx, settings.aspect, dpr);
    if (src.isNull() || (step.canvas == src.size() &&
                         settings.backgroundMode == BackgroundMode::None)) {
        return src;
    }
    QImage out = backgroundfx::renderBackground(step.canvas, settings, shot);
    {
        QPainter p(&out);
        p.drawImage(step.origin, physical(src));
    }
    out.setDevicePixelRatio(dpr);
    return out;
}

// Glass path: bezel around the (bordered, rounded) shot, shadow under the
// bezel, then background, glass, shot
QImage composeWithGlass(const QImage& shotImage,
                        const EffectSettings& settings,
                        const QImage& source)
{
    using namespace beautifygeometry;
    const qreal dpr = shotImage.devicePixelRatio();
    const Step glass = glassStep(shotImage.size(), settings.glassPx, dpr);
    const int g = glass.origin.x();
    const qreal innerRadius =
      settings.cornerRadius > 0 ? settings.cornerRadius * dpr : 0.0;
    const qreal outerRadius = innerRadius + g;

    Step shadow{ glass.canvas, QPoint(0, 0) };
    QImage shadowImage;
    if (settings.shadow) {
        // The bezel's shape casts the shadow
        QImage plate(glass.canvas, QImage::Format_ARGB32_Premultiplied);
        plate.fill(Qt::transparent);
        {
            QPainter p(&plate);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            p.drawRoundedRect(
              QRectF(QPointF(0, 0), QSizeF(glass.canvas)), outerRadius, outerRadius);
        }
        plate.setDevicePixelRatio(dpr);
        shadow = shadowStep(
          glass.canvas, settings.shadowBlur, settings.shadowOffset, dpr);
        shadowImage = shadowLayer(
          plate, settings.shadowBlur, settings.shadowOffset, settings.shadowColor);
    }
    const Step bg =
      backgroundStep(shadow.canvas, settings.paddingPx, settings.aspect, dpr);
    QImage out = backgroundfx::renderBackground(bg.canvas, settings, source);
    // The glass refracts the background alone, not the shadow
    const QImage behind = out;
    const QRect bezel(bg.origin + shadow.origin, glass.canvas);
    const QRect shotRect(bezel.topLeft() + glass.origin, shotImage.size());
    {
        QPainter p(&out);
        if (!shadowImage.isNull()) {
            p.drawImage(bg.origin, shadowImage);
        }
    }
    glassframe::drawBezel(
      out, behind, bezel, outerRadius, shotRect, innerRadius, dpr);
    {
        QPainter p(&out);
        p.drawImage(shotRect.topLeft(), physical(shotImage));
    }
    out.setDevicePixelRatio(dpr);
    return out;
}

} // namespace

namespace imageeffects {

QImage dropShadow(const QImage& src,
                  int blur,
                  const QPoint& offset,
                  const QColor& color)
{
    if (src.isNull()) {
        return src;
    }
    const qreal dpr = src.devicePixelRatio();
    const beautifygeometry::Step step =
      beautifygeometry::shadowStep(src.size(), blur, offset, dpr);
    QImage out = shadowLayer(src, blur, offset, color);
    {
        QPainter p(&out);
        p.drawImage(step.origin, physical(src));
    }
    out.setDevicePixelRatio(dpr);
    return out;
}

QImage addBackground(const QImage& src,
                     int padding,
                     BackgroundMode mode,
                     const QColor& color1,
                     const QColor& color2,
                     int aspect)
{
    EffectSettings settings;
    settings.paddingPx = padding;
    settings.backgroundMode = mode;
    settings.backgroundColor1 = color1;
    settings.backgroundColor2 = color2;
    settings.aspect = aspect;
    return composeOnBackground(src, settings, src);
}

QImage autoBalance(const QImage& src)
{
    if (src.isNull()) {
        return src;
    }
    const QImage plain = physical(src).convertToFormat(
      QImage::Format_ARGB32);
    // Trim amount for one axis: the wider margin shrinks to the narrower one
    auto trims = [&](bool rows) {
        const int size = rows ? plain.height() : plain.width();
        const int cap = std::max(0, int(size * kMaxTrimFraction));
        const int a = uniformMargin(plain, rows, true, cap);
        const int b = uniformMargin(plain, rows, false, cap);
        const int keep = std::min(a, b);
        return std::pair<int, int>{ a - keep, b - keep };
    };
    const auto [top, bottom] = trims(true);
    const auto [left, right] = trims(false);
    if (top + bottom + left + right == 0) {
        return src;
    }
    QImage out = src.copy(
      left, top, src.width() - left - right, src.height() - top - bottom);
    out.setDevicePixelRatio(src.devicePixelRatio());
    return out;
}

QImage applyEffects(const QImage& src, const EffectSettings& settings)
{
    // The shot's own pixels (before border) feed From screenshot and auto
    // colors
    const QImage source = settings.autoBalance ? autoBalance(src) : src;
    QImage out = source;
    if (settings.borderPx > 0) {
        out = addBorder(out, settings.borderPx, settings.borderColor);
    }
    if (settings.cornerRadius > 0) {
        out = roundCorners(out, settings.cornerRadius);
    }
    if (glassActive(settings, out.devicePixelRatio()) && !out.isNull()) {
        return composeWithGlass(out, settings, source);
    }
    if (settings.shadow) {
        out = dropShadow(
          out, settings.shadowBlur, settings.shadowOffset, settings.shadowColor);
    }
    return composeOnBackground(out, settings, source);
}

bool backgroundUsesShot(const EffectSettings& settings)
{
    return settings.backgroundMode == BackgroundMode::FromScreenshot ||
           (settings.backgroundMode == BackgroundMode::Gradient &&
            settings.autoColors);
}

EffectSettings basicEffects(const EffectSettings& settings)
{
    EffectSettings basic;
    basic.borderPx = settings.borderPx;
    basic.borderColor = settings.borderColor;
    basic.cornerRadius = settings.cornerRadius;
    basic.shadow = settings.shadow;
    basic.shadowBlur = settings.shadowBlur;
    basic.shadowOffset = settings.shadowOffset;
    basic.shadowColor = settings.shadowColor;
    return basic;
}

bool needsAlpha(const EffectSettings& settings)
{
    if (settings.backgroundMode != BackgroundMode::None) {
        return false;
    }
    // Glass over no background is translucent
    return settings.cornerRadius > 0 || settings.shadow ||
           settings.paddingPx > 0 || settings.aspect > 0 || settings.glass;
}

} // namespace imageeffects
