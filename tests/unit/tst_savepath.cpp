// SPDX-License-Identifier: GPL-3.0-or-later
// Where the editor's overwriting save writes.

#include "utils/savepath.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
void touch(const QString& path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}
} // namespace

class TestSavePath : public QObject
{
    Q_OBJECT
private slots:
    void existingFileIsReplacedInPlace()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("a.png");
        touch(p);
        QCOMPARE(savepath::overwriteTarget(p, QString()), p);
    }

    // Bug: transparent effects saved over an existing .jpg were written as
    // JPEG (forceExtension "png" ignored), so corners came out black.
    void jpegNeedingAlphaGetsPngSibling_bugBlackCornersInJpeg()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("shot.jpg");
        touch(p);
        QCOMPARE(savepath::overwriteTarget(p, "png"), dir.filePath("shot.png"));
    }

    void jpegWithoutAlphaStaysJpeg()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("shot.jpeg");
        touch(p);
        QCOMPARE(savepath::overwriteTarget(p, QString()), p);
    }

    void pngSiblingIsNeverClobbered()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("shot.jpg");
        touch(p);
        touch(dir.filePath("shot.png"));
        QVERIFY(savepath::overwriteTarget(p, "png").isEmpty());
    }

    // Bug: an opened project "a.fsproj" saves to "a.png"; with the save
    // format set to jpg that wrote a.jpg, then a_1.jpg, a_2.jpg ... because
    // a.png never existed. A not-yet-existing path is written as named.
    void newPathIsWrittenAsNamed_bugProjectSavesToOtherFile()
    {
        QTemporaryDir dir;
        const QString p = dir.filePath("a.png");
        QCOMPARE(savepath::overwriteTarget(p, QString()), p);
        QCOMPARE(savepath::overwriteTarget(p, "png"), p);
    }

    void unusablePathsFallBackToNumbering()
    {
        QTemporaryDir dir;
        QVERIFY(savepath::overwriteTarget(QString(), QString()).isEmpty());
        QVERIFY(savepath::overwriteTarget(dir.path(), QString()).isEmpty());
        QVERIFY(savepath::overwriteTarget(dir.filePath("nodir/a.png"),
                                          QString()).isEmpty());
        QVERIFY(savepath::overwriteTarget(dir.filePath("noext"),
                                          QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSavePath)
#include "tst_savepath.moc"
