// SPDX-License-Identifier: GPL-3.0-or-later
// The round 5 Animate styles: Mesh, Bokeh, Waves, Grid and Prism. Same
// rules as animatedbg.cpp: drawn small (work size), deterministic for a
// seed, and every time term a whole multiple of T so the loop closes.

#include "animbgkit.h"

#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>

namespace animbgkit {

namespace {

const Rgb kWhite{ 1, 1, 1 };

QRgb* lineOf(QImage& out, int y)
{
    return reinterpret_cast<QRgb*>(out.scanLine(y));
}

void packAll(QImage& out, const QVector<Rgb>& buf)
{
    const int w = out.width();
    for (int y = 0; y < out.height(); ++y) {
        QRgb* line = lineOf(out, y);
        const Rgb* src = buf.constData() + y * w;
        for (int x = 0; x < w; ++x) {
            line[x] = pack(src[x]);
        }
    }
}

} // namespace

// Mesh: six colour points on a loose grid, each circling slowly, blended by
// inverse distance so every point owns a soft region, with a gentle warp so
// the seams between them curve like a mesh gradient
void renderMesh(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float unit = float(std::max(w, h));
    const float sx = float(w) / unit;
    const float sy = float(h) / unit;
    constexpr int kPoints = 6;
    // Neighbours on the grid take colours from apart on the palette
    constexpr float kOrder[kPoints] = { 0.f, 0.6f, 0.2f, 0.8f, 0.4f, 1.f };
    struct Point
    {
        float bx, by, ax, ay, ph1, ph2;
        int nx, ny;
        Rgb color;
    };
    std::array<Point, kPoints> pts{};
    for (int j = 0; j < kPoints; ++j) {
        Point& p = pts[j];
        p.bx = (float(j % 3) + 0.5f) / 3.f + rnd.range(-0.08f, 0.08f);
        p.by = (float(j / 3) + 0.5f) / 2.f + rnd.range(-0.1f, 0.1f);
        p.ax = rnd.range(0.1f, 0.18f);
        p.ay = rnd.range(0.08f, 0.16f);
        p.ph1 = rnd.range(0.f, kTau);
        p.ph2 = rnd.range(0.f, kTau);
        p.nx = (j % 2 == 0) ? 1 : -1;
        p.ny = (j % 3 == 1) ? 2 : ((j % 2 == 0) ? -1 : 1);
        const Rgb c = pal.along(kOrder[j]);
        p.color = (j % 2 == 0) ? lighter(c, 0.1f) : scale(c, 0.84f);
    }
    std::array<float, kPoints> px{};
    std::array<float, kPoints> py{};
    for (int j = 0; j < kPoints; ++j) {
        const Point& p = pts[j];
        px[j] = p.bx + p.ax * std::cos(p.nx * T + p.ph1);
        py[j] = p.by + p.ay * std::sin(p.ny * T + p.ph2);
    }
    const float wph1 = rnd.range(0.f, kTau);
    const float wph2 = rnd.range(0.f, kTau);
    QVector<float> warpU(h);
    QVector<float> warpV(w);
    for (int y = 0; y < h; ++y) {
        const float v = (y + 0.5f) / h;
        warpU[y] = 0.04f * std::sin(kTau * 1.2f * v + T + wph1);
    }
    for (int x = 0; x < w; ++x) {
        const float u = (x + 0.5f) / w;
        warpV[x] = 0.04f * std::sin(kTau * 1.1f * u - T + wph2);
    }
    for (int y = 0; y < h; ++y) {
        QRgb* line = lineOf(out, y);
        const float v = (y + 0.5f) / h;
        for (int x = 0; x < w; ++x) {
            const float u = (x + 0.5f) / w;
            const float uu = u + warpU[y];
            const float vv = v + warpV[x];
            Rgb sum{ 0, 0, 0 };
            float total = 0.f;
            for (int j = 0; j < kPoints; ++j) {
                const float dx = (uu - px[j]) * sx;
                const float dy = (vv - py[j]) * sy;
                const float q = dx * dx + dy * dy + 0.01f;
                const float k = 1.f / (q * q);
                sum.r += pts[j].color.r * k;
                sum.g += pts[j].color.g * k;
                sum.b += pts[j].color.b * k;
                total += k;
            }
            // A soft vignette keeps the edges calm behind the shot
            const float du = u - 0.5f;
            const float dv = v - 0.5f;
            const float vignette = 1.f - 0.28f * (du * du + dv * dv);
            line[x] = pack(scale(sum, vignette / total));
        }
    }
}

// Bokeh: out-of-focus discs, near ones large and soft, far ones small and
// crisp, each circling slowly and breathing, over a dim glowing ground
void renderBokeh(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float unit = float(std::max(w, h));
    QVector<Rgb> buf(w * h);
    const Rgb top = scale(pal.along(0.2f), 0.34f);
    const Rgb bottom = scale(pal.along(0.8f), 0.22f);
    const Rgb glowColor = scale(pal.along(0.5f), 0.6f);
    const float gph = rnd.range(0.f, kTau);
    const float gx = (rnd.range(0.35f, 0.65f) + 0.12f * std::cos(T + gph)) * w;
    const float gy = (rnd.range(0.35f, 0.6f) + 0.08f * std::sin(T + gph)) * h;
    const float gs = 0.45f * unit;
    QVector<float> ex(w);
    for (int x = 0; x < w; ++x) {
        const float d = (x + 0.5f - gx) / gs;
        ex[x] = std::exp(-d * d);
    }
    for (int y = 0; y < h; ++y) {
        const float d = (y + 0.5f - gy) / gs;
        const float ey = std::exp(-d * d);
        const Rgb ground = mix(top, bottom, (y + 0.5f) / h);
        Rgb* row = buf.data() + y * w;
        for (int x = 0; x < w; ++x) {
            row[x] = screen(ground, glowColor, 0.75f * ex[x] * ey);
        }
    }
    struct Disc
    {
        float z, cx, cy, r, soft, alpha, rad, ph, ph2;
        int turns, breath;
        Rgb color;
    };
    const int count =
      std::clamp(int(26.f * float(w) * float(h) / (480.f * 270.f)), 10, 40);
    QVector<Disc> discs(count);
    for (Disc& d : discs) {
        d.z = rnd.next();
        d.cx = rnd.range(-0.05f, 1.05f) * w;
        d.cy = rnd.range(-0.05f, 1.05f) * h;
        d.r = (0.02f + 0.075f * d.z * d.z) * unit;
        d.soft = 1.8f + 0.35f * d.r * d.z;
        d.alpha = 0.6f - 0.32f * d.z;
        d.rad = (0.02f + 0.05f * d.z) * unit;
        d.ph = rnd.range(0.f, kTau);
        d.ph2 = rnd.range(0.f, kTau);
        d.turns = rnd.next() < 0.5f ? 1 : -1;
        d.breath = rnd.next() < 0.6f ? 1 : 2;
        d.color = lighter(pal.along(rnd.next()), 0.2f + 0.25f * (1.f - d.z));
    }
    // Far discs first, the large near ones over them
    std::sort(discs.begin(), discs.end(), [](const Disc& a, const Disc& b) {
        return a.z < b.z;
    });
    for (const Disc& d : discs) {
        const float cx = d.cx + d.rad * std::cos(d.turns * T + d.ph);
        const float cy = d.cy + 0.7f * d.rad * std::sin(d.turns * T + d.ph);
        const float a = d.alpha * (0.7f + 0.3f * std::sin(d.breath * T + d.ph2));
        const int x0 = std::max(0, int(std::floor(cx - d.r - 1.f)));
        const int x1 = std::min(w - 1, int(std::ceil(cx + d.r + 1.f)));
        const int y0 = std::max(0, int(std::floor(cy - d.r - 1.f)));
        const int y1 = std::min(h - 1, int(std::ceil(cy + d.r + 1.f)));
        const float inner = d.r - d.soft;
        for (int y = y0; y <= y1; ++y) {
            Rgb* row = buf.data() + y * w;
            const float dy = y + 0.5f - cy;
            for (int x = x0; x <= x1; ++x) {
                const float dx = x + 0.5f - cx;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= d.r) {
                    continue;
                }
                const float cover = 1.f - smooth(inner, d.r, dist);
                // A lens disc is a touch brighter at its rim
                const float rim = 0.8f + 0.35f * smooth(0.55f * d.r, d.r, dist);
                row[x] = screen(row[x], d.color, a * cover * rim);
            }
        }
    }
    packAll(out, buf);
}

