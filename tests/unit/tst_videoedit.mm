// SPDX-License-Identifier: GPL-3.0-or-later
// Integration test for the recording editor's export: a synthetic MP4 is
// written with AVAssetWriter, then trimmed and beautified with the same code
// the app uses. macOS only. Compiled with ARC.

#define QT_NO_KEYWORDS

#include "platform/videoedit.h"
#include "platform/videothumbnail.h"
#include "utils/beautifylayout.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

namespace {

const QSize kSource(160, 120);
const int kFps = 30;
const double kSeconds = 2.0;
// Source frames are this colour (BGRA order below)
const QColor kFrameColor(220, 40, 40);

NSString* ns(const QString& s)
{
    return [NSString stringWithUTF8String:s.toUtf8().constData()];
}

enum class Written
{
    Ok,
    NoEncoder, // the system H.264 encoder is unreachable (e.g. a sandbox)
    Failed,
};

// 2 s of solid frames, H.264 MP4
Written writeSyntheticMp4(const QString& path)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:ns(path)];
        NSError* error = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url
                                                         fileType:AVFileTypeMPEG4
                                                            error:&error];
        if (writer == nil) {
            return Written::Failed;
        }
        AVAssetWriterInput* input = [AVAssetWriterInput
          assetWriterInputWithMediaType:AVMediaTypeVideo
                         outputSettings:@{
                             AVVideoCodecKey : AVVideoCodecTypeH264,
                             AVVideoWidthKey : @(kSource.width()),
                             AVVideoHeightKey : @(kSource.height()),
                         }];
        input.expectsMediaDataInRealTime = NO;
        AVAssetWriterInputPixelBufferAdaptor* adaptor =
          [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                       sourcePixelBufferAttributes:@{
                                           (NSString*)
                                           kCVPixelBufferPixelFormatTypeKey :
                                             @(kCVPixelFormatType_32BGRA),
                                           (NSString*)kCVPixelBufferWidthKey :
                                             @(kSource.width()),
                                           (NSString*)kCVPixelBufferHeightKey :
                                             @(kSource.height()),
                                       }];
        [writer addInput:input];
        if (![writer startWriting]) {
            qWarning("startWriting: %s",
                     writer.error.localizedDescription.UTF8String);
            return Written::NoEncoder;
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        const int frames = int(kSeconds * kFps);
        for (int i = 0; i < frames; ++i) {
            while (!input.readyForMoreMediaData) {
                [NSThread sleepForTimeInterval:0.005];
            }
            CVPixelBufferRef buffer = NULL;
            CVPixelBufferPoolCreatePixelBuffer(
              NULL, adaptor.pixelBufferPool, &buffer);
            if (buffer == NULL) {
                qWarning("no pixel buffer (writer status %ld)",
                         long(writer.status));
                return Written::Failed;
            }
            CVPixelBufferLockBaseAddress(buffer, 0);
            auto* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
            const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
            for (int y = 0; y < kSource.height(); ++y) {
                uint8_t* row = base + y * stride;
                for (int x = 0; x < kSource.width(); ++x) {
                    row[4 * x + 0] = uint8_t(kFrameColor.blue());
                    row[4 * x + 1] = uint8_t(kFrameColor.green());
                    row[4 * x + 2] = uint8_t(kFrameColor.red());
                    row[4 * x + 3] = 255;
                }
            }
            CVPixelBufferUnlockBaseAddress(buffer, 0);
            [adaptor appendPixelBuffer:buffer
                  withPresentationTime:CMTimeMake(i, kFps)];
            CVPixelBufferRelease(buffer);
        }
        [input markAsFinished];
        [writer endSessionAtSourceTime:CMTimeMake(frames, kFps)];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (writer.status != AVAssetWriterStatusCompleted) {
            qWarning("finishWriting: %s",
                     writer.error.localizedDescription.UTF8String);
        }
        return writer.status == AVAssetWriterStatusCompleted ? Written::Ok
                                                             : Written::Failed;
    }
}

bool near(const QColor& a, const QColor& b, int tolerance)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

