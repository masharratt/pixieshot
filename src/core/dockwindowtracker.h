// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>

// Remembers which windows currently count towards the Dock icon. Each window
// is counted at most once, however many times it reports "visible", and a
// "hidden" report for a window that was never counted does nothing.
class DockWindowTracker
{
public:
    // Records the window's visibility. Returns true if the count changed.
    bool setVisible(const void* window, bool visible);
    int count() const { return static_cast<int>(m_visible.size()); }
    bool any() const { return !m_visible.isEmpty(); }

private:
    QSet<const void*> m_visible;
};
