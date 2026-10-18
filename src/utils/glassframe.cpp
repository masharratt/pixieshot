// SPDX-License-Identifier: GPL-3.0-or-later

#include "glassframe.h"
#include "backgroundfx.h"

#include <algorithm>
#include <cmath>

namespace {

// Look of the glass (macOS Liquid Glass: vibrant, lit from above, lifted)
constexpr float kTintTop = 0.20f;       // white sheen at the top of the pane
constexpr float kTintBottom = 0.04f;    // ... fading to this at the bottom
constexpr float kVibrancy = 1.6f;       // saturation boost of what is behind
constexpr float kLift = 1.06f;          // brightness lift of what is behind
constexpr float kRefractionOuter = 0.75f; // max displacement at the outer edge
constexpr float kRefractionInner = 0.35f; // ... and at the inner edge
constexpr float kBlur = 0.9f;           // blur radius per pass, share of bezel
constexpr int kBlurPasses = 3;
constexpr float kRimAlpha = 0.75f;      // specular highlight strength
constexpr float kRimFloor = 0.35f;      // highlight left at the bottom-right
constexpr float kRimWidth = 2.5f;       // logical px
constexpr float kGlowAlpha = 0.14f;     // soft light inside the outer edge
constexpr float kGlowWidth = 0.35f;     // share of bezel
constexpr float kInnerShadowAlpha = 0.20f; // soft shade where the shot sits
constexpr float kInnerShadowWidth = 0.35f; // share of bezel
constexpr float kHairlineAlpha = 0.22f; // dark 1 px line against the shot
constexpr float kShadowAlpha = 0.24f;   // drop shadow right under the edge
constexpr float kShadowSpread = 0.45f;  // how far it fades out, share of bezel
constexpr float kShadowDrop = 0.3f;     // shifted down, share of the spread
constexpr float kNoise = 2.0f;          // +- levels of static noise

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

// Static noise in -1..1 for a pixel, keyed to its place on the pane so it
// moves with the pane and never shimmers between frames
float noiseAt(int x, int y)
{
    quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0xffff) / 32767.5f - 1.f;
}

// Vibrancy: what shows through the glass gets richer and a little brighter
void vibrance(QImage& img)
{
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const Premul c = unpack(line[x]);
            const float l = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            auto boost = [&](float v) { return (l + (v - l) * kVibrancy) * kLift; };
            line[x] = pack({ boost(c.r), boost(c.g), boost(c.b), c.a });
        }
    }
}

struct DropShadow
{
    float spread; // fades to nothing this far from the (shifted) bezel
    float drop;   // shifted down by this much: light comes from above
};

DropShadow dropShadowOf(int thickness)
{
    const float spread = std::max(2.f, kShadowSpread * thickness);
    return { spread, kShadowDrop * spread };
}

int thicknessOf(const QRect& bezel, const QRect& shot)
{
    return std::min(shot.left() - bezel.left(), shot.top() - bezel.top());
}

} // namespace

