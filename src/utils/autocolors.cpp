// SPDX-License-Identifier: GPL-3.0-or-later
// Auto gradient colors: k-means on a small copy of the shot in OKLab.

#include "backgroundfx.h"

#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace {

// ---- OKLab (perceptual-ish space for clustering and nudging) ----------

struct Lab
{
    float L = 0, a = 0, b = 0;
};

float toLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float toGamma(float c)
{
    c = std::clamp(c, 0.f, 1.f);
    return c <= 0.0031308f ? c * 12.92f
                           : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

// sRGB 0..255 lookup to linear, the clustering hot path
const std::array<float, 256>& linearTable()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[i] = toLinear(i / 255.f);
        }
        return t;
    }();
    return table;
}

Lab toLab(QRgb c)
{
    const auto& lin = linearTable();
    const float r = lin[qRed(c)];
    const float g = lin[qGreen(c)];
    const float b = lin[qBlue(c)];
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g +
                              0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g +
                              0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g +
                              0.6299787005f * b);
    return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
             1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
             0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
}

// Linear RGB of a Lab color, possibly out of the 0..1 gamut
std::array<float, 3> linearRgb(const Lab& c)
{
    const float l = std::pow(c.L + 0.3963377774f * c.a + 0.2158037573f * c.b, 3.f);
    const float m = std::pow(c.L - 0.1055613458f * c.a - 0.0638541728f * c.b, 3.f);
    const float s = std::pow(c.L - 0.0894841775f * c.a - 1.2914855480f * c.b, 3.f);
    return { 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
             -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
             -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s };
}

bool inGamut(const Lab& c)
{
    for (float v : linearRgb(c)) {
        if (v < -0.0005f || v > 1.0005f) {
            return false;
        }
    }
    return true;
}

// Lab to sRGB, shrinking chroma (keeping hue and lightness) until it fits
QColor toColor(Lab c)
{
    for (int i = 0; i < 40 && !inGamut(c); ++i) {
        c.a *= 0.92f;
        c.b *= 0.92f;
    }
    const auto rgb = linearRgb(c);
    return QColor::fromRgbF(toGamma(rgb[0]), toGamma(rgb[1]), toGamma(rgb[2]))
      .toRgb();
}

float chroma(const Lab& c)
{
    return std::hypot(c.a, c.b);
}

float deltaE(const Lab& x, const Lab& y)
{
    return std::sqrt((x.L - y.L) * (x.L - y.L) + (x.a - y.a) * (x.a - y.a) +
                     (x.b - y.b) * (x.b - y.b));
}

Lab withChroma(const Lab& c, float newChroma)
{
    const float current = chroma(c);
    if (current < 1e-6f) {
        return c;
    }
    const float k = newChroma / current;
    return { c.L, c.a * k, c.b * k };
}

Lab rotateHue(const Lab& c, float degrees)
{
    const float r = degrees * float(M_PI) / 180.f;
    return { c.L,
             c.a * std::cos(r) - c.b * std::sin(r),
             c.a * std::sin(r) + c.b * std::cos(r) };
}

// ---- auto colors ------------------------------------------------------

constexpr int kSampleLongSide = 64;
constexpr int kClusters = 4;
constexpr int kIterations = 12;
constexpr uint32_t kSeed = 0x9E3779B9u;
// Below this chroma a color reads as grey; outside the L range as
// black or white
constexpr float kGreyChroma = 0.045f;
constexpr float kMinL = 0.16f;
constexpr float kMaxL = 0.94f;
// A cluster must hold this share of the sampled pixels to be picked
constexpr float kMinShare = 0.015f;
// Designed range for picked colors
constexpr float kMinChroma = 0.09f;
constexpr float kMaxChroma = 0.19f;
constexpr float kDesignMinL = 0.42f;
constexpr float kDesignMaxL = 0.78f;
// Two colors closer than this get pushed apart
constexpr float kMinSeparation = 0.10f;
// The background's mean lightness keeps at least this distance from the
// shot's edge
constexpr float kEdgeContrast = 0.18f;
constexpr float kEdgeShift = 0.26f;

