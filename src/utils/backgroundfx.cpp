// SPDX-License-Identifier: GPL-3.0-or-later

#include "backgroundfx.h"

#include <QLinearGradient>
#include <QPainter>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace {

// ---- blur --------------------------------------------------------------

void blurLine(uint32_t* base, int length, int stride, int radius, uint32_t* tmp)
{
    const uint32_t window = uint32_t(2 * radius + 1);
    // Fixed-point reciprocal keeps the loop free of divisions; rounded, and
    // clamped so a full window of 255 stays 255
    const uint64_t mul = ((uint64_t(1) << 16) + window / 2) / window;
    auto avg = [mul](uint32_t sum) {
        return uint32_t(std::min<uint64_t>((sum * mul + 0x8000) >> 16, 255));
    };
    std::array<uint32_t, 4> sum{ 0, 0, 0, 0 };
    auto px = [&](int i) { return base[std::clamp(i, 0, length - 1) * stride]; };
    for (int i = -radius; i <= radius; ++i) {
        const uint32_t p = px(i);
        sum[0] += p & 0xff;
        sum[1] += (p >> 8) & 0xff;
        sum[2] += (p >> 16) & 0xff;
        sum[3] += p >> 24;
    }
    for (int i = 0; i < length; ++i) {
        tmp[i] = avg(sum[0]) | (avg(sum[1]) << 8) | (avg(sum[2]) << 16) |
                 (avg(sum[3]) << 24);
        const uint32_t add = px(i + radius + 1);
        const uint32_t sub = px(i - radius);
        sum[0] += (add & 0xff) - (sub & 0xff);
        sum[1] += ((add >> 8) & 0xff) - ((sub >> 8) & 0xff);
        sum[2] += ((add >> 16) & 0xff) - ((sub >> 16) & 0xff);
        sum[3] += (add >> 24) - (sub >> 24);
    }
    for (int i = 0; i < length; ++i) {
        base[i * stride] = tmp[i];
    }
}

// ---- from screenshot ---------------------------------------------------

// Work size for the blurred backdrop; the result is scaled up to the canvas
// cfn: blurred at 256 px long side then upscaled bilinear; raise it if
// big canvases show blocky gradients in the backdrop
constexpr int kBackdropLongSide = 256;
constexpr double kBackdropBlur = 0.04; // of the long side, per pass
constexpr int kBackdropPasses = 3;
constexpr float kBackdropSaturation = 1.4f;
constexpr float kBackdropDarken = 0.86f;

uint32_t hash(uint32_t x, uint32_t y)
{
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

} // namespace

namespace backgroundfx {

void boxBlur(QImage& image, int radius, int passes)
{
    if (radius <= 0 || image.isNull()) {
        return;
    }
    const int w = image.width();
    const int h = image.height();
    const int stride = int(image.bytesPerLine() / 4);
    uint32_t* bits = reinterpret_cast<uint32_t*>(image.bits());
    QVector<uint32_t> tmp(std::max(w, h));
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < h; ++y) {
            blurLine(bits + y * stride, w, 1, radius, tmp.data());
        }
        for (int x = 0; x < w; ++x) {
            blurLine(bits + x, h, stride, radius, tmp.data());
        }
    }
}

QImage fromScreenshot(QSize canvas, const QImage& shot)
{
    QImage out(canvas, QImage::Format_RGB32);
    if (shot.isNull() || canvas.isEmpty()) {
        out.fill(Qt::black);
        return out;
    }
    const int longSide = std::max(canvas.width(), canvas.height());
    const double scale = std::min(1.0, double(kBackdropLongSide) / longSide);
    const QSize small(std::max(1, int(std::lround(canvas.width() * scale))),
                      std::max(1, int(std::lround(canvas.height() * scale))));
    // Cover: scale the shot to fill `small`, crop the overflow evenly
    QImage plain = shot;
    plain.setDevicePixelRatio(1.0);
    const QSize covered = plain.size().scaled(small, Qt::KeepAspectRatioByExpanding);
    if (plain.width() > 8 * covered.width() && plain.height() > 8 * covered.height()) {
        plain = plain.scaled(covered * 4, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    QImage work(small, QImage::Format_RGB32);
    work.fill(Qt::black);
    {
        QPainter p(&work);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QImage scaled =
          plain.scaled(covered, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.drawImage((small.width() - covered.width()) / 2,
                    (small.height() - covered.height()) / 2,
                    scaled);
    }
    const int radius = std::max(
      1, int(std::lround(kBackdropBlur * std::max(small.width(), small.height()))));
    boxBlur(work, radius, kBackdropPasses);
    for (int y = 0; y < work.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(work.scanLine(y));
        for (int x = 0; x < work.width(); ++x) {
            const float r = qRed(line[x]);
            const float g = qGreen(line[x]);
            const float b = qBlue(line[x]);
            const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            auto adjust = [&](float c) {
                const float v = (luma + kBackdropSaturation * (c - luma)) *
                                kBackdropDarken;
                return std::clamp(int(v + 0.5f), 0, 255);
            };
            line[x] = qRgb(adjust(r), adjust(g), adjust(b));
        }
    }
    if (work.size() == canvas) {
        return work;
    }
    return work.scaled(canvas, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
      .convertToFormat(QImage::Format_RGB32);
}

void addGrain(QImage& image, int amount)
{
    if (amount <= 0 || image.isNull()) {
        return;
    }
    if (image.format() != QImage::Format_RGB32 &&
        image.format() != QImage::Format_ARGB32_Premultiplied) {
        image.convertTo(QImage::Format_ARGB32_Premultiplied);
    }
    // Up to +-24 levels at 100; triangular noise looks like film grain
    const int amp = std::clamp(amount, 0, 100) * 24 / 100;
    if (amp == 0) {
        return;
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const uint32_t h = hash(uint32_t(x), uint32_t(y));
            // Sum of two 8-bit uniforms: -255..255, peaked at 0
            const int n = int(h & 0xff) + int((h >> 8) & 0xff) - 255;
            const int delta = n * amp / 255;
            const QRgb c = line[x];
            const int a = qAlpha(c);
            // Premultiplied: keep each channel within its alpha
            auto add = [&](int v) { return std::clamp(v + delta * a / 255, 0, a); };
            line[x] = qRgba(add(qRed(c)), add(qGreen(c)), add(qBlue(c)), a);
        }
    }
}

QImage renderBackground(QSize canvas,
                        const EffectSettings& settings,
                        const QImage& shot)
{
    QImage out;
    switch (settings.backgroundMode) {
        case BackgroundMode::None:
            out = QImage(canvas, QImage::Format_ARGB32_Premultiplied);
            out.fill(Qt::transparent);
            return out; // nothing to grain
        case BackgroundMode::Solid:
            out = QImage(canvas, QImage::Format_RGB32);
            out.fill(settings.backgroundColor1);
            break;
        case BackgroundMode::Gradient: {
            out = QImage(canvas, QImage::Format_RGB32);
            const auto colors =
              settings.autoColors
                ? autoGradientColors(shot)
                : std::pair{ settings.backgroundColor1, settings.backgroundColor2 };
            QPainter p(&out);
            QLinearGradient gradient(0, 0, out.width(), out.height());
            gradient.setColorAt(0, colors.first);
            gradient.setColorAt(1, colors.second);
            p.fillRect(out.rect(), gradient);
            break;
        }
        case BackgroundMode::FromScreenshot:
            out = fromScreenshot(canvas, shot);
            break;
    }
    addGrain(out, settings.grain);
    return out;
}

} // namespace backgroundfx
