// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the Liquid Glass bezel: it must read as glass (vibrancy, sheen,
// specular edge all round, inner shadow, a soft drop shadow, faint noise),
// not as a flat transparent layer.

#include "utils/glassframe.h"
#include "utils/imageeffects.h"

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <algorithm>
#include <set>

namespace {

const QRect kBezel(60, 50, 280, 180);
const int kThickness = 20;
const QRect kShot = kBezel.adjusted(kThickness, kThickness, -kThickness, -kThickness);
const qreal kOuterRadius = 28;
const qreal kInnerRadius = 8;

QImage plain(const QColor& color, QSize size = QSize(400, 280))
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

QImage withBezel(const QImage& background)
{
    QImage canvas = background;
    glassframe::drawBezel(
      canvas, background, kBezel, kOuterRadius, kShot, kInnerRadius, 1.0);
    return canvas;
}

// Mean HSL lightness of a horizontal run of pixels
double meanLightness(const QImage& img, int y, int x0, int x1)
{
    double sum = 0;
    for (int x = x0; x <= x1; ++x) {
        sum += img.pixelColor(x, y).lightness();
    }
    return sum / (x1 - x0 + 1);
}

QString describe(const QColor& c)
{
    return QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

class TestGlassFrame : public QObject
{
    Q_OBJECT

private slots:
    // Bug: "the glass frame just looks like a transparent layer", then
    // "make the glass bolder": thresholds raised for the bolder look. A white
    // tint over a blurred copy of the background washes colour out; real
    // glass (vibrancy) makes what is behind it richer.
    void glassIsMoreSaturatedThanTheBackground_bugFlatTransparentLayer()
    {
        const QColor bg(90, 130, 200);
        const QImage out = withBezel(plain(bg));
        // Centre line of the left side of the ring, mid height
        const QColor glass =
          out.pixelColor(kBezel.left() + kThickness / 2, kBezel.center().y());
        QVERIFY2(glass.hsvSaturationF() > bg.hsvSaturationF() + 0.08,
                 qPrintable(describe(glass)));
    }

    // A sheen: the top of the pane catches more light than the bottom
    void topBandIsBrighterThanBottomBand()
    {
        const QImage out = withBezel(plain(QColor(128, 128, 128)));
        const int x0 = kShot.left() + 20;
        const int x1 = kShot.right() - 20;
        const double top = meanLightness(out, kBezel.top() + kThickness / 2, x0, x1);
        const double bottom =
          meanLightness(out, kBezel.bottom() - kThickness / 2, x0, x1);
        QVERIFY2(top > bottom + 16,
                 qPrintable(QStringLiteral("top %1 bottom %2").arg(top).arg(bottom)));
    }

    // The bright edge runs all round: strongest top-left, still visible at
    // the bottom-right
    void specularEdgeIsVisibleAtTheBottomRight()
    {
        const QImage out = withBezel(plain(QColor(128, 128, 128)));
        const int y = kBezel.center().y();
        const QColor edge = out.pixelColor(kBezel.right(), y);
        const QColor middle = out.pixelColor(kBezel.right() - kThickness / 2, y);
        QVERIFY2(edge.lightness() >= middle.lightness() + 25,
                 qPrintable(describe(edge) + " " + describe(middle)));
        const QColor leftEdge = out.pixelColor(kBezel.left(), y);
        QVERIFY2(leftEdge.lightness() > edge.lightness(),
                 qPrintable(describe(leftEdge) + " " + describe(edge)));
    }

    // The shot sits in the glass: a soft shadow just outside it on the ring
    void innerShadowAlongTheShot()
    {
        const QImage out = withBezel(plain(QColor(128, 128, 128)));
        // Below the shot (the bottom inner edge faces away from the light,
        // so no highlight competes there)
        const int x = kBezel.center().x();
        const QColor nearShot = out.pixelColor(x, kShot.bottom() + 4);
        const QColor middle = out.pixelColor(x, kShot.bottom() + kThickness / 2);
        QVERIFY2(nearShot.lightness() + 4 < middle.lightness(),
                 qPrintable(describe(nearShot) + " " + describe(middle)));
    }

    // The pane lifts off the background: a soft shadow just outside the
    // bezel, heavier below (light from above), gone past outset()
    void dropShadowDarkensJustOutsideTheBezel()
    {
        const QColor bg(200, 200, 200);
        const QImage out = withBezel(plain(bg));
        const int x = kBezel.center().x();
        const QColor below = out.pixelColor(x, kBezel.bottom() + 2);
        const QColor above = out.pixelColor(x, kBezel.top() - 2);
        QVERIFY2(below.lightness() <= bg.lightness() - 14, qPrintable(describe(below)));
        QVERIFY2(below.lightness() < above.lightness(),
                 qPrintable(describe(below) + " " + describe(above)));
        const int reach = glassframe::outset(kBezel, kShot, 1.0);
        QVERIFY(reach >= 3);
        // Nothing changes beyond the reach, on any side
        const QRect painted = kBezel.adjusted(-reach, -reach, reach, reach);
        int touched = 0;
        for (int yy = 0; yy < out.height(); ++yy) {
            for (int xx = 0; xx < out.width(); ++xx) {
                if (!painted.contains(xx, yy) && out.pixel(xx, yy) != bg.rgb()) {
                    ++touched;
                }
            }
        }
        QCOMPARE(touched, 0);
    }

    // Faint noise breaks up banding, without visible speckle
    void faintNoiseOverTheGlass()
    {
        const QImage out = withBezel(plain(QColor(128, 128, 128)));
        // Along the top side's centre line everything else is constant
        const int y = kBezel.top() + kThickness / 2;
        std::set<QRgb> values;
        int lo = 255;
        int hi = 0;
        for (int x = kShot.left() + 20; x <= kShot.right() - 20; ++x) {
            const QRgb c = out.pixel(x, y);
            values.insert(c);
            lo = std::min(lo, qGray(c));
            hi = std::max(hi, qGray(c));
        }
        QVERIFY2(values.size() > 3, qPrintable(QString::number(values.size())));
        QVERIFY2(hi - lo <= 8, qPrintable(QStringLiteral("%1..%2").arg(lo).arg(hi)));
    }

    // Same input, same pixels: the animated export relies on it
    void deterministic()
    {
        const QImage bg = plain(QColor(70, 140, 90));
        QVERIFY(withBezel(bg) == withBezel(bg));
    }

    // Before/after pictures for review: set PIXIE_GLASS_SAMPLES to a folder
    void writeSamples()
    {
        const QString dir = qEnvironmentVariable("PIXIE_GLASS_SAMPLES");
        if (dir.isEmpty()) {
            QSKIP("PIXIE_GLASS_SAMPLES not set");
        }
        const QString tag = qEnvironmentVariable("PIXIE_GLASS_TAG", "after");
        QDir().mkpath(dir);
        // A light window with a title bar and some text lines
        QImage shot(640, 400, QImage::Format_ARGB32_Premultiplied);
        shot.fill(QColor(246, 247, 250));
        {
            QPainter p(&shot);
            p.fillRect(QRect(0, 0, 640, 34), QColor(52, 120, 246));
            p.fillRect(QRect(0, 34, 140, 366), QColor(232, 236, 244));
            for (int i = 0; i < 6; ++i) {
                p.fillRect(QRect(180, 70 + i * 44, 360, 12), QColor(60, 64, 72));
            }
            p.fillRect(QRect(470, 340, 120, 30), QColor(236, 72, 96));
            p.fillRect(QRect(180, 340, 120, 30), QColor(34, 197, 94));
        }
        shot.setDevicePixelRatio(2.0);
        EffectSettings s;
        s.paddingPx = 48;
        s.cornerRadius = 10;
        s.glass = true;
        s.glassPx = 16;
        s.shadow = true;
        struct Bg
        {
            const char* name;
            BackgroundMode mode;
            QColor a, b;
        };
        const Bg bgs[] = {
            { "midnight",
              BackgroundMode::Gradient,
              QColor(0x0F, 0x17, 0x2A),
              QColor(0x6D, 0x28, 0xD9) },
            { "pastel",
              BackgroundMode::Gradient,
              QColor(0xFB, 0xC2, 0xEB),
              QColor(0xA6, 0xC1, 0xEE) },
            { "screenshot", BackgroundMode::FromScreenshot, QColor(), QColor() },
        };
        for (const Bg& b : bgs) {
            s.backgroundMode = b.mode;
            s.backgroundColor1 = b.a;
            s.backgroundColor2 = b.b;
            QImage out = imageeffects::applyEffects(shot, s);
            out.setDevicePixelRatio(1.0);
            QVERIFY(out.save(
              QStringLiteral("%1/%2-%3.png").arg(dir, QString::fromLatin1(b.name), tag)));
        }
    }
};

QTEST_GUILESS_MAIN(TestGlassFrame)
#include "tst_glassframe.moc"