struct Sample
{
    Lab lab;
    float pos; // 0 at the top-left corner, 1 at the bottom-right
};

struct Cluster
{
    Lab center;
    float pos = 0;
    int count = 0;
};

uint32_t nextRandom(uint32_t& state)
{
    // xorshift32: fixed, platform independent sequence
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Deterministic k-means++ seeding, then Lloyd iterations
QVector<Cluster> kmeans(const QVector<Sample>& samples, int k)
{
    QVector<Cluster> clusters;
    if (samples.isEmpty()) {
        return clusters;
    }
    uint32_t state = kSeed;
    clusters.append({ samples[int(nextRandom(state) % samples.size())].lab });
    QVector<float> dist(samples.size());
    while (clusters.size() < k) {
        double total = 0;
        for (int i = 0; i < samples.size(); ++i) {
            float best = 1e9f;
            for (const Cluster& c : clusters) {
                best = std::min(best, deltaE(samples[i].lab, c.center));
            }
            dist[i] = best * best;
            total += dist[i];
        }
        if (total <= 0) {
            break; // every sample sits on a center already
        }
        double pick = (nextRandom(state) / double(UINT32_MAX)) * total;
        int chosen = samples.size() - 1;
        for (int i = 0; i < samples.size(); ++i) {
            pick -= dist[i];
            if (pick <= 0) {
                chosen = i;
                break;
            }
        }
        clusters.append({ samples[chosen].lab });
    }
    QVector<int> owner(samples.size(), 0);
    for (int iter = 0; iter < kIterations; ++iter) {
        for (int i = 0; i < samples.size(); ++i) {
            float best = 1e9f;
            for (int c = 0; c < clusters.size(); ++c) {
                const float d = deltaE(samples[i].lab, clusters[c].center);
                if (d < best) {
                    best = d;
                    owner[i] = c;
                }
            }
        }
        QVector<std::array<double, 4>> sums(clusters.size(), { 0, 0, 0, 0 });
        QVector<int> counts(clusters.size(), 0);
        for (int i = 0; i < samples.size(); ++i) {
            auto& s = sums[owner[i]];
            s[0] += samples[i].lab.L;
            s[1] += samples[i].lab.a;
            s[2] += samples[i].lab.b;
            s[3] += samples[i].pos;
            ++counts[owner[i]];
        }
        for (int c = 0; c < clusters.size(); ++c) {
            clusters[c].count = counts[c];
            if (counts[c] > 0) {
                const auto& s = sums[c];
                clusters[c].center = { float(s[0] / counts[c]),
                                       float(s[1] / counts[c]),
                                       float(s[2] / counts[c]) };
                clusters[c].pos = float(s[3] / counts[c]);
            }
        }
    }
    return clusters;
}

QImage sampleCopy(const QImage& shot, int longSide)
{
    QImage plain = shot;
    plain.setDevicePixelRatio(1.0);
    const int w = plain.width();
    const int h = plain.height();
    const double scale = std::min(1.0, double(longSide) / std::max(w, h));
    const QSize size(std::max(1, int(std::lround(w * scale))),
                     std::max(1, int(std::lround(h * scale))));
    // A fast pre-shrink keeps the smooth pass cheap on huge captures
    if (w > 8 * size.width() && h > 8 * size.height()) {
        plain = plain.scaled(size * 4, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return plain.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
      .convertToFormat(QImage::Format_RGB32);
}

// Average lightness of the outermost ring of pixels
float edgeLightness(const QImage& img)
{
    double sum = 0;
    int n = 0;
    for (int x = 0; x < img.width(); ++x) {
        sum += toLab(img.pixel(x, 0)).L + toLab(img.pixel(x, img.height() - 1)).L;
        n += 2;
    }
    for (int y = 1; y < img.height() - 1; ++y) {
        sum += toLab(img.pixel(0, y)).L + toLab(img.pixel(img.width() - 1, y)).L;
        n += 2;
    }
    return n == 0 ? 0.5f : float(sum / n);
}

Lab designed(Lab c, bool chromatic)
{
    c.L = std::clamp(c.L, kDesignMinL, kDesignMaxL);
    if (chromatic) {
        c = withChroma(c, std::clamp(chroma(c), kMinChroma, kMaxChroma));
    }
    return c;
}

} // namespace

namespace backgroundfx {

std::pair<QColor, QColor> autoGradientColors(const QImage& shot)
{
    if (shot.isNull()) {
        return { QColor(0x8B, 0x5C, 0xF6), QColor(0x3B, 0x82, 0xF6) };
    }
    const QImage small = sampleCopy(shot, kSampleLongSide);
    const int w = small.width();
    const int h = small.height();
    QVector<Sample> all;
    QVector<Sample> vivid;
    all.reserve(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Lab lab = toLab(small.pixel(x, y));
            const float pos =
              (w + h > 2) ? float(x + y) / float(w + h - 2) : 0.5f;
            all.append({ lab, pos });
            if (chroma(lab) >= kGreyChroma && lab.L >= kMinL && lab.L <= kMaxL) {
                vivid.append({ lab, pos });
            }
        }
    }
    // Greys only when the image has (next to) nothing else
    const bool chromatic = vivid.size() >= std::max(8, int(all.size() * 0.02));
    const QVector<Sample>& pool = chromatic ? vivid : all;
    const QVector<Cluster> clusters = kmeans(pool, kClusters);
    const int minCount = std::max(1, int(pool.size() * kMinShare));

    // Best pair: vivid, far apart, and not a speck of a few pixels
    int bi = -1;
    int bj = -1;
    float bestScore = -1;
    for (int i = 0; i < clusters.size(); ++i) {
        for (int j = i + 1; j < clusters.size(); ++j) {
            const Cluster& a = clusters[i];
            const Cluster& b = clusters[j];
            if (a.count < minCount || b.count < minCount) {
                continue;
            }
            const float share =
              std::pow(float(std::min(a.count, b.count)) / pool.size(), 0.25f);
            const float vividness =
              chromatic ? chroma(a.center) + chroma(b.center) : 1.f;
            const float score =
              vividness * (deltaE(a.center, b.center) + 0.01f) * share;
            if (score > bestScore) {
                bestScore = score;
                bi = i;
                bj = j;
            }
        }
    }
    Lab first;
    Lab second;
    float firstPos = 0.f;
    float secondPos = 1.f;
    if (bi >= 0) {
        first = clusters[bi].center;
        second = clusters[bj].center;
        firstPos = clusters[bi].pos;
        secondPos = clusters[bj].pos;
    } else {
        // One usable cluster: the biggest one, and a neighbour hue
        const auto biggest = std::max_element(
          clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) {
              return a.count < b.count;
          });
        first = biggest == clusters.end() ? Lab{ 0.6f, 0, 0 } : biggest->center;
        second = chromatic ? rotateHue(first, 38.f) : first;
    }
    first = designed(first, chromatic);
    second = designed(second, chromatic);
    if (deltaE(first, second) < kMinSeparation) {
        // Too alike: split the lightness, and the hue too when there is one
        const float mid = (first.L + second.L) / 2;
        first.L = std::clamp(mid + 0.07f, kDesignMinL, kDesignMaxL + 0.06f);
        second.L = std::clamp(mid - 0.07f, kDesignMinL - 0.06f, kDesignMaxL);
        if (chromatic) {
            second = rotateHue(second, 30.f);
        }
    }
    // The shot's edge must stand out against the background
    const float edgeL = edgeLightness(small);
    const float meanL = (first.L + second.L) / 2;
    if (std::abs(meanL - edgeL) < kEdgeContrast) {
        const float target = edgeL > 0.55f ? edgeL - kEdgeShift : edgeL + kEdgeShift;
        const float shift = target - meanL;
        first.L = std::clamp(first.L + shift, 0.18f, 0.92f);
        second.L = std::clamp(second.L + shift, 0.18f, 0.92f);
    }
    if (secondPos < firstPos) {
        std::swap(first, second);
    }
    return { toColor(first), toColor(second) };
}

} // namespace backgroundfx
