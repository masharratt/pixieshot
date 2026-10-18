// SPDX-License-Identifier: GPL-3.0-or-later

#include "gifquant.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr int kMaxColors = 256;
constexpr int kLevels = 64; // lookup table levels per channel
constexpr int kShift = 2;   // 256 / kLevels = 1 << kShift

// 4x4 Bayer matrix, 0..15. A 4 px repeat costs the GIF's LZW less than
// 8x8 does, and on soft gradients it looks the same.
constexpr std::array<int, 16> kBayer = {
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5,
};
// Dither spread in levels per channel: about the gap between neighbouring
// palette entries on a background gradient, so a step becomes a fine mix
constexpr int kDitherSpread = 6;

struct Box
{
    int begin, end; // range in the sample array
    int lo[3], hi[3];
};

int channel(QRgb c, int i)
{
    return i == 0 ? qRed(c) : (i == 1 ? qGreen(c) : qBlue(c));
}

void measure(const QVector<QRgb>& px, Box& box)
{
    for (int i = 0; i < 3; ++i) {
        box.lo[i] = 255;
        box.hi[i] = 0;
    }
    for (int k = box.begin; k < box.end; ++k) {
        for (int i = 0; i < 3; ++i) {
            const int v = channel(px[k], i);
            box.lo[i] = std::min(box.lo[i], v);
            box.hi[i] = std::max(box.hi[i], v);
        }
    }
}

int widestAxis(const Box& b)
{
    int axis = 0;
    for (int i = 1; i < 3; ++i) {
        if (b.hi[i] - b.lo[i] > b.hi[axis] - b.lo[axis]) {
            axis = i;
        }
    }
    return axis;
}

// Green counts most, as the eye is most sensitive to it
int distance(int r1, int g1, int b1, QRgb c)
{
    const int dr = r1 - qRed(c);
    const int dg = g1 - qGreen(c);
    const int db = b1 - qBlue(c);
    return 3 * dr * dr + 4 * dg * dg + 2 * db * db;
}

} // namespace

