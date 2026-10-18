// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the pure rule that decides when open editor windows give the
// app a Dock icon. Editor windows themselves are never hidden when the user
// switches apps (they used to be; that made them vanish behind Cmd+Tab).

#include "widgets/editorvisibility.h"

#include <QTest>

class TestEditorVisibility : public QObject
{
    Q_OBJECT
private slots:
    // The Dock icon (and so Cmd+Tab) appears only once the app is inactive,
    // because switching to a Dock app while showing a window in another
    // app's full-screen Space makes macOS jump to the desktop.
    void dockIconForEditors()
    {
        // No editors: never
        QVERIFY(!editorsWantDockIcon(0, false, false));
        QVERIFY(!editorsWantDockIcon(0, true, true));
        // Editors open, app active, not yet a Dock app: do not switch now
        QVERIFY(!editorsWantDockIcon(2, true, false));
        // Editors open, app inactive: show it so Cmd+Tab can return
        QVERIFY(editorsWantDockIcon(1, false, false));
        // Already a Dock app: keep it while editors stay open
        QVERIFY(editorsWantDockIcon(1, true, true));
    }
};

QTEST_MAIN(TestEditorVisibility)
#include "tst_editorvisibility.moc"
