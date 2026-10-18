// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Qt>

// Whether open editor windows should give the app a Dock icon (and so a
// Cmd+Tab entry). Only switch while the app is inactive: switching while an
// editor is shown in another app's full-screen Space makes macOS jump to the
// desktop. Once switched, keep it while editors stay open.
bool editorsWantDockIcon(int openEditors, bool appActive, bool isDockApp);
