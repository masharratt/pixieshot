// SPDX-License-Identifier: GPL-3.0-or-later
// beautifyLayout must describe exactly what imageeffects::applyEffects draws,
// so video frames (which only use the geometry) match stills.

#include "utils/beautifylayout.h"
#include "utils/imageeffects.h"

#include <QImage>
#include <QTest>

namespace {

QImage solid(QSize size, const QColor& color, qreal dpr)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(color);
    img.setDevicePixelRatio(dpr);
    return img;
}

EffectSettings bordered()
{
    EffectSettings s;
    s.borderPx = 4;
    return s;
}

EffectSettings shadowed()
{
    EffectSettings s;
    s.shadow = true;
    s.shadowBlur = 10;
    s.shadowOffset = QPoint(-3, 7);
    return s;
}

EffectSettings everything()
{
    EffectSettings s;
    s.borderPx = 3;
    s.cornerRadius = 12;
    s.shadow = true;
    s.shadowBlur = 16;
    s.shadowOffset = QPoint(0, 6);
    s.paddingPx = 24;
    s.backgroundMode = BackgroundMode::Gradient;
    return s;
}

EffectSettings withAspect(int aspect)
{
    EffectSettings s = everything();
    s.aspect = aspect;
    return s;
}

EffectSettings paddingNoBackground()
{
    EffectSettings s;
    s.paddingPx = 10;
    return s;
}

EffectSettings glassy()
{
    EffectSettings s = everything();
    s.glass = true;
    s.glassPx = 18;
    return s;
}

EffectSettings glassFromShot()
{
    EffectSettings s = glassy();
    s.backgroundMode = BackgroundMode::FromScreenshot;
    s.aspect = 2;
    s.grain = 30;
    return s;
}

EffectSettings glassOnly()
{
    EffectSettings s;
    s.glass = true;
    s.glassPx = 7;
    return s;
}

} // namespace

Q_DECLARE_METATYPE(EffectSettings)

class TestBeautifyLayout : public QObject
{
    Q_OBJECT
private slots:
    void canvasMatchesApplyEffects_data()
    {
        QTest::addColumn<QSize>("source");
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<EffectSettings>("settings");

        const QList<QSize> sizes = { QSize(40, 30), QSize(301, 157) };
        const QList<qreal> dprs = { 1.0, 2.0 };
        const QList<std::pair<const char*, EffectSettings>> cases = {
            { "off", EffectSettings{} },
            { "border", bordered() },
            { "shadow", shadowed() },
            { "everything", everything() },
            { "square", withAspect(1) },
            { "landscape", withAspect(2) },
            { "portrait", withAspect(3) },
            { "social", withAspect(4) },
            { "classic", withAspect(5) },
            { "paddingNoBackground", paddingNoBackground() },
            { "glass", glassy() },
            { "glassFromShot", glassFromShot() },
            { "glassOnly", glassOnly() },
        };
        for (const QSize& size : sizes) {
            for (qreal dpr : dprs) {
                for (const auto& [name, settings] : cases) {
                    QTest::addRow("%s %dx%d @%g",
                                  name,
                                  size.width(),
                                  size.height(),
                                  dpr)
                      << size << dpr << settings;
                }
            }
        }
    }

    void canvasMatchesApplyEffects()
    {
        QFETCH(QSize, source);
        QFETCH(qreal, dpr);
        QFETCH(EffectSettings, settings);
        const QImage out =
          imageeffects::applyEffects(solid(source, Qt::white, dpr), settings);
        const BeautifyLayout layout = beautifyLayout(source, settings, dpr);
        QCOMPARE(layout.canvas, out.size());
    }

    // The source lands where the layout says: a red source on a blue
    // background shows red at the frame's inner corners and blue just outside
    void frameRectMatchesDrawnPixels()
    {
        EffectSettings s;
        s.paddingPx = 15;
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = QColor(0, 0, 255);
        s.shadow = true;
        s.shadowOffset = QPoint(4, 8);
        s.aspect = 2;
        const QSize source(60, 40);
        const QImage out =
          imageeffects::applyEffects(solid(source, Qt::red, 1.0), s);
        const BeautifyLayout layout = beautifyLayout(source, s, 1.0);
        QCOMPARE(layout.frame.size(), source);
        QCOMPARE(out.pixelColor(layout.frame.topLeft()), QColor(Qt::red));
        QCOMPARE(out.pixelColor(layout.frame.bottomRight()), QColor(Qt::red));
        const QPoint outside = layout.frame.topLeft() - QPoint(1, 1);
        QVERIFY(out.pixelColor(outside) != QColor(Qt::red));
    }