namespace gifquant {

Palette Palette::fromSamples(const QVector<QRgb>& samples)
{
    Palette palette;
    QVector<QRgb> px = samples;
    if (px.isEmpty()) {
        palette.m_colors = { qRgb(0, 0, 0) };
        palette.buildLookup();
        return palette;
    }
    // Median cut: split the box with the widest spread (weighted by how many
    // pixels it holds) at its median until there are enough boxes
    QVector<Box> boxes;
    Box all{ 0, int(px.size()), {}, {} };
    measure(px, all);
    boxes << all;
    while (boxes.size() < kMaxColors) {
        int pick = -1;
        double best = 0;
        for (int i = 0; i < boxes.size(); ++i) {
            const Box& b = boxes[i];
            const int spread = b.hi[widestAxis(b)] - b.lo[widestAxis(b)];
            if (spread == 0 || b.end - b.begin < 2) {
                continue;
            }
            const double score = double(spread) * std::sqrt(double(b.end - b.begin));
            if (score > best) {
                best = score;
                pick = i;
            }
        }
        if (pick < 0) {
            break; // every box holds a single colour
        }
        Box b = boxes[pick];
        const int axis = widestAxis(b);
        const int mid = (b.begin + b.end) / 2;
        std::nth_element(px.begin() + b.begin,
                         px.begin() + mid,
                         px.begin() + b.end,
                         [axis](QRgb x, QRgb y) {
                             return channel(x, axis) < channel(y, axis);
                         });
        Box left{ b.begin, mid, {}, {} };
        Box right{ mid, b.end, {}, {} };
        measure(px, left);
        measure(px, right);
        boxes[pick] = left;
        boxes << right;
    }
    for (const Box& b : boxes) {
        qint64 sum[3] = { 0, 0, 0 };
        for (int k = b.begin; k < b.end; ++k) {
            for (int i = 0; i < 3; ++i) {
                sum[i] += channel(px[k], i);
            }
        }
        const qint64 n = b.end - b.begin;
        const QRgb c = qRgb(int((sum[0] + n / 2) / n),
                            int((sum[1] + n / 2) / n),
                            int((sum[2] + n / 2) / n));
        if (!palette.m_colors.contains(c)) {
            palette.m_colors << c;
        }
    }
    palette.buildLookup();
    return palette;
}

void Palette::buildLookup()
{
    // Filled on first use per cell: a GIF only ever hits a small part of the
    // 262144 cells, and a full fill would cost ~0.7 G distance checks
    m_lookup.fill(kUnset, kLevels * kLevels * kLevels);
}

uchar Palette::indexOf(int r, int g, int b) const
{
    r = std::clamp(r, 0, 255) >> kShift;
    g = std::clamp(g, 0, 255) >> kShift;
    b = std::clamp(b, 0, 255) >> kShift;
    quint16& cell = m_lookup[(r * kLevels + g) * kLevels + b];
    if (cell == kUnset) {
        const int rv = (r << kShift) + (1 << kShift) / 2;
        const int gv = (g << kShift) + (1 << kShift) / 2;
        const int bv = (b << kShift) + (1 << kShift) / 2;
        int best = 0;
        int bestD = std::numeric_limits<int>::max();
        for (int i = 0; i < m_colors.size(); ++i) {
            const int d = distance(rv, gv, bv, m_colors[i]);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        cell = quint16(best);
    }
    return uchar(cell);
}

QVector<QRgb> samplePixels(const QImage& image, int maxCount, const QRect& area)
{
    QVector<QRgb> out;
    const QRect rect = (area.isNull() ? image.rect() : area).intersected(image.rect());
    if (rect.isEmpty() || maxCount <= 0) {
        return out;
    }
    const QImage plain = image.format() == QImage::Format_RGB32 ||
                             image.format() == QImage::Format_ARGB32
                           ? image
                           : image.convertToFormat(QImage::Format_RGB32);
    const qint64 total = qint64(rect.width()) * rect.height();
    const int step = std::max(1, int(std::ceil(std::sqrt(double(total) / maxCount))));
    for (int y = rect.top(); y <= rect.bottom(); y += step) {
        const QRgb* line = reinterpret_cast<const QRgb*>(plain.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); x += step) {
            out << (line[x] | 0xff000000u);
        }
    }
    return out;
}

QImage quantize(const QImage& image, const Palette& palette, const QRect& noDither)
{
    QImage out(image.size(), QImage::Format_Indexed8);
    out.setColorTable(palette.colors());
    const QImage plain = image.format() == QImage::Format_RGB32 ||
                             image.format() == QImage::Format_ARGB32
                           ? image
                           : image.convertToFormat(QImage::Format_RGB32);
    const QRect clean = noDither.intersected(image.rect());
    for (int y = 0; y < plain.height(); ++y) {
        const QRgb* in = reinterpret_cast<const QRgb*>(plain.constScanLine(y));
        uchar* line = out.scanLine(y);
        const bool rowHasClean = y >= clean.top() && y <= clean.bottom();
        for (int x = 0; x < plain.width(); ++x) {
            const QRgb c = in[x];
            int offset = 0;
            if (!(rowHasClean && x >= clean.left() && x <= clean.right())) {
                // -spread/2 .. +spread/2, fixed per pixel position
                offset = ((kBayer[(y & 3) * 4 + (x & 3)] * 2 + 1) * kDitherSpread) / 32 -
                         kDitherSpread / 2;
            }
            line[x] = palette.indexOf(qRed(c) + offset, qGreen(c) + offset, qBlue(c) + offset);
        }
    }
    return out;
}

QList<int> frameDelaysCs(int frames, double seconds)
{
    QList<int> delays;
    if (frames <= 0) {
        return delays;
    }
    const qint64 total = std::llround(seconds * 100.0);
    delays.reserve(frames);
    for (int i = 0; i < frames; ++i) {
        const qint64 from = (i * total * 2 + frames) / (2 * frames);
        const qint64 to = ((i + 1) * total * 2 + frames) / (2 * frames);
        delays << int(to - from);
    }
    return delays;
}

} // namespace gifquant
