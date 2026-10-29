// SPDX-License-Identifier: GPL-3.0-or-later
// Design-director check for the Animate export: every style x motion, with
// and without draw-in annotations, on a synthetic UI screenshot. Samples one
// frame per 0.5 s of a 5 s loop and checks what a design lead would:
// seamless loop, never frozen, shot never off canvas, still readable as a
// small thumbnail (contrast and hero size), annotations shown in the hold.
// Not timing based.

#include "fakes/drawobject.h"
#include "tools/animannotations.h"
#include "utils/animatedframe.h"
#include "utils/animtimeline.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QtTest>

#include <cmath>
#include <memory>

using testtools::drawObject;

namespace {

constexpr int kDurationSec = 5;
constexpr double kStepSec = 0.5;
// Mean absolute channel difference (0..255) two samples 0.5 s apart must
// exceed: anything above rounding noise counts as alive
constexpr double kAliveEpsilon = 0.02;
// Luma difference (0..255) between the shot and the ring around it at
// 200 px wide
constexpr double kMinThumbContrast = 30.0;
constexpr double kMinHeroShare = 0.45;
constexpr double kMaxHeroShare = 0.85;
constexpr int kThumbWidth = 200;

const AnimStyle kStyles[] = { AnimStyle::Aurora, AnimStyle::GradientShift,
                              AnimStyle::Blobs,  AnimStyle::Twinkle,
                              AnimStyle::Flow,   AnimStyle::Silk,
                              AnimStyle::Mesh,   AnimStyle::Bokeh,
                              AnimStyle::Waves,  AnimStyle::Grid,
                              AnimStyle::Prism,  AnimStyle::Contours };
const ShotMotion kMotions[] = { ShotMotion::None, ShotMotion::Tilt,
                                ShotMotion::Float, ShotMotion::Orbit };

const char* styleName(AnimStyle s)
{
    static const char* names[] = { "aurora", "gradient", "blobs",
                                   "twinkle", "flow", "silk",
                                   "mesh", "bokeh", "waves",
                                   "grid", "prism", "contours" };
    return names[int(s)];
}

const char* motionName(ShotMotion m)
{
    static const char* names[] = { "none", "tilt", "float", "orbit" };
    return names[int(m)];
}

// A light app window: title bar, sidebar, text lines, a button, a card
QImage uiShot()
{
    QImage img(QSize(480, 300), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(246, 247, 250));
    QPainter p(&img);
    p.fillRect(QRect(0, 0, 480, 28), QColor(226, 228, 234));
    for (int i = 0; i < 3; ++i) {
        p.setBrush(QColor(i == 0 ? 0xff5f57 : i == 1 ? 0xfebc2e : 0x28c840));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(14 + 18 * i, 14), 5, 5);
    }
    p.fillRect(QRect(0, 28, 110, 272), QColor(236, 238, 243));
    for (int i = 0; i < 6; ++i) {
        p.fillRect(QRect(14, 48 + 26 * i, 70 - (i * 13) % 30, 9), QColor(120, 126, 140));
    }
    for (int i = 0; i < 7; ++i) {
        p.fillRect(QRect(132, 50 + 22 * i, 300 - (i * 47) % 120, 9), QColor(60, 66, 80));
    }
    p.fillRect(QRect(132, 220, 96, 30), QColor(56, 120, 230));
    p.fillRect(QRect(250, 210, 200, 70), QColor(232, 236, 245));
    return img;
}

EffectSettings lookSettings()
{
    EffectSettings s;
    s.paddingPx = 48;
    s.cornerRadius = 12;
    s.shadow = true;
    s.backgroundMode = BackgroundMode::Gradient;
    return s;
}

// The annotations a person would add: an arrow at the button, a box round
// the text, a note, a numbered step
AnimSource annotatedSource()
{
    std::unique_ptr<CaptureTool> arrow(drawObject(
      CaptureTool::TYPE_ARROW, QString(), QPoint(360, 150), QPoint(236, 228)));
    std::unique_ptr<CaptureTool> box(drawObject(
      CaptureTool::TYPE_SELECTION, QString(), QPoint(124, 42), QPoint(440, 196)));
    std::unique_ptr<CaptureTool> text(drawObject(
      CaptureTool::TYPE_TEXT, "plain", QPoint(270, 224), QPoint(270, 224)));
    std::unique_ptr<CaptureTool> counter(drawObject(
      CaptureTool::TYPE_CIRCLECOUNT, QString(), QPoint(110, 230), QPoint(110, 230)));
    const QList<QPointer<CaptureTool>> tools = { arrow.get(), box.get(),
                                                 text.get(), counter.get() };
    AnimSource source;
    source.base = uiShot();
    source.objects = captureproject::encodeObjects(tools, QPoint());
    source.dpr = 1.0;
    return source;
}

double luma(QRgb c)
{
    return 0.2126 * qRed(c) + 0.7152 * qGreen(c) + 0.0722 * qBlue(c);
}