namespace glassframe {

int outset(const QRect& bezel, const QRect& shot, qreal dpr)
{
    Q_UNUSED(dpr);
    const int thickness = thicknessOf(bezel, shot);
    if (thickness <= 0) {
        return 0;
    }
    const DropShadow s = dropShadowOf(thickness);
    return int(std::ceil(s.spread + s.drop)) + 1;
}

void drawBezel(QImage& canvas,
               const QImage& background,
               const QRect& bezel,
               qreal outerRadius,
               const QRect& shot,
               qreal innerRadius,
               qreal dpr)
{
    const int thickness = thicknessOf(bezel, shot);
    if (bezel.intersected(canvas.rect()).isEmpty() || thickness <= 0) {
        return;
    }
    // The bezel plus its drop shadow
    const int reach = outset(bezel, shot, dpr);
    const QRect area =
      bezel.adjusted(-reach, -reach, reach, reach).intersected(canvas.rect());
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
    vibrance(blurred);
    // All float, so at full resolution the arithmetic is unchanged
    const float sx = float(blurred.width()) / float(region.width());
    const float sy = float(blurred.height()) / float(region.height());
    const float regionX = float(region.x());
    const float regionY = float(region.y());

    const Shape outer = shapeOf(bezel, outerRadius);
    const Shape inner = shapeOf(shot, innerRadius);
    const DropShadow drop = dropShadowOf(thickness);
    const Shape shadowShape{
      outer.cx, outer.cy + drop.drop, outer.hx, outer.hy, outer.r
    };
    const float rimWidth = std::max(1.f, kRimWidth * float(dpr));
    const float hairline = std::max(1.f, float(dpr));
    const float glowWidth = std::max(1.f, kGlowWidth * thickness);
    const float innerShadowWidth = std::max(1.f, kInnerShadowWidth * thickness);
    const float top = float(bezel.top());
    const float height = std::max(1.f, float(bezel.height()));
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
            // Drop shadow under the pane: full at its (shifted) edge,
            // fading out over the spread
            float shadow = 0;
            if (dOut < 0.5f) {
                float sxn = 0;
                float syn = 0;
                const float d = roundedRectDistance(shadowShape, px, py, &sxn, &syn);
                const float s = std::clamp(
                  (drop.spread - d) / (1.3f * drop.spread), 0.f, 1.f);
                shadow = kShadowAlpha * s * s;
            }
            if (dOut <= -0.5f) {
                // outside the bezel
                if (shadow > 0) {
                    line[x] = pack(mix(unpack(line[x]), black, shadow));
                }
                continue;
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
            // Lensing: bends harder at the outer edge, as a thick rim does
            const float strength =
              (u < 0 ? kRefractionOuter : kRefractionInner) * thickness;
            const float shift = strength * u * std::abs(u);
            const float bxp = (px + nx * shift - regionX) * sx - 0.5f;
            const float byp = (py + ny * shift - regionY) * sy - 0.5f;
            // Sheen: the top of the pane catches the most light
            const float v = std::clamp((py - top) / height, 0.f, 1.f);
            const float sheen =
              kTintBottom + (kTintTop - kTintBottom) * (1.f - v) * (1.f - v);
            Premul c = mix(sample(blurred, bxp, byp), white, sheen);

            // Specular rim on both edges, bright where the edge faces the light
            const float facing = std::max(0.f, nx * lx + ny * ly);
            const float light = kRimFloor + (1.f - kRimFloor) * facing;
            const float rimOuter = std::clamp(1.f - dOut / rimWidth, 0.f, 1.f);
            const float rimInner =
              std::clamp(1.f - (dIn - hairline) / rimWidth, 0.f, 1.f) *
              (dIn >= hairline ? 1.f : 0.f);
            const float spec = kRimAlpha * light * std::max(rimOuter, rimInner);
            // Soft light just inside the outer edge, for volume
            const float g = std::clamp(1.f - dOut / glowWidth, 0.f, 1.f);
            const float glow = kGlowAlpha * (0.4f + 0.6f * facing) * g * g;
            c = mix(c, white, std::max(spec, glow));
            // The shot sits in the glass: soft shade along its edge
            const float t = std::clamp((dIn - hairline) / innerShadowWidth, 0.f, 1.f);
            if (dIn >= hairline && t < 1.f) {
                c = mix(c, black, kInnerShadowAlpha * (1.f - t) * (1.f - t));
            }
            if (dIn < hairline) {
                c = mix(c, black, kHairlineAlpha);
            }
            // Faint static noise kills banding in the smooth blur
            const float n =
              kNoise * noiseAt(x - bezel.left(), y - bezel.top()) * (c.a / 255.f);
            c = { c.r + n, c.g + n, c.b + n, c.a };

            const float coverage = std::clamp(dOut + 0.5f, 0.f, 1.f);
            if (coverage >= 1.f) {
                line[x] = pack(c);
            } else {
                const Premul under = mix(unpack(line[x]), black, shadow);
                line[x] = pack(mix(under, c, coverage));
            }
        }
    }
}

} // namespace glassframe
