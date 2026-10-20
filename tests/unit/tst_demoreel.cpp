// SPDX-License-Identifier: GPL-3.0-or-later
// README demo reel (tools/demogif): the generator's own scene list renders
// offscreen into a non-empty loop that closes without a jump.

#include "../../tools/demogif/demoreel.h"
#include "fakes/annotatedsample.h"

#include <QTest>

#include <algorithm>
#include <cstdlib>

namespace {

// Stands in for ConfigHandler::effectSettings: a padded 16:9 gradient, with
// the glass key honoured so scenes still differ
EffectSettings testSettings(const DemoScene& scene)
{
    EffectSettings s;
    s.paddingPx = 48;
    s.cornerRadius = 12;
    s.shadow = true;
    s.backgroundMode = BackgroundMode::Gradient;
    s.aspect = 2;
    s.glass = scene.look.value(QStringLiteral("effectGlass")).toBool();
    return s;
}

// Mean absolute channel difference, 0..255
double meanDiff(const QImage& a, const QImage& b)
{
    const QImage x = a.convertToFormat(QImage::Format_RGB32);
    const QImage y = b.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    for (int row = 0; row < x.height(); ++row) {
        const auto* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
        const auto* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
        for (int col = 0; col < x.width(); ++col) {
            sum += std::abs(qRed(p[col]) - qRed(q[col])) +
                   std::abs(qGreen(p[col]) - qGreen(q[col])) +
                   std::abs(qBlue(p[col]) - qBlue(q[col]));
        }
    }
    return sum / (3.0 * x.width() * x.height());
}

} // namespace

class TestDemoReel : public QObject
{
    Q_OBJECT

    static DemoReel makeReel()
    {
        const PreparedAnimSource sample = testtools::annotatedSample();
        return DemoReel(sample.base,
                        sample.annotations,
                        demoreel::scenes(),
                        testSettings,
                        QSize(320, 180));
    }

private slots:
    void sceneListShowsSeveralLooks()
    {
        const QList<DemoScene> scenes = demoreel::scenes();
        QVERIFY2(scenes.size() >= 3, "three or more scenes");
        int stills = 0;
        int drawIn = 0;
        QList<int> styles;
        for (const DemoScene& s : scenes) {
            QVERIFY(s.seconds > 2 * demoreel::kFadeSec);
            stills += s.still ? 1 : 0;
            drawIn += s.annotations == AnnotationMode::DrawIn ? 1 : 0;
            if (!s.still) {
                QVERIFY2(!styles.contains(int(s.style)),
                         "each animated scene has its own background");
                styles.append(int(s.style));
            }
        }
        QCOMPARE(stills, 1);
        QVERIFY2(drawIn >= 1, "annotations draw themselves in somewhere");
        const DemoReel reel = makeReel();
        double total = 0;
        for (const DemoScene& s : scenes) {
            total += s.seconds - demoreel::kFadeSec;
        }
        QVERIFY(qAbs(reel.durationSec() - total) < 1e-9);
        QVERIFY(reel.durationSec() >= 8.0 && reel.durationSec() <= 12.0);
    }

    void crossfadeMixesNeighbours()
    {
        const DemoReel reel = makeReel();
        const QList<DemoScene> scenes = demoreel::scenes();
        // Halfway through the fade from scene 0 into scene 1
        const double t = scenes.at(0).seconds - demoreel::kFadeSec / 2;
        const DemoReel::Mix mix = reel.mixAt(t);
        QCOMPARE(mix.a, 0);
        QCOMPARE(mix.b, 1);
        QVERIFY(qAbs(mix.weight - 0.5) < 1e-6);
        // The loop starts in the fade from the last scene back to the first
        const DemoReel::Mix start = reel.mixAt(demoreel::kFadeSec / 2);
        QCOMPARE(start.a, reel.sceneCount() - 1);
        QCOMPARE(start.b, 0);
        // Mid scene: no fade
        const DemoReel::Mix mid = reel.mixAt(scenes.at(0).seconds / 2 + 0.2);
        QCOMPARE(mid.b, -1);
    }

    void loopIsNonEmptyAndSeamless()
    {
        const DemoReel reel = makeReel();
        const int fps = 8;
        const int count = reel.frameCount(fps);
        QVERIFY(count >= 8 * fps);
        QList<QImage> frames;
        for (int i = 0; i < count; ++i) {
            const QImage f = reel.frameAt(reel.frameTime(i, fps));
            QCOMPARE(f.size(), QSize(320, 180));
            QCOMPARE(f.format(), QImage::Format_RGB32);
            frames.append(f);
        }
        // The frame after the last is the first again: first == last after
        // the closing crossfade
        QCOMPARE(reel.frameAt(reel.durationSec()), frames.first());
        // Not one picture repeated
        QVERIFY(meanDiff(frames.first(), frames.at(count / 2)) > 4.0);
        // No hard cut anywhere, the wrap included: every step is small next
        // to a scene change
        QList<double> steps;
        for (int i = 0; i < count; ++i) {
            steps.append(meanDiff(frames.at(i), frames.at((i + 1) % count)));
        }
        const double cut = meanDiff(reel.sceneFrame(0, 1.5),
                                    reel.sceneFrame(1, 1.5));
        const double worst = *std::max_element(steps.begin(), steps.end());
        QVERIFY2(worst < 0.6 * cut,
                 qPrintable(QString("worst step %1, scene cut %2")
                              .arg(worst)
                              .arg(cut)));
        QVERIFY2(steps.last() < 0.6 * cut, "the loop closes without a jump");
    }

    void captionChipIsDrawn()
    {
        const DemoReel reel = makeReel();
        const QList<DemoScene> scenes = demoreel::scenes();
        QVERIFY(!scenes.at(1).caption.isEmpty());
        // The caption is the only difference between these two
        QList<DemoScene> bare = scenes;
        bare[1].caption.clear();
        const PreparedAnimSource sample = testtools::annotatedSample();
        const DemoReel plain(
          sample.base, sample.annotations, bare, testSettings, QSize(320, 180));
        const QImage with = reel.sceneFrame(1, 1.0);
        const QImage without = plain.sceneFrame(1, 1.0);
        QVERIFY(with != without);
        // ...and it stays small, in a corner
        int changed = 0;
        for (int y = 0; y < with.height(); ++y) {
            for (int x = 0; x < with.width(); ++x) {
                changed += with.pixel(x, y) != without.pixel(x, y) ? 1 : 0;
            }
        }
        QVERIFY(changed < with.width() * with.height() / 10);
    }
};

QTEST_MAIN(TestDemoReel)
#include "tst_demoreel.moc"