double meanAbsDiff(const QImage& a, const QImage& b)
{
    double sum = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            sum += std::abs(qRed(pa[x]) - qRed(pb[x])) +
                   std::abs(qGreen(pa[x]) - qGreen(pb[x])) +
                   std::abs(qBlue(pa[x]) - qBlue(pb[x]));
        }
    }
    return sum / (3.0 * a.width() * a.height());
}

// Mean luma of the shot vs the ring just outside it, at thumbnail size
double thumbContrast(const QImage& frame, const QRectF& shot)
{
    const double k = double(kThumbWidth) / frame.width();
    const QImage thumb =
      frame.scaledToWidth(kThumbWidth, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB32);
    const QRect s(QPoint(int(std::ceil(shot.left() * k)), int(std::ceil(shot.top() * k))),
                  QPoint(int(std::floor(shot.right() * k)) - 1,
                         int(std::floor(shot.bottom() * k)) - 1));
    const QRect inner = s.adjusted(2, 2, -2, -2);
    const QRect ring = s.adjusted(-6, -6, 6, 6).intersected(thumb.rect());
    double in = 0, out = 0;
    int nIn = 0, nOut = 0;
    for (int y = ring.top(); y <= ring.bottom(); ++y) {
        for (int x = ring.left(); x <= ring.right(); ++x) {
            const double l = luma(thumb.pixel(x, y));
            if (inner.contains(x, y)) {
                in += l;
                ++nIn;
            } else if (!s.adjusted(-2, -2, 2, 2).contains(x, y)) {
                out += l;
                ++nOut;
            }
        }
    }
    if (nIn == 0 || nOut == 0) {
        return 0;
    }
    return std::abs(in / nIn - out / nOut);
}

} // namespace

class TestAnimDirector : public QObject
{
    Q_OBJECT

    PreparedAnimSource m_prepared;
    QImage m_plain;

private slots:
    void initTestCase()
    {
        ConfigHandler cfg;
        cfg.setArrowStyle(0);
        cfg.setReverseArrow(false);
        cfg.setDrawCircleCounterOutline(true);
        m_prepared = animannotations::prepare(annotatedSource());
        QVERIFY(m_prepared.annotations);
        QCOMPARE(m_prepared.annotations->count(), 4);
        m_plain = uiShot();
    }

    void directorCheck()
    {
        int combos = 0;
        int frames = 0;
        int annotationChecks = 0;
        double minContrast = 1e9;
        double minShare = 1.0;
        double maxShare = 0.0;
        double minAlive = 1e9;
        QStringList failures;
        auto fail = [&](const QString& what) {
            if (failures.size() < 40) {
                failures << what;
            }
        };
        const int samples = int(std::lround(kDurationSec / kStepSec)) + 1;
        for (AnimStyle style : kStyles) {
            for (ShotMotion motion : kMotions) {
                for (bool annotated : { false, true }) {
                    ++combos;
                    const QString tag = QStringLiteral("%1/%2/%3")
                                          .arg(styleName(style),
                                               motionName(motion),
                                               annotated ? "annotated" : "plain");
                    AnimatedFrames f(annotated ? m_prepared.base : m_plain,
                                     lookSettings(),
                                     style);
                    f.setMotion(motion, kDurationSec);
                    // Same frames without annotations, for the hold check
                    AnimatedFrames bare(m_plain, lookSettings(), style);
                    bare.setMotion(motion, kDurationSec);
                    if (annotated) {
                        f.setAnnotations(m_prepared.annotations, AnnotationMode::DrawIn);
                    }
                    const QRectF canvas(QPointF(0, 0), QSizeF(f.size()));
                    const animtimeline::Plan plan = f.plan();

                    if (f.frame(0.0) != f.frame(1.0)) {
                        fail(tag + QStringLiteral(": frame(0) != frame(1)"));
                    }
                    QImage previous;
                    for (int i = 0; i < samples; ++i) {
                        const double t = i * kStepSec;
                        const double phase = t / kDurationSec;
                        const QImage img = f.frame(phase);
                        ++frames;
                        const QString at = QStringLiteral("%1 t=%2s").arg(tag).arg(t);
                        if (!previous.isNull()) {
                            const double d = meanAbsDiff(previous, img);
                            minAlive = std::min(minAlive, d);
                            if (d <= kAliveEpsilon) {
                                fail(at + QStringLiteral(": frozen, mean diff %1 since t-0.5")
                                            .arg(d));
                            }
                        }
                        previous = img;

                        const shotmotion::Pose pose = f.poseAt(phase);
                        const QRectF shot = f.shotArea(pose);
                        if (!canvas.contains(shot)) {
                            fail(at + QStringLiteral(": shot (%1,%2 %3x%4) leaves the canvas")
                                        .arg(shot.x())
                                        .arg(shot.y())
                                        .arg(shot.width())
                                        .arg(shot.height()));
                        }
                        const double share =
                          shot.width() * shot.height() / (canvas.width() * canvas.height());
                        minShare = std::min(minShare, share);
                        maxShare = std::max(maxShare, share);
                        if (share < kMinHeroShare || share > kMaxHeroShare) {
                            fail(at + QStringLiteral(": shot covers %1 of the canvas").arg(share));
                        }
                        const double contrast = thumbContrast(img, shot);
                        minContrast = std::min(minContrast, contrast);
                        if (contrast < kMinThumbContrast) {
                            fail(at + QStringLiteral(": thumbnail contrast %1").arg(contrast));
                        }

                        if (annotated && t > plan.revealEnd() && t < plan.fadeStart()) {
                            const QImage ref = bare.frameAt(phase, pose);
                            for (int a = 0; a < m_prepared.annotations->count(); ++a) {
                                ++annotationChecks;
                                const QRect box =
                                  f.annotationBounds(a, pose).intersected(img.rect());
                                int differing = 0;
                                for (int y = box.top(); y <= box.bottom(); ++y) {
                                    for (int x = box.left(); x <= box.right(); ++x) {
                                        differing += img.pixel(x, y) != ref.pixel(x, y);
                                    }
                                }
                                if (differing < 10) {
                                    fail(at + QStringLiteral(": annotation %1 shows %2 pixels")
                                                .arg(a)
                                                .arg(differing));
                                }
                            }
                        }
                    }
                }
            }
        }
        qInfo("director: checked %d combos, %d frames, %d annotation-in-hold checks",
              combos,
              frames,
              annotationChecks);
        qInfo("director: min change per 0.5 s %.3f, min thumbnail contrast %.1f, "
              "shot share %.3f..%.3f",
              minAlive,
              minContrast,
              minShare,
              maxShare);
        QCOMPARE(combos, kAnimStyleCount * kShotMotionCount * 2);
        QCOMPARE(combos, 96);
        QCOMPARE(frames, combos * 11);
        QVERIFY2(annotationChecks > 0, "no hold frame was checked for annotations");
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    }

