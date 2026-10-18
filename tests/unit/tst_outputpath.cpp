// SPDX-License-Identifier: GPL-3.0-or-later
// Bug: nextOutputPath returned the first name that did not exist yet, but the
// file only appeared when an export finished, so two exports started close
// together got the same name and one overwrote the other.

#include "utils/outputpath.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestOutputPath : public QObject
{
    Q_OBJECT
private slots:
    void twoReservationsInARowGetDifferentNames()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Like FileNameHandler: first name that does not exist on disk
        auto propose = [&dir]() {
            for (int i = 1;; ++i) {
                const QString p =
                  dir.filePath(QStringLiteral("shot-%1.mp4").arg(i));
                if (!QFile::exists(p)) {
                    return p;
                }
            }
        };
        const QString a = outputpath::reserve(propose);
        const QString b = outputpath::reserve(propose);
        QVERIFY2(a != b, qPrintable(QStringLiteral("both got %1").arg(a)));
        QVERIFY(QFile::exists(a));
        QVERIFY(QFile::exists(b));
    }
};

QTEST_MAIN(TestOutputPath)
#include "tst_outputpath.moc"