// Waves: five rolling layers of colour, back to front, each lit along its
// crest and casting a soft shadow on the layer behind
void renderWaves(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float fh = float(h);
    constexpr int kLayers = 5;
    struct Layer
    {
        float base, a1, f1, ph1, q1, a2, f2, ph2, q2;
        Rgb color;
    };
    std::array<Layer, kLayers> layers{};
    for (int i = 0; i < kLayers; ++i) {
        Layer& l = layers[i];
        l.base = 0.36f + 0.125f * i + rnd.range(-0.02f, 0.02f);
        l.a1 = rnd.range(0.045f, 0.075f) * (1.f - 0.1f * i);
        l.f1 = rnd.range(0.45f, 0.9f);
        l.ph1 = rnd.range(0.f, kTau);
        l.a2 = rnd.range(0.012f, 0.025f);
        l.f2 = rnd.range(1.4f, 2.4f);
        l.ph2 = rnd.range(0.f, kTau);
        l.q1 = rnd.range(0.f, kTau);
        l.q2 = rnd.range(0.f, kTau);
        const float t = float(i) / float(kLayers - 1);
        const Rgb c = pal.along(t);
        l.color = mix(lighter(c, 0.3f), scale(c, 0.68f), t);
    }
    const Rgb skyTop = mix(pal.deep, pal.along(0.f), 0.45f);
    const Rgb skyLow = lighter(pal.along(0.1f), 0.25f);
    const float crest = std::max(2.2f, 0.011f * fh);
    const float soft = std::max(3.5f, 0.016f * fh); // edge width
    const float shadowReach = 0.035f * fh;
    const float depth = 0.28f * fh;
    QVector<Rgb> buf(w * h);
    std::array<float, kLayers> edge{};
    for (int x = 0; x < w; ++x) {
        const float u = (x + 0.5f) / w;
        for (int i = 0; i < kLayers; ++i) {
            const Layer& l = layers[i];
            // Each hill sways (its phase rocks back and forth) rather than
            // travelling, so crests move slowly and never flicker
            edge[i] = (l.base +
                       l.a1 * std::sin(kTau * l.f1 * u + l.ph1 +
                                       0.9f * std::sin(T + l.q1)) +
                       l.a2 * std::sin(kTau * l.f2 * u + l.ph2 +
                                       0.6f * std::sin(2.f * T + l.q2))) *
                      fh;
        }
        for (int y = 0; y < h; ++y) {
            const float yc = y + 0.5f;
            Rgb c = mix(skyTop, skyLow, std::min(1.f, yc / (0.5f * fh)));
            for (int i = 0; i < kLayers; ++i) {
                const float d = yc - edge[i];
                // The shadow runs on under the layer, so nothing jumps where
                // the edge passes
                if (d > -4.f * shadowReach) {
                    c = scale(c, 1.f - 0.3f * std::exp(std::min(d, 0.f) / shadowReach));
                }
                if (d <= -0.5f * soft) {
                    continue;
                }
                const float cover = smooth(-0.5f * soft, 0.5f * soft, d);
                Rgb lc = scale(layers[i].color,
                               1.f - 0.24f * std::clamp(d / depth, 0.f, 1.f));
                if (d < 4.f * crest) {
                    const float q = d / crest;
                    lc = screen(lc, kWhite, 0.26f * std::exp(-q * q));
                }
                c = mix(c, lc, cover);
            }
            buf[y * w + x] = c;
        }
    }
    packAll(out, buf);
}