QString describe(const QColor& c)
{
    return QStringLiteral("(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

class TestVideoEdit : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_source;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_source = m_dir.filePath(QStringLiteral("source.mp4"));
        const Written written = writeSyntheticMp4(m_source);
        if (written == Written::NoEncoder) {
            QSKIP("No H.264 encoder reachable (sandboxed run?): nothing "
                  "was exported or checked");
        }
        QVERIFY(written == Written::Ok);
        const VideoInfo info = probeVideo(m_source);
        QVERIFY(info.ok);
        QCOMPARE(info.pixels, kSource);
        QVERIFY2(std::abs(info.duration - kSeconds) < 0.1,
                 qPrintable(QString::number(info.duration)));
    }

    void trimPassthroughKeepsTheChosenSecond()
    {
        VideoExportParams p;
        p.input = m_source;
        p.output = m_dir.filePath(QStringLiteral("trimmed.mp4"));
        p.range = { 0.5, 1.5 };
        QString error;
        QVERIFY2(exportVideoBlocking(p, &error), qPrintable(error));
        const VideoInfo info = probeVideo(p.output);
        QVERIFY(info.ok);
        QVERIFY2(std::abs(info.duration - 1.0) <= 0.1,
                 qPrintable(QString::number(info.duration)));
        QCOMPARE(info.pixels, kSource);
    }

    void exportReplacesAnExistingOutput()
    {
        VideoExportParams p;
        p.input = m_source;
        p.output = m_dir.filePath(QStringLiteral("again.mp4"));
        p.range = { 0.0, 1.0 };
        QString error;
        QVERIFY2(exportVideoBlocking(p, &error), qPrintable(error));
        p.range = { 0.0, 0.5 };
        QVERIFY2(exportVideoBlocking(p, &error), qPrintable(error));
        QVERIFY(std::abs(probeVideo(p.output).duration - 0.5) <= 0.1);
    }

    void invalidRangeIsRefused()
    {
        VideoExportParams p;
        p.input = m_source;
        p.output = m_dir.filePath(QStringLiteral("bad.mp4"));
        p.range = { 1.5, 0.5 };
        QString error;
        QVERIFY(!exportVideoBlocking(p, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(p.output));
    }

    void beautifyOutputIsTheCanvas()
    {
        EffectSettings s;
        s.borderPx = 2;
        s.cornerRadius = 8;
        s.shadow = true;
        s.paddingPx = 20;
        s.backgroundMode = BackgroundMode::Solid;
        s.backgroundColor1 = QColor(40, 80, 220);
        s.aspect = 2;

        VideoExportParams p;
        p.input = m_source;
        p.output = m_dir.filePath(QStringLiteral("beautified.mp4"));
        p.range = { 0.5, 1.5 };
        p.beautify = true;
        p.effects = s;
        p.dpr = 1.0;
        QString error;
        QVERIFY2(exportVideoBlocking(p, &error), qPrintable(error));

        const BeautifyLayout layout = beautifyLayout(kSource, s, 1.0);
        const VideoInfo info = probeVideo(p.output);
        QVERIFY(info.ok);
        // The aspect preset makes an odd width here, so this also covers the
        // even rounding H.264 needs
        QCOMPARE(layout.canvas.width() % 2, 1);
        QCOMPARE(info.pixels, videoCanvasSize(layout.canvas));
        QVERIFY2(std::abs(info.duration - 1.0) <= 0.1,
                 qPrintable(QString::number(info.duration)));

        // The frame shows in the shot area and the background around it
        const QImage frame = videoFrameAt(p.output, 0.5, QSize());
        QCOMPARE(frame.size(), videoCanvasSize(layout.canvas));
        const QColor center = frame.pixelColor(layout.frame.center());
        const QColor corner = frame.pixelColor(2, 2);
        QVERIFY2(near(center, kFrameColor, 24), qPrintable(describe(center)));
        QVERIFY2(near(corner, s.backgroundColor1, 24),
                 qPrintable(describe(corner)));
    }

    // From screenshot + glass: the static layer is drawn once from the first
    // frame (red), so the background around the bezel is reddish, and the
    // canvas grows by the bezel like the still
    void beautifyGlassFromScreenshotUsesTheFirstFrame()
    {
        EffectSettings s;
        s.cornerRadius = 8;
        s.paddingPx = 24;
        s.backgroundMode = BackgroundMode::FromScreenshot;
        s.glass = true;
        s.glassPx = 10;
        s.grain = 10;

        VideoExportParams p;
        p.input = m_source;
        p.output = m_dir.filePath(QStringLiteral("glass.mp4"));
        p.range = { 0.0, 1.0 };
        p.beautify = true;
        p.effects = s;
        p.dpr = 1.0;
        QString error;
        QVERIFY2(exportVideoBlocking(p, &error), qPrintable(error));

        const BeautifyLayout layout = beautifyLayout(kSource, s, 1.0);
        QCOMPARE(layout.bezel.adjusted(10, 10, -10, -10), layout.shot);
        const VideoInfo info = probeVideo(p.output);
        QVERIFY(info.ok);
        QCOMPARE(info.pixels, videoCanvasSize(layout.canvas));

        const QImage frame = videoFrameAt(p.output, 0.5, QSize());
        const QColor center = frame.pixelColor(layout.frame.center());
        const QColor corner = frame.pixelColor(3, 3);
        QVERIFY2(near(center, kFrameColor, 24), qPrintable(describe(center)));
        QVERIFY2(corner.red() > corner.blue() + 60 &&
                   corner.red() > corner.green() + 60,
                 qPrintable(describe(corner)));
    }
};

QTEST_MAIN(TestVideoEdit)
#include "tst_videoedit.moc"
