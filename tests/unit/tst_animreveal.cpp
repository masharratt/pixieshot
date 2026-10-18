// SPDX-License-Identifier: GPL-3.0-or-later
// Draw-itself annotations in the Animate export: the timeline (stagger,
// hold, fade, where the shot motion may play), trimming a stroke to part of
// its length, and each tool drawing itself part of the way.

#include "fakes/drawobject.h"
#include "tools/animannotations.h"
#include "tools/capturetool.h"
#include "utils/animtimeline.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QtTest>

#include <cmath>
#include <memory>

using testtools::drawObject;
namespace tl = animtimeline;

namespace {

const QSize kCanvas(160, 120);

QImage baseImage()
{
    QImage img(kCanvas, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(240, 240, 236));
    return img;
}

// One object drawn into the base, like CaptureWidget::processPixmapWithTool
QImage drawn(CaptureTool* tool, double progress)
{
    QImage img = baseImage();
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    if (progress < 0) {
        tool->process(p, QPixmap());
    } else {
        tool->processReveal(p, QPixmap(), progress);
    }
    return img;
}

// Pixels that differ from the base, and their bounding box
struct Ink
{
    int count = 0;
    QRect box;
};

Ink inkOf(const QImage& img)
{
    const QImage base = baseImage();
    Ink ink;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (img.pixel(x, y) != base.pixel(x, y)) {
                ++ink.count;
                ink.box |= QRect(x, y, 1, 1);
            }
        }
    }
    return ink;
}

QString boxText(const QRect& r)
{
    return QStringLiteral("(%1,%2 %3x%4)")
      .arg(r.x())
      .arg(r.y())
      .arg(r.width())
      .arg(r.height());
}

double pathLength(const QPainterPath& path)
{
    return path.length();
}

} // namespace