// Grid: a perspective floor grid scrolling toward the viewer under a glowing
// horizon, with a pool of light travelling over it
void renderGrid(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float fh = float(h);
    const float horizon = (0.4f + rnd.range(-0.03f, 0.03f)) * fh;
    constexpr float kCamera = 0.8f; // floor depth at one screen height down
    constexpr float kSpread = 1.6f; // world units across one screen height
    constexpr int kScroll = 1;      // cells the floor moves per loop
    const float p = T / kTau;
    const Rgb horizonColor = lighter(pal.along(0.5f), 0.18f);
    const Rgb skyTop = scale(pal.along(0.1f), 0.26f);
    const float gph = rnd.range(0.f, kTau);
    const float glowX = 2.2f; // how far the light swings, world units
    const float lightX = glowX * std::sin(T + gph);
    const float lightZ = 2.4f + 1.1f * std::cos(T + gph);
    const Rgb lightColor = lighter(pal.along(0.5f + 0.5f * std::sin(T + gph)), 0.1f);
    const float sunX = (0.5f + 0.06f * std::sin(T + gph)) * w;
    // Line profile in pixels: a fine core and a faint halo
    const float lineW = std::max(1.1f, 0.0055f * fh);
    const float core = 2.f * lineW * lineW;
    const float halo = 8.f * core;
    QVector<float> colX(w);
    QVector<Rgb> floorTint(w);
    QVector<Rgb> lineTint(w);
    for (int x = 0; x < w; ++x) {
        colX[x] = (x + 0.5f - w / 2.f) / fh * kSpread;
        const Rgb c = pal.along((x + 0.5f) / w);
        floorTint[x] = scale(c, 0.24f);
        lineTint[x] = lighter(c, 0.3f);
    }
    for (int y = 0; y < h; ++y) {
        QRgb* line = lineOf(out, y);
        const float yc = y + 0.5f;
        if (yc <= horizon) {
            // Sky: deep at the top, the horizon colour at the bottom, with a
            // soft sun drifting along it
            const float s = (horizon - yc) / std::max(1.f, horizon);
            const Rgb sky = mix(horizonColor, skyTop, smooth(0.f, 0.8f, s));
            for (int x = 0; x < w; ++x) {
                const float dx = (x + 0.5f - sunX) / (0.35f * w);
                const float dy = (horizon - yc) / (0.22f * fh);
                const float sun = std::exp(-dx * dx - dy * dy);
                line[x] = pack(screen(sky, lighter(horizonColor, 0.3f), 0.55f * sun));
            }
            continue;
        }
        const float down = (yc - horizon) / fh;
        const float z = kCamera / down;
        const float zScroll = z + p * float(kScroll);
        const float footZ = kCamera / (down * down * fh); // world per pixel
        const float fz = zScroll - std::floor(zScroll);
        const float lineZpx = (0.5f - std::abs(fz - 0.5f)) / footZ;
        const float fadeZ = std::clamp(1.3f - footZ * 4.f, 0.f, 1.f);
        const float lz = (std::exp(-lineZpx * lineZpx / core) +
                          0.3f * std::exp(-lineZpx * lineZpx / halo)) *
                         fadeZ;
        const float fog = std::exp(-0.12f * z);
        const float footX = z * kSpread / fh;
        const float fadeX = std::clamp(1.3f - footX * 4.f, 0.f, 1.f);
        const float haze = std::exp(-down / 0.05f);
        const float dz = (z - lightZ) / 1.3f;
        const float lightRow = std::exp(-dz * dz);
        for (int x = 0; x < w; ++x) {
            const float X = colX[x] * z;
            const float fx = X - std::floor(X);
            const float lineXpx = (0.5f - std::abs(fx - 0.5f)) / footX;
            const float lx = (std::exp(-lineXpx * lineXpx / core) +
                              0.3f * std::exp(-lineXpx * lineXpx / halo)) *
                             fadeX;
            const float gx = (X - lightX) / 1.3f;
            const float glow = lightRow * std::exp(-gx * gx);
            Rgb c = screen(floorTint[x], lightColor, 0.45f * glow);
            const float lines = std::min(1.f, std::max(lx, lz)) * fog;
            c = screen(c, lineTint[x], lines * (0.4f + 0.6f * glow));
            c = screen(c, horizonColor, 0.6f * haze);
            line[x] = pack(c);
        }
    }
}