    void borderSitsBetweenShotAndFrame()
    {
        EffectSettings s = everything();
        const BeautifyLayout layout = beautifyLayout(QSize(100, 80), s, 2.0);
        // 3 logical px of border at 2x is 6 physical px on each side
        QCOMPARE(layout.shot.adjusted(6, 6, -6, -6), layout.frame);
        QCOMPARE(layout.cornerRadius, 24.0);
        QVERIFY(QRect(QPoint(0, 0), layout.canvas).contains(layout.shot));
    }

    // The bezel wraps the shot by glassPx on each side, rounded by the
    // shot's radius plus the bezel, and the source still lands on `frame`
    void glassBezelWrapsTheShot()
    {
        EffectSettings s = glassy();
        const BeautifyLayout layout = beautifyLayout(QSize(100, 80), s, 2.0);
        QCOMPARE(layout.bezel.adjusted(36, 36, -36, -36), layout.shot);
        QCOMPARE(layout.bezelRadius, layout.cornerRadius + 36.0);
        QVERIFY(QRect(QPoint(0, 0), layout.canvas).contains(layout.bezel));

        EffectSettings off = everything();
        const BeautifyLayout plain = beautifyLayout(QSize(100, 80), off, 2.0);
        QCOMPARE(plain.bezel, plain.shot);
        QCOMPARE(layout.canvas, plain.canvas + QSize(72, 72));

        // Red source lands exactly on the frame under the glass
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = QColor(0, 0, 255);
        s.borderPx = 0;
        s.cornerRadius = 0;
        const QSize source(60, 40);
        const QImage out =
          imageeffects::applyEffects(solid(source, Qt::red, 1.0), s);
        const BeautifyLayout l1 = beautifyLayout(source, s, 1.0);
        QCOMPARE(out.size(), l1.canvas);
        QCOMPARE(out.pixelColor(l1.frame.topLeft()), QColor(Qt::red));
        QCOMPARE(out.pixelColor(l1.frame.bottomRight()), QColor(Qt::red));
        QVERIFY(out.pixelColor(l1.frame.topLeft() - QPoint(1, 1)) !=
                QColor(Qt::red));
    }

    void identityWhenOff()
    {
        const BeautifyLayout layout =
          beautifyLayout(QSize(40, 30), EffectSettings{}, 2.0);
        QVERIFY(layout.isIdentity());
        QCOMPARE(layout.frame, QRect(0, 0, 40, 30));
        QVERIFY(!beautifyLayout(QSize(40, 30), everything(), 1.0).isIdentity());
    }

    // H.264 needs even sizes: the video canvas grows by at most one pixel
    // per axis and keeps the frame where the still has it
    void videoCanvasIsEven()
    {
        QCOMPARE(videoCanvasSize(QSize(367, 206)), QSize(368, 206));
        QCOMPARE(videoCanvasSize(QSize(366, 205)), QSize(366, 206));
        QCOMPARE(videoCanvasSize(QSize(1, 1)), QSize(2, 2));
        QCOMPARE(videoCanvasSize(QSize(100, 80)), QSize(100, 80));
    }

    void autoBalanceIsIgnored()
    {
        // Auto-balance depends on the pixels, so the layout (used for video)
        // leaves it out
        EffectSettings s = everything();
        s.autoBalance = true;
        EffectSettings off = s;
        off.autoBalance = false;
        QCOMPARE(beautifyLayout(QSize(50, 50), s, 1.0).canvas,
                 beautifyLayout(QSize(50, 50), off, 1.0).canvas);
    }
};

QTEST_MAIN(TestBeautifyLayout)
#include "tst_beautifylayout.moc"