class TestAnimReveal : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        ConfigHandler cfg;
        cfg.setArrowStyle(0);
        cfg.setReverseArrow(false);
        cfg.setInsecurePixelate(false);
        cfg.setDrawCircleCounterOutline(true);
    }

    // ---- timeline ----

    void noObjectsNoTimeline()
    {
        const tl::Plan plan = tl::plan(5.0, 0);
        QVERIFY(!plan.drawsIn());
        for (double ph : { 0.0, 0.2, 0.5, 0.93 }) {
            QCOMPARE(tl::motionPhase(plan, ph), ph);
        }
        QCOMPARE(tl::motionDurationSec(plan), 5.0);
    }

    void revealsAreStaggeredInOrder()
    {
        const tl::Plan plan = tl::plan(5.0, 4);
        QCOMPARE(plan.reveal, tl::kRevealSec);
        QCOMPARE(plan.stagger, tl::kStaggerSec);
        QVERIFY(plan.lead > 0.0); // frame 0 shows no annotation
        for (int i = 0; i < 4; ++i) {
            const double s = plan.revealStart(i);
            QCOMPARE(tl::progress(plan, i, 0.0), 0.0);
            QCOMPARE(tl::progress(plan, i, s), 0.0);
            const double mid = tl::progress(plan, i, s + plan.reveal / 2);
            QVERIFY2(mid > 0.5 && mid < 1.0, qPrintable(QString::number(mid)));
            QCOMPARE(tl::progress(plan, i, s + plan.reveal), 1.0);
            if (i > 0) {
                QVERIFY(s > plan.revealStart(i - 1));
            }
        }
    }

    void revealsEndByFortyFivePercentThenHold_data()
    {
        QTest::addColumn<int>("duration");
        QTest::addColumn<int>("count");
        for (int d : { 3, 5, 10 }) {
            for (int n : { 1, 3, 8, 30 }) {
                QTest::addRow("%ds-%d", d, n) << d << n;
            }
        }
    }

    void revealsEndByFortyFivePercentThenHold()
    {
        QFETCH(int, duration);
        QFETCH(int, count);
        const tl::Plan plan = tl::plan(duration, count);
        QVERIFY2(plan.revealEnd() <= tl::kRevealShare * duration + 1e-9,
                 qPrintable(QStringLiteral("revealEnd=%1").arg(plan.revealEnd())));
        const double hold = plan.fadeStart() - plan.revealEnd();
        QVERIFY2(hold >= tl::kMinHoldSec,
                 qPrintable(QStringLiteral("hold=%1").arg(hold)));
        QCOMPARE(plan.fadeOut, tl::kFadeOutSec);
        // During the hold every object is fully drawn and visible
        for (int i = 0; i < count; ++i) {
            const double t = plan.revealEnd() + 0.01;
            QCOMPARE(tl::progress(plan, i, t), 1.0);
            QVERIFY(tl::opacity(plan, i, t) >= tl::kPulseLow);
        }
    }

    void fadeOutClosesTheLoop()
    {
        const tl::Plan plan = tl::plan(5.0, 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(tl::opacity(plan, i, plan.fadeStart()), 1.0);
            QCOMPARE(tl::opacity(plan, i, 5.0), 0.0);
            const double mid = tl::opacity(plan, i, plan.fadeStart() + 0.2);
            QVERIFY(mid > 0.0 && mid < 1.0);
        }
    }

    // Only the last object pulses, slowly, between kPulseLow and 1, and it
    // is back at 1 when the hold ends so the fade starts from full
    void lastObjectPulsesDuringTheHold()
    {
        const tl::Plan plan = tl::plan(5.0, 3);
        double lo = 1.0;
        double hi = 0.0;
        for (double t = plan.revealEnd(); t <= plan.fadeStart(); t += 0.02) {
            const double o = tl::opacity(plan, 2, t);
            lo = std::min(lo, o);
            hi = std::max(hi, o);
            QCOMPARE(tl::opacity(plan, 0, t), 1.0);
            QCOMPARE(tl::opacity(plan, 1, t), 1.0);
        }
        QVERIFY2(lo >= tl::kPulseLow - 1e-9 && lo < 0.9,
                 qPrintable(QString::number(lo)));
        QCOMPARE(hi, 1.0);
        QVERIFY(std::abs(tl::opacity(plan, 2, plan.revealEnd()) - 1.0) < 1e-9);
        QVERIFY(std::abs(tl::opacity(plan, 2, plan.fadeStart()) - 1.0) < 1e-9);
    }

    // Shot motion and draw-in never move at once: the motion is at rest
    // until the last reveal ends and again once the fade starts
    void motionWaitsForTheReveals()
    {
        const tl::Plan plan = tl::plan(5.0, 4);
        const double d = plan.durationSec;
        QCOMPARE(tl::motionPhase(plan, 0.0), 0.0);
        QCOMPARE(tl::motionPhase(plan, (plan.revealEnd() - 0.01) / d), 0.0);
        QCOMPARE(tl::motionPhase(plan, (plan.fadeStart() + 0.01) / d), 0.0);
        const double mid = tl::motionPhase(
          plan, 0.5 * (plan.revealEnd() + plan.fadeStart()) / d);
        QVERIFY(std::abs(mid - 0.5) < 1e-9);
        QCOMPARE(tl::motionDurationSec(plan),
                 plan.fadeStart() - plan.revealEnd());
    }

    // ---- trimming ----

    void trimHalfALine()
    {
        QPainterPath line(QPointF(0, 0));
        line.lineTo(100, 0);
        const QPainterPath half = animannotations::trimPath(line, 0.5);
        QVERIFY(std::abs(pathLength(half) - 50.0) < 0.5);
        QCOMPARE(half.currentPosition(), QPointF(50, 0));
        QVERIFY(animannotations::trimPath(line, 0.0).isEmpty());
        QVERIFY(std::abs(pathLength(animannotations::trimPath(line, 1.0)) - 100.0) <
                0.5);
    }

    void trimFollowsCorners()
    {
        QPainterPath poly(QPointF(0, 0));
        poly.lineTo(10, 0);
        poly.lineTo(10, 10);
        const QPainterPath part = animannotations::trimPath(poly, 0.75);
        QVERIFY2(QLineF(part.currentPosition(), QPointF(10, 5)).length() < 0.01,
                 qPrintable(QStringLiteral("%1,%2")
                              .arg(part.currentPosition().x())
                              .arg(part.currentPosition().y())));
    }

    void trimARectOutline()
    {
        QPainterPath rect;
        rect.addRect(0, 0, 100, 50);
        const QPainterPath part = animannotations::trimPath(rect, 0.5);
        QVERIFY(std::abs(pathLength(part) - 150.0) < 1.0);
    }

    // ---- per tool ----

    void revealRows()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("variant");
        using T = CaptureTool;
        QTest::newRow("pencil") << int(T::TYPE_PENCIL) << QString();
        QTest::newRow("line") << int(T::TYPE_DRAWER) << QString();
        QTest::newRow("line-bent") << int(T::TYPE_DRAWER) << "bent";
        QTest::newRow("arrow") << int(T::TYPE_ARROW) << QString();
        QTest::newRow("arrow-bent") << int(T::TYPE_ARROW) << "bent";
        QTest::newRow("arrow-curved-head") << int(T::TYPE_ARROW) << "curvedHead";
        QTest::newRow("selection-rect") << int(T::TYPE_SELECTION) << QString();
        QTest::newRow("rectangle") << int(T::TYPE_RECTANGLE) << QString();
        QTest::newRow("circle") << int(T::TYPE_CIRCLE) << QString();
        QTest::newRow("marker") << int(T::TYPE_MARKER) << QString();
        QTest::newRow("text") << int(T::TYPE_TEXT) << QString();
        QTest::newRow("circle-counter") << int(T::TYPE_CIRCLECOUNT) << QString();
        QTest::newRow("ruler") << int(T::TYPE_RULER) << QString();
    }

    void fullRevealIsTheObject_data() { revealRows(); }
    void fullRevealIsTheObject()
    {
        QFETCH(int, type);
        QFETCH(QString, variant);
        std::unique_ptr<CaptureTool> tool(
          drawObject(static_cast<CaptureTool::Type>(type), variant));
        QVERIFY(tool);
        QVERIFY(tool->animatesIn());
        const QImage full = drawn(tool.get(), -1);
        QVERIFY2(inkOf(full).count > 0, "object drew nothing");
        QCOMPARE(drawn(tool.get(), 1.0), full);
        QCOMPARE(inkOf(drawn(tool.get(), 0.0)).count, 0);
    }

    // Part way is neither nothing nor the whole object
    void partRevealIsInBetween_data() { revealRows(); }
    void partRevealIsInBetween()
    {
        QFETCH(int, type);
        QFETCH(QString, variant);
        std::unique_ptr<CaptureTool> tool(
          drawObject(static_cast<CaptureTool::Type>(type), variant));
        const QImage full = drawn(tool.get(), -1);
        const QImage part = drawn(tool.get(), 0.5);
        QVERIFY(inkOf(part).count > 0);
        QVERIFY(part != full);
    }

    // Strokes draw along their path: early on the end is still blank
    void strokesDrawFromTheStart_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("variant");
        using T = CaptureTool;
        QTest::newRow("pencil") << int(T::TYPE_PENCIL) << QString();
        QTest::newRow("line") << int(T::TYPE_DRAWER) << QString();
        QTest::newRow("line-bent") << int(T::TYPE_DRAWER) << "bent";
        QTest::newRow("marker") << int(T::TYPE_MARKER) << QString();
        QTest::newRow("arrow") << int(T::TYPE_ARROW) << QString();
        QTest::newRow("arrow-bent") << int(T::TYPE_ARROW) << "bent";
    }
    void strokesDrawFromTheStart()
    {
        QFETCH(int, type);
        QFETCH(QString, variant);
        std::unique_ptr<CaptureTool> tool(
          drawObject(static_cast<CaptureTool::Type>(type), variant));
        const Ink early = inkOf(drawn(tool.get(), 0.3));
        QVERIFY(early.count > 0);
        // Started at (20, 30), ends at (110, 90)
        QVERIFY2(early.box.contains(QPoint(20, 30)), qPrintable(boxText(early.box)));
        QVERIFY2(!early.box.contains(QPoint(108, 88)), qPrintable(boxText(early.box)));
    }

    // The arrow head appears only at the end of the reveal
    void arrowHeadPopsAtTheEnd()
    {
        std::unique_ptr<CaptureTool> tool(
          drawObject(CaptureTool::TYPE_ARROW, QString()));
        const QImage full = drawn(tool.get(), -1);
        // The shaft is almost done but the head has not appeared
        const QImage late = drawn(tool.get(), 0.7);
        const QRect tip(100, 80, 12, 12);
        int fullInk = 0;
        int lateInk = 0;
        const QImage base = baseImage();
        for (int y = tip.top(); y <= tip.bottom(); ++y) {
            for (int x = tip.left(); x <= tip.right(); ++x) {
                fullInk += full.pixel(x, y) != base.pixel(x, y);
                lateInk += late.pixel(x, y) != base.pixel(x, y);
            }
        }
        QVERIFY2(fullInk > 20, qPrintable(QString::number(fullInk)));
        QVERIFY2(lateInk < fullInk / 4,
                 qPrintable(QStringLiteral("late=%1 full=%2").arg(lateInk).arg(fullInk)));
    }

    // Outlines trace around: part way, part of the outline is missing
    void outlinesTraceAround_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("selection-rect") << int(CaptureTool::TYPE_SELECTION);
        QTest::newRow("circle") << int(CaptureTool::TYPE_CIRCLE);
    }
    void outlinesTraceAround()
    {
        QFETCH(int, type);
        std::unique_ptr<CaptureTool> tool(
          drawObject(static_cast<CaptureTool::Type>(type), QString()));
        const Ink full = inkOf(drawn(tool.get(), -1));
        const Ink part = inkOf(drawn(tool.get(), 0.4));
        // Not a fade: drawn pixels are a strict subset, about 40% of them
        const double share = double(part.count) / full.count;
        QVERIFY2(share > 0.2 && share < 0.65, qPrintable(QString::number(share)));
    }

    // Text types in: fewer characters part way, left edge already in place
    void textTypesIn()
    {
        std::unique_ptr<CaptureTool> tool(
          drawObject(CaptureTool::TYPE_TEXT, "plain"));
        const Ink full = inkOf(drawn(tool.get(), -1));
        const Ink part = inkOf(drawn(tool.get(), 0.3));
        QVERIFY(part.count > 0);
        QVERIFY2(part.count < full.count * 0.6,
                 qPrintable(QStringLiteral("%1 of %2").arg(part.count).arg(full.count)));
        // Same place (glyph side bearings differ by a pixel between lines)
        QVERIFY2(std::abs(part.box.left() - full.box.left()) <= 2,
                 qPrintable(boxText(part.box) + " vs " + boxText(full.box)));
        // Typed text is full strength, not faded
        QVERIFY(part.box.width() < full.box.width());
    }

    // The counter pops: smaller and centred on the same point part way
    void counterPops()
    {
        std::unique_ptr<CaptureTool> tool(
          drawObject(CaptureTool::TYPE_CIRCLECOUNT, QString(), QPoint(80, 60),
                     QPoint(80, 60)));
        const Ink full = inkOf(drawn(tool.get(), -1));
        const Ink part = inkOf(drawn(tool.get(), 0.02));
        QVERIFY2(part.box.width() < full.box.width() * 0.85,
                 qPrintable(boxText(part.box) + " vs " + boxText(full.box)));
        QVERIFY(std::abs(part.box.center().x() - full.box.center().x()) <= 1);
        QVERIFY(std::abs(part.box.center().y() - full.box.center().y()) <= 1);
    }

    // Pixelate and invert hide or filter what is under them: they never
    // animate, so hidden content is never shown, not even for one frame
    void filtersAreNeverAnimated()
    {
        for (auto type : { CaptureTool::TYPE_PIXELATE, CaptureTool::TYPE_INVERT }) {
            std::unique_ptr<CaptureTool> tool(drawObject(type, QString()));
            QVERIFY(!tool->animatesIn());
        }
    }

    // prepare(): filters are baked into the base, the rest become the layer
    // in creation order, with their bounds
    void prepareSplitsStaticFromDrawIn()
    {
        QList<QPointer<CaptureTool>> tools;
        std::unique_ptr<CaptureTool> pencil(drawObject(CaptureTool::TYPE_PENCIL, QString()));
        std::unique_ptr<CaptureTool> pixelate(
          drawObject(CaptureTool::TYPE_PIXELATE, QString(), QPoint(5, 5), QPoint(40, 40)));
        std::unique_ptr<CaptureTool> arrow(
          drawObject(CaptureTool::TYPE_ARROW, QString(), QPoint(100, 20), QPoint(150, 100)));
        tools << pencil.get() << pixelate.get() << arrow.get();

        QImage base(kCanvas * 2, QImage::Format_RGB32);
        for (int y = 0; y < base.height(); ++y) {
            for (int x = 0; x < base.width(); ++x) {
                base.setPixel(x, y, qRgb((x * 9) % 256, (y * 7) % 256, 128));
            }
        }
        base.setDevicePixelRatio(2.0);
        AnimSource source;
        source.base = base;
        source.dpr = 2.0;
        source.objects = captureproject::encodeObjects(tools, QPoint());
        QCOMPARE(source.objects.size(), 3);

        const PreparedAnimSource prepared = animannotations::prepare(source);
        QVERIFY(prepared.annotations);
        QCOMPARE(prepared.annotations->count(), 2);
        QCOMPARE(prepared.base.devicePixelRatio(), 2.0);
        QCOMPARE(prepared.base.size(), base.size());
        // Pixelated area changed, the rest of the base did not
        QVERIFY(prepared.base.pixel(40, 40) != base.pixel(40, 40) ||
                prepared.base.pixel(30, 50) != base.pixel(30, 50));
        QCOMPARE(prepared.base.pixel(300, 230), base.pixel(300, 230));
        QVERIFY(prepared.annotations->bounds(0).intersects(QRectF(15, 25, 10, 10)));
        QVERIFY(prepared.annotations->bounds(1).contains(QPointF(125, 60)));

        // Draws through the layer in base logical coordinates
        QImage canvas(base.size(), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        canvas.setDevicePixelRatio(2.0);
        {
            QPainter p(&canvas);
            prepared.annotations->paint(p, 1, 1.0);
        }
        // The arrow tip in physical pixels
        QVERIFY(canvas.pixel(298, 198) != qRgb(255, 255, 255) ||
                canvas.pixel(296, 196) != qRgb(255, 255, 255));
    }

    void noObjectsNoLayer()
    {
        AnimSource source;
        source.base = baseImage();
        const PreparedAnimSource prepared = animannotations::prepare(source);
        QVERIFY(!prepared.annotations);
        QCOMPARE(prepared.base, source.base);
    }
};

QTEST_MAIN(TestAnimReveal)
#include "tst_animreveal.moc"
