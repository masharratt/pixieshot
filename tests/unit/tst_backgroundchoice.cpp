// SPDX-License-Identifier: GPL-3.0-or-later

// The editor's Beautify background choice laid over the saved settings.

#include "utils/backgroundchoice.h"
#include "utils/beautifypalette.h"

#include <QTest>

namespace {

EffectSettings savedSettings()
{
    EffectSettings s;
    s.paddingPx = 40;
    s.cornerRadius = 12;
    s.backgroundMode = BackgroundMode::Gradient;
    s.backgroundColor1 = QColor(0x8B, 0x5C, 0xF6);
    s.backgroundColor2 = QColor(0x3B, 0x82, 0xF6);
    return s;
}

} // namespace

class TestBackgroundChoice : public QObject
{
    Q_OBJECT

private slots:
    void defaultEqualsConfig()
    {
        const EffectSettings s = savedSettings();
        const BackgroundChoice c = backgroundchoice::fromSettings(s);
        QCOMPARE(int(c.mode), int(BackgroundMode::Gradient));
        QCOMPARE(c.color1, s.backgroundColor1);
        QCOMPARE(c.color2, s.backgroundColor2);
        QVERIFY(c.palette.isEmpty());
        QVERIFY(!c.autoColors);
    }

    void overlayHoldsOnlyTheBackgroundKeys()
    {
        BackgroundChoice c;
        c.mode = BackgroundMode::Solid;
        c.color1 = Qt::red;
        c.color2 = Qt::green;
        const QVariantMap o = backgroundchoice::overlayOf(c);
        QCOMPARE(o.keys(),
                 (QStringList{ "effectAutoColors",
                               "effectBackground",
                               "effectBackgroundColor1",
                               "effectBackgroundColor2",
                               "effectPalette" }));
        QCOMPARE(o.value("effectBackground").toInt(), int(BackgroundMode::Solid));
        QCOMPARE(QColor(o.value("effectBackgroundColor1").toString()),
                 QColor(Qt::red));
        QCOMPARE(QColor(o.value("effectBackgroundColor2").toString()),
                 QColor(Qt::green));
        QCOMPARE(o.value("effectPalette").toString(), QString());
        QCOMPARE(o.value("effectAutoColors").toBool(), false);
    }

    void overlayKeepsPaletteAndAlpha()
    {
        BackgroundChoice c;
        c.palette = beautifypalette::presets().first().stops;
        c.color1 = QColor(10, 20, 30, 128);
        const QVariantMap o = backgroundchoice::overlayOf(c);
        QCOMPARE(beautifypalette::parse(o.value("effectPalette").toString()),
                 c.palette);
        QCOMPARE(QColor(o.value("effectBackgroundColor1").toString()).alpha(),
                 128);
    }

    void swatchesMatchTheType()
    {
        BackgroundChoice c = backgroundchoice::fromSettings(savedSettings());
        QCOMPARE(backgroundchoice::swatchColors(c).size(), 2);
        c.mode = BackgroundMode::Solid;
        QCOMPARE(backgroundchoice::swatchColors(c),
                 QVector<QColor>{ c.color1 });
        c.mode = BackgroundMode::None;
        QVERIFY(backgroundchoice::swatchColors(c).isEmpty());
        c.mode = BackgroundMode::FromScreenshot;
        QVERIFY(backgroundchoice::swatchColors(c).isEmpty());
        QVERIFY(!backgroundchoice::hasColors(BackgroundMode::None));
        QVERIFY(backgroundchoice::hasColors(BackgroundMode::Solid));
    }

    void gradientSwatchesShowPaletteEnds()
    {
        BackgroundChoice c = backgroundchoice::fromSettings(savedSettings());
        c.palette = beautifypalette::presets().first().stops;
        const QVector<QColor> sw = backgroundchoice::swatchColors(c);
        QCOMPARE(sw.size(), 2);
        QCOMPARE(sw.first(), c.palette.first());
        QCOMPARE(sw.last(), c.palette.last());
    }

    void editingAPaletteEndKeepsTheOtherEnd()
    {
        BackgroundChoice c = backgroundchoice::fromSettings(savedSettings());
        c.palette = beautifypalette::presets().first().stops;
        c.autoColors = true;
        const QColor lastStop = c.palette.last();
        const BackgroundChoice out =
          backgroundchoice::withColor(c, 0, QColor(Qt::white));
        QVERIFY(out.palette.isEmpty());
        QVERIFY(!out.autoColors);
        QCOMPARE(out.color1, QColor(Qt::white));
        QCOMPARE(out.color2, lastStop);
        const BackgroundChoice end =
          backgroundchoice::withColor(out, 1, QColor(Qt::black));
        QCOMPARE(end.color1, QColor(Qt::white));
        QCOMPARE(end.color2, QColor(Qt::black));
    }

    void withModeKeepsColours()
    {
        const BackgroundChoice c =
          backgroundchoice::fromSettings(savedSettings());
        const BackgroundChoice solid =
          backgroundchoice::withMode(c, BackgroundMode::Solid);
        QCOMPARE(int(solid.mode), int(BackgroundMode::Solid));
        QCOMPARE(solid.color1, c.color1);
        QCOMPARE(solid.color2, c.color2);
    }

    void presetsComeFromBuiltInPalettes()
    {
        const auto gradient =
          backgroundchoice::quickPresets(BackgroundMode::Gradient);
        QCOMPARE(gradient.size(), beautifypalette::presets().size());
        for (int i = 0; i < gradient.size(); ++i) {
            QCOMPARE(int(gradient[i].choice.mode),
                     int(BackgroundMode::Gradient));
            QCOMPARE(gradient[i].choice.palette,
                     beautifypalette::presets()[i].stops);
            QVERIFY(!gradient[i].name.isEmpty());
        }
        const auto solid = backgroundchoice::quickPresets(BackgroundMode::Solid);
        QVERIFY(!solid.isEmpty());
        for (const auto& p : solid) {
            QCOMPARE(int(p.choice.mode), int(BackgroundMode::Solid));
            QVERIFY(p.choice.color1.isValid());
        }
        QVERIFY(backgroundchoice::quickPresets(BackgroundMode::None).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBackgroundChoice)
#include "tst_backgroundchoice.moc"
