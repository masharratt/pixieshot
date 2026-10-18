// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for scrolling-capture stitching: frames are sliced from a synthetic
// tall image at known scroll positions, and the stitched result must equal
// the source pixel for pixel.

#include "sanitizer.h"
#include "utils/scrollstitch.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

using namespace scrollstitch;

Q_DECLARE_METATYPE(scrollstitch::Step)

namespace {

// Every row is its own deterministic noise, so no two rows are alike
QImage noiseImage(int width, int height, quint32 seed)
{
    QImage img(width, height, QImage::Format_RGB32);
    QRandomGenerator rng(seed);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < width; ++x) {
            line[x] = 0xff000000u | (rng.generate() & 0x00ffffffu);
        }
    }
    return img;
}

QImage solidRows(int width, int height, QRgb color)
{
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

// Rows of several images stacked top to bottom (all the same width)
QImage stack(const QList<QImage>& parts)
{
    int h = 0;
    for (const QImage& p : parts) {
        h += p.height();
    }
    QImage out(parts.first().width(), h, QImage::Format_RGB32);
    int y = 0;
    for (const QImage& p : parts) {
        for (int r = 0; r < p.height(); ++r) {
            memcpy(out.scanLine(y + r), p.constScanLine(r), p.bytesPerLine());
        }
        y += p.height();
    }
    return out;
}

QImage rows(const QImage& src, int from, int count)
{
    return src.copy(0, from, src.width(), count);
}

// One viewport frame: header, content rows [y, y + contentH), footer
QImage frameAt(const QImage& content,
               int y,
               int contentH,
               const QImage& header = QImage(),
               const QImage& footer = QImage())
{
    QList<QImage> parts;
    if (!header.isNull()) {
        parts << header;
    }
    parts << rows(content, y, contentH);
    if (!footer.isNull()) {
        parts << footer;
    }
    return stack(parts);
}

// Feeds one frame per scroll position and returns the steps
QList<Step> feed(ScrollStitcher& s,
                 const QImage& content,
                 const QList<int>& positions,
                 int contentH,
                 const QImage& header = QImage(),
                 const QImage& footer = QImage())
{
    QList<Step> steps;
    for (int y : positions) {
        steps << s.add(frameAt(content, y, contentH, header, footer));
    }
    return steps;
}

QImage expected(const QImage& content,
                int firstY,
                int lastY,
                int contentH,
                const QImage& header = QImage(),
                const QImage& footer = QImage())
{
    return frameAt(content, firstY, lastY - firstY + contentH, header, footer);
}

// Pixel comparison that names the first differing row
QString firstDifference(const QImage& a, const QImage& b)
{
    if (a.size() != b.size()) {
        return QStringLiteral("size %1x%2 vs %3x%4")
          .arg(a.width())
          .arg(a.height())
          .arg(b.width())
          .arg(b.height());
    }
    const QImage ca = a.convertToFormat(QImage::Format_RGB32);
    const QImage cb = b.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < ca.height(); ++y) {
        if (memcmp(ca.constScanLine(y), cb.constScanLine(y), ca.width() * 4) !=
            0) {
            return QStringLiteral("row %1 differs").arg(y);
        }
    }
    return {};
}

const int kW = 320;

} // namespace

class TestScrollStitch : public QObject
{
    Q_OBJECT
private slots:
    void firstFrameIsTheWholeImage()
    {
        const QImage content = noiseImage(kW, 400, 1);
        ScrollStitcher s;
        QCOMPARE(s.add(rows(content, 0, 200)), Step::Started);
        QCOMPARE(firstDifference(s.result(), rows(content, 0, 200)), QString());
        QCOMPARE(s.height(), 200);
        QCOMPARE(s.framesAccepted(), 1);
    }

    void stitchesKnownOffsets_data()
    {
        QTest::addColumn<QList<int>>("positions");
        QTest::addColumn<int>("contentH");
        QTest::newRow("small steps") << QList<int>{ 0, 1, 3, 6, 10, 15 } << 200;
        QTest::newRow("large steps")
          << QList<int>{ 0, 180, 360, 540, 700 } << 200;
        QTest::newRow("mixed") << QList<int>{ 0, 2, 120, 121, 300, 450 } << 200;
        QTest::newRow("single big step") << QList<int>{ 0, 190 } << 200;
    }

    void stitchesKnownOffsets()
    {
        QFETCH(QList<int>, positions);
        QFETCH(int, contentH);
        const QImage content = noiseImage(kW, 1200, 2);
        ScrollStitcher s;
        const QList<Step> steps = feed(s, content, positions, contentH);
        QCOMPARE(steps.first(), Step::Started);
        for (int i = 1; i < steps.size(); ++i) {
            QCOMPARE(steps[i], Step::Appended);
        }
        QCOMPARE(firstDifference(
                   s.result(),
                   expected(content, positions.first(), positions.last(), contentH)),
                 QString());
        QCOMPARE(s.bands(), Bands());
    }

