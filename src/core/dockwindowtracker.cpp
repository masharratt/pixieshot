// SPDX-License-Identifier: GPL-3.0-or-later

#include "dockwindowtracker.h"

bool DockWindowTracker::setVisible(const void* window, bool visible)
{
    if (visible) {
        return !m_visible.contains(window) && (m_visible.insert(window), true);
    }
    return m_visible.remove(window);
}
