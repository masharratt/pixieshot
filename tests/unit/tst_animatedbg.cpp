// SPDX-License-Identifier: GPL-3.0-or-later
// Animated backgrounds must loop exactly, be deterministic, honour the
// Beautify colours, and composite frames whose shot matches the still.

#include "sanitizer.h"
#include "utils/animatedbg.h"
#include "utils/animatedframe.h"
#include "utils/backgroundfx.h"
#include "utils/beautifypalette.h"
#include "utils/beautifylayout.h"
#include "utils/glassframe.h"
#include "utils/imageeffects.h"
#include "utils/shotmotion.h"

#include <QDir>
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
                              AnimStyle::Twinkle,
                              AnimStyle::Flow,
                              AnimStyle::Silk };

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
        case AnimStyle::Flow:
            return "flow";
        case AnimStyle::Silk:
            return "silk";
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

// Hue in degrees of a QRgb, -1 for greys and near-black
double hueOf(QRgb p)
{
    const QColor c = QColor::fromRgb(p);
    if (c.hsvSaturationF() < 0.25 || c.valueF() < 0.15) {
        return -1;
    }
    return c.hsvHueF() * 360.0;
}

// Share of pixels whose hue lies within `tol` degrees of `hue`
double hueShare(const QImage& img, double hue, double tol)
{
    int n = 0;
    int hit = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            ++n;
            const double h = hueOf(img.pixel(x, y));
            if (h >= 0 && hueDistance(h, hue) <= tol) {
                ++hit;
            }
        }
    }
    return double(hit) / std::max(1, n);
}

const QVector<QColor>& sunsetPop()
{
    static const QVector<QColor> stops = beautifypalette::parse(
      QStringLiteral("#FFD601,#FF6118,#EE30FB,#FB76FA,#7232F1"));
    return stops;
}

const ShotMotion kMotions[] = { ShotMotion::Tilt, ShotMotion::Float };

const char* motionName(ShotMotion m)
{
    switch (m) {
        case ShotMotion::None:
            return "none";
        case ShotMotion::Tilt:
            return "tilt";
        case ShotMotion::Float:
            return "float";
    }
    return "?";
}

