// SPDX-License-Identifier: GPL-3.0-or-later

#include "glassframe.h"
#include "backgroundfx.h"

#include <algorithm>
#include <cmath>

namespace {

// Look of the glass
constexpr float kTint = 0.18f;          // white mixed into the glass
constexpr float kRefraction = 0.45f;    // max displacement, share of bezel
constexpr float kBlur = 0.9f;           // blur radius per pass, share of bezel
constexpr int kBlurPasses = 3;
constexpr float kRimAlpha = 0.65f;      // specular highlight strength
constexpr float kRimFloor = 0.10f;      // highlight left at the bottom-right
constexpr float kRimWidth = 2.0f;       // logical px
constexpr float kHairlineAlpha = 0.22f; // dark 1 px line against the shot

struct Shape
{
    float cx, cy; // center
    float hx, hy; // half size
    float r;      // corner radius
};

// Signed distance to a rounded rectangle (negative inside) and the outward
// unit normal of the nearest boundary
float roundedRectDistance(const Shape& s, float px, float py, float* nx, float* ny)
{
    const float dx = px - s.cx;
    const float dy = py - s.cy;
    const float sx = dx < 0 ? -1.f : 1.f;
    const float sy = dy < 0 ? -1.f : 1.f;
    const float qx = std::abs(dx) - s.hx + s.r;
    const float qy = std::abs(dy) - s.hy + s.r;
    if (qx > 0 && qy > 0) {
        const float len = std::hypot(qx, qy);
        *nx = sx * qx / len;
        *ny = sy * qy / len;
        return len - s.r;
    }
    if (qx > qy) {
        *nx = sx;
        *ny = 0;
        return qx - s.r;
    }
    *nx = 0;
    *ny = sy;
    return qy - s.r;
}

Shape shapeOf(const QRect& rect, qreal radius)
{
    const float hx = rect.width() / 2.f;
    const float hy = rect.height() / 2.f;
    return { rect.x() + hx,
             rect.y() + hy,
             hx,
             hy,
             std::clamp(float(radius), 0.f, std::min(hx, hy)) };
}

struct Premul
{
    float r, g, b, a;
};

Premul unpack(QRgb c)
{
    return { float(qRed(c)), float(qGreen(c)), float(qBlue(c)), float(qAlpha(c)) };
}

Premul mix(const Premul& x, const Premul& y, float t)
{
    return { x.r + (y.r - x.r) * t,
             x.g + (y.g - x.g) * t,
             x.b + (y.b - x.b) * t,
             x.a + (y.a - x.a) * t };
}

// Bilinear sample of a premultiplied image, clamped at its edges
Premul sample(const QImage& img, float x, float y)
{
    x = std::clamp(x, 0.f, float(img.width() - 1));
    y = std::clamp(y, 0.f, float(img.height() - 1));
    const int x0 = int(x);
    const int y0 = int(y);
    const int x1 = std::min(x0 + 1, img.width() - 1);
    const int y1 = std::min(y0 + 1, img.height() - 1);
    const float fx = x - x0;
    const float fy = y - y0;
    const QRgb* l0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
    const QRgb* l1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
    const Premul top = mix(unpack(l0[x0]), unpack(l0[x1]), fx);
    const Premul bottom = mix(unpack(l1[x0]), unpack(l1[x1]), fx);
    return mix(top, bottom, fy);
}

QRgb pack(const Premul& c)
{
    const int a = std::clamp(int(c.a + 0.5f), 0, 255);
    auto ch = [a](float v) { return std::clamp(int(v + 0.5f), 0, a); };
    return qRgba(ch(c.r), ch(c.g), ch(c.b), a);
}

} // namespace

