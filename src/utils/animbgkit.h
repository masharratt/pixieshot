// SPDX-License-Identifier: GPL-3.0-or-later
// Shared kit for the animated background styles (animatedbg.cpp and
// animbgscenes.cpp): float colour maths, the per-seed generator and the
// palette every style draws from. Internal to the renderer.

#pragma once

#include <QColor>
#include <QImage>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace animbgkit {

inline constexpr float kTau = 6.28318530718f;

struct Rgb
{
    float r, g, b; // 0..1
};

inline Rgb rgbOf(const QColor& c)
{
    return { float(c.redF()), float(c.greenF()), float(c.blueF()) };
}

inline Rgb mix(const Rgb& a, const Rgb& b, float t)
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

inline Rgb scale(const Rgb& a, float k)
{
    return { a.r * k, a.g * k, a.b * k };
}

// Screen blend of `light` at strength t: brightens without clipping
inline Rgb screen(const Rgb& base, const Rgb& light, float t)
{
    auto ch = [t](float b, float l) { return 1.f - (1.f - b) * (1.f - l * t); };
    return { ch(base.r, light.r), ch(base.g, light.g), ch(base.b, light.b) };
}

inline QRgb pack(const Rgb& c)
{
    auto ch = [](float v) { return std::clamp(int(v * 255.f + 0.5f), 0, 255); };
    return qRgb(ch(c.r), ch(c.g), ch(c.b));
}

// Same colour, more light, hue kept
inline Rgb lighter(const Rgb& c, float t)
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
    Rgb b;     // last colour
    Rgb light; // a light tint of their mix, for highlights
    Rgb deep;  // a dark shade of their mix, for the ground
    QVector<Rgb> stops; // all colours, first to last (two or more)

    bool multi() const { return stops.size() > 2; }
    // Colour at t (0..1) along the stops; with two stops mix(a, b, t)
    Rgb along(float t) const
    {
        if (!multi()) {
            return mix(a, b, t);
        }
        const float pos = std::clamp(t, 0.f, 1.f) * float(stops.size() - 1);
        const int i = std::min(int(pos), int(stops.size()) - 2);
        return mix(stops[i], stops[i + 1], pos - float(i));
    }
    // The i-th of n colours spread over the stops
    Rgb spread(int i, int n) const
    {
        return along(n <= 1 ? 0.f : float(i) / float(n - 1));
    }
};

inline Palette paletteOf(const QVector<QColor>& colors)
{
    Palette p;
    for (const QColor& c : colors) {
        p.stops << rgbOf(c);
    }
    p.a = p.stops.first();
    p.b = p.stops.last();
    Rgb mid = mix(p.a, p.b, 0.5f);
    if (p.multi()) {
        // The average of every stop, so the middle colours count too
        mid = { 0, 0, 0 };
        for (const Rgb& c : p.stops) {
            mid = { mid.r + c.r, mid.g + c.g, mid.b + c.b };
        }
        mid = scale(mid, 1.f / float(p.stops.size()));
    }
    p.light = lighter(mid, 0.35f);
    p.deep = scale(mid, 0.55f);
    return p;
}

// Hermite 0..1 between edges a and b
inline float smooth(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Styles added in round 5, in animbgscenes.cpp. Same contract as the rest:
// draw into `out` (Format_RGB32) for T = 2 pi phase, every time term a whole
// multiple of T so the loop closes.
void renderMesh(QImage& out, const Palette& pal, float T, Random& rnd);
void renderBokeh(QImage& out, const Palette& pal, float T, Random& rnd);
void renderWaves(QImage& out, const Palette& pal, float T, Random& rnd);
void renderGrid(QImage& out, const Palette& pal, float T, Random& rnd);
void renderPrism(QImage& out, const Palette& pal, float T, Random& rnd);
// In animbgcontours.cpp
void renderContours(QImage& out, const Palette& pal, float T, Random& rnd);

} // namespace animbgkit
