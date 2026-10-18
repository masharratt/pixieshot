// SPDX-License-Identifier: GPL-3.0-or-later
// Bugs: the login item was created at a hardcoded /Applications path, an old
// "Flameshot" login item was never removed on upgrade, and an old Flameshot
// copy could hold the single-instance key and block Pixieshot from starting.

#include "utils/appidentity.h"
#include "utils/loginitem.h"

#include <QTest>

class TestLoginItem : public QObject
{
    Q_OBJECT
private slots:
    void bundlePathComesFromTheRunningBundle()
    {
        QCOMPARE(loginitem::bundlePath(QStringLiteral(
                   "/Users/me/Apps/Pixieshot.app/Contents/MacOS/flameshot")),
                 QStringLiteral("/Users/me/Apps/Pixieshot.app"));
    }

    void bundlePathFallsBackOutsideABundle()
    {
        QCOMPARE(loginitem::bundlePath(QStringLiteral("/tmp/build/flameshot")),
                 QStringLiteral("/Applications/Pixieshot.app"));
    }

    void addScriptNamesPixieshotAndTheRealBundle()
    {
        const QString s =
          loginitem::addScript(QStringLiteral("/Users/me/Apps/Pixieshot.app"));
        QVERIFY2(s.contains(QStringLiteral("name: \"Pixieshot\"")),
                 qPrintable(s));
        QVERIFY2(
          s.contains(QStringLiteral("path:\"/Users/me/Apps/Pixieshot.app\"")),
          qPrintable(s));
    }

    void oldFlameshotLoginItemIsMigrated()
    {
        QVERIFY(loginitem::legacyNames().contains(QStringLiteral("Flameshot")));
        QVERIFY(!loginitem::legacyNames().contains(AppIdentity::displayName()));
    }

    void oldFlameshotCopyDoesNotHoldTheSingleInstanceKey()
    {
        QVERIFY2(!AppIdentity::singleInstanceKey().contains(
                   QStringLiteral("Flameshot")),
                 qPrintable(AppIdentity::singleInstanceKey()));
    }

    void settingsNameStaysFlameshot()
    {
        QCOMPARE(AppIdentity::settingsName(), QStringLiteral("flameshot"));
    }
};

QTEST_MAIN(TestLoginItem)
#include "tst_loginitem.moc"
