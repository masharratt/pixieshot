// SPDX-License-Identifier: GPL-3.0-or-later

#include "beautifypalette.h"

#include <QCoreApplication>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace beautifypalette {

QVector<QColor> parse(const QString& text)
{
    QVector<QColor> stops;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QColor c = QColor::fromString(part.trimmed());
        if (c.isValid()) {
            stops << QColor(c.red(), c.green(), c.blue());
        }
    }
    if (stops.size() < 2) {
        stops.clear();
    }
    return stops;
}

QString format(const QVector<QColor>& stops)
{
    QStringList names;
    for (const QColor& c : stops) {
        names << c.name(QColor::HexRgb);
    }
    return names.join(QLatin1Char(','));
}

const QVector<Preset>& presets()
{
    // Names are translated in the UI (context "WorkflowsConf")
    static const QVector<Preset> list{
        { QT_TRANSLATE_NOOP("WorkflowsConf", "Sunset Pop"),
          { QColor(0xFF, 0xD6, 0x01),
            QColor(0xFF, 0x61, 0x18),
            QColor(0xEE, 0x30, 0xFB),
            QColor(0xFB, 0x76, 0xFA),
            QColor(0x72, 0x32, 0xF1) } },
        { QT_TRANSLATE_NOOP("WorkflowsConf", "Midnight"), { QColor(0x0F, 0x17, 0x2A), QColor(0x6D, 0x28, 0xD9) } },
        { QT_TRANSLATE_NOOP("WorkflowsConf", "Ocean"), { QColor(0x0E, 0xA5, 0xE9), QColor(0x1E, 0x3A, 0x8A) } },
    };
    return list;
}

QVector<QColor> stopsOf(const EffectSettings& settings)
{
    if (settings.palette.size() >= 2) {
        return settings.palette;
    }
    return { settings.backgroundColor1, settings.backgroundColor2 };
}

QColor colorAt(const QVector<QColor>& stops, double t)
{
    if (stops.isEmpty()) {
        return {};
    }
    if (stops.size() == 1) {
        return stops.first();
    }
    const double pos = std::clamp(t, 0.0, 1.0) * (stops.size() - 1);
    const int i = std::min(int(pos), int(stops.size()) - 2);
    const double f = pos - i;
    const QColor& a = stops.at(i);
    const QColor& b = stops.at(i + 1);
    auto ch = [f](int x, int y) { return int(std::lround(x + (y - x) * f)); };
    return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
}

} // namespace beautifypalette
