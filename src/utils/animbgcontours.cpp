// SPDX-License-Identifier: GPL-3.0-or-later
// The Contours Animate style, a topographic map look: a
// range of hills drawn as fine bright contour lines over the palette, under
// a pale sky whose skyline rises like an exponential curve. Same rules as
// the other styles: drawn at work size, deterministic for a seed, and every
// time term a whole multiple of T so the loop closes.

#include "animbgkit.h"

#include <QVector>
#include <array>
#include <cmath>

namespace animbgkit {

namespace {

// Contour levels per unit of height: thirty-odd fine lines across the frame
constexpr float kLevels = 20.f;
// Line core half width and soft edge in work pixels. The edge is soft
// enough that a line creeping a twentieth of a pixel per frame never pops
constexpr float kLineHalf = 0.35f;
constexpr float kLineSoft = 1.8f;

} // namespace

// Contours: a still height field of hills and ridges, coloured along the
// palette from the far hills to the near ones and lit from the upper left,
// with contour lines that climb one level per loop: the motion is the lines
// flowing uphill, slow enough per frame that the crisp lines stay smooth.
// The skyline follows an exponential curve rising to the right, with a pale
// sky above it that sways a little.
void renderContours(QImage& out, const Palette& pal, float T, Random& rnd)
{
    const int w = out.width();
    const int h = out.height();
    const float unit = float(std::max(w, h));
    const float sx = float(w) / unit;
    const float sy = float(h) / unit;

    constexpr int kHills = 5;
    struct Hill
    {
        float x, y, amp, inv; // inv = 1 / radius^2
    };
    std::array<Hill, kHills> hills{};
    for (int j = 0; j < kHills; ++j) {
        Hill& k = hills[j];
        k.x = (float(j) + 0.5f) / kHills * sx + rnd.range(-0.06f, 0.06f);
        k.y = rnd.range(0.35f, 0.85f) * sy;
        k.amp = rnd.range(0.35f, 0.7f) * ((j % 2 == 0) ? 1.f : -0.7f);
        const float r = rnd.range(0.12f, 0.22f);
        k.inv = 1.f / (r * r);
    }
    // Two ridges running across the hills
    const float r1 = rnd.range(0.f, kTau);
    const float r2 = rnd.range(0.f, kTau);
    const float skyPh = rnd.range(0.f, kTau);

    // Pass 1: height in contour levels, and the skyline per column
    QVector<float> level(w * h);
    QVector<float> skyline(w);
    const float curveK = 2.6f;
    const float curveNorm = 1.f / (std::exp(curveK) - 1.f);
    for (int x = 0; x < w; ++x) {
        const float u = (x + 0.5f) / w;
        // Low on the left, climbing steeply at the right, like the logo
        const float rise = (std::exp(curveK * u) - 1.f) * curveNorm;
        skyline[x] = 0.36f - 0.3f * rise +
                     0.02f * std::sin(kTau * 3.f * u + skyPh) +
                     0.01f * std::sin(kTau * 7.f * u + 2.f * skyPh) +
                     0.006f * std::sin(kTau * 2.f * u + T);
    }
    for (int y = 0; y < h; ++y) {
        const float py = (y + 0.5f) / unit;
        for (int x = 0; x < w; ++x) {
            const float px = (x + 0.5f) / unit;
            // A gentle slope down to the viewer, so lines run across
            float H = 0.9f * py + 0.35f * px;
            for (int j = 0; j < kHills; ++j) {
                const float dx = px - hills[j].x;
                const float dy = py - hills[j].y;
                H += hills[j].amp * std::exp(-(dx * dx + dy * dy) * hills[j].inv);
            }
            H += 0.07f * std::sin(kTau * (1.3f * px + 0.6f * py) + r1);
            H += 0.05f * std::sin(kTau * (0.5f * px - 1.7f * py) + r2);
            level[y * w + x] = H * kLevels;
        }
    }

    const Rgb sky = lighter(pal.along(0.1f), 0.86f);
    const Rgb skyLow = lighter(pal.along(0.35f), 0.7f);
    // Lines climb one whole level per loop, so the last frame meets the first
    const float shift = T / kTau;
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const float v = (y + 0.5f) / h;
        const int yu = std::max(y - 1, 0);
        const int yd = std::min(y + 1, h - 1);
        for (int x = 0; x < w; ++x) {
            const float u = (x + 0.5f) / w;
            const int xl = std::max(x - 1, 0);
            const int xr = std::min(x + 1, w - 1);
            const float L = level[y * w + x];
            const float gx = (level[y * w + xr] - level[y * w + xl]) / float(xr - xl);
            const float gy = (level[yd * w + x] - level[yu * w + x]) / float(yd - yu);
            const float slope = std::sqrt(gx * gx + gy * gy) + 1e-4f;

            // Far hills under the skyline take the start of the palette, the
            // near ones at the lower right its end, like the hero mountains
            const float below = (v - skyline[x]) / std::max(1.f - skyline[x], 0.2f);
            const float depth = std::clamp(
              0.9f * below + 0.25f * u - 0.08f + 0.05f * std::sin(L / kLevels * kTau),
              0.f,
              1.f);
            Rgb ground = pal.along(depth);
            // Lit from the upper left: faces turned that way are brighter.
            // The gradient is in levels per pixel; this makes it a slope
            const float lit =
              std::clamp(-(gx + gy) * unit / kLevels * 0.3f, -1.f, 1.f);
            ground = lit >= 0.f ? lighter(ground, 0.22f * lit)
                                : scale(ground, 1.f + 0.22f * lit);

            // Distance to the nearest contour line, in pixels
            const float f = L - shift;
            const float frac = f - std::floor(f);
            const float dist = std::min(frac, 1.f - frac) / slope;
            const float ink = 1.f - smooth(kLineHalf, kLineHalf + kLineSoft, dist);
            // On near-flat ground (hilltops, saddles) a line sweeps far per
            // frame and rings shrink to nothing: fade lines out there
            const float steady = smooth(0.012f, 0.04f, slope);
            Rgb c = screen(ground, lighter(ground, 0.85f), 0.75f * ink * steady);

            // The sky above the skyline, fading into the haze just over it
            const float edge = (v - skyline[x]) * h;
            const float land = smooth(-2.f, 2.f, edge);
            if (land < 1.f) {
                const Rgb air = mix(sky, skyLow, smooth(0.f, 1.f, v / std::max(skyline[x], 0.05f)));
                c = mix(air, c, land);
            }
            line[x] = pack(c);
        }
    }
}

} // namespace animbgkit
