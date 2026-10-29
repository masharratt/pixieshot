// SPDX-License-Identifier: GPL-3.0-or-later
// Multi-stop colour palettes for the Beautify gradient and the animated
// backgrounds: stored as text, named presets, and sampling along the stops.

#pragma once

#include "utils/imageeffects.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace beautifypalette {

// "#rrggbb,#rrggbb,..." to colours. Invalid entries are skipped; fewer
// than two valid colours gives an empty palette (meaning "not set").
QVector<QColor> parse(const QString& text);
QString format(const QVector<QColor>& stops);

struct Preset
{
    const char* name; // translate with QCoreApplication::translate("WorkflowsConf", name)
    QVector<QColor> stops;
};
// Sunset Pop, Midnight, Ocean, Rainbow Ridge (Custom is the absence of a preset)
const QVector<Preset>& presets();

// The colours a gradient uses: the palette when it has two or more stops,
// otherwise backgroundColor1 and backgroundColor2
QVector<QColor> stopsOf(const EffectSettings& settings);

// Colour at t (0..1) along evenly spaced stops, mixed linearly in sRGB
QColor colorAt(const QVector<QColor>& stops, double t);

} // namespace beautifypalette
