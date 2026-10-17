// SPDX-License-Identifier: GPL-3.0-or-later
// The Pixieshot rename must not move user data: config and history paths stay
// under "flameshot".

#include "utils/appidentity.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTest>

class TestAppIdentity : public QObject
{
    Q_OBJECT
private slots:
    void displayNameIsPixieshot()
    {
        QCOMPARE(AppIdentity::displayName(), QStringLiteral("Pixieshot"));
    }

    void settingsNameIsStillFlameshot()
    {
        QCOMPARE(AppIdentity::settingsName(), QStringLiteral("flameshot"));
    }

    void configFilePathUnchanged()
    {
        AppIdentity::apply();
        // Same construction as ConfigHandler (INI, user scope, org + app name).
        const QString f = QSettings(QSettings::IniFormat,
                                    QSettings::UserScope,
                                    QCoreApplication::organizationName(),
                                    QCoreApplication::applicationName())
                            .fileName();
        QVERIFY2(f.endsWith(QStringLiteral("/flameshot/flameshot.ini")),
                 qPrintable(f));
    }

    void appDataPathUnchanged()
    {
        AppIdentity::apply();
        const QString d =
          QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY2(d.endsWith(QStringLiteral("/flameshot")), qPrintable(d));
    }
};

QTEST_MAIN(TestAppIdentity)
#include "tst_appidentity.moc"
