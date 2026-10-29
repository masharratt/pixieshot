// SPDX-License-Identifier: GPL-3.0-or-later

#include "animatedbg.h"
#include "animbgkit.h"
#include "backgroundfx.h"
#include "beautifypalette.h"

#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

using namespace animbgkit;

// Each style renders into `out` (Format_RGB32) for an angle T = 2 pi phase.
// Every time term is sin/cos of a whole multiple of T, so T = 0 and T = 2 pi
// are the same picture.

void renderGradientShift(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    // One full turn per loop, from a seeded starting angle, plus a gentle
    // sway of the colour stops along the axis
    const float angle = rnd.range(0.f, kTau) + T;
    const float sway = 0.22f * std::sin(T + rnd.range(0.f, kTau));
    const float half = 0.5f * std::hypot(float(w), float(h));
    const float c = std::cos(angle) / half;
    const float s = std::sin(angle) / half;
    QVector<float> col(w);
    for (int x = 0; x < w; ++x) {
        col[x] = (x + 0.5f - w / 2.f) * c;
    }
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const float row = (y + 0.5f - h / 2.f) * s + sway;
        for (int x = 0; x < w; ++x) {
            // -1..1 along the axis, eased so the middle is a long blend
            float t = std::clamp((col[x] + row) * 0.5f + 0.5f, 0.f, 1.f);
            t = t * t * (3.f - 2.f * t);
            line[x] = pack(pal.along(t));
        }
    }
}

void renderAurora(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    constexpr int kBands = 3;
    // With more than two stops the bands take the middle colours
    const Rgb colors[kBands] = {
        lighter(pal.multi() ? pal.spread(1, 4) : pal.a, 0.25f),
        lighter(pal.multi() ? pal.spread(2, 4) : pal.b, 0.3f),
        pal.multi() ? lighter(pal.spread(3, 4), 0.2f) : pal.light
    };
    struct Band
    {
        float base, amp1, freq1, amp2, freq2, width, glowFreq;
        int speed1, speed2;
        float ph1, ph2, ph3;
    };
    std::array<Band, kBands> bands{};
    for (int i = 0; i < kBands; ++i) {
        Band& b = bands[i];
        b.base = 0.3f + 0.2f * i + rnd.range(-0.05f, 0.05f);
        b.amp1 = rnd.range(0.06f, 0.11f);
        b.freq1 = rnd.range(0.6f, 1.3f);
        b.amp2 = rnd.range(0.02f, 0.05f);
        b.freq2 = rnd.range(1.6f, 2.6f);
        b.width = rnd.range(0.07f, 0.11f);
        b.glowFreq = rnd.range(0.8f, 1.6f);
        b.speed1 = (i % 2 == 0) ? 1 : -1;
        b.speed2 = (i == 1) ? 2 : -2;
        b.ph1 = rnd.range(0.f, kTau);
        b.ph2 = rnd.range(0.f, kTau);
        b.ph3 = rnd.range(0.f, kTau);
    }
    // Per column: each band's centre line and brightness
    QVector<float> centre(w * kBands);
    QVector<float> glow(w * kBands);
    for (int x = 0; x < w; ++x) {
        const float u = (x + 0.5f) / w;
        for (int i = 0; i < kBands; ++i) {
            const Band& b = bands[i];
            centre[x * kBands + i] =
              b.base + b.amp1 * std::sin(kTau * b.freq1 * u + b.speed1 * T + b.ph1) +
              b.amp2 * std::sin(kTau * b.freq2 * u + b.speed2 * T + b.ph2);
            glow[x * kBands + i] =
              0.5f + 0.4f * std::sin(kTau * b.glowFreq * u - b.speed1 * T + b.ph3);
        }
    }
    const Rgb top = mix(pal.deep, scale(pal.a, 0.45f), 0.5f);
    const Rgb bottom = mix(pal.deep, scale(pal.b, 0.5f), 0.5f);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const float v = (y + 0.5f) / h;
        const Rgb ground = mix(top, bottom, v);
        for (int x = 0; x < w; ++x) {
            Rgb c = ground;
            for (int i = 0; i < kBands; ++i) {
                // Curtains: sharp at the bottom edge, a long fade upward
                const float d = v - centre[x * kBands + i];
                const float width = bands[i].width * (d < 0 ? 2.4f : 0.8f);
                const float q = d / width;
                const float k = std::exp(-q * q) * glow[x * kBands + i];
                c = screen(c, colors[i], k);
            }
            line[x] = pack(c);
        }
    }
}