    void stickyHeaderAndFooterAppearOnce()
    {
        const QImage content = noiseImage(kW, 1500, 3);
        const QImage header = noiseImage(kW, 60, 100);
        const QImage footer = noiseImage(kW, 40, 200);
        const QList<int> positions{ 0, 4, 90, 250, 251, 400, 640 };
        ScrollStitcher s;
        feed(s, content, positions, 300, header, footer);
        QCOMPARE(s.bands(), (Bands{ 60, 40 }));
        QCOMPARE(firstDifference(
                   s.result(),
                   expected(content, 0, positions.last(), 300, header, footer)),
                 QString());
    }

    void zeroOffsetIsSkipped()
    {
        const QImage content = noiseImage(kW, 800, 4);
        ScrollStitcher s;
        const QList<Step> steps = feed(s, content, { 0, 50, 50, 50, 120 }, 200);
        QCOMPARE(steps,
                 (QList<Step>{ Step::Started,
                               Step::Appended,
                               Step::NoChange,
                               Step::NoChange,
                               Step::Appended }));
        QCOMPARE(firstDifference(s.result(), expected(content, 0, 120, 200)),
                 QString());
    }

    void scrollingUpIsIgnoredAndResumes()
    {
        const QImage content = noiseImage(kW, 1000, 5);
        ScrollStitcher s;
        const QList<Step> steps =
          feed(s, content, { 0, 100, 200, 150, 60, 140, 260 }, 200);
        QCOMPARE(steps,
                 (QList<Step>{ Step::Started,
                               Step::Appended,
                               Step::Appended,
                               Step::ScrolledUp,
                               Step::ScrolledUp,
                               Step::ScrolledUp,
                               Step::Appended }));
        QCOMPARE(firstDifference(s.result(), expected(content, 0, 260, 200)),
                 QString());
    }

    void mismatchedFrameIsSkippedAndCaptureRecovers()
    {
        const QImage content = noiseImage(kW, 1000, 6);
        ScrollStitcher s;
        feed(s, content, { 0, 80 }, 200);
        const int before = s.height();
        // Some other picture of the same size
        QCOMPARE(s.add(noiseImage(kW, 200, 77)), Step::NoMatch);
        // A frame of another size
        QCOMPARE(s.add(noiseImage(kW + 10, 200, 78)), Step::NoMatch);
        QCOMPARE(s.add(noiseImage(kW, 150, 79)), Step::NoMatch);
        QCOMPARE(s.height(), before);
        QCOMPARE(feed(s, content, { 160 }, 200), QList<Step>{ Step::Appended });
        QCOMPARE(firstDifference(s.result(), expected(content, 0, 160, 200)),
                 QString());
    }

    void jumpPastTheViewIsNoMatch()
    {
        const QImage content = noiseImage(kW, 1000, 7);
        ScrollStitcher s;
        QCOMPARE(feed(s, content, { 0, 300 }, 200),
                 (QList<Step>{ Step::Started, Step::NoMatch }));
        QCOMPARE(s.height(), 200);
    }

    void blankAreaMistakenForFooterHeals()
    {
        // Content with a blank stretch: the first two frames both end in
        // white rows, which look like a still footer until content arrives
        const QImage content =
          stack({ noiseImage(kW, 300, 8),
                  solidRows(kW, 100, qRgb(255, 255, 255)),
                  noiseImage(kW, 600, 9) });
        ScrollStitcher s;
        feed(s, content, { 150, 160, 260, 330 }, 200);
        QCOMPARE(s.bands().bottom, 0);
        QCOMPARE(firstDifference(s.result(), expected(content, 150, 330, 200)),
                 QString());
    }

    void scrollBarColumnsDoNotBreakMatching()
    {
        // The right 12 px change on every frame like an overlay scroll bar
        const QImage content = noiseImage(kW, 1000, 10);
        ScrollStitcher s;
        const QList<int> positions{ 0, 40, 130, 260 };
        quint32 seed = 500;
        for (int y : positions) {
            QImage f = rows(content, y, 200);
            const QImage bar = noiseImage(12, 200, seed++);
            for (int r = 0; r < 200; ++r) {
                memcpy(f.scanLine(r) + (kW - 12) * 4, bar.constScanLine(r), 48);
            }
            QVERIFY(s.add(f) != Step::NoMatch);
        }
        const int keep = kW - 24;
        QCOMPARE(firstDifference(s.result().copy(0, 0, keep, s.height()),
                                 expected(content, 0, 260, 200)
                                   .copy(0, 0, keep, 460)),
                 QString());
    }

    void retinaScrollBarIsIgnoredScaledByDpr()
    {
        // Bug: the 24 px ignore band was not scaled by the frame's device
        // pixel ratio, so a 32 px (16 pt) legacy scroll bar on a 2x display
        // stayed inside the hashed columns and every step was NoMatch.
        const int w = 400;
        const int h = 400;
        const QImage content = noiseImage(w, 1200, 20);
        ScrollStitcher s;
        const QList<int> positions{ 0, 60, 120, 180 };
        quint32 seed = 700;
        for (int y : positions) {
            QImage f = rows(content, y, h);
            const QImage bar = noiseImage(32, h, seed++);
            for (int r = 0; r < h; ++r) {
                memcpy(f.scanLine(r) + (w - 32) * 4, bar.constScanLine(r), 128);
            }
            f.setDevicePixelRatio(2.0);
            const Step step = s.add(f);
            QVERIFY2(step != Step::NoMatch, "scroll bar broke matching");
        }
        QCOMPARE(s.height(), 180 + h);
    }

