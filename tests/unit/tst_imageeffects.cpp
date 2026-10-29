// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the pure image effects (border, rounded corners, drop shadow).

#include "sanitizer.h"
#include "utils/backgroundfx.h"
#include "utils/beautifylayout.h"
#include "utils/beautifypalette.h"
#include "utils/imageeffects.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QThread>
#include <QTest>
#include <algorithm>
#include <cmath>

namespace {

QImage solid(int w, int h, const QColor& color, qreal dpr = 1.0)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(color);
    img.setDevicePixelRatio(dpr);
    return img;
}

// Left half one color, right half another
QImage halves(int w, int h, const QColor& left, const QColor& right)
{
    QImage img = solid(w, h, left);
    QPainter p(&img);
    p.fillRect(QRect(w / 2, 0, w - w / 2, h), right);
    return img;
}

// A light window with a title bar, sidebar, buttons and text lines
QImage uiLike(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(246, 247, 250));
    QPainter p(&img);
    p.fillRect(QRect(0, 0, w, h / 12), QColor(52, 120, 246));
    p.fillRect(QRect(0, h / 12, w / 5, h), QColor(232, 236, 244));
    for (int i = 0; i < 6; ++i) {
        p.fillRect(QRect(w / 4, h / 6 + i * h / 9, w / 2, h / 40),
                   QColor(60, 64, 72));
    }
    p.fillRect(
      QRect(w * 3 / 4, h * 5 / 6, w / 6, h / 14), QColor(236, 72, 96));
    p.fillRect(QRect(w / 4, h * 5 / 6, w / 6, h / 14), QColor(34, 197, 94));
    return img;
}

QColor meanColor(const QImage& img, const QRect& exclude = QRect())
{
    double r = 0, g = 0, b = 0;
    qint64 n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (exclude.contains(x, y)) {
                continue;
            }
            const QRgb c = img.pixel(x, y);
            r += qRed(c);
            g += qGreen(c);
            b += qBlue(c);
            ++n;
        }
    }
    return n == 0 ? QColor() : QColor(int(r / n), int(g / n), int(b / n));
}

int rgbDistance(const QColor& a, const QColor& b)
{
    const int dr = a.red() - b.red();
    const int dg = a.green() - b.green();
    const int db = a.blue() - b.blue();
    return int(std::sqrt(double(dr * dr + dg * dg + db * db)));
}

