// SPDX-License-Identifier: GPL-3.0-or-later
// Integration test for the "Animate" export: MP4 through AVAssetWriter and
// GIF through ImageIO, with the code the app uses. macOS only. ARC.

#define QT_NO_KEYWORDS

#include "platform/animexport.h"
#include "platform/videoedit.h"
#include "utils/animatedframe.h"
#include "utils/beautifylayout.h"

#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

namespace {

QImage sampleShot()
{
    QImage img(QSize(241, 151), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(245, 246, 250));
    QPainter p(&img);
    p.fillRect(QRect(20, 30, 120, 40), QColor(236, 72, 96));
    p.end();
    img.setDevicePixelRatio(1.0);
    return img;
}

EffectSettings settings()
{
    EffectSettings s;
    s.cornerRadius = 8;
    s.shadow = true;
    s.paddingPx = 32;
    s.backgroundMode = BackgroundMode::Gradient;
    s.glass = true;
    s.glassPx = 10;
    return s;
}

// Frame count and total delay of a GIF, read back through ImageIO
int gifFrames(const QString& path, double* seconds)
{
    NSURL* url = [NSURL
      fileURLWithPath:[NSString stringWithUTF8String:path.toUtf8().constData()]];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    if (src == NULL) {
        return -1;
    }
    const size_t n = CGImageSourceGetCount(src);
    *seconds = 0;
    for (size_t i = 0; i < n; ++i) {
        NSDictionary* props =
          (__bridge_transfer NSDictionary*)CGImageSourceCopyPropertiesAtIndex(src, i, NULL);
        NSDictionary* gif = props[(__bridge NSString*)kCGImagePropertyGIFDictionary];
        *seconds +=
          [gif[(__bridge NSString*)kCGImagePropertyGIFUnclampedDelayTime] doubleValue];
    }
    CFRelease(src);
    return int(n);
}

} // namespace

class TestAnimExport : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private Q_SLOTS:
    void mp4HasTheLoopLengthAndCanvasSize()
    {
        AnimExportParams p;
        p.shot = sampleShot();
        p.effects = settings();
        p.style = AnimStyle::Blobs;
        p.durationSec = 2;
        p.fps = 15;
        p.mp4Path = m_dir.filePath(QStringLiteral("loop.mp4"));
        AnimExportResult result;
        QString error;
        double lastProgress = 0;
        const bool ok = exportAnimationBlocking(
          p, [&](double f) { lastProgress = f; }, &result, &error);
        if (!ok && result.encoderUnavailable) {
            QSKIP("No H.264 encoder reachable (sandboxed run?): nothing was "
                  "exported or checked");
        }
        QVERIFY2(ok, qPrintable(error));
        QCOMPARE(lastProgress, 1.0);
        const QSize expected = videoCanvasSize(
          beautifyLayout(p.shot.size(), AnimatedFrames::animatedSettings(p.effects), 1.0)
            .canvas);
        QCOMPARE(result.mp4Size, expected);
        const VideoInfo info = probeVideo(p.mp4Path);
        QVERIFY(info.ok);
        QCOMPARE(info.pixels, expected);
        QVERIFY2(std::abs(info.duration - 2.0) <= 0.1,
                 qPrintable(QString::number(info.duration)));
    }

    void gifHasOneFramePerStepAndLoopsExactly()
    {
        AnimExportParams p;
        p.shot = sampleShot();
        p.effects = settings();
        p.style = AnimStyle::Aurora;
        p.durationSec = 3;
        p.fps = 30; // the GIF drops to 15
        p.gifPath = m_dir.filePath(QStringLiteral("loop.gif"));
        AnimExportResult result;
        QString error;
        QVERIFY2(exportAnimationBlocking(p, {}, &result, &error), qPrintable(error));
        QCOMPARE(result.gifFrames, 45);
        double seconds = 0;
        QCOMPARE(gifFrames(p.gifPath, &seconds), 45);
        QVERIFY2(std::abs(seconds - 3.0) < 0.005, qPrintable(QString::number(seconds)));
        QImageReader reader(p.gifPath);
        QCOMPARE(reader.size(), result.gifSize);
        QCOMPARE(result.gifSize,
                 beautifyLayout(
                   p.shot.size(), AnimatedFrames::animatedSettings(p.effects), 1.0)
                   .canvas);
    }

    void missingImageIsRefused()
    {
        AnimExportParams p;
        p.gifPath = m_dir.filePath(QStringLiteral("none.gif"));
        QString error;
        QVERIFY(!exportAnimationBlocking(p, {}, nullptr, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(p.gifPath));
    }
};

QTEST_MAIN(TestAnimExport)
#include "tst_animexport.moc"
