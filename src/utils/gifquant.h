// SPDX-License-Identifier: GPL-3.0-or-later
// 256-colour quantisation for the animated-background GIF. One palette is
// shared by every frame (no palette flicker between frames), and the moving
// background is ordered-dithered: the Bayer pattern is fixed to the pixel
// position, so smooth gradients lose their banding without the crawling
// "boil" that error diffusion causes when the content under it moves. The
// static shot is mapped without dither, so its flat UI colours stay clean
// and its pixels come out the same in every frame.

#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QVector>
#include <QtGui/qrgb.h>

namespace gifquant {

class Palette
{
public:
    // Median cut over `samples` (opaque colours), at most 256 entries
    static Palette fromSamples(const QVector<QRgb>& samples);

    const QVector<QRgb>& colors() const { return m_colors; }
    // Index of the nearest entry, through a 64-level-per-channel table that
    // fills as it is used (so one Palette must not be shared across threads)
    uchar indexOf(int r, int g, int b) const;

private:
    void buildLookup();

    static constexpr quint16 kUnset = 0xffff;
    QVector<QRgb> m_colors;
    mutable QVector<quint16> m_lookup; // 64 x 64 x 64
};

// Up to `maxCount` pixels of `image` (inside `area`, or all of it when
// area is null), evenly strided, for Palette::fromSamples
QVector<QRgb> samplePixels(const QImage& image,
                           int maxCount,
                           const QRect& area = QRect());

// Format_Indexed8 copy of `image` with `palette` as its colour table.
// Pixels inside `noDither` map to the nearest colour; the rest are
// ordered-dithered.
QImage quantize(const QImage& image,
                const Palette& palette,
                const QRect& noDither = QRect());

// GIF delays are whole hundredths of a second, so 1/15 s cannot be stored.
// Delays (in 1/100 s) for `frames` evenly spaced frames that add up to
// exactly `seconds`: frame i is shown from round(i * T / n) to
// round((i + 1) * T / n), T the total in 1/100 s.
QList<int> frameDelaysCs(int frames, double seconds);

} // namespace gifquant
