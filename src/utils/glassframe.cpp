// SPDX-License-Identifier: GPL-3.0-or-later

#include "glassframe.h"
#include "backgroundfx.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

// Look of the glass (macOS Liquid Glass: vibrant, lit from the top left,
// lifted off the page). Tuned bold on purpose: it must read as glass at a
// glance over a flat colour, a smooth gradient, dark or light.
constexpr float kVibrancy = 1.5f;       // saturation boost of what is behind
// The body is always a clear step away from the backdrop: lighter over a
// dark backdrop, darker over a light one
constexpr float kLiftDark = 1.18f;      // brightness lift over dark backdrops
constexpr float kWhiteDark = 0.20f;     // white added over near-black ...
constexpr float kWhiteFade = 0.45f;     // ... fading out by this luminance
constexpr float kShadeLight = 0.12f;    // black tint over light backdrops
constexpr float kLightFrom = 0.56f;     // mean luminance where the tint
constexpr float kLightTo = 0.62f;       // ... turns from light to dark
constexpr float kSheen = 0.22f;         // white sheen at the very top
constexpr float kSheenOnLight = 0.3f;   // ... share of it over light backdrops
// The outer bevel: a convex squircle bending what is behind and the shot's
// own edge into view. The flat glass next to the shot is not bent.
constexpr float kBevel = 0.5f;          // bevel width, share of bezel
constexpr float kRefraction = 0.9f;     // displacement at the outer edge
constexpr float kContentMix = 0.55f;    // how much of the shot shows there
constexpr float kContentDepth = 0.6f;   // how far into the shot it reaches
constexpr float kBlur = 0.4f;           // blur radius per pass, share of bezel
constexpr int kBlurPasses = 2;
constexpr float kRimAlpha = 0.85f;      // specular rim strength (screen)
constexpr float kRimFloor = 0.25f;      // rim left where the edge is side-on
constexpr float kRimOpposite = 0.45f;   // weaker rim at the far (exit) side
constexpr float kRimWidth = 2.0f;       // logical px
constexpr float kRimWhite = 0.7f;       // rim colour: backdrop mixed to white
constexpr float kGlowAlpha = 0.16f;     // soft light inside the outer edge
constexpr float kGlowWidth = 0.3f;      // share of bezel
constexpr float kLipAlpha = 0.40f;      // bright lip just outside the shot
constexpr float kInnerShadowAlpha = 0.30f; // soft shade where the shot sits
constexpr float kInnerShadowWidth = 0.35f; // share of bezel
constexpr float kHairlineAlpha = 0.25f; // dark 1 px line against the shot
constexpr float kNoise = 2.0f;          // +- levels of static noise

struct DropShadow
{
    float spread; // fades to nothing this far from the (shifted) bezel
    float drop;   // shifted down by this much: light comes from above
    float alpha;  // darkness right at the edge
};

// A tight dark contact shadow plus a broad soft one
DropShadow contactShadowOf(int thickness)
{
    return { std::max(1.5f, 0.15f * thickness), 0.1f * thickness, 0.35f };
}

DropShadow castShadowOf(int thickness)
{
    return { std::max(2.f, 0.6f * thickness), 0.3f * thickness, 0.18f };
}

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

Premul scaled(const Premul& c, float k)
{
    return { c.r * k, c.g * k, c.b * k, c.a };
}

// `top` composited over `under` (both premultiplied)
Premul over(const Premul& top, const Premul& under)
{
    const float k = 1.f - top.a / 255.f;
    return { top.r + under.r * k,
             top.g + under.g * k,
             top.b + under.b * k,
             top.a + under.a * k };
}

// Screen blend of an opaque light colour onto c, by `amount`
Premul screen(const Premul& c, const Premul& light, float amount)
{
    auto ch = [&](float v, float s) { return v + amount * s * (1.f - v / 255.f); };
    return { ch(c.r, light.r), ch(c.g, light.g), ch(c.b, light.b), ch(c.a, 255.f) };
}

float lumaOf(const Premul& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
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

// Vibrancy: what shows through the glass gets richer. Returns the mean
// luminance (0..1) of what is behind, which picks the tint.
float vibrance(QImage& img)
{
    double sum = 0;
    double weight = 0;
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const Premul c = unpack(line[x]);
            const float l = lumaOf(c);
            sum += l;
            weight += c.a;
            auto boost = [&](float v) { return l + (v - l) * kVibrancy; };
            line[x] = pack({ boost(c.r), boost(c.g), boost(c.b), c.a });
        }
    }
    return weight > 0 ? float(sum / weight) : 0.f;
}

float smoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Darkness a drop shadow adds at a pixel `d` px outside its shifted shape
float shadowAt(const DropShadow& s, float d)
{
    const float t = std::clamp((s.spread - d) / (1.3f * s.spread), 0.f, 1.f);
    return s.alpha * t * t;
}

int thicknessOf(const QRect& bezel, const QRect& shot)
{
    return std::min(shot.left() - bezel.left(), shot.top() - bezel.top());
}

// The shot shrunk and softened, so the bevel bends a gentle copy of it.
// The animated export passes the same shot every frame: keep the last one
// (per thread, as frames may render in parallel).
// Only a band `band` content pixels wide along the edges is ever sampled, so
// only that band is filled; the rest stays transparent.
QImage softContent(const QImage& content, int factor, int band)
{
    struct Cached
    {
        qint64 key = 0;
        QSize size;
        int factor = 0;
        int band = 0;
        QImage soft;
    };
    thread_local Cached cached;
    if (cached.key == content.cacheKey() && cached.size == content.size() &&
        cached.factor == factor && cached.band == band) {
        return cached.soft;
    }
    auto shrink = [factor](const QImage& img) {
        QImage small = img.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                         .scaled(std::max(1, img.width() / factor),
                                 std::max(1, img.height() / factor),
                                 Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
        backgroundfx::boxBlur(small, 1, 2);
        return small;
    };
    const int w = content.width();
    const int h = content.height();
    QImage soft;
    if (4 * band >= std::min(w, h)) {
        soft = shrink(content);
    } else {
        // A large shot: shrink just the four edge strips
        const int sw = std::max(1, w / factor);
        const int shh = std::max(1, h / factor);
        soft = QImage(sw, shh, QImage::Format_ARGB32_Premultiplied);
        soft.fill(Qt::transparent);
        QPainter p(&soft);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        for (const QRect& strip : { QRect(0, 0, w, band),
                                    QRect(0, h - band, w, band),
                                    QRect(0, 0, band, h),
                                    QRect(w - band, 0, band, h) }) {
            const QRect to(QPoint(strip.left() * sw / w, strip.top() * shh / h),
                           QPoint((strip.right() + 1) * sw / w - 1,
                                  (strip.bottom() + 1) * shh / h - 1));
            p.drawImage(to, shrink(content.copy(strip)));
        }
    }
    cached = { content.cacheKey(), content.size(), factor, band, soft };
    return soft;
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
    const DropShadow tight = contactShadowOf(thickness);
    const DropShadow broad = castShadowOf(thickness);
    return int(std::ceil(
             std::max(tight.spread + tight.drop, broad.spread + broad.drop))) +
           1;
}

void drawBezel(QImage& canvas,
               const QImage& background,
               const QRect& bezel,
               qreal outerRadius,
               const QRect& shot,
               qreal innerRadius,
               qreal dpr,
               const QImage& content)
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

    // The background around the bezel, shrunk so the blur stays cheap
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
    // 0 over a dark backdrop (the glass lifts), 1 over a light one (it
    // shades); decided once for the whole pane so it never goes patchy
    const float lightBackdrop = smoothstep(kLightFrom, kLightTo, vibrance(blurred));
    // All float, so at full resolution the arithmetic is unchanged
    const float sx = float(blurred.width()) / float(region.width());
    const float sy = float(blurred.height()) / float(region.height());
    const float regionX = float(region.x());
    const float regionY = float(region.y());

    // The shot's own pixels, for the bevel to bend into view
    const int contentFactor = std::max(2, factor / 2);
    const QImage soft =
      content.isNull()
        ? QImage()
        : softContent(content,
                      contentFactor,
                      int(std::ceil((kContentDepth * thickness + 2.f) *
                                    float(content.width()) /
                                    std::max(1, shot.width()))) +
                        4 * contentFactor);
    const float csx = soft.isNull() ? 0.f : float(soft.width()) / shot.width();
    const float csy = soft.isNull() ? 0.f : float(soft.height()) / shot.height();

    const Shape outer = shapeOf(bezel, outerRadius);
    const Shape inner = shapeOf(shot, innerRadius);
    const DropShadow tight = contactShadowOf(thickness);
    const DropShadow broad = castShadowOf(thickness);
    const Shape tightShape{
      outer.cx, outer.cy + tight.drop, outer.hx, outer.hy, outer.r
    };
    const Shape broadShape{
      outer.cx, outer.cy + broad.drop, outer.hx, outer.hy, outer.r
    };
    const float rimWidth = std::max(1.f, kRimWidth * float(dpr));
    const float hairline = std::max(1.f, float(dpr));
    const float glowWidth = std::max(1.f, kGlowWidth * thickness);
    const float innerShadowWidth = std::max(1.f, kInnerShadowWidth * thickness);
    const float bevelWidth = std::max(1.f, kBevel * thickness);
    const float top = float(bezel.top());
    const float height = std::max(1.f, float(bezel.height()));
    const float sheenMax = kSheen * (1.f - (1.f - kSheenOnLight) * lightBackdrop);
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
            // Drop shadows under the pane: a tight contact edge and a broad
            // soft cast shadow, each full at its (shifted) edge
            float shadow = 0;
            if (dOut < 0.5f) {
                float ignore = 0;
                const float dt =
                  roundedRectDistance(tightShape, px, py, &ignore, &ignore);
                const float db =
                  roundedRectDistance(broadShape, px, py, &ignore, &ignore);
                shadow = 1.f - (1.f - shadowAt(tight, dt)) * (1.f - shadowAt(broad, db));
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
            // Across the bevel t runs 0 at the outer edge to 1 where the
            // flat glass begins; h is the squircle height of the surface
            const float t = std::clamp(dOut / bevelWidth, 0.f, 1.f);
            const float k = 1.f - t;
            const float h = std::sqrt(std::sqrt(1.f - k * k * k * k));
            // Lensing: the steep outer bevel bends what is behind inward
            const float shift = kRefraction * thickness * (1.f - h);
            const float bxp = (px - nx * shift - regionX) * sx - 0.5f;
            const float byp = (py - ny * shift - regionY) * sy - 0.5f;
            Premul b = sample(blurred, bxp, byp);
            // ... and brings the shot's edge into view: what lies a little
            // way inside the shot, mirrored outward, as in a thick lens.
            // Strongest mid-bevel: the outermost edge belongs to the rim,
            // so the lit and the far side stay told apart.
            if (!soft.isNull() && k > 0) {
                const float depth = dIn + kContentDepth * thickness * k;
                const float qx = (px - inx * depth - shot.x()) * csx - 0.5f;
                const float qy = (py - iny * depth - shot.y()) * csy - 0.5f;
                b = mix(b, over(sample(soft, qx, qy), b), kContentMix * 4.f * t * k);
            }
            // A clear step from the backdrop: lifted (and over near-black,
            // whitened) when it is dark, shaded when it is light
            const float l = lumaOf(b) / std::max(1.f, b.a);
            Premul lifted = scaled(b, kLiftDark);
            lifted = mix(lifted,
                         white,
                         kWhiteDark * std::clamp(1.f - l / kWhiteFade, 0.f, 1.f));
            const Premul shaded = mix(b, black, kShadeLight);
            Premul c = mix(lifted, shaded, lightBackdrop);
            // Sheen: the top of the pane catches the most light
            const float v = 1.f - std::clamp((py - top) / height, 0.f, 1.f);
            c = mix(c, white, sheenMax * v * v * v);

            // Specular rim along the outer edge, screened on in a light
            // tint of what is behind: bright where the edge faces the
            // light, a weaker lobe at the opposite corner where it leaves
            const float facing = nx * lx + ny * ly;
            const float lobe = std::max(std::max(0.f, facing),
                                        kRimOpposite * std::max(0.f, -facing));
            const float light = kRimFloor + (1.f - kRimFloor) * lobe;
            const float rim = std::clamp(1.f - (dOut - 0.5f) / rimWidth, 0.f, 1.f);
            const Premul rimColor = mix(scaled(b, 255.f / std::max(1.f, b.a)),
                                        white,
                                        kRimWhite);
            c = screen(c, rimColor, kRimAlpha * light * rim);
            // Soft light just inside the outer edge, for volume
            const float g = std::clamp(1.f - dOut / glowWidth, 0.f, 1.f);
            c = mix(c, white, kGlowAlpha * (0.4f + 0.6f * std::max(0.f, facing)) * g * g);
            // The shot sits in the glass: dark hairline against it, then a
            // thin lit lip (brightest where it faces the light), then a
            // soft shade fading out across the flat glass
            if (dIn < hairline) {
                c = mix(c, black, kHairlineAlpha);
            } else if (dIn < 2.f * hairline) {
                const float lip = std::max(0.f, -(inx * lx + iny * ly));
                c = mix(c, white, kLipAlpha * (0.5f + 0.5f * lip));
            } else {
                const float s =
                  std::clamp((dIn - 2.f * hairline) / innerShadowWidth, 0.f, 1.f);
                c = mix(c, black, kInnerShadowAlpha * (1.f - s) * (1.f - s));
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