namespace glassframe {

void drawBezel(QImage& canvas,
               const QImage& background,
               const QRect& bezel,
               qreal outerRadius,
               const QRect& shot,
               qreal innerRadius,
               qreal dpr)
{
    const QRect area = bezel.intersected(canvas.rect());
    const int thickness = std::min(shot.left() - bezel.left(), shot.top() - bezel.top());
    if (area.isEmpty() || thickness <= 0) {
        return;
    }
    if (canvas.format() != QImage::Format_RGB32 &&
        canvas.format() != QImage::Format_ARGB32_Premultiplied) {
        canvas.convertTo(QImage::Format_ARGB32_Premultiplied);
    }

    // The background around the bezel, shrunk so the heavy blur stays
    // cheap, blurred harder than anything else on the canvas
    const int factor = std::clamp(thickness / 4, 1, 8);
    const int margin = thickness + 2 * factor;
    // `background` covers the canvas at its own resolution (the animated
    // export passes its smaller work image); bs* map canvas to background
    const double bsx = double(background.width()) / canvas.width();
    const double bsy = double(background.height()) / canvas.height();
    const QRect wanted = bezel.adjusted(-margin, -margin, margin, margin)
                           .intersected(canvas.rect());
    const QRect source =
      QRect(QPoint(int(std::floor(wanted.left() * bsx)),
                   int(std::floor(wanted.top() * bsy))),
            QPoint(int(std::ceil((wanted.right() + 1) * bsx)) - 1,
                   int(std::ceil((wanted.bottom() + 1) * bsy)) - 1))
        .intersected(background.rect());
    if (source.isEmpty()) {
        return;
    }
    // The canvas area `source` covers (the same rect at full resolution)
    const QRectF region(source.x() / bsx,
                        source.y() / bsy,
                        source.width() / bsx,
                        source.height() / bsy);
    QImage blurred = background.copy(source)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied)
                       .scaled(std::max(1, int(region.width()) / factor),
                               std::max(1, int(region.height()) / factor),
                               Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    backgroundfx::boxBlur(
      blurred, std::max(1, int(std::lround(kBlur * thickness / factor))), kBlurPasses);
    // All float, so at full resolution the arithmetic is unchanged
    const float sx = float(blurred.width()) / float(region.width());
    const float sy = float(blurred.height()) / float(region.height());
    const float regionX = float(region.x());
    const float regionY = float(region.y());

    const Shape outer = shapeOf(bezel, outerRadius);
    const Shape inner = shapeOf(shot, innerRadius);
    const float strength = kRefraction * thickness;
    const float rimWidth = std::max(1.f, kRimWidth * float(dpr));
    const float hairline = std::max(1.f, float(dpr));
    // Light comes from the top left
    const float lx = -0.70710678f;
    const float ly = -0.70710678f;
    const Premul white{ 255, 255, 255, 255 };
    const Premul black{ 0, 0, 0, 255 };

    // Rows away from the inner corners only need their left and right
    // strips; the middle lies well inside the shot
    const int straightTop = shot.top() + int(std::ceil(inner.r)) + 2;
    const int straightBottom = shot.bottom() - int(std::ceil(inner.r)) - 2;

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(canvas.scanLine(y));
        // A shot 4px wide or less has no interior to skip (skipTo < skipFrom
        // would loop forever)
        const bool straight =
          y >= straightTop && y <= straightBottom && shot.width() > 4;
        const int skipFrom = straight ? shot.left() + 2 : area.right() + 1;
        const int skipTo = straight ? shot.right() - 2 : area.right();
        for (int x = area.left(); x <= area.right(); ++x) {
            if (x == skipFrom) {
                x = skipTo;
                continue;
            }
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            float nx = 0;
            float ny = 0;
            const float dOut = -roundedRectDistance(outer, px, py, &nx, &ny);
            if (dOut <= -0.5f) {
                continue; // outside the bezel
            }
            float inx = 0;
            float iny = 0;
            const float dIn = roundedRectDistance(inner, px, py, &inx, &iny);
            if (dIn < -1.5f) {
                continue; // under the shot, which is drawn on top
            }
            // u runs -1 at the outer edge, 0 on the centre line, 1 at the
            // inner edge. Sample toward the centre line, strongest at edges.
            const float ring = std::max(0.f, dIn);
            const float u = (dOut - ring) / std::max(1.f, dOut + ring);
            const float shift = strength * u * std::abs(u);
            const float bxp = (px + nx * shift - regionX) * sx - 0.5f;
            const float byp = (py + ny * shift - regionY) * sy - 0.5f;
            Premul c = mix(sample(blurred, bxp, byp), white, kTint);

            // Specular rim on both edges, bright where the edge faces the light
            const float facing = std::max(0.f, nx * lx + ny * ly);
            const float light = kRimFloor + (1.f - kRimFloor) * facing;
            const float rimOuter = std::clamp(1.f - dOut / rimWidth, 0.f, 1.f);
            const float rimInner =
              std::clamp(1.f - (dIn - hairline) / rimWidth, 0.f, 1.f) *
              (dIn >= hairline ? 1.f : 0.f);
            const float spec = kRimAlpha * light * std::max(rimOuter, rimInner);
            c = mix(c, white, spec);
            if (dIn < hairline) {
                c = mix(c, black, kHairlineAlpha);
            }

            const float coverage = std::clamp(dOut + 0.5f, 0.f, 1.f);
            line[x] = coverage >= 1.f ? pack(c) : pack(mix(unpack(line[x]), c, coverage));
        }
    }
}

} // namespace glassframe
