// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the Liquid Glass bezel: it must read as glass (vibrancy, sheen,
// specular edge all round, inner shadow, a soft drop shadow, faint noise),
// not as a flat transparent layer.

#include "utils/glassframe.h"
#include "utils/beautifylayout.h"
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

QImage withBezel(const QImage& background, const QImage& content = QImage())
{
    QImage canvas = background;
    glassframe::drawBezel(
      canvas, background, kBezel, kOuterRadius, kShot, kInnerRadius, 1.0, content);
    return canvas;
}

// Rec. 709 luma of a pixel, 0..255
double luma(QRgb c)
{
    return 0.2126 * qRed(c) + 0.7152 * qGreen(c) + 0.0722 * qBlue(c);
}

// Mean luma of the glass body on one side of the ring: a band along the
// side's centre line, clear of the rims and the rounded corners
double sideLuma(const QImage& img, char side)
{
    const int mid = kThickness / 2;
    double sum = 0;
    int n = 0;
    auto add = [&](int x, int y) {
        sum += luma(img.pixel(x, y));
        ++n;
    };
    for (int d = mid - 2; d <= mid + 2; ++d) {
        if (side == 't' || side == 'b') {
            const int y = side == 't' ? kBezel.top() + d : kBezel.bottom() - d;
            for (int x = kShot.left() + 20; x <= kShot.right() - 20; ++x) {
                add(x, y);
            }
        } else {
            const int x = side == 'l' ? kBezel.left() + d : kBezel.right() - d;
            for (int y = kShot.top() + 20; y <= kShot.bottom() - 20; ++y) {
                add(x, y);
            }
        }
    }
    return sum / n;
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

// The review samples: a light window with a title bar and some text lines,
// beautified with glass on each of these backdrops
struct SampleBg
{
    const char* name;
    BackgroundMode mode;
    QColor a, b;
};

const SampleBg kSampleBgs[] = {
    { "midnight",
      BackgroundMode::Gradient,
      QColor(0x0F, 0x17, 0x2A),
      QColor(0x6D, 0x28, 0xD9) },
    { "pastel",
      BackgroundMode::Gradient,
      QColor(0xFB, 0xC2, 0xEB),
      QColor(0xA6, 0xC1, 0xEE) },
    { "screenshot", BackgroundMode::FromScreenshot, QColor(), QColor() },
    { "flat-dark", BackgroundMode::Solid, QColor(0x1C, 0x1C, 0x22), QColor() },
    { "flat-light", BackgroundMode::Solid, QColor(0xEE, 0xEF, 0xF2), QColor() },
};
constexpr int kSampleBgCount = int(sizeof(kSampleBgs) / sizeof(kSampleBgs[0]));

QImage sampleShot()
{
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
    return shot;
}

EffectSettings sampleSettings(const SampleBg& bg)
{
    EffectSettings s;
    s.paddingPx = 48;
    s.cornerRadius = 10;
    s.glass = true;
    s.glassPx = 16;
    s.shadow = true;
    s.backgroundMode = bg.mode;
    s.backgroundColor1 = bg.a;
    s.backgroundColor2 = bg.b;
    return s;
}

QImage physicalCopy(QImage img)
{
    img.setDevicePixelRatio(1.0);
    return img;
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

    // Bug: "glass is still not quite there on the visibility": over a dark or
    // a light backdrop the pane took the backdrop's own colour, so only a
    // thin rim showed where it was. The body must be clearly lighter than a
    // dark backdrop and clearly darker than a light one, on every side (the
    // old sheen faded out towards the bottom).
    void bodyStandsOffDarkAndLightBackdrops_bugBezelBlendsIn_data()
    {
        QTest::addColumn<QColor>("bg");
        QTest::addColumn<bool>("dark");
        QTest::newRow("flat near-black") << QColor(28, 28, 34) << true;
        QTest::newRow("midnight navy") << QColor(15, 23, 42) << true;
        QTest::newRow("deep purple") << QColor(76, 36, 160) << true;
        QTest::newRow("mid grey") << QColor(128, 128, 128) << true;
        QTest::newRow("flat light") << QColor(238, 239, 242) << false;
        QTest::newRow("pastel pink") << QColor(251, 194, 235) << false;
        QTest::newRow("pastel blue") << QColor(166, 193, 238) << false;
        QTest::newRow("white") << QColor(255, 255, 255) << false;
    }
    void bodyStandsOffDarkAndLightBackdrops_bugBezelBlendsIn()
    {
        QFETCH(QColor, bg);
        QFETCH(bool, dark);
        const QImage out = withBezel(plain(bg));
        const double base = luma(bg.rgb());
        for (char side : { 't', 'b', 'l', 'r' }) {
            const double body = sideLuma(out, side);
            const double step = dark ? body - base : base - body;
            QVERIFY2(step >= (dark ? 22 : 18),
                     qPrintable(QStringLiteral("side %1: body %2 backdrop %3 step %4")
                                  .arg(side)
                                  .arg(body)
                                  .arg(base)
                                  .arg(step)));
        }
    }

    // A thin bright rim outlines the pane on dark and light backdrops alike:
    // strongest on the lit (top-left) side, weaker but still there opposite
    void rimStandsOutOnDarkAndLight_data()
    {
        QTest::addColumn<QColor>("bg");
        QTest::newRow("dark") << QColor(28, 28, 34);
        QTest::newRow("light") << QColor(238, 239, 242);
        QTest::newRow("pastel") << QColor(251, 194, 235);
    }
    void rimStandsOutOnDarkAndLight()
    {
        QFETCH(QColor, bg);
        const QImage out = withBezel(plain(bg));
        const int x = kBezel.center().x();
        const double topRim = luma(out.pixel(x, kBezel.top()));
        const double topBody = sideLuma(out, 't');
        QVERIFY2(topRim >= topBody + 25,
                 qPrintable(
                   QStringLiteral("top rim %1 body %2").arg(topRim).arg(topBody)));
        const double bottomRim = luma(out.pixel(x, kBezel.bottom()));
        const double bottomBody = sideLuma(out, 'b');
        QVERIFY2(bottomRim >= bottomBody + 10,
                 qPrintable(QStringLiteral("bottom rim %1 body %2")
                              .arg(bottomRim)
                              .arg(bottomBody)));
        QVERIFY2(
          topRim > bottomRim,
          qPrintable(QStringLiteral("top %1 bottom %2").arg(topRim).arg(bottomRim)));
    }

    // Bug: lensing a smooth backdrop gives back the same smooth backdrop, so
    // the bevel never looked thick. The outer bevel now bends the shot's own
    // edge into view; the flat glass next to the shot stays clear, so the
    // shot's edge is not smeared.
    void bevelRefractsTheShot_bugLensingInvisible()
    {
        const QImage bg = plain(QColor(40, 40, 48));
        QImage red(kShot.size(), QImage::Format_ARGB32_Premultiplied);
        red.fill(QColor(230, 30, 30));
        const QImage with = withBezel(bg, red);
        const QImage without = withBezel(bg);
        const int y = kBezel.center().y();
        auto redness = [](QRgb c) { return qRed(c) - qGreen(c); };
        // Outer bevel, left and right
        for (int x : { kBezel.left() + 3, kBezel.right() - 3 }) {
            const int gain = redness(with.pixel(x, y)) - redness(without.pixel(x, y));
            QVERIFY2(gain >= 30,
                     qPrintable(QStringLiteral("x %1 gain %2").arg(x).arg(gain)));
        }
        // Flat glass beside the shot: untouched by the content
        for (int x = kShot.left() - 6; x <= kShot.left() - 3; ++x) {
            const int gain =
              std::abs(redness(with.pixel(x, y)) - redness(without.pixel(x, y)));
            QVERIFY2(gain <= 3,
                     qPrintable(QStringLiteral("x %1 gain %2").arg(x).arg(gain)));
        }
    }

    // The bevel shows content from inside the shot, not just its border
    // pixel smeared outward: a lens bends what lies a little way in
    void bevelShowsContentFromInsideTheShot()
    {
        const QImage bg = plain(QColor(40, 40, 48));
        // A thin blue frame round a green shot
        QImage shot(kShot.size(), QImage::Format_ARGB32_Premultiplied);
        shot.fill(QColor(20, 200, 40));
        {
            QPainter p(&shot);
            const QRect r = shot.rect();
            const QColor blue(30, 40, 230);
            p.fillRect(QRect(0, 0, r.width(), 2), blue);
            p.fillRect(QRect(0, r.height() - 2, r.width(), 2), blue);
            p.fillRect(QRect(0, 0, 2, r.height()), blue);
            p.fillRect(QRect(r.width() - 2, 0, 2, r.height()), blue);
        }
        const QImage out = withBezel(bg, shot);
        const QRgb c = out.pixel(kBezel.left() + 2, kBezel.center().y());
        QVERIFY2(qGreen(c) > qBlue(c) + 10, qPrintable(describe(QColor(c))));
    }

    // Light lip: a thin bright line just outside the shot's edge, with the
    // dark hairline right against the shot
    void lightLipBesideTheShot()
    {
        const QImage out = withBezel(plain(QColor(28, 28, 34)));
        const int y = kBezel.center().y();
        // Right side: the shot ends at kShot.right(); hairline, then lip
        const double hairline = luma(out.pixel(kShot.right() + 1, y));
        const double lip = luma(out.pixel(kShot.right() + 2, y));
        const double beyond = luma(out.pixel(kShot.right() + 6, y));
        QVERIFY2(lip >= beyond + 20 && lip >= hairline + 20,
                 qPrintable(QStringLiteral("hairline %1 lip %2 beyond %3")
                              .arg(hairline)
                              .arg(lip)
                              .arg(beyond)));
    }

    // Two shadow layers: a tight dark contact edge right under the pane and
    // a broad soft one further out
    void contactShadowIsTightAndDark()
    {
        const QColor bg(28, 28, 34);
        const QImage out = withBezel(plain(bg));
        const int x = kBezel.center().x();
        const double base = luma(bg.rgb());
        const double contact = luma(out.pixel(x, kBezel.bottom() + 2));
        QVERIFY2(
          contact <= base * 0.65,
          qPrintable(QStringLiteral("contact %1 backdrop %2").arg(contact).arg(base)));
        const int reach = glassframe::outset(kBezel, kShot, 1.0);
        const double broad = luma(out.pixel(x, kBezel.bottom() + reach / 2));
        QVERIFY2(broad < base - 1 && broad > contact,
                 qPrintable(QStringLiteral("broad %1 contact %2 backdrop %3")
                              .arg(broad)
                              .arg(contact)
                              .arg(base)));
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

    // Bug: "still not quite there yet on the visibility". Through the real
    // Beautify pipeline, on each sample backdrop (Midnight, pastel, From
    // screenshot, flat dark, flat light), the glass body differs clearly from
    // the backdrop right outside it.
    void sampleBackdropsShowTheGlass_bugGlassNotVisible_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < kSampleBgCount; ++i) {
            QTest::newRow(kSampleBgs[i].name) << i;
        }
    }
    void sampleBackdropsShowTheGlass_bugGlassNotVisible()
    {
        QFETCH(int, index);
        const QImage shot = sampleShot();
        EffectSettings s = sampleSettings(kSampleBgs[index]);
        s.shadow = false; // measure the glass, not the shot's own shadow
        const QImage out = physicalCopy(imageeffects::applyEffects(shot, s));
        const BeautifyLayout layout = beautifyLayout(shot.size(), s, 2.0);
        const QRect bezel = layout.bezel;
        const int g = layout.shot.left() - bezel.left();
        const int reach = glassframe::outset(bezel, layout.shot, 2.0);
        // Left and right sides, middle third of the height
        for (bool left : { true, false }) {
            double body = 0;
            double backdrop = 0;
            int n = 0;
            for (int y = bezel.top() + bezel.height() / 3;
                 y <= bezel.bottom() - bezel.height() / 3;
                 ++y) {
                const int mid = left ? bezel.left() + g / 2 : bezel.right() - g / 2;
                const int outside =
                  left ? bezel.left() - reach - 2 : bezel.right() + reach + 2;
                body += luma(out.pixel(mid, y));
                backdrop += luma(out.pixel(outside, y));
                ++n;
            }
            body /= n;
            backdrop /= n;
            QVERIFY2(std::abs(body - backdrop) >= 18,
                     qPrintable(QStringLiteral("%1 side: body %2 backdrop %3")
                                  .arg(left ? "left" : "right")
                                  .arg(body)
                                  .arg(backdrop)));
        }
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
        const QImage shot = sampleShot();
        for (const SampleBg& b : kSampleBgs) {
            const EffectSettings s = sampleSettings(b);
            QImage out = imageeffects::applyEffects(shot, s);
            out.setDevicePixelRatio(1.0);
            QVERIFY(out.save(
              QStringLiteral("%1/%2-%3.png").arg(dir, QString::fromLatin1(b.name), tag)));
            // The lit top-left and the far bottom-right corners, enlarged
            const QSize crop(240, 180);
            const QRect corners[] = {
                QRect(QPoint(100, 110), crop),
                QRect(QPoint(out.width() - 340, out.height() - 290), crop),
            };
            const char* where[] = { "tl", "br" };
            for (int i = 0; i < 2; ++i) {
                QVERIFY(out.copy(corners[i])
                          .scaled(crop * 3, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                          .save(QStringLiteral("%1/zoom-%2-%3-%4.png")
                                  .arg(dir,
                                       QString::fromLatin1(b.name),
                                       QString::fromLatin1(where[i]),
                                       tag)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestGlassFrame)
#include "tst_glassframe.moc"
