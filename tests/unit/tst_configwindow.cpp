// SPDX-License-Identifier: GPL-3.0-or-later
// Settings window: tall tabs scroll instead of running off the screen.

#include "config/configwindow.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTest>

class TestConfigWindow : public QObject
{
    Q_OBJECT
private slots:
    // Bug: the Workflows tab is taller than a short screen and was cut off by
    // the bottom of the screen, with no way to reach the lower settings.
    void tallTabScrollsAndWindowFitsShortScreen()
    {
        ConfigWindow w;
        w.fitToAvailableHeight(400);
        w.show();
        auto* tabs = w.findChild<QTabWidget*>();
        QVERIFY(tabs);
        for (int i = 0; i < tabs->count(); ++i) {
            QVERIFY2(qobject_cast<QScrollArea*>(tabs->widget(i)),
                     "every tab page is wrapped in a scroll area");
        }
        const int last = tabs->count() - 1;
        tabs->setCurrentIndex(last); // Workflows
        QTest::qWait(50);
        QVERIFY2(w.height() <= 400,
                 qPrintable(QString("height %1").arg(w.height())));
        auto* area = qobject_cast<QScrollArea*>(tabs->widget(last));
        QVERIFY(area->verticalScrollBar()->maximum() > 0);
        QCOMPARE(area->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        // Optional visual sample for humans: set CONFIGWIN_SAMPLE_DIR
        const QString dir = qEnvironmentVariable("CONFIGWIN_SAMPLE_DIR");
        if (!dir.isEmpty()) {
            w.grab().save(dir + "/settings-workflows-400px.png");
        }
    }
};

QTEST_MAIN(TestConfigWindow)
#include "tst_configwindow.moc"