    // Nothing is drawn at the start: the loop begins on the plain shot
    void loopStartsWithoutAnnotations()
    {
        AnimatedFrames f(m_prepared.base, lookSettings(), AnimStyle::Flow);
        f.setMotion(ShotMotion::Tilt, kDurationSec);
        f.setAnnotations(m_prepared.annotations, AnnotationMode::DrawIn);
        AnimatedFrames bare(m_plain, lookSettings(), AnimStyle::Flow);
        bare.setMotion(ShotMotion::Tilt, kDurationSec);
        QCOMPARE(f.frame(0.0), bare.frame(0.0));
        // Mid hold they differ
        QVERIFY(f.frame(0.5) != bare.frameAt(0.5, f.poseAt(0.5)));
    }

    // While objects draw in, the shot holds still
    void shotWaitsForTheReveals()
    {
        AnimatedFrames f(m_prepared.base, lookSettings(), AnimStyle::Aurora);
        f.setMotion(ShotMotion::Tilt, kDurationSec);
        f.setAnnotations(m_prepared.annotations, AnnotationMode::DrawIn);
        const animtimeline::Plan plan = f.plan();
        for (double t = 0; t < plan.revealEnd(); t += 0.05) {
            QCOMPARE(f.poseAt(t / kDurationSec).amount, 0.0);
        }
        bool moved = false;
        for (double t = plan.revealEnd(); t < plan.fadeStart(); t += 0.05) {
            moved = moved || f.poseAt(t / kDurationSec).amount > 0.5;
        }
        QVERIFY(moved);
    }

    // Static mode: annotations in every frame, exactly like the flat still
    void staticModeShowsThemThroughout()
    {
        AnimatedFrames f(m_prepared.base, lookSettings(), AnimStyle::Blobs);
        f.setAnnotations(m_prepared.annotations, AnnotationMode::Static);
        QVERIFY(!f.plan().drawsIn());
        QImage flat = m_prepared.base;
        {
            QPainter p(&flat);
            p.setRenderHint(QPainter::Antialiasing);
            for (int i = 0; i < m_prepared.annotations->count(); ++i) {
                m_prepared.annotations->paint(p, i, 1.0);
            }
        }
        AnimatedFrames still(flat, lookSettings(), AnimStyle::Blobs);
        for (double ph : { 0.0, 0.3, 0.8 }) {
            QCOMPARE(f.frame(ph), still.frame(ph));
        }
    }

    // Opt-in: frames at t = 0.1, 0.3, 0.6 of the loop
    void saveSamples()
    {
        const QString dir = qEnvironmentVariable("PIXIE_ANIM_SAMPLES");
        if (dir.isEmpty()) {
            QSKIP("Set PIXIE_ANIM_SAMPLES to a folder to save sample frames");
        }
        QDir().mkpath(dir);
        AnimatedFrames f(m_prepared.base, lookSettings(), AnimStyle::Flow);
        f.setMotion(ShotMotion::Tilt, kDurationSec);
        f.setAnnotations(m_prepared.annotations, AnnotationMode::DrawIn);
        for (double ph : { 0.1, 0.3, 0.6 }) {
            QVERIFY(f.frame(ph).save(
              QStringLiteral("%1/drawin-flow-tilt-%2.png").arg(dir).arg(ph)));
        }
    }
};

QTEST_MAIN(TestAnimDirector)
#include "tst_animdirector.moc"
