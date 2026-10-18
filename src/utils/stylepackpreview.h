// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/imageeffects.h"
#include "utils/stylepack.h"

#include <QImage>
#include <QStringList>

// What a style pack looks like, without applying it: a built-in sample shot
// drawn with the pack's settings laid over the saved ones. Nothing here
// writes anything to the config.
namespace stylepackpreview {

// A window-sized stand-in for a capture (1280 x 800)
QImage sampleShot();

// The pack's Beautify settings: the pack's values over the saved ones, built
// by the same code as ConfigHandler::effectSettings()
EffectSettings settingsFor(const stylepack::Pack& pack);

// The Beautify still of the sample, scaled to `width` physical pixels
QImage renderBeautify(const stylepack::Pack& pack, int width);
// One frame of the Animate export of the sample (the pack's Animate style),
// `width` physical pixels on the long side
QImage renderAnimate(const stylepack::Pack& pack, int width);

// Short ingredient labels derived from the settings, e.g. "Gradient",
// "Glass", "Grain", "Animate: Flow"
QStringList chips(const stylepack::Pack& pack);

} // namespace stylepackpreview
