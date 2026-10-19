// SPDX-License-Identifier: GPL-3.0-or-later

#include "backgroundchoice.h"
#include "utils/beautifypalette.h"

#include <QCoreApplication>

namespace {

QString colorText(const QColor& c)
{
    return c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
}

} // namespace

bool BackgroundChoice::operator==(const BackgroundChoice& other) const
{
    return mode == other.mode && color1 == other.color1 &&
           color2 == other.color2 && palette == other.palette &&
           autoColors == other.autoColors;
}

namespace backgroundchoice {

BackgroundChoice fromSettings(const EffectSettings& settings)
{
    BackgroundChoice choice;
    choice.mode = settings.backgroundMode;
    choice.color1 = settings.backgroundColor1;
    choice.color2 = settings.backgroundColor2;
    choice.palette = settings.palette;
    choice.autoColors = settings.autoColors;
    return choice;
}

QVariantMap overlayOf(const BackgroundChoice& choice)
{
    QVariantMap overlay;
    overlay.insert(QStringLiteral("effectBackground"), int(choice.mode));
    overlay.insert(QStringLiteral("effectBackgroundColor1"),
                   colorText(choice.color1));
    overlay.insert(QStringLiteral("effectBackgroundColor2"),
                   colorText(choice.color2));
    overlay.insert(QStringLiteral("effectPalette"),
                   choice.palette.size() >= 2
                     ? beautifypalette::format(choice.palette)
                     : QString());
    overlay.insert(QStringLiteral("effectAutoColors"), choice.autoColors);
    return overlay;
}

bool hasColors(BackgroundMode mode)
{
    return mode == BackgroundMode::Solid || mode == BackgroundMode::Gradient;
}

QVector<QColor> swatchColors(const BackgroundChoice& choice)
{
    switch (choice.mode) {
        case BackgroundMode::Solid:
            return { choice.color1 };
        case BackgroundMode::Gradient: {
            if (choice.palette.size() >= 2) {
                return { choice.palette.first(), choice.palette.last() };
            }
            return { choice.color1, choice.color2 };
        }
        default:
            return {};
    }
}

BackgroundChoice withColor(BackgroundChoice choice, int index, const QColor& color)
{
    if (choice.palette.size() >= 2) {
        choice.color1 = choice.palette.first();
        choice.color2 = choice.palette.last();
        choice.palette.clear();
    }
    if (index <= 0) {
        choice.color1 = color;
    } else {
        choice.color2 = color;
    }
    // An explicit colour replaces colours taken from the shot
    choice.autoColors = false;
    return choice;
}

BackgroundChoice withMode(BackgroundChoice choice, BackgroundMode mode)
{
    choice.mode = mode;
    return choice;
}

QVector<QuickPreset> quickPresets(BackgroundMode mode)
{
    QVector<QuickPreset> out;
    const auto& palettes = beautifypalette::presets();
    if (mode == BackgroundMode::Gradient) {
        for (const auto& p : palettes) {
            BackgroundChoice c;
            c.mode = BackgroundMode::Gradient;
            c.palette = p.stops;
            c.color1 = p.stops.first();
            c.color2 = p.stops.last();
            out.append(
              { QCoreApplication::translate("WorkflowsConf", p.name), c });
        }
    } else if (mode == BackgroundMode::Solid) {
        // The middle colour of each built-in palette, plus white
        for (const auto& p : palettes) {
            BackgroundChoice c;
            c.mode = BackgroundMode::Solid;
            c.color1 = beautifypalette::colorAt(p.stops, 0.5);
            c.color2 = c.color1;
            out.append(
              { QCoreApplication::translate("WorkflowsConf", p.name), c });
        }
        BackgroundChoice white;
        white.mode = BackgroundMode::Solid;
        white.color1 = QColor(Qt::white);
        white.color2 = white.color1;
        out.append({ QCoreApplication::translate("EditorSettingsBar", "White"),
                     white });
    }
    return out;
}

} // namespace backgroundchoice