void renderBlobs(QImage& out, const Palette& pal, float T, Random& rnd, uint32_t seed)
{
    const int w = out.width();
    const int h = out.height();
    const int count = 3 + int(seed % 3);
    Rgb colors[5] = {
        pal.a, pal.b, pal.light, lighter(pal.a, 0.15f), lighter(pal.b, 0.15f)
    };
    if (pal.multi()) {
        for (int j = 0; j < 5; ++j) {
            colors[j] = pal.spread((j * 3) % 5, 5); // stops interleaved
        }
    }
    const float unit = float(std::max(w, h));
    const Rgb ground = mix(pal.deep, mix(pal.a, pal.b, 0.5f), 0.4f);
    constexpr float kGroundWeight = 0.35f;
    // Separable gaussians: weight = ex[x] * ey[y]
    QVector<float> ex(count * w);
    QVector<float> ey(count * h);
    for (int j = 0; j < count; ++j) {
        const float cx0 = rnd.range(0.25f, 0.75f);
        const float cy0 = rnd.range(0.25f, 0.75f);
        const float rx = rnd.range(0.12f, 0.28f);
        const float ry = rnd.range(0.12f, 0.28f);
        const int turns = (j % 3 == 2) ? 2 : ((j % 2 == 0) ? 1 : -1);
        const float ph = rnd.range(0.f, kTau);
        const float sigma = rnd.range(0.26f, 0.4f) * unit;
        const float cx = (cx0 + rx * std::cos(turns * T + ph)) * w;
        const float cy = (cy0 + ry * std::sin(turns * T + ph)) * h;
        for (int x = 0; x < w; ++x) {
            const float d = (x + 0.5f - cx) / sigma;
            ex[j * w + x] = std::exp(-d * d);
        }
        for (int y = 0; y < h; ++y) {
            const float d = (y + 0.5f - cy) / sigma;
            ey[j * h + y] = std::exp(-d * d);
        }
    }
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            Rgb sum = scale(ground, kGroundWeight);
            float total = kGroundWeight;
            for (int j = 0; j < count; ++j) {
                const float k = ex[j * w + x] * ey[j * h + y];
                const Rgb& c = colors[j % 5];
                sum.r += c.r * k;
                sum.g += c.g * k;
                sum.b += c.b * k;
                total += k;
            }
            line[x] = pack(scale(sum, 1.f / total));
        }
    }
}

void renderTwinkle(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    // The still's gradient: top-left first colour to bottom-right second
    const float norm = 1.f / (float(w) * w + float(h) * h);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float t = std::clamp((x * float(w) + y * float(h)) * norm, 0.f, 1.f);
            line[x] = pack(pal.along(t));
        }
    }
    // Sparkles: a soft glow and a faint four-point cross, each fading in
    // and out a whole number of times per loop
    const Rgb star = lighter(pal.light, 0.7f);
    const int count = std::clamp(int(70.f * w * h / (480.f * 270.f)), 12, 140);
    for (int i = 0; i < count; ++i) {
        const float sx = rnd.range(0.f, float(w));
        const float sy = rnd.range(0.f, float(h));
        const float size = rnd.range(0.8f, 2.2f);
        const int times = 1 + int(rnd.next() * 3.f) % 3;
        const float ph = rnd.range(0.f, kTau);
        const float s = std::max(0.f, std::sin(times * T + ph));
        const float strength = 0.9f * s * s * s;
        if (strength < 0.004f) {
            continue;
        }
        const int reach = int(std::ceil(size * 5.f));
        const int x0 = std::max(0, int(sx) - reach);
        const int x1 = std::min(w - 1, int(sx) + reach);
        const int y0 = std::max(0, int(sy) - reach);
        const int y1 = std::min(h - 1, int(sy) + reach);
        for (int y = y0; y <= y1; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            const float dy = (y + 0.5f - sy) / size;
            for (int x = x0; x <= x1; ++x) {
                const float dx = (x + 0.5f - sx) / size;
                const float glowK = std::exp(-(dx * dx + dy * dy));
                const float cross = 0.5f * (std::exp(-std::abs(dx) * 0.5f - dy * dy * 6.f) +
                                            std::exp(-std::abs(dy) * 0.5f - dx * dx * 6.f));
                const float k = strength * std::min(1.f, glowK + cross);
                if (k < 0.002f) {
                    continue;
                }
                const QRgb p = line[x];
                const Rgb base{ qRed(p) / 255.f, qGreen(p) / 255.f, qBlue(p) / 255.f };
                line[x] = pack(screen(base, star, k));
            }
        }
    }
}

