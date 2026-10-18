// SPDX-License-Identifier: GPL-3.0-or-later
// Animated backgrounds must loop exactly, be deterministic, honour the
// Beautify colours, and composite frames whose shot matches the still.

#include "utils/animatedbg.h"
#include "utils/animatedframe.h"
#include "utils/backgroundfx.h"
#include "utils/beautifylayout.h"
#include "utils/imageeffects.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <algorithm>
#include <cmath>

namespace {

const std::pair<QColor, QColor> kColors{ QColor(0x8B, 0x5C, 0xF6),
                                         QColor(0x3B, 0x82, 0xF6) };
const QSize kSize(320, 200);

const AnimStyle kStyles[] = { AnimStyle::Aurora,
                              AnimStyle::GradientShift,
                              AnimStyle::Blobs,
                              AnimStyle::Twinkle };

const char* nameOf(AnimStyle s)
{
    switch (s) {
        case AnimStyle::Aurora:
            return "aurora";
        case AnimStyle::GradientShift:
            return "gradient";
        case AnimStyle::Blobs:
            return "blobs";
        case AnimStyle::Twinkle:
            return "twinkle";
    }
    return "?";
}

// Largest per-channel difference and the share of pixels that differ
struct Diff
{
    int maxChannel = 0;
    double changedShare = 0.0;
};

Diff diff(const QImage& a, const QImage& b)
{
    Diff d;
    if (a.size() != b.size()) {
        d.maxChannel = 255;
        d.changedShare = 1.0;
        return d;
    }
    qint64 changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y);
            const QRgb q = b.pixel(x, y);
            const int m = std::max({ std::abs(qRed(p) - qRed(q)),
                                     std::abs(qGreen(p) - qGreen(q)),
                                     std::abs(qBlue(p) - qBlue(q)) });
            d.maxChannel = std::max(d.maxChannel, m);
            changed += m > 0 ? 1 : 0;
        }
    }
    d.changedShare = double(changed) / (qint64(a.width()) * a.height());
    return d;
}

// Circular mean hue (degrees) of the clearly coloured pixels
double meanHue(const QImage& img, int* counted)
{
    double sx = 0;
    double sy = 0;
    int n = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            const QColor c = QColor::fromRgb(img.pixel(x, y));
            if (c.hsvSaturationF() < 0.2 || c.valueF() < 0.08) {
                continue;
            }
            const double h = c.hsvHueF() * 2 * M_PI;
            sx += std::cos(h);
            sy += std::sin(h);
            ++n;
        }
    }
    *counted = n;
    double deg = std::atan2(sy, sx) * 180 / M_PI;
    return deg < 0 ? deg + 360 : deg;
}

double hueDistance(double a, double b)
{
    const double d = std::fmod(std::abs(a - b), 360.0);
    return std::min(d, 360.0 - d);
}

// A capture stand-in with some structure, so pixel mix-ups show
QImage sampleShot(QSize size, qreal dpr)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QLinearGradient g(0, 0, size.width(), size.height());
    g.setColorAt(0, QColor(70, 130, 220));
    g.setColorAt(1, QColor(240, 170, 60));
    QPainter p(&img);
    p.fillRect(img.rect(), g);
    p.fillRect(QRect(0, 0, size.width(), 12), QColor(245, 246, 250));
    p.fillRect(QRect(size.width() / 2, size.height() / 2, 30, 14),
               QColor(236, 72, 96));
    p.end();
    img.setDevicePixelRatio(dpr);
    return img;
}

EffectSettings beautify(bool glass)
{
    EffectSettings s;
    s.borderPx = 2;
    s.borderColor = QColor(20, 20, 20);
    s.cornerRadius = 10;
    s.shadow = true;
    s.shadowBlur = 16;
    s.shadowOffset = QPoint(0, 6);
    s.paddingPx = 40;
    s.backgroundMode = BackgroundMode::Gradient;
    s.backgroundColor1 = kColors.first;
    s.backgroundColor2 = kColors.second;
    s.glass = glass;
    s.glassPx = 12;
    return s;
}

// The shot's fully opaque pixels in `frame` equal those of `reference`
bool shotMatches(const QImage& frame,
                 const QImage& reference,
                 const AnimatedFrames& frames,
                 QString* why)
{
    const QRect shot = frames.layout().shot;
    const QImage& layer = frames.shotLayer();
    int checked = 0;
    for (int y = 0; y < shot.height(); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            if (qAlpha(layer.pixel(x, y)) != 255) {
                continue; // anti-aliased corner: blends with the background
            }
            const QPoint at = shot.topLeft() + QPoint(x, y);
            const QRgb a = frame.pixel(at) | 0xff000000u;
            const QRgb b = reference.pixel(at) | 0xff000000u;
            if (a != b) {
                *why = QStringLiteral("pixel (%1,%2): %3 vs %4")
                         .arg(at.x())
                         .arg(at.y())
                         .arg(a, 8, 16)
                         .arg(b, 8, 16);
                return false;
            }
            ++checked;
        }
    }
    if (checked < shot.width() * shot.height() / 2) {
        *why = QStringLiteral("only %1 pixels checked").arg(checked);
        return false;
    }
    return true;
}

} // namespace

