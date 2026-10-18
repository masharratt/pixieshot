// SPDX-License-Identifier: GPL-3.0-or-later

#include "animatedbg.h"
#include "backgroundfx.h"

#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr float kTau = 6.28318530718f;

struct Rgb
{
    float r, g, b; // 0..1
};

Rgb rgbOf(const QColor& c)
{
    return { float(c.redF()), float(c.greenF()), float(c.blueF()) };
}

Rgb mix(const Rgb& a, const Rgb& b, float t)
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

Rgb scale(const Rgb& a, float k)
{
    return { a.r * k, a.g * k, a.b * k };
}

// Screen blend of `light` at strength t: brightens without clipping
Rgb screen(const Rgb& base, const Rgb& light, float t)
{
    auto ch = [t](float b, float l) { return 1.f - (1.f - b) * (1.f - l * t); };
    return { ch(base.r, light.r), ch(base.g, light.g), ch(base.b, light.b) };
}

QRgb pack(const Rgb& c)
{
    auto ch = [](float v) { return std::clamp(int(v * 255.f + 0.5f), 0, 255); };
    return qRgb(ch(c.r), ch(c.g), ch(c.b));
}

// Same colour, more light, hue kept
Rgb lighter(const Rgb& c, float t)
{
    return mix(c, { 1, 1, 1 }, t);
}

// Small deterministic generator for per-seed parameters
class Random
{
public:
    explicit Random(uint32_t seed)
      : m_state(uint64_t(seed) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull)
    {}
    // 0..1
    float next()
    {
        m_state += 0x9E3779B97F4A7C15ull;
        uint64_t z = m_state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return float(z >> 40) / float(1u << 24);
    }
    float range(float lo, float hi) { return lo + (hi - lo) * next(); }

private:
    uint64_t m_state;
};

// The palette every style draws from
struct Palette
{
    Rgb a;     // first colour
    Rgb b;     // second colour
    Rgb light; // a light tint of their mix, for highlights
    Rgb deep;  // a dark shade of their mix, for the ground
};

Palette paletteOf(const std::pair<QColor, QColor>& colors)
{
    Palette p;
    p.a = rgbOf(colors.first);
    p.b = rgbOf(colors.second);
    const Rgb mid = mix(p.a, p.b, 0.5f);
    p.light = lighter(mid, 0.35f);
    p.deep = scale(mid, 0.55f);
    return p;
}

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
            line[x] = pack(mix(pal.a, pal.b, t));
        }
    }
}

void renderAurora(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    constexpr int kBands = 3;
    const Rgb colors[kBands] = { lighter(pal.a, 0.25f),
                                 lighter(pal.b, 0.3f),
                                 pal.light };
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
    const Rgb colors[5] = {
        pal.a, pal.b, pal.light, lighter(pal.a, 0.15f), lighter(pal.b, 0.15f)
    };
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
            line[x] = pack(mix(pal.a, pal.b, t));
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
    QImage out(workSize(size), QImage::Format_RGB32);
    if (out.isNull()) {
        return out;
    }
    // Wrap first: phase 1 is exactly phase 0, not sin(2 pi) ~ 1e-16
    double p = phase - std::floor(phase);
    if (p >= 1.0) {
        p = 0.0;
    }
    const float T = float(p * 2.0 * M_PI);
    const Palette pal = paletteOf(colors);
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
    }
    return out;
}

QImage renderFrame(AnimStyle style,
                   const std::pair<QColor, QColor>& colors,
                   QSize size,
                   double phase,
                   uint32_t seed)
{
    QImage work = renderWork(style, colors, size, phase, seed);
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

AnimStyle styleFrom(int value)
{
    return AnimStyle(std::clamp(value, 0, 3));
}

} // namespace animatedbg