// Flow: a bold slanted gradient through all the stops, warped like liquid,
// with soft ribbons of the other colours flowing through it
void renderFlow(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float unit = float(std::max(w, h));
    // The axis runs down to the right, about 28 degrees below horizontal
    const float angle = 0.49f + rnd.range(-0.08f, 0.08f);
    const float ca = std::cos(angle);
    const float sa = std::sin(angle);
    const float span = std::abs(ca) * w + std::abs(sa) * h;
    struct Wave
    {
        float amp, freq, ph;
        int speed;
    };
    const Wave warp[2] = {
        { 0.07f, rnd.range(0.7f, 1.1f), rnd.range(0.f, kTau), 1 },
        { 0.03f, rnd.range(1.6f, 2.4f), rnd.range(0.f, kTau), -2 },
    };
    constexpr int kRibbons = 4;
    struct Ribbon
    {
        float centre, amp, freq, ph, width;
        int speed;
        Rgb color;
    };
    std::array<Ribbon, kRibbons> ribbons{};
    for (int i = 0; i < kRibbons; ++i) {
        Ribbon& r = ribbons[i];
        r.centre = 0.2f + 0.2f * i + rnd.range(-0.04f, 0.04f);
        r.amp = rnd.range(0.04f, 0.08f);
        r.freq = rnd.range(0.6f, 1.2f);
        r.ph = rnd.range(0.f, kTau);
        r.width = rnd.range(0.035f, 0.06f);
        r.speed = (i % 2 == 0) ? 1 : -1;
        // A colour from elsewhere on the axis, so the ribbon reads against
        // the ground it crosses
        float pick = r.centre + 0.45f;
        pick -= std::floor(pick);
        r.color = lighter(pal.along(pick), 0.12f);
    }
    // Everything that moves depends only on the position across the axis
    // (q), so it is tabulated once per frame along q and interpolated
    const float qMin = -0.5f * (std::abs(sa) * w + std::abs(ca) * h) - 1.f;
    const int steps = int(std::ceil(-2.f * qMin)) + 2;
    QVector<float> table(steps * (1 + kRibbons));
    for (int i = 0; i < steps; ++i) {
        const float q = (qMin + float(i)) / unit;
        float wv = 0.f;
        for (const Wave& wave : warp) {
            wv += wave.amp * std::sin(kTau * wave.freq * q + wave.speed * T + wave.ph);
        }
        float* row = table.data() + i * (1 + kRibbons);
        row[0] = wv;
        for (int r = 0; r < kRibbons; ++r) {
            const Ribbon& rb = ribbons[r];
            row[1 + r] = rb.centre +
                         rb.amp * std::sin(kTau * rb.freq * q + rb.speed * T + rb.ph) +
                         0.6f * wv;
        }
    }
    const Rgb white{ 1, 1, 1 };
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const float Y = y + 0.5f - h / 2.f;
        for (int x = 0; x < w; ++x) {
            const float X = x + 0.5f - w / 2.f;
            const float a = (X * ca + Y * sa) / span + 0.5f;
            const float qi = std::clamp(-X * sa + Y * ca - qMin, 0.f, float(steps - 2));
            const int i0 = int(qi);
            const float f = qi - float(i0);
            const float* r0 = table.constData() + i0 * (1 + kRibbons);
            const float* r1 = r0 + (1 + kRibbons);
            const float wv = r0[0] + (r1[0] - r0[0]) * f;
            float t = std::clamp(a + wv, 0.f, 1.f);
            t = t * t * (3.f - 2.f * t);
            Rgb c = pal.along(t);
            for (int r = 0; r < kRibbons; ++r) {
                const float centre = r0[1 + r] + (r1[1 + r] - r0[1 + r]) * f;
                const float d = (a - centre) / ribbons[r].width;
                if (d < -3.f || d > 3.f) {
                    continue;
                }
                c = mix(c, ribbons[r].color, 0.55f * std::exp(-d * d));
                // A glossy edge on the leading side, like a liquid surface
                const float e = (d - 0.7f) / 0.35f;
                c = screen(c, white, 0.18f * std::exp(-e * e));
            }
            line[x] = pack(c);
        }
    }
}

