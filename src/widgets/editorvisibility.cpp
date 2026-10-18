// SPDX-License-Identifier: GPL-3.0-or-later

#include "editorvisibility.h"

bool editorsWantDockIcon(int openEditors, bool appActive, bool isDockApp)
{
    if (openEditors <= 0) {
        return false;
    }
    return isDockApp || !appActive;
}
