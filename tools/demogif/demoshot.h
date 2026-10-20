// SPDX-License-Identifier: GPL-3.0-or-later
// The demo's subject: Pixieshot's own Settings window, grabbed offscreen,
// with real annotations on it, and the effect settings of a scene. Plain
// C++ (the tool headers are kept out of Objective-C++), used by main.mm.

#pragma once

#include "tools/animannotations.h"
#include "utils/imageeffects.h"

struct DemoScene;

namespace demoshot {

// The Settings window on tab `tab` grabbed offscreen, with a box, a
// numbered counter, an arrow and a short note drawn on it by the real
// tools. Needs a QApplication; GUI thread only.
PreparedAnimSource settingsShot(int tab);

// The scene's built-in style pack with its look on top, through
// ConfigHandler::effectSettings (nothing is written to the config)
EffectSettings settingsFor(const DemoScene& scene);

} // namespace demoshot
