// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the bookkeeping that decides when Pixieshot shows a Dock icon
// (and so appears in Cmd+Tab) for its settings, info and launcher windows.

#include "core/dockwindowtracker.h"

#include <QObject>
#include <QTest>

class TestDockWindowTracker : public QObject
{
    Q_OBJECT
private slots:
    // Bug: the settings window was already visible when it was registered, so
    // its first "visible" event was missed, the count stayed 0 and the app
    // never became a Dock app (no Cmd+Tab entry).
    void windowAlreadyVisibleWhenRegisteredIsCounted()
    {
        DockWindowTracker t;
        int a = 0;
        QVERIFY(t.setVisible(&a, true));
        QCOMPARE(t.count(), 1);
    }

    void visibleTwiceCountsOnce()
    {
        DockWindowTracker t;
        int a = 0;
        QVERIFY(t.setVisible(&a, true));
        QVERIFY(!t.setVisible(&a, true));
        QCOMPARE(t.count(), 1);
    }

    void hiddenDecrementsOnce()
    {
        DockWindowTracker t;
        int a = 0;
        t.setVisible(&a, true);
        QVERIFY(t.setVisible(&a, false));
        QVERIFY(!t.setVisible(&a, false));
        QCOMPARE(t.count(), 0);
    }

    void hiddenWithoutBeingCountedNeverGoesNegative()
    {
        DockWindowTracker t;
        int a = 0;
        QVERIFY(!t.setVisible(&a, false));
        QCOMPARE(t.count(), 0);
    }

    void severalWindowsAreIndependent()
    {
        DockWindowTracker t;
        int a = 0, b = 0;
        t.setVisible(&a, true);
        t.setVisible(&b, true);
        QCOMPARE(t.count(), 2);
        t.setVisible(&a, false);
        QCOMPARE(t.count(), 1);
        QVERIFY(t.any());
        t.setVisible(&b, false);
        QVERIFY(!t.any());
    }
};

QTEST_MAIN(TestDockWindowTracker)
#include "tst_dockwindowtracker.moc"