    void maxHeightCapsTheImage()
    {
        const QImage content = noiseImage(kW, 2000, 11);
        Options options;
        options.maxHeight = 500;
        ScrollStitcher s(options);
        const QList<Step> steps =
          feed(s, content, { 0, 150, 300, 450, 600 }, 200);
        QCOMPARE(steps,
                 (QList<Step>{ Step::Started,
                               Step::Appended,
                               Step::MaxHeight,
                               Step::MaxHeight,
                               Step::MaxHeight }));
        QVERIFY(s.isFull());
        QCOMPARE(s.height(), 500);
        QCOMPARE(firstDifference(s.result(), rows(content, 0, 500)), QString());
    }

    void maxHeightKeepsTheFooter()
    {
        const QImage content = noiseImage(kW, 2000, 12);
        const QImage header = noiseImage(kW, 60, 13);
        const QImage footer = noiseImage(kW, 40, 14);
        Options options;
        options.maxHeight = 600;
        ScrollStitcher s(options);
        feed(s, content, { 0, 100, 250, 400 }, 300, header, footer);
        QVERIFY(s.isFull());
        QCOMPARE(s.height(), 600);
        // Header + 500 content rows + footer
        QCOMPARE(firstDifference(
                   s.result(), stack({ header, rows(content, 0, 500), footer })),
                 QString());
    }

    void sameInputGivesSameOutput()
    {
        const QImage content = noiseImage(kW, 1200, 15);
        ScrollStitcher a;
        ScrollStitcher b;
        feed(a, content, { 0, 33, 170, 171, 320 }, 250);
        feed(b, content, { 0, 33, 170, 171, 320 }, 250);
        QCOMPARE(firstDifference(a.result(), b.result()), QString());
    }

    void previewFitsTheBound()
    {
        const QImage content = noiseImage(kW, 1200, 16);
        ScrollStitcher s;
        QVERIFY(s.preview(QSize(80, 120)).isNull());
        feed(s, content, { 0, 150, 300, 450 }, 200);
        const QImage p = s.preview(QSize(80, 120));
        QVERIFY(!p.isNull());
        QVERIFY(p.width() <= 80 && p.height() <= 120);
        QVERIFY(p.height() == 120 || p.width() == 80);
    }

    void fastEnoughForLiveCapture()
    {
        SKIP_TIMING_UNDER_SANITIZER();
        // 1500x900 frames, 60 px apart: at least 30 matches a second
        const QImage content = noiseImage(1500, 900 + 60 * 40, 17);
        QList<QImage> frames;
        for (int i = 0; i <= 40; ++i) {
            frames << rows(content, i * 60, 900);
        }
        ScrollStitcher s(Options{ 100000 });
        QElapsedTimer timer;
        timer.start();
        for (const QImage& f : frames) {
            s.add(f);
        }
        const qint64 ms = timer.elapsed();
        qInfo() << "41 frames stitched in" << ms << "ms";
        QVERIFY2(ms * 30 <= 41 * 1000,
                 qPrintable(QStringLiteral("%1 ms for 41 frames").arg(ms)));
        QCOMPARE(s.height(), 900 + 60 * 40);
    }

    // Auto-scroll stop rule

    void autoStopsAfterUnchangedFrames()
    {
        AutoScrollStop stop(3);
        QVERIFY(!stop.shouldStop(Step::Started));
        QVERIFY(!stop.shouldStop(Step::NoChange));
        QVERIFY(!stop.shouldStop(Step::NoChange));
        QVERIFY(!stop.shouldStop(Step::Appended));
        QCOMPARE(stop.unchangedStreak(), 0);
        QVERIFY(!stop.shouldStop(Step::NoChange));
        QVERIFY(!stop.shouldStop(Step::ScrolledUp));
        QVERIFY(stop.shouldStop(Step::NoChange));
    }

    void autoStopsOnLimitOrLostTrack()
    {
        QVERIFY(AutoScrollStop(3).shouldStop(Step::MaxHeight));
        QVERIFY(AutoScrollStop(3).shouldStop(Step::NoMatch));
    }

    // Helpers

    void sameBandsCountsStillRows()
    {
        const QVector<quint64> a{ 1, 2, 3, 4, 5, 6 };
        const QVector<quint64> b{ 1, 2, 9, 9, 5, 6 };
        QCOMPARE(sameBands(a, b), (Bands{ 2, 2 }));
        QCOMPARE(sameBands(a, a), (Bands{ 6, 0 }));
    }
};

QTEST_MAIN(TestScrollStitch)
#include "tst_scrollstitch.moc"
