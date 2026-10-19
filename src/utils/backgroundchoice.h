// SPDX-License-Identifier: GPL-3.0-or-later
// The Beautify background picked in the windowed editor: its type and
// colours, laid over the saved effect settings for one editor session. The
// config is never written. It travels as a ConfigHandler::effectSettings()
// overlay (overlayOf), so Beautify, Save, Copy and the Animate preview and
// export in that editor all build their settings the same way.

#pragma once

#include "utils/imageeffects.h"

#include <QColor>
#include <QString>
#include <QVariantMap>
#include <QVector>

struct BackgroundChoice
{
    BackgroundMode mode = BackgroundMode::Gradient;
    QColor color1;
    QColor color2;
    // Gradient stops; used instead of color1/color2 with two or more
    QVector<QColor> palette;
    // Gradient only: colours picked from the shot
    bool autoColors = false;

    bool operator==(const BackgroundChoice& other) const;
    bool operator!=(const BackgroundChoice& other) const
    {
        return !(*this == other);
    }
};

namespace backgroundchoice {

// The background part of saved settings
BackgroundChoice fromSettings(const EffectSettings& settings);

// The choice as effect option overrides (option name -> plain value) for
// ConfigHandler::effectSettings(overlay)
QVariantMap overlayOf(const BackgroundChoice& choice);

// True when the type has colours to pick (solid, gradient)
bool hasColors(BackgroundMode mode);

// The colours the swatches show: one for solid, start and end for gradient,
// none otherwise
QVector<QColor> swatchColors(const BackgroundChoice& choice);

// `choice` with swatch `index` (0 start / solid, 1 end) set to `color`. A
// multi-stop palette becomes a two-colour gradient that keeps the other end.
BackgroundChoice withColor(BackgroundChoice choice, int index, const QColor& color);

// `choice` switched to `mode`, keeping its colours
BackgroundChoice withMode(BackgroundChoice choice, BackgroundMode mode);

// Quick picks for the choice's type, from the built-in palettes (empty when
// the type has no colours)
struct QuickPreset
{
    QString name;
    BackgroundChoice choice;
};
QVector<QuickPreset> quickPresets(BackgroundMode mode);

} // namespace backgroundchoice