// Silk: fine strands twisting through a band across a deep ground. Where a
// strand faces the viewer it is wide and lit; turned away it is thin and
// in shade, so the band reads as one twisting ribbon of fabric.
void renderSilk(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    constexpr int kStrands = 26;
    const float fh = float(h);
    // Ground: deep shades of the outer stops
    const Rgb top = scale(pal.along(0.1f), 0.22f);
    const Rgb bottom = scale(pal.along(0.9f), 0.28f);
    QVector<Rgb> buf(w * h);
    for (int y = 0; y < h; ++y) {
        const Rgb g = mix(top, bottom, (y + 0.5f) / fh);
        std::fill(buf.begin() + y * w, buf.begin() + (y + 1) * w, g);
    }
    // The band's shape is mostly fixed; only a little of it drifts, slowly,
    // so the strands never slide fast enough to flicker
    const float c0 = rnd.range(0.42f, 0.58f);
    const float shapeAmp = rnd.range(0.12f, 0.18f);
    const float shapeFreq = rnd.range(0.5f, 0.8f);
    const float shapePh = rnd.range(0.f, kTau);
    const float driftFreq = rnd.range(1.0f, 1.5f);
    const float driftPh = rnd.range(0.f, kTau);
    const float fanFreq = rnd.range(0.6f, 0.9f);
    const float fanPh = rnd.range(0.f, kTau);
    const float twistFreq = rnd.range(0.9f, 1.4f);
    const float twistPh = rnd.range(0.f, kTau);
    std::array<float, kStrands> ripplePh{};
    for (float& ph : ripplePh) {
        ph = rnd.range(0.f, kTau);
    }
    struct Seg
    {
        float y, sigma, alpha;
        Rgb color;
    };
    std::array<Seg, kStrands> front{};
    std::array<Seg, kStrands> back{};
    const float minSigma = std::max(1.1f, 0.0045f * fh);
    for (int x = 0; x < w; ++x) {
        const float u = (x + 0.5f) / float(w);
        const float centre = c0 + shapeAmp * std::sin(kTau * shapeFreq * u + shapePh) +
                             0.012f * std::sin(kTau * driftFreq * u + T + driftPh);
        const float spread = 0.34f + 0.12f * std::sin(kTau * fanFreq * u + fanPh) +
                             0.02f * std::sin(kTau * 0.9f * u - T + fanPh);
        int nFront = 0;
        int nBack = 0;
        for (int i = 0; i < kStrands; ++i) {
            const float ti = float(i) / float(kStrands - 1);
            const float y = (centre + (ti - 0.5f) * spread +
                             0.006f * std::sin(kTau * 3.f * u + ripplePh[i])) *
                            fh;
            const float theta = kTau * twistFreq * u + 2.f * T + 0.24f * float(i) + twistPh;
            const float cs = std::cos(theta);
            const float face = std::abs(cs);
            const Rgb base = pal.along(ti);
            Seg seg;
            seg.y = y;
            seg.sigma = minSigma * (0.7f + 0.9f * face);
            // Edge-on (face 0) both sides are the same faint mid shade, so a
            // strand turning over changes smoothly
            const Rgb edge = scale(base, 0.7f);
            if (cs >= 0.f) {
                seg.color = mix(edge, lighter(base, 0.5f), face * face);
                seg.alpha = 0.12f + 0.78f * face;
                front[nFront++] = seg;
            } else {
                seg.color = mix(edge, scale(base, 0.4f), face);
                seg.alpha = 0.12f + 0.38f * face;
                back[nBack++] = seg;
            }
        }
        // Back strands first, the lit front ones over them
        auto paint = [&](const std::array<Seg, kStrands>& segs, int n) {
            for (int k = 0; k < n; ++k) {
                const Seg& sg = segs[k];
                const float reach = 3.f * sg.sigma;
                const int y0 = std::max(0, int(std::floor(sg.y - reach)));
                const int y1 = std::min(h - 1, int(std::ceil(sg.y + reach)));
                const float inv = 1.f / sg.sigma;
                for (int yy = y0; yy <= y1; ++yy) {
                    const float d = (yy + 0.5f - sg.y) * inv;
                    Rgb& px = buf[yy * w + x];
                    px = mix(px, sg.color, sg.alpha * std::exp(-d * d));
                }
            }
        };
        paint(back, nBack);
        paint(front, nFront);
    }
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const Rgb* src = buf.constData() + y * w;
        for (int x = 0; x < w; ++x) {
            line[x] = pack(src[x]);
        }
    }
}

} // namespace