class TestAnimatedBg : public QObject
{
    Q_OBJECT

private slots:
    void eachStyleLoopsExactly_data()
    {
        QTest::addColumn<int>("style");
        for (AnimStyle s : kStyles) {
            QTest::newRow(nameOf(s)) << int(s);
        }
    }
    void eachStyleLoopsExactly()
    {
        QFETCH(int, style);
        const auto s = AnimStyle(style);
        const QImage start = animatedbg::renderFrame(s, kColors, kSize, 0.0, 7);
        const QImage end = animatedbg::renderFrame(s, kColors, kSize, 1.0, 7);
        QCOMPARE(start.size(), kSize);
        QVERIFY(start == end);
        // It moves...
        const Diff half =
          diff(start, animatedbg::renderFrame(s, kColors, kSize, 0.5, 7));
        QVERIFY2(half.changedShare > 0.01,
                 qPrintable(QString::number(half.changedShare)));
        // ...and the last frame of a 30 fps, 10 s loop runs smoothly into
        // the first (no jump at the seam)
        const Diff seam = diff(
          start, animatedbg::renderFrame(s, kColors, kSize, 1.0 - 1.0 / 300, 7));
        QVERIFY2(seam.maxChannel <= 12, qPrintable(QString::number(seam.maxChannel)));
    }

    void sameSeedSameFrame_data() { eachStyleLoopsExactly_data(); }
    void sameSeedSameFrame()
    {
        QFETCH(int, style);
        const auto s = AnimStyle(style);
        const QImage a = animatedbg::renderFrame(s, kColors, kSize, 0.37, 11);
        const QImage b = animatedbg::renderFrame(s, kColors, kSize, 0.37, 11);
        QVERIFY(a == b);
        const QImage other = animatedbg::renderFrame(s, kColors, kSize, 0.37, 12);
        QVERIFY(diff(a, other).changedShare > 0.0);
    }

    void coloursAreHonoured_data()
    {
        QTest::addColumn<int>("style");
        QTest::addColumn<QColor>("first");
        QTest::addColumn<QColor>("second");
        QTest::addColumn<double>("hue");
        for (AnimStyle s : kStyles) {
            QTest::newRow(qPrintable(QStringLiteral("%1 orange").arg(nameOf(s))))
              << int(s) << QColor(235, 120, 30) << QColor(220, 90, 20) << 23.0;
            QTest::newRow(qPrintable(QStringLiteral("%1 teal").arg(nameOf(s))))
              << int(s) << QColor(20, 170, 160) << QColor(30, 150, 190) << 182.0;
        }
    }
    void coloursAreHonoured()
    {
        QFETCH(int, style);
        QFETCH(QColor, first);
        QFETCH(QColor, second);
        QFETCH(double, hue);
        for (double phase : { 0.0, 0.4 }) {
            const QImage img = animatedbg::renderFrame(
              AnimStyle(style), { first, second }, kSize, phase, 3);
            int counted = 0;
            const double mean = meanHue(img, &counted);
            QVERIFY2(counted > (kSize.width() / 2) * (kSize.height() / 2) / 2,
                     "most of the frame should be clearly coloured");
            QVERIFY2(hueDistance(mean, hue) < 15.0,
                     qPrintable(QStringLiteral("mean hue %1").arg(mean)));
        }
    }

    void workSizeIsReduced()
    {
        QCOMPARE(animatedbg::workSize(QSize(1920, 1080)), QSize(480, 270));
        QCOMPARE(animatedbg::workSize(QSize(300, 200)), QSize(300, 200));
        QCOMPARE(animatedbg::workSize(QSize(1000, 3000)).height(), 480);
    }

    void colorsFollowTheSettings()
    {
        EffectSettings s = beautify(false);
        QCOMPARE(animatedbg::colorsFor(s, QImage()), kColors);
        s.autoColors = true;
        const QImage shot = sampleShot(QSize(120, 80), 1.0);
        QCOMPARE(animatedbg::colorsFor(s, shot),
                 backgroundfx::autoGradientColors(shot));
    }