QString describe(const QColor& c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

// True when `rect` of `a` equals all of `b`, pixel for pixel
bool sameRegion(const QImage& a, const QRect& rect, const QImage& b)
{
    for (int y = 0; y < rect.height(); ++y) {
        for (int x = 0; x < rect.width(); ++x) {
            if (a.pixel(rect.x() + x, rect.y() + y) != b.pixel(x, y)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

class TestImageEffects : public QObject
{
    Q_OBJECT
private slots:
    void identityWhenOff()
    {
        const QImage src = solid(40, 30, QColor(10, 200, 30));
        const QImage out = imageeffects::applyEffects(src, EffectSettings{});
        QCOMPARE(out.size(), src.size());
        QCOMPARE(out, src);
    }

    void borderGrowsCanvas()
    {
        const QImage src = solid(40, 30, Qt::white);
        const QImage out = imageeffects::addBorder(src, 5, QColor(255, 0, 0));
        QCOMPARE(out.size(), QSize(50, 40));
        QCOMPARE(out.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(out.pixelColor(49, 39), QColor(255, 0, 0));
        QCOMPARE(out.pixelColor(25, 20), QColor(Qt::white));
    }

    void roundedCorners()
    {
        const QImage src = solid(60, 60, QColor(0, 0, 255));
        const QImage out = imageeffects::roundCorners(src, 12);
        QCOMPARE(out.size(), src.size());
        QCOMPARE(out.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(out.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(out.pixelColor(59, 59).alpha(), 0);
        QCOMPARE(out.pixelColor(30, 30), QColor(0, 0, 255));
    }

    void shadowGrowsCanvas()
    {
        const QImage src = solid(40, 30, Qt::white);
        const QPoint offset(0, 6);
        const int blur = 8;
        const QImage out = imageeffects::dropShadow(
          src, blur, offset, QColor(0, 0, 0, 200));
        QVERIFY(out.width() >= src.width() + 2 * blur);
        QVERIFY(out.height() >= src.height() + 2 * blur + offset.y());
        // Find the original: the centre of the opaque image stays opaque white
        // and a pixel just below the image carries partial shadow alpha.
        const int ox = (out.width() - src.width()) / 2;
        // Image is placed so that shadow room is symmetric minus the offset.
        int top = -1;
        for (int y = 0; y < out.height(); ++y) {
            if (out.pixelColor(out.width() / 2, y) == QColor(Qt::white)) {
                top = y;
                break;
            }
        }
        QVERIFY(top >= 0);
        QVERIFY(ox >= 0);
        const int below = top + src.height() + 2;
        QVERIFY(below < out.height());
        const int alpha = out.pixelColor(out.width() / 2, below).alpha();
        QVERIFY2(alpha > 0 && alpha < 255, qPrintable(QString::number(alpha)));
    }

    void combinedOrder()
    {
        const QImage src = solid(50, 50, Qt::white);
        EffectSettings s;
        s.borderPx = 4;
        s.borderColor = QColor(255, 0, 0);
        s.cornerRadius = 10;
        s.shadow = true;
        s.shadowBlur = 6;
        s.shadowOffset = QPoint(0, 4);
        const QImage out = imageeffects::applyEffects(src, s);
        // Border first, then shadow around the bordered image.
        QVERIFY(out.width() >= 58 + 12);
        QVERIFY(out.height() >= 58 + 12 + 4);
        // Centre is the original image; border colour sits between.
        QCOMPARE(out.pixelColor(out.width() / 2, out.height() / 2),
                 QColor(Qt::white));
        // Corners of the canvas are not opaque (shadow/rounding).
        QVERIFY(out.pixelColor(0, 0).alpha() < 255);
        // Walking down the centre column we meet red border before white.
        bool sawRed = false;
        for (int y = 0; y < out.height() / 2; ++y) {
            if (out.pixelColor(out.width() / 2, y) == QColor(255, 0, 0)) {
                sawRed = true;
            }
        }
        QVERIFY(sawRed);
    }

    void dprScalesBorder()
    {
        const QImage src = solid(40, 30, Qt::white, 2.0);
        const QImage out = imageeffects::addBorder(src, 3, QColor(255, 0, 0));
        QCOMPARE(out.size(), QSize(40 + 12, 30 + 12));
        QCOMPARE(out.devicePixelRatio(), 2.0);
        QCOMPARE(out.pixelColor(5, 5), QColor(255, 0, 0));
        QCOMPARE(out.pixelColor(6, 6), QColor(Qt::white));
    }

    void dprPreservedByAllEffects()
    {
        const QImage src = solid(40, 30, Qt::white, 2.0);
        EffectSettings s;
        s.cornerRadius = 4;
        s.shadow = true;
        QCOMPARE(imageeffects::roundCorners(src, 4).devicePixelRatio(), 2.0);
        QCOMPARE(imageeffects::applyEffects(src, s).devicePixelRatio(), 2.0);
    }

    void needsAlphaTable()
    {
        EffectSettings s;
        QVERIFY(!imageeffects::needsAlpha(s));
        s.borderPx = 5;
        QVERIFY(!imageeffects::needsAlpha(s));
        s.cornerRadius = 3;
        QVERIFY(imageeffects::needsAlpha(s));
        s.cornerRadius = 0;
        s.shadow = true;
        QVERIFY(imageeffects::needsAlpha(s));
    }

    void paddingGrowsCanvasByTwicePad()
    {
        const QImage src = solid(40, 30, Qt::white);
        EffectSettings s;
        s.paddingPx = 20;
        s.backgroundMode = BackgroundMode::Solid;
        const QImage out = imageeffects::applyEffects(src, s);
        QCOMPARE(out.size(), QSize(80, 70));
        QCOMPARE(out.pixelColor(40, 35), QColor(Qt::white));
    }

    void paddingAddsToShadowMargins()
    {
        const QImage src = solid(40, 30, Qt::white);
        EffectSettings s;
        s.shadow = true;
        const QImage shadowOnly = imageeffects::applyEffects(src, s);
        s.paddingPx = 10;
        s.backgroundMode = BackgroundMode::Solid;
        const QImage out = imageeffects::applyEffects(src, s);
        QCOMPARE(out.size(), shadowOnly.size() + QSize(20, 20));
    }

    void solidBackgroundFillsCorner()
    {
        const QImage src = solid(40, 30, Qt::white);
        EffectSettings s;
        s.paddingPx = 16;
        s.cornerRadius = 8;
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = QColor(139, 92, 246);
        const QImage out = imageeffects::applyEffects(src, s);
        QCOMPARE(out.pixelColor(0, 0), QColor(139, 92, 246));
        QCOMPARE(out.pixelColor(out.width() - 1, out.height() - 1),
                 QColor(139, 92, 246));
        QCOMPARE(out.pixelColor(0, 0).alpha(), 255);
    }

    void gradientRunsDiagonally()
    {
        const QImage src = solid(10, 10, Qt::white);
        EffectSettings s;
        s.paddingPx = 100;
        s.backgroundMode = BackgroundMode::Gradient;
        s.backgroundColor1 = QColor(255, 0, 0);
        s.backgroundColor2 = QColor(0, 0, 255);
        const QImage out = imageeffects::applyEffects(src, s);
        const QColor tl = out.pixelColor(0, 0);
        const QColor br = out.pixelColor(out.width() - 1, out.height() - 1);
        QVERIFY2(tl.red() > 240 && tl.blue() < 15, qPrintable(tl.name()));
        QVERIFY2(br.blue() > 240 && br.red() < 15, qPrintable(br.name()));
    }

    void backgroundNoneKeepsPaddingTransparent()
    {
        const QImage src = solid(40, 30, Qt::white);
        EffectSettings s;
        s.paddingPx = 10;
        const QImage out = imageeffects::applyEffects(src, s);
        QCOMPARE(out.size(), QSize(60, 50));
        QCOMPARE(out.pixelColor(0, 0).alpha(), 0);
        QVERIFY(imageeffects::needsAlpha(s));
    }

    void paddingScalesWithDpr()
    {
        const QImage src = solid(40, 30, Qt::white, 2.0);
        EffectSettings s;
        s.paddingPx = 5;
        s.backgroundMode = BackgroundMode::Solid;
        const QImage out = imageeffects::applyEffects(src, s);
        QCOMPARE(out.size(), QSize(60, 50));
        QCOMPARE(out.devicePixelRatio(), 2.0);
    }

    void needsAlphaFalseWithBackground()
    {
        EffectSettings s;
        s.cornerRadius = 8;
        s.shadow = true;
        QVERIFY(imageeffects::needsAlpha(s));
        s.backgroundMode = BackgroundMode::Gradient;
        QVERIFY(!imageeffects::needsAlpha(s));
    }

    void aspectRatiosAreExactAndKeepShot_data()
    {
        QTest::addColumn<int>("aspect");
        QTest::addColumn<int>("rw");
        QTest::addColumn<int>("rh");
        QTest::newRow("square") << 1 << 1 << 1;
        QTest::newRow("landscape") << 2 << 16 << 9;
        QTest::newRow("portrait") << 3 << 9 << 16;
        QTest::newRow("social") << 4 << 4 << 5;
        QTest::newRow("classic") << 5 << 4 << 3;
    }

    void aspectRatiosAreExactAndKeepShot()
    {
        QFETCH(int, aspect);
        QFETCH(int, rw);
        QFETCH(int, rh);
        const QImage src = solid(200, 120, QColor(10, 200, 30));
        EffectSettings s;
        s.paddingPx = 10;
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = QColor(255, 0, 0);
        s.aspect = aspect;
        const QImage out = imageeffects::applyEffects(src, s);
        QVERIFY2(out.width() >= 220 && out.height() >= 140,
                 qPrintable(QString("%1x%2").arg(out.width()).arg(out.height())));
        QVERIFY2(std::abs(out.width() * rh - out.height() * rw) <=
                   std::max(rw, rh),
                 qPrintable(QString("%1x%2").arg(out.width()).arg(out.height())));
        // Shot is unscaled and centered: its center pixel and size survive
        QCOMPARE(out.pixelColor(out.width() / 2, out.height() / 2),
                 QColor(10, 200, 30));
        QCOMPARE(out.pixelColor(0, 0), QColor(255, 0, 0));
    }

    void aspectOffLeavesSizeAlone()
    {
        const QImage src = solid(200, 120, Qt::white);
        EffectSettings s;
        s.paddingPx = 10;
        s.backgroundMode = BackgroundMode::Solid;
        QCOMPARE(imageeffects::applyEffects(src, s).size(), QSize(220, 140));
    }

    void aspectWithoutBackgroundExpandsTransparent()
    {
        const QImage src = solid(200, 100, Qt::white);
        EffectSettings s;
        s.aspect = 1;
        const QImage out = imageeffects::applyEffects(src, s);
        QCOMPARE(out.size(), QSize(200, 200));
        QCOMPARE(out.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(out.pixelColor(100, 100), QColor(Qt::white));
        QVERIFY(imageeffects::needsAlpha(s));
    }

    void autoBalanceTrimsToEqualMargins()
    {
        // 100x60 canvas: content 60x40 with 30px white margin left, 5px right
        QImage src = solid(100, 60, Qt::white);
        for (int y = 10; y < 50; ++y) {
            for (int x = 30; x < 95; ++x) {
                src.setPixelColor(x, y, QColor(20, 40, 200));
            }
        }
        const QImage out = imageeffects::autoBalance(src);
        // Margins end up equal horizontally (5px each side) and vertically
        QCOMPARE(out.size(), QSize(75, 40 + 2 * 10));
        QCOMPARE(out.pixelColor(0, 30), QColor(Qt::white));
        QCOMPARE(out.pixelColor(5, 30), QColor(20, 40, 200));
        QCOMPARE(out.pixelColor(out.width() - 6, 30), QColor(20, 40, 200));
        QCOMPARE(out.pixelColor(out.width() - 1, 30), QColor(Qt::white));
    }

    void autoBalanceLeavesNoMarginImageUnchanged()
    {
        // Content reaches every edge, so there is no margin to trim
        QImage src = solid(50, 40, QColor(20, 40, 200));
        for (int i = 0; i < 40; ++i) {
            src.setPixelColor(0, i, Qt::white);
            src.setPixelColor(25, i, Qt::red);
        }
        for (int i = 0; i < 50; ++i) {
            src.setPixelColor(i, 0, Qt::yellow);
            src.setPixelColor(i, 39, Qt::green);
        }
        QCOMPARE(imageeffects::autoBalance(src), src);
    }

    void autoBalanceNeverTrimsMoreThanFortyPercentPerSide()
    {
        const QImage src = solid(100, 100, Qt::white); // all margin
        const QImage out = imageeffects::autoBalance(src);
        QVERIFY(out.width() >= 20 && out.height() >= 20);
        QVERIFY(!out.isNull());
    }

    void autoBalanceRunsFirstInApplyEffects()
    {
        QImage src = solid(100, 60, Qt::white);
        for (int y = 10; y < 50; ++y) {
            for (int x = 30; x < 95; ++x) {
                src.setPixelColor(x, y, QColor(20, 40, 200));
            }
        }
        EffectSettings s;
        s.autoBalance = true;
        QCOMPARE(imageeffects::applyEffects(src, s).size(), QSize(75, 60));
    }

    // --- glass pack -----------------------------------------------------

    void fromScreenshotIsOpaqueAndKeepsShotMean()
    {
        const QImage src = uiLike(300, 200);
        EffectSettings s;
        s.paddingPx = 80;
        s.backgroundMode = BackgroundMode::FromScreenshot;
        const QImage out = imageeffects::applyEffects(src, s);
        QVERIFY(!imageeffects::needsAlpha(s));
        const BeautifyLayout layout = beautifyLayout(src.size(), s, 1.0);
        QCOMPARE(out.size(), layout.canvas);
        for (const QPoint& pt : { QPoint(0, 0),
                                  QPoint(out.width() - 1, out.height() - 1),
                                  QPoint(5, out.height() / 2) }) {
            QCOMPARE(out.pixelColor(pt).alpha(), 255);
        }
        const QColor bg = meanColor(out, layout.shot);
        const QColor shot = meanColor(src);
        QVERIFY2(rgbDistance(bg, shot) < 60,
                 qPrintable(describe(bg) + " vs " + describe(shot)));
        // Not just a copy of the shot's mean: it carries the darkening
        QVERIFY2(bg.lightness() < shot.lightness(),
                 qPrintable(describe(bg) + " vs " + describe(shot)));
        // Blurred: no hard edges survive in the background
        QVERIFY(rgbDistance(out.pixelColor(0, 0), out.pixelColor(1, 1)) < 8);
        // The shot itself is untouched
        QVERIFY(sameRegion(out, layout.frame, src));
    }

    void fromScreenshotKeepsTheShotLayout()
    {
        // Red left, blue right: the backdrop is not a flat fill
        const QImage src =
          halves(200, 100, QColor(220, 40, 40), QColor(40, 60, 220));
        EffectSettings s;
        s.paddingPx = 60;
        s.backgroundMode = BackgroundMode::FromScreenshot;
        const QImage out = imageeffects::applyEffects(src, s);
        const QColor left = out.pixelColor(4, out.height() / 2);
        const QColor right = out.pixelColor(out.width() - 5, out.height() / 2);
        QVERIFY2(left.red() > left.blue() + 60, qPrintable(describe(left)));
        QVERIFY2(right.blue() > right.red() + 60, qPrintable(describe(right)));
    }

    void autoColorsAreDeterministicDistinctAndFromTheImage()
    {
        const QImage src =
          halves(240, 160, QColor(220, 40, 40), QColor(40, 60, 220));
        const auto a = backgroundfx::autoGradientColors(src);
        const auto b = backgroundfx::autoGradientColors(src);
        QCOMPARE(a.first, b.first);
        QCOMPARE(a.second, b.second);
        QVERIFY2(rgbDistance(a.first, a.second) > 80,
                 qPrintable(describe(a.first) + " " + describe(a.second)));
        auto reddish = [](const QColor& c) {
            return c.red() > c.blue() + 40 && c.red() > c.green() + 40;
        };
        auto bluish = [](const QColor& c) {
            return c.blue() > c.red() + 40 && c.blue() > c.green() + 20;
        };
        // Red is on the left, so it starts the top-left to bottom-right run
        QVERIFY2(reddish(a.first) && bluish(a.second),
                 qPrintable(describe(a.first) + " " + describe(a.second)));
    }

    void autoColorsSkipGreysWhenColorExists()
    {
        // Mostly white UI with a blue bar and colored buttons
        const QImage src = uiLike(320, 200);
        const auto c = backgroundfx::autoGradientColors(src);
        QVERIFY2(c.first.hsvSaturationF() > 0.25 &&
                   c.second.hsvSaturationF() > 0.25,
                 qPrintable(describe(c.first) + " " + describe(c.second)));
    }

    void autoColorsOnGreyFallBackSanely()
    {
        QImage src = solid(200, 120, QColor(128, 128, 128));
        {
            QPainter p(&src);
            p.fillRect(QRect(20, 20, 80, 40), QColor(200, 200, 200));
        }
        const auto a = backgroundfx::autoGradientColors(src);
        const auto b = backgroundfx::autoGradientColors(src);
        QCOMPARE(a.first, b.first);
        QCOMPARE(a.second, b.second);
        QVERIFY(a.first.isValid() && a.second.isValid());
        // No invented hue, but still two tones
        QVERIFY2(a.first.hsvSaturationF() < 0.25 &&
                   a.second.hsvSaturationF() < 0.25,
                 qPrintable(describe(a.first) + " " + describe(a.second)));
        QVERIFY2(rgbDistance(a.first, a.second) >= 20,
                 qPrintable(describe(a.first) + " " + describe(a.second)));
    }

    void autoColorsContrastWithTheShotEdge()
    {
        // Mid-grey edge with mid-lightness colors inside: the gradient must
        // move away from the edge's lightness
        QImage src = solid(240, 160, QColor(128, 128, 128));
        {
            QPainter p(&src);
            p.fillRect(QRect(20, 20, 100, 120), QColor(200, 90, 90));
            p.fillRect(QRect(120, 20, 100, 120), QColor(90, 120, 200));
        }
        const auto c = backgroundfx::autoGradientColors(src);
        const qreal bgL = (c.first.lightnessF() + c.second.lightnessF()) / 2;
        const qreal edgeL = QColor(128, 128, 128).lightnessF();
        QVERIFY2(std::abs(bgL - edgeL) >= 0.12,
                 qPrintable(QString::number(bgL) + " " + describe(c.first) +
                            " " + describe(c.second)));
    }

    void autoColorsDriveTheGradient()
    {
        const QImage src =
          halves(100, 60, QColor(220, 40, 40), QColor(40, 60, 220));
        EffectSettings s;
        s.paddingPx = 60;
        s.backgroundMode = BackgroundMode::Gradient;
        s.backgroundColor1 = QColor(0, 255, 0);
        s.backgroundColor2 = QColor(0, 255, 0);
        s.autoColors = true;
        const QImage out = imageeffects::applyEffects(src, s);
        const auto c = backgroundfx::autoGradientColors(src);
        QVERIFY2(rgbDistance(out.pixelColor(0, 0), c.first) < 12,
                 qPrintable(describe(out.pixelColor(0, 0))));
        QVERIFY(rgbDistance(out.pixelColor(out.width() - 1, out.height() - 1),
                            c.second) < 12);
        QVERIFY(imageeffects::backgroundUsesShot(s));
        s.autoColors = false;
        QVERIFY(!imageeffects::backgroundUsesShot(s));
        s.backgroundMode = BackgroundMode::FromScreenshot;
        QVERIFY(imageeffects::backgroundUsesShot(s));
    }

    void glassBezelDiffersFromBackgroundAndShotIsUntouched()
    {
        const QImage src = uiLike(200, 140);
        const QColor fill(120, 90, 200);
        EffectSettings s;
        s.paddingPx = 40;
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = fill;
        s.glass = true;
        s.glassPx = 16;
        const QImage out = imageeffects::applyEffects(src, s);
        const BeautifyLayout layout = beautifyLayout(src.size(), s, 1.0);
        QCOMPARE(out.size(), layout.canvas);
        QCOMPARE(layout.frame.size(), src.size());
        QVERIFY(sameRegion(out, layout.frame, src));
        // Every sampled bezel pixel differs from the plain background
        int changed = 0;
        int samples = 0;
        for (int i = 1; i < 15; ++i) {
            for (const QPoint& pt :
                 { QPoint(layout.frame.center().x(), layout.bezel.top() + i),
                   QPoint(layout.bezel.left() + i, layout.frame.center().y()),
                   QPoint(layout.frame.center().x(),
                          layout.bezel.bottom() - i) }) {
                ++samples;
                if (rgbDistance(out.pixelColor(pt), fill) > 6) {
                    ++changed;
                }
            }
        }
        QCOMPARE(changed, samples);
        // Top-left rim is brighter than the bottom-right rim
        const QColor tl =
          out.pixelColor(layout.bezel.left(), layout.bezel.top() + 30);
        const QColor br =
          out.pixelColor(layout.bezel.right(), layout.bezel.bottom() - 30);
        QVERIFY2(tl.lightness() > br.lightness() + 10,
                 qPrintable(describe(tl) + " " + describe(br)));
        // Outside the bezel the canvas is plain background again
        QCOMPARE(out.pixelColor(0, 0), fill);
        QCOMPARE(out.pixelColor(layout.bezel.left() - 2, layout.bezel.top() - 2),
                 fill);
    }

    // Bug: glass bezel looped forever on a shot 4 physical px wide or less
    // (skipTo < skipFrom on the "straight" rows). Runs on a thread so a
    // regression fails instead of hanging the suite.
    void glassOnNarrowShotTerminates()
    {
        for (const int w : { 1, 3, 4, 5 }) {
            EffectSettings s;
            s.glass = true;
            s.glassPx = 8;
            const QImage src = solid(w, 40, Qt::white);
            QImage out;
            QThread* t = QThread::create(
              [&]() { out = imageeffects::applyEffects(src, s); });
            t->start();
            const bool finished = t->wait(8000);
            if (finished) {
                delete t;
            } // else: leaked on purpose, a spinning thread cannot be joined
            QVERIFY2(finished,
                     qPrintable(QStringLiteral("glass hangs at width %1")
                                  .arg(w)));
            QVERIFY(out.width() > w);
        }
    }

    // Bug: the "Apply effects" workflow action started adding the Beautify
    // padding and gradient to every hotkey capture. It must only ever apply
    // border, corners and shadow, whatever the Beautify defaults are.
    void workflowActionOnlyAppliesBorderCornersShadow_bugGradientByDefault()
    {
        EffectSettings beautifyDefaults; // as ConfigHandler defaults build it
        beautifyDefaults.cornerRadius = 12;
        beautifyDefaults.paddingPx = 64;
        beautifyDefaults.backgroundMode = BackgroundMode::Gradient;
        beautifyDefaults.aspect = 2;
        beautifyDefaults.glass = true;
        beautifyDefaults.grain = 30;
        beautifyDefaults.autoBalance = true;
        const EffectSettings basic = imageeffects::basicEffects(beautifyDefaults);
        QCOMPARE(basic.cornerRadius, 12);
        QCOMPARE(basic.paddingPx, 0);
        QVERIFY(basic.backgroundMode == BackgroundMode::None);
        QCOMPARE(basic.aspect, 0);
        QVERIFY(!basic.glass);
        QCOMPARE(basic.grain, 0);
        QVERIFY(!basic.autoBalance);
        // Output grows by nothing and is not painted over
        const QImage src = solid(100, 60, Qt::white);
        const QImage out = imageeffects::applyEffects(src, basic);
        QCOMPARE(out.size(), src.size());
        QCOMPARE(out.pixelColor(50, 30), QColor(Qt::white));
        QVERIFY(imageeffects::needsAlpha(basic)); // transparent corners
    }

    void glassShadowSitsUnderTheBezel()
    {
        const QImage src = solid(120, 80, Qt::white);
        EffectSettings s;
        s.paddingPx = 30;
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = QColor(200, 200, 200);
        s.glass = true;
        s.glassPx = 12;
        s.shadow = true;
        s.shadowBlur = 12;
        s.shadowOffset = QPoint(0, 8);
        const QImage out = imageeffects::applyEffects(src, s);
        const BeautifyLayout layout = beautifyLayout(src.size(), s, 1.0);
        QCOMPARE(out.size(), layout.canvas);
        QVERIFY(sameRegion(out, layout.frame, src));
        // Just below the bezel the shadow darkens the background
        const QColor below = out.pixelColor(layout.bezel.center().x(),
                                            layout.bezel.bottom() + 4);
        QVERIFY2(below.lightness() < 190, qPrintable(describe(below)));
    }

    void needsAlphaWithGlass()
    {
        EffectSettings s;
        s.glass = true;
        QVERIFY(imageeffects::needsAlpha(s));
        s.backgroundMode = BackgroundMode::FromScreenshot;
        QVERIFY(!imageeffects::needsAlpha(s));
    }

    void grainIsDeterministicAndBackgroundOnly()
    {
        const QImage src = uiLike(160, 100);
        EffectSettings s;
        s.paddingPx = 30;
        s.backgroundMode = BackgroundMode::Gradient;
        const QImage none = imageeffects::applyEffects(src, s);
        s.grain = 0;
        QCOMPARE(imageeffects::applyEffects(src, s), none);
        s.grain = 40;
        const QImage a = imageeffects::applyEffects(src, s);
        const QImage b = imageeffects::applyEffects(src, s);
        QCOMPARE(a, b);
        QVERIFY(a != none);
        const BeautifyLayout layout = beautifyLayout(src.size(), s, 1.0);
        QVERIFY(sameRegion(a, layout.frame, src));
        // Monochrome: a pixel's channels all move by the same amount
        int mono = 0;
        int moved = 0;
        for (int x = 0; x < 40; ++x) {
            const QColor g = a.pixelColor(x, 3);
            const QColor n = none.pixelColor(x, 3);
            const int dr = g.red() - n.red();
            moved += dr != 0 ? 1 : 0;
            if (dr == g.green() - n.green() && dr == g.blue() - n.blue()) {
                ++mono;
            }
        }
        QVERIFY2(moved >= 20, qPrintable(QString::number(moved)));
        QVERIFY2(mono >= 32, qPrintable(QString::number(mono)));
    }

    void grainZeroLeavesTheImageAlone()
    {
        QImage bg(64, 32, QImage::Format_RGB32);
        bg.fill(QColor(90, 100, 110));
        const QImage before = bg;
        backgroundfx::addGrain(bg, 0);
        QCOMPARE(bg, before);
        backgroundfx::addGrain(bg, 50);
        QVERIFY(bg != before);
    }

    // 4000x3000 with every expensive option on. Release bound 400 ms; Debug
    // builds (no NDEBUG) get a generous 4 s so the test still catches
    // order-of-magnitude regressions there.
    void performanceOnLargeCapture()
    {
        SKIP_TIMING_UNDER_SANITIZER();
        const QImage src = uiLike(4000, 3000);
        EffectSettings s;
        s.cornerRadius = 12;
        s.shadow = true;
        s.paddingPx = 64;
        s.backgroundMode = BackgroundMode::FromScreenshot;
        s.glass = true;
        s.grain = 20;
#ifdef NDEBUG
        const qint64 bound = 400;
#else
        const qint64 bound = 4000;
#endif
        QElapsedTimer timer;
        timer.start();
        QImage out = imageeffects::applyEffects(src, s);
        const qint64 fromShot = timer.restart();
        s.backgroundMode = BackgroundMode::Gradient;
        s.autoColors = true;
        out = imageeffects::applyEffects(src, s);
        const qint64 autoColors = timer.elapsed();
        qInfo("4000x3000: from-screenshot+glass+shadow+grain %lld ms, "
              "auto-colors+glass+shadow+grain %lld ms",
              fromShot,
              autoColors);
        QVERIFY(!out.isNull());
        QVERIFY2(fromShot < bound, qPrintable(QString::number(fromShot)));
        QVERIFY2(autoColors < bound, qPrintable(QString::number(autoColors)));
    }

    // Multi-stop palette: the gradient background runs first stop at the
    // top-left corner to last stop at the bottom-right, through the others
    void multiStopGradientHitsEveryStop()
    {
        const QVector<QColor> sunset = beautifypalette::parse(
          QStringLiteral("#FFD601,#FF6118,#EE30FB,#FB76FA,#7232F1"));
        QCOMPARE(sunset.size(), 5);
        EffectSettings s;
        s.backgroundMode = BackgroundMode::Gradient;
        s.palette = sunset;
        const QImage bg = backgroundfx::renderBackground(QSize(401, 401), s, QImage());
        auto close = [](QRgb a, const QColor& b, int tol) {
            return std::abs(qRed(a) - b.red()) <= tol &&
                   std::abs(qGreen(a) - b.green()) <= tol &&
                   std::abs(qBlue(a) - b.blue()) <= tol;
        };
        QVERIFY2(close(bg.pixel(0, 0), sunset.first(), 3),
                 qPrintable(QColor(bg.pixel(0, 0)).name()));
        QVERIFY2(close(bg.pixel(400, 400), sunset.last(), 3),
                 qPrintable(QColor(bg.pixel(400, 400)).name()));
        QVERIFY2(close(bg.pixel(200, 200), sunset.at(2), 6),
                 qPrintable(QColor(bg.pixel(200, 200)).name()));
        // Without a palette the two colours are used, as before
        s.palette.clear();
        const QImage two = backgroundfx::renderBackground(QSize(401, 401), s, QImage());
        QVERIFY(close(two.pixel(0, 0), s.backgroundColor1, 3));
        QVERIFY(close(two.pixel(400, 400), s.backgroundColor2, 3));
    }

    void paletteTextRoundTrips()
    {
        QCOMPARE(beautifypalette::parse(QString()), QVector<QColor>());
        // One valid colour is not a palette
        QCOMPARE(beautifypalette::parse(QStringLiteral("#ff0000,nonsense")),
                 QVector<QColor>());
        const QVector<QColor> stops{ QColor(0x0f, 0x17, 0x2a), QColor(0x6d, 0x28, 0xd9) };
        QCOMPARE(beautifypalette::parse(beautifypalette::format(stops)), stops);
        QCOMPARE(beautifypalette::format(stops), QStringLiteral("#0f172a,#6d28d9"));
    }

    void presetsAreTheAgreedOnes()
    {
        const auto& presets = beautifypalette::presets();
        QCOMPARE(presets.size(), 4);
        QCOMPARE(QString::fromLatin1(presets.at(0).name), QStringLiteral("Sunset Pop"));
        QCOMPARE(beautifypalette::format(presets.at(0).stops),
                 QStringLiteral("#ffd601,#ff6118,#ee30fb,#fb76fa,#7232f1"));
        QCOMPARE(QString::fromLatin1(presets.at(1).name), QStringLiteral("Midnight"));
        QCOMPARE(beautifypalette::format(presets.at(1).stops),
                 QStringLiteral("#0f172a,#6d28d9"));
        QCOMPARE(QString::fromLatin1(presets.at(2).name), QStringLiteral("Ocean"));
        QCOMPARE(beautifypalette::format(presets.at(2).stops),
                 QStringLiteral("#0ea5e9,#1e3a8a"));
        // Rainbow Ridge: sunlit ridges down to deep indigo
        QCOMPARE(QString::fromLatin1(presets.at(3).name), QStringLiteral("Rainbow Ridge"));
        QCOMPARE(beautifypalette::format(presets.at(3).stops),
                 QStringLiteral("#f5c04a,#e07539,#ff625e,#c2378f,#4c4edd"));
    }

    void stopsFollowTheSettings()
    {
        EffectSettings s;
        QCOMPARE(beautifypalette::stopsOf(s),
                 (QVector<QColor>{ s.backgroundColor1, s.backgroundColor2 }));
        s.palette = beautifypalette::presets().at(0).stops;
        QCOMPARE(beautifypalette::stopsOf(s), s.palette);
        QCOMPARE(beautifypalette::colorAt(s.palette, 0.0), s.palette.first());
        QCOMPARE(beautifypalette::colorAt(s.palette, 1.0), s.palette.last());
        QCOMPARE(beautifypalette::colorAt(s.palette, 0.5), s.palette.at(2));
    }
};

QTEST_GUILESS_MAIN(TestImageEffects)
#include "tst_imageeffects.moc"