namespace animatedbg {

QSize workSize(QSize size)
{
    const int longSide = std::max(size.width(), size.height());
    if (longSide <= kWorkLongSide || size.isEmpty()) {
        return size;
    }
    const double k = double(kWorkLongSide) / longSide;
    return { std::max(1, int(std::lround(size.width() * k))),
             std::max(1, int(std::lround(size.height() * k))) };
}

QImage renderWork(AnimStyle style,
                  const std::pair<QColor, QColor>& colors,
                  QSize size,
                  double phase,
                  uint32_t seed)
{
    return renderWorkStops(
      style, QVector<QColor>{ colors.first, colors.second }, size, phase, seed);
}

QImage renderWorkStops(AnimStyle style,
                       const QVector<QColor>& stops,
                       QSize size,
                       double phase,
                       uint32_t seed)
{
    QImage out(workSize(size), QImage::Format_RGB32);
    if (out.isNull() || stops.isEmpty()) {
        return out;
    }
    // Wrap first: phase 1 is exactly phase 0, not sin(2 pi) ~ 1e-16
    double p = phase - std::floor(phase);
    if (p >= 1.0) {
        p = 0.0;
    }
    const float T = float(p * 2.0 * M_PI);
    const Palette pal =
      paletteOf(stops.size() == 1 ? QVector<QColor>{ stops.first(), stops.first() }
                                  : stops);
    // The style takes part in the seed so styles do not share parameters
    Random rnd(seed * 4u + uint32_t(style));
    switch (style) {
        case AnimStyle::Aurora:
            renderAurora(out, pal, T, rnd);
            break;
        case AnimStyle::GradientShift:
            renderGradientShift(out, pal, T, rnd);
            break;
        case AnimStyle::Blobs:
            renderBlobs(out, pal, T, rnd, seed);
            break;
        case AnimStyle::Twinkle:
            renderTwinkle(out, pal, T, rnd);
            break;
        case AnimStyle::Flow:
            renderFlow(out, pal, T, rnd);
            break;
        case AnimStyle::Silk:
            renderSilk(out, pal, T, rnd);
            break;
        case AnimStyle::Mesh:
            renderMesh(out, pal, T, rnd);
            break;
        case AnimStyle::Bokeh:
            renderBokeh(out, pal, T, rnd);
            break;
        case AnimStyle::Waves:
            renderWaves(out, pal, T, rnd);
            break;
        case AnimStyle::Grid:
            renderGrid(out, pal, T, rnd);
            break;
        case AnimStyle::Prism:
            renderPrism(out, pal, T, rnd);
            break;
        case AnimStyle::Contours:
            renderContours(out, pal, T, rnd);
            break;
    }
    return out;
}

QImage renderFrame(AnimStyle style,
                   const std::pair<QColor, QColor>& colors,
                   QSize size,
                   double phase,
                   uint32_t seed)
{
    return renderFrameStops(
      style, QVector<QColor>{ colors.first, colors.second }, size, phase, seed);
}

QImage renderFrameStops(AnimStyle style,
                        const QVector<QColor>& stops,
                        QSize size,
                        double phase,
                        uint32_t seed)
{
    QImage work = renderWorkStops(style, stops, size, phase, seed);
    if (work.size() == size || work.isNull()) {
        return work;
    }
    return work.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
      .convertToFormat(QImage::Format_RGB32);
}

std::pair<QColor, QColor> colorsFor(const EffectSettings& settings,
                                    const QImage& shot)
{
    if (!shot.isNull() &&
        (settings.backgroundMode == BackgroundMode::FromScreenshot ||
         (settings.backgroundMode == BackgroundMode::Gradient &&
          settings.autoColors))) {
        return backgroundfx::autoGradientColors(shot);
    }
    if (settings.backgroundMode == BackgroundMode::Solid) {
        return { settings.backgroundColor1,
                 settings.backgroundColor1.darker(150) };
    }
    return { settings.backgroundColor1, settings.backgroundColor2 };
}

QVector<QColor> stopsFor(const EffectSettings& settings, const QImage& shot)
{
    const bool usesShot =
      !shot.isNull() &&
      (settings.backgroundMode == BackgroundMode::FromScreenshot ||
       (settings.backgroundMode == BackgroundMode::Gradient && settings.autoColors));
    if (settings.backgroundMode == BackgroundMode::Gradient && !usesShot &&
        settings.palette.size() >= 2) {
        return settings.palette;
    }
    const auto pair = colorsFor(settings, shot);
    return { pair.first, pair.second };
}

AnimStyle styleFrom(int value)
{
    return AnimStyle(std::clamp(value, 0, kAnimStyleCount - 1));
}

} // namespace animatedbg