    void frameIsTheBeautifyCanvas_data()
    {
        QTest::addColumn<bool>("glass");
        QTest::addColumn<qreal>("dpr");
        QTest::newRow("plain 1x") << false << qreal(1.0);
        QTest::newRow("glass 2x") << true << qreal(2.0);
    }
    void frameIsTheBeautifyCanvas()
    {
        QFETCH(bool, glass);
        QFETCH(qreal, dpr);
        // Odd sizes so the even adjustment has something to do
        const QImage shot = sampleShot(QSize(201, 133), dpr);
        const EffectSettings s = beautify(glass);
        const BeautifyLayout layout = beautifyLayout(shot.size(), s, dpr);
        const AnimatedFrames frames(shot, s, AnimStyle::Aurora);
        QCOMPARE(frames.size(), layout.canvas);
        QCOMPARE(frames.layout().shot, layout.shot);
        QCOMPARE(frames.frame(0.2).size(), layout.canvas);
        const AnimatedFrames video(
          shot, s, AnimStyle::Aurora, animatedbg::kDefaultSeed, 0, true);
        QCOMPARE(video.size(), videoCanvasSize(layout.canvas));
        QCOMPARE(video.frame(0.2).size(), videoCanvasSize(layout.canvas));
    }

    void shotIsStaticAndMatchesTheStill_data() { frameIsTheBeautifyCanvas_data(); }
    void shotIsStaticAndMatchesTheStill()
    {
        QFETCH(bool, glass);
        QFETCH(qreal, dpr);
        const QImage shot = sampleShot(QSize(200, 130), dpr);
        const EffectSettings s = beautify(glass);
        QImage still = imageeffects::applyEffects(shot, s);
        still.setDevicePixelRatio(1.0);
        for (AnimStyle style : kStyles) {
            const AnimatedFrames frames(shot, s, style);
            QCOMPARE(frames.size(), still.size());
            for (double phase : { 0.0, 0.31, 0.77 }) {
                QString why;
                QVERIFY2(shotMatches(frames.frame(phase), still, frames, &why),
                         qPrintable(QStringLiteral("%1 @%2: %3")
                                      .arg(nameOf(style))
                                      .arg(phase)
                                      .arg(why)));
            }
        }
    }

    void backgroundAroundTheShotMoves()
    {
        const QImage shot = sampleShot(QSize(200, 130), 1.0);
        const AnimatedFrames frames(shot, beautify(true), AnimStyle::Blobs);
        const QImage a = frames.frame(0.0);
        const QImage b = frames.frame(0.5);
        // Corner of the canvas (background) and the bezel ring both change
        QVERIFY(a.pixel(2, 2) != b.pixel(2, 2));
        const QRect bezel = frames.layout().bezel;
        const QPoint ring(bezel.center().x(), bezel.top() + 3);
        QVERIFY(a.pixel(ring) != b.pixel(ring));
    }

    void noPaddingStillLeavesRoom()
    {
        EffectSettings s;
        s.paddingPx = 0;
        QVERIFY(AnimatedFrames::animatedSettings(s).paddingPx > 0);
        s.paddingPx = 12;
        QCOMPARE(AnimatedFrames::animatedSettings(s).paddingPx, 12);
        s.paddingPx = 0;
        s.aspect = 1; // the size preset makes room on the short axis
        QCOMPARE(AnimatedFrames::animatedSettings(s).paddingPx, 0);
    }

    void longSideIsCapped()
    {
        const QImage shot = sampleShot(QSize(3000, 1800), 2.0);
        const AnimatedFrames frames(
          shot, beautify(true), AnimStyle::Aurora, 1, 1920, true);
        const int longSide = std::max(frames.size().width(), frames.size().height());
        QVERIFY2(longSide <= 1920 && longSide >= 1880,
                 qPrintable(QString::number(longSide)));
        QCOMPARE(frames.size().width() % 2, 0);
        QCOMPARE(frames.size().height() % 2, 0);
    }

    // cfn: wall-clock bound, flaky on a loaded machine; move to a benchmark
    // target if CI ever runs these in parallel with heavy jobs
    void fullHdFrameWithGlassIsFast()
    {
        // A 1920x1080 canvas: the shot plus padding, glass and shadow
        EffectSettings s = beautify(true);
        s.paddingPx = 64;
        s.grain = 30;
        const QSize canvas(1920, 1080);
        const BeautifyLayout probe = beautifyLayout(QSize(100, 100), s, 1.0);
        const QSize extra = probe.canvas - QSize(100, 100);
        const QImage shot = sampleShot(canvas - extra, 1.0);
        const AnimatedFrames frames(shot, s, AnimStyle::Aurora);
        QCOMPARE(frames.size(), canvas);
        frames.frame(0.0); // warm up
        QList<qint64> times;
        for (int i = 0; i < 7; ++i) {
            QElapsedTimer t;
            t.start();
            const QImage f = frames.frame(i / 7.0);
            times << t.nsecsElapsed();
            QVERIFY(!f.isNull());
        }
        std::sort(times.begin(), times.end());
        const double ms = times.at(times.size() / 2) / 1e6;
#if defined(QT_NO_DEBUG) || defined(NDEBUG)
        const double bound = 25.0;
#else
        const double bound = 150.0;
#endif
        qInfo("median 1920x1080 glass frame: %.1f ms (bound %.0f ms)", ms, bound);
        QVERIFY2(ms <= bound, qPrintable(QString::number(ms)));
    }
};

QTEST_MAIN(TestAnimatedBg)
#include "tst_animatedbg.moc"