// Pixels that differ between a and b, all inside `allowed`; count returned
int changedOutside(const QImage& a, const QImage& b, const QRect& allowed, int* changed)
{
    int outside = 0;
    *changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++*changed;
                if (!allowed.contains(x, y)) {
                    ++outside;
                }
            }
        }
    }
    return outside;
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
        SKIP_TIMING_UNDER_SANITIZER();
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

    // ---- multi-stop palettes ---------------------------------------------

    void paletteChangesEveryStyle_data() { eachStyleLoopsExactly_data(); }
    void paletteChangesEveryStyle()
    {
        QFETCH(int, style);
        const auto s = AnimStyle(style);
        const QVector<QColor>& stops = sunsetPop();
        const QImage pair = animatedbg::renderFrame(
          s, { stops.first(), stops.last() }, kSize, 0.3, 5);
        const QImage multi = animatedbg::renderFrameStops(s, stops, kSize, 0.3, 5);
        QVERIFY2(diff(pair, multi).changedShare > 0.05,
                 "the middle stops should show up");
        // Two stops are exactly the pair overload
        const QImage two = animatedbg::renderFrameStops(
          s, QVector<QColor>{ stops.first(), stops.last() }, kSize, 0.3, 5);
        QVERIFY(two == pair);
        // And the loop still closes
        QVERIFY(animatedbg::renderFrameStops(s, stops, kSize, 0.0, 5) ==
                animatedbg::renderFrameStops(s, stops, kSize, 1.0, 5));
    }

    void flowAndSilkShowTheMiddleStops_data()
    {
        QTest::addColumn<int>("style");
        QTest::newRow("flow") << int(AnimStyle::Flow);
        QTest::newRow("silk") << int(AnimStyle::Silk);
    }
    void flowAndSilkShowTheMiddleStops()
    {
        QFETCH(int, style);
        const QImage img =
          animatedbg::renderFrameStops(AnimStyle(style), sunsetPop(), kSize, 0.0, 9);
        // #EE30FB magenta (~297 deg) sits in the middle of Sunset Pop
        const double magenta = hueShare(img, 297.0, 20.0);
        QVERIFY2(magenta > 0.03, qPrintable(QString::number(magenta)));
    }

    void stopsFollowThePalette()
    {
        EffectSettings s = beautify(false);
        QCOMPARE(animatedbg::stopsFor(s, QImage()),
                 (QVector<QColor>{ kColors.first, kColors.second }));
        s.palette = sunsetPop();
        QCOMPARE(animatedbg::stopsFor(s, QImage()), sunsetPop());
        // Auto colours win over the palette
        s.autoColors = true;
        const QImage shot = sampleShot(QSize(120, 80), 1.0);
        const auto auto2 = backgroundfx::autoGradientColors(shot);
        QCOMPARE(animatedbg::stopsFor(s, shot),
                 (QVector<QColor>{ auto2.first, auto2.second }));
    }

    // ---- shot motion ------------------------------------------------------

    void restAtTheLoopEnds()
    {
        for (ShotMotion m : { ShotMotion::None, ShotMotion::Tilt, ShotMotion::Float }) {
            for (int d = 3; d <= 10; ++d) {
                const shotmotion::Pose rest;
                QVERIFY(shotmotion::poseAt(m, 0.0, d) == rest);
                QVERIFY(shotmotion::poseAt(m, 1.0, d) == rest);
            }
        }
        // None never moves
        for (double p : { 0.1, 0.3, 0.5, 0.9 }) {
            QVERIFY(shotmotion::poseAt(ShotMotion::None, p, 5) == shotmotion::Pose());
        }
        QVERIFY(shotmotion::transformFor(
                  ShotMotion::Tilt, 0.0, QRectF(10, 10, 300, 200), 1.0)
                  .isIdentity());
    }

    void tiltHoldsLongEnough()
    {
        for (int d = 3; d <= 10; ++d) {
            const shotmotion::Timeline tl = shotmotion::timeline(d);
            QVERIFY2(tl.hold >= shotmotion::kMinHoldSec,
                     qPrintable(QString::number(tl.hold)));
            QVERIFY(std::abs(tl.easeIn - 0.5) < 0.11);
            QVERIFY(std::abs(tl.easeOut - 0.5) < 0.11);
            QVERIFY(tl.holdEnd() + tl.easeOut <= d + 1e-9);
        }
        // At 5 s and 30 fps: the frames at full tilt form one run of at
        // least 1.2 s, all with the very same transform
        const QRectF rect(64, 64, 800, 500);
        const int fps = 30;
        const int count = 5 * fps;
        int run = 0;
        int best = 0;
        QTransform held;
        bool haveHeld = false;
        for (int i = 0; i < count; ++i) {
            const shotmotion::Pose pose =
              shotmotion::poseAt(ShotMotion::Tilt, double(i) / count, 5);
            if (pose.amount == 1.0) {
                const QTransform t =
                  shotmotion::transformFor(ShotMotion::Tilt, pose.amount, rect, 2.0);
                if (haveHeld) {
                    QVERIFY(t == held);
                }
                held = t;
                haveHeld = true;
                best = std::max(best, ++run);
            } else {
                run = 0;
            }
        }
        QVERIFY2(best >= int(std::ceil(shotmotion::kMinHoldSec * fps)),
                 qPrintable(QString::number(best)));
        // The light sweeps during the hold, and only then
        bool swept = false;
        for (int i = 0; i < count; ++i) {
            const shotmotion::Pose pose =
              shotmotion::poseAt(ShotMotion::Tilt, double(i) / count, 5);
            if (pose.sweep >= 0) {
                QCOMPARE(pose.amount, 1.0);
                swept = swept || (pose.sweep > 0.3 && pose.sweep < 0.7);
            }
        }
        QVERIFY(swept);
    }

    void tiltShotIsStillDuringTheHold()
    {
        const QImage shot = sampleShot(QSize(200, 130), 1.0);
        AnimatedFrames frames(shot, beautify(false), AnimStyle::Aurora);
        frames.setMotion(ShotMotion::Tilt, 5);
        const shotmotion::Timeline tl = shotmotion::timeline(5);
        const double a = (tl.holdStart() + 0.05) / 5.0;
        const double b = (tl.holdEnd() - 0.05) / 5.0;
        // Same pose, no light: the shot's middle is pixel for pixel the same
        // while the background behind it moves on
        const shotmotion::Pose held{ 1.0, -1.0 };
        const QImage fa = frames.frameAt(a, held);
        const QImage fb = frames.frameAt(b, held);
        const QRect inner = frames.layout().shot.adjusted(30, 25, -30, -25);
        for (int y = inner.top(); y <= inner.bottom(); ++y) {
            for (int x = inner.left(); x <= inner.right(); ++x) {
                QCOMPARE(fa.pixel(x, y), fb.pixel(x, y));
            }
        }
        QVERIFY(fa.pixel(2, 2) != fb.pixel(2, 2));
        // And the shot really is tilted: it differs from the still there
        const QImage rest = frames.frameAt(a, shotmotion::Pose());
        int changed = 0;
        changedOutside(fa, rest, QRect(QPoint(0, 0), fa.size()), &changed);
        QVERIFY(changed > inner.width() * inner.height() / 10);
    }

    void sweepOnlyLightsTheShot_data()
    {
        QTest::addColumn<bool>("glass");
        QTest::newRow("plain") << false;
        QTest::newRow("glass") << true;
    }
    void sweepOnlyLightsTheShot()
    {
        QFETCH(bool, glass);
        const QImage shot = sampleShot(QSize(220, 140), 2.0);
        AnimatedFrames frames(shot, beautify(glass), AnimStyle::Blobs);
        frames.setMotion(ShotMotion::Tilt, 5);
        for (double sweep : { 0.35, 0.5, 0.65 }) {
            const shotmotion::Pose lit{ 1.0, sweep };
            const QImage with = frames.frameAt(0.4, lit);
            const QImage without = frames.frameAt(0.4, { 1.0, -1.0 });
            const QRect bounds = frames.shotBounds(lit);
            QVERIFY(QRect(QPoint(0, 0), with.size()).contains(bounds));
            int changed = 0;
            const int outside = changedOutside(with, without, bounds, &changed);
            QVERIFY2(outside == 0, qPrintable(QString::number(outside)));
            QVERIFY2(changed > bounds.width() * bounds.height() / 50,
                     qPrintable(QString::number(changed)));
            // Light, never dark: the band brightens what it crosses
            qint64 sumWith = 0;
            qint64 sumWithout = 0;
            for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
                for (int x = bounds.left(); x <= bounds.right(); ++x) {
                    sumWith += qGray(with.pixel(x, y));
                    sumWithout += qGray(without.pixel(x, y));
                }
            }
            QVERIFY(sumWith > sumWithout);
        }
    }

    void eachMotionLoopsExactly_data()
    {
        QTest::addColumn<int>("motion");
        QTest::addColumn<bool>("glass");
        for (ShotMotion m : kMotions) {
            QTest::newRow(qPrintable(QStringLiteral("%1 plain").arg(motionName(m))))
              << int(m) << false;
            QTest::newRow(qPrintable(QStringLiteral("%1 glass").arg(motionName(m))))
              << int(m) << true;
        }
    }
    void eachMotionLoopsExactly()
    {
        QFETCH(int, motion);
        QFETCH(bool, glass);
        const QImage shot = sampleShot(QSize(200, 130), 2.0);
        for (int d : { 3, 5, 10 }) {
            AnimatedFrames frames(shot, beautify(glass), AnimStyle::GradientShift);
            frames.setMotion(ShotMotion(motion), d);
            QVERIFY(frames.frame(0.0) == frames.frame(1.0));
            // The shot moves somewhere in the loop
            const QRect inner = frames.layout().shot.adjusted(20, 20, -20, -20);
            const QImage a = frames.frame(0.0).copy(inner);
            bool moved = false;
            for (double p : { 0.25, 0.5 }) {
                moved = moved || diff(a, frames.frame(p).copy(inner)).changedShare > 0.05;
            }
            QVERIFY2(moved, qPrintable(QString::number(d)));
        }
    }

    // The glass drop shadow reaches past the bezel. Moving frames draw the
    // bezel into a layer, which must leave room for it, or the shadow is
    // cut off the moment the shot starts to move.
    void glassShadowIsNotClippedWhenMoving()
    {
        const qreal dpr = 2.0;
        const QImage shot = sampleShot(QSize(200, 130), dpr);
        AnimatedFrames frames(shot, beautify(true), AnimStyle::GradientShift);
        frames.setMotion(ShotMotion::Float, 5);
        const QImage rest = frames.frameAt(0.3, { 0.0, -1.0 });
        const QImage moving = frames.frameAt(0.3, { 1e-3, -1.0 });
        const QRect bezel = frames.layout().bezel;
        const int reach = glassframe::outset(bezel, frames.layout().shot, dpr);
        int worst = 0;
        for (int y = bezel.bottom() + 1; y <= bezel.bottom() + reach; ++y) {
            for (int x = bezel.center().x() - 30; x <= bezel.center().x() + 30; ++x) {
                const QRgb a = rest.pixel(x, y);
                const QRgb b = moving.pixel(x, y);
                worst = std::max({ worst,
                                   std::abs(qRed(a) - qRed(b)),
                                   std::abs(qGreen(a) - qGreen(b)),
                                   std::abs(qBlue(a) - qBlue(b)) });
            }
        }
        QVERIFY2(worst <= 6, qPrintable(QString::number(worst)));
    }

    void floatDriftsSixToTenPixels()
    {
        const QRectF rect(100, 100, 600, 400);
        for (qreal dpr : { 1.0, 2.0 }) {
            const QTransform t =
              shotmotion::transformFor(ShotMotion::Float, 1.0, rect, dpr);
            QVERIFY(!t.isIdentity());
            const QPointF c = t.map(rect.center()) - rect.center();
            QVERIFY2(std::abs(c.x()) < 1e-6, qPrintable(QString::number(c.x())));
            QVERIFY2(-c.y() >= 6 * dpr && -c.y() <= 10 * dpr,
                     qPrintable(QString::number(c.y())));
            // Breathing: a slight scale, at most a couple of percent
            const double w = t.map(QLineF(rect.topLeft(), rect.topRight())).length();
            QVERIFY(w > rect.width() && w < rect.width() * 1.03);
        }
    }

    void tiltIsGentle()
    {
        const QRectF rect(100, 100, 800, 500);
        const QTransform t = shotmotion::transformFor(ShotMotion::Tilt, 1.0, rect, 1.0);
        QCOMPARE(t.type(), QTransform::TxProject);
        // The corners move, but by a few percent of the size at most
        for (const QPointF& p :
             { rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight() }) {
            const QPointF d = t.map(p) - p;
            QVERIFY2(std::abs(d.x()) < 0.08 * rect.width() &&
                       std::abs(d.y()) < 0.08 * rect.height(),
                     qPrintable(QStringLiteral("%1,%2").arg(d.x()).arg(d.y())));
            QVERIFY(std::hypot(d.x(), d.y()) > 2.0);
        }
    }

    void noneLeavesTheShotWhereTheStillHasIt()
    {
        const QImage shot = sampleShot(QSize(200, 130), 2.0);
        const EffectSettings s = beautify(true);
        QImage still = imageeffects::applyEffects(shot, s);
        still.setDevicePixelRatio(1.0);
        AnimatedFrames frames(shot, s, AnimStyle::Silk);
        frames.setMotion(ShotMotion::None, 5);
        for (double phase : { 0.0, 0.3, 0.5 }) {
            QString why;
            QVERIFY2(shotMatches(frames.frame(phase), still, frames, &why),
                     qPrintable(why));
        }
    }

    void saveSamples()
    {
        const QString dir = qEnvironmentVariable("PIXIE_ANIM_SAMPLES");
        if (dir.isEmpty()) {
            QSKIP("Set PIXIE_ANIM_SAMPLES to a folder to save sample frames");
        }
        QDir().mkpath(dir);
        EffectSettings s = beautify(true);
        s.palette = sunsetPop();
        s.paddingPx = 72;
        s.cornerRadius = 14;
        const QImage shot = sampleShot(QSize(900, 560), 2.0);
        AnimatedFrames tilt(shot, s, AnimStyle::Flow, animatedbg::kDefaultSeed, 1920);
        tilt.setMotion(ShotMotion::Tilt, 5);
        for (double p : { 0.0, 0.25, 0.5 }) {
            QVERIFY(tilt.frame(p).save(
              QStringLiteral("%1/tilt-sunset-flow-%2.png").arg(dir).arg(p)));
        }
        QVERIFY(tilt.frameAt(0.5, { 1.0, 0.45 })
                  .save(QStringLiteral("%1/tilt-sunset-flow-sweep.png").arg(dir)));
        for (AnimStyle style : { AnimStyle::Flow, AnimStyle::Silk }) {
            AnimatedFrames f(shot, s, style, animatedbg::kDefaultSeed, 1920);
            QVERIFY(f.frame(0.0).save(
              QStringLiteral("%1/%2-first.png").arg(dir, nameOf(style))));
        }
    }

    // cfn: wall-clock bound, flaky on a loaded machine; move to a benchmark
    // target if CI ever runs these in parallel with heavy jobs
    void fullHdTiltSweepGlassIsFast()
    {
        SKIP_TIMING_UNDER_SANITIZER();
        EffectSettings s = beautify(true);
        s.paddingPx = 64;
        s.grain = 30;
        const QSize canvas(1920, 1080);
        const BeautifyLayout probe = beautifyLayout(QSize(100, 100), s, 1.0);
        const QImage shot = sampleShot(canvas - (probe.canvas - QSize(100, 100)), 1.0);
        AnimatedFrames frames(shot, s, AnimStyle::Aurora);
        frames.setMotion(ShotMotion::Tilt, 5);
        QCOMPARE(frames.size(), canvas);
        const shotmotion::Timeline tl = shotmotion::timeline(5);
        // Frames in the ease (moving transform) and in the hold (with light)
        QList<double> phases;
        for (int i = 0; i < 7; ++i) {
            phases << (tl.leadRest + tl.easeIn * (i + 0.5) / 7.0) / 5.0;
            phases << (tl.holdStart() + tl.hold * (i + 0.5) / 7.0) / 5.0;
        }
        frames.frame(phases.first()); // warm up
        QList<qint64> times;
        for (double p : phases) {
            QElapsedTimer t;
            t.start();
            const QImage f = frames.frame(p);
            times << t.nsecsElapsed();
            QVERIFY(!f.isNull());
        }
        std::sort(times.begin(), times.end());
        const double median = times.at(times.size() / 2) / 1e6;
        const double worst = times.last() / 1e6;
#if defined(QT_NO_DEBUG) || defined(NDEBUG)
        const double bound = 25.0;
#else
        const double bound = 200.0;
#endif
        qInfo("1920x1080 tilt+sweep+glass frame: median %.1f ms, worst %.1f ms "
              "(bound %.0f ms)",
              median,
              worst,
              bound);
        QVERIFY2(median <= bound, qPrintable(QString::number(median)));
    }
};

QTEST_MAIN(TestAnimatedBg)
#include "tst_animatedbg.moc"