// Prism: five plane waves at fifths of a turn interfere into a shifting
// net of bright veins, like light through rippled glass. Each vein is drawn
// three times a little apart in the palette's colours, a soft dispersion.
void renderPrism(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float unit = float(std::max(w, h));
    constexpr int kWaves = 5;
    constexpr int kSpeed[kWaves] = { 1, -1, 1, -1, 1 };
    const float rot = rnd.range(0.f, kTau);
    const float k = kTau * rnd.range(2.2f, 2.8f) / unit;
    // cos(a x + b y + phase) = cos(a x) cos(b y + phase) - sin(a x) sin(...)
    QVector<float> cx(kWaves * w);
    QVector<float> sx(kWaves * w);
    QVector<float> cy(kWaves * h);
    QVector<float> sy(kWaves * h);
    for (int i = 0; i < kWaves; ++i) {
        const float theta = rot + float(i) * kTau / (2.f * kWaves);
        const float a = k * std::cos(theta);
        const float b = k * std::sin(theta);
        const float ph = kSpeed[i] * T + rnd.range(0.f, kTau);
        for (int x = 0; x < w; ++x) {
            cx[i * w + x] = std::cos(a * (x + 0.5f));
            sx[i * w + x] = std::sin(a * (x + 0.5f));
        }
        for (int y = 0; y < h; ++y) {
            cy[i * h + y] = std::cos(b * (y + 0.5f) + ph);
            sy[i * h + y] = std::sin(b * (y + 0.5f) + ph);
        }
    }
    const Rgb inner = mix(pal.deep, pal.along(0.5f), 0.5f);
    const Rgb outer = scale(pal.along(0.9f), 0.3f);
    const Rgb veins[3] = { lighter(pal.along(0.15f), 0.35f),
                           lighter(pal.along(0.5f), 0.4f),
                           lighter(pal.along(0.85f), 0.35f) };
    constexpr float kOffsets[3] = { -0.42f, 0.f, 0.42f };
    const Rgb hot = lighter(pal.light, 0.6f);
    const float gph = rnd.range(0.f, kTau);
    const float gx = 0.5f + 0.1f * std::cos(T + gph);
    const float gy = 0.45f + 0.08f * std::sin(T + gph);
    for (int y = 0; y < h; ++y) {
        QRgb* line = lineOf(out, y);
        const float dy = ((y + 0.5f) / h - gy) * h / unit;
        for (int x = 0; x < w; ++x) {
            float s = 0.f;
            for (int i = 0; i < kWaves; ++i) {
                s += cx[i * w + x] * cy[i * h + y] - sx[i * w + x] * sy[i * h + y];
            }
            const float dx = ((x + 0.5f) / w - gx) * w / unit;
            Rgb c = mix(inner, outer, std::min(1.f, 2.2f * (dx * dx + dy * dy)));
            for (int v = 0; v < 3; ++v) {
                const float q = (s - kOffsets[v]) / 0.6f;
                c = screen(c, veins[v], 0.5f * std::exp(-q * q));
            }
            c = screen(c, hot, 0.55f * smooth(2.4f, 4.6f, s));
            line[x] = pack(c);
        }
    }
}

} // namespace animbgkit
