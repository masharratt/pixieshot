// SPDX-License-Identifier: GPL-3.0-or-later
// Integration test for the "Animate" export: MP4 through AVAssetWriter and
// GIF through ImageIO, with the code the app uses. macOS only. ARC.

#define QT_NO_KEYWORDS

#include "fakes/annotatedsample.h"
#include "platform/animexport.h"
#include "platform/videoedit.h"
#include "utils/animatedframe.h"
#include "utils/beautifylayout.h"
#include "utils/beautifypalette.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

// One solid box per object that wipes in with its progress: enough to see
// the annotations reach the encoded frames
class BoxLayer : public AnnotationLayer
{
public:
    int count() const override { return 1; }
    void paint(QPainter& painter, int, double progress) override
    {
        painter.fillRect(QRectF(150, 90, 60 * progress, 40), QColor(20, 160, 60));
    }
    QRectF bounds(int) const override { return QRectF(150, 90, 60, 40); }
};

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

    void gifWithMotionAndPaletteLoops()
    {
        AnimExportParams p;
        p.shot = sampleShot();
        p.effects = settings();
        p.effects.palette = beautifypalette::presets().at(0).stops;
        p.style = AnimStyle::Flow;
        p.motion = ShotMotion::Tilt;
        p.durationSec = 3;
        p.fps = 15;
        p.gifPath = m_dir.filePath(QStringLiteral("tilt.gif"));
        AnimExportResult result;
        QString error;
        QVERIFY2(exportAnimationBlocking(p, {}, &result, &error), qPrintable(error));
        double seconds = 0;
        QCOMPARE(gifFrames(p.gifPath, &seconds), 45);
        // The shot moves, so frames differ inside its rect, and the first
        // frame (at rest) is not the frame at mid hold
        QImageReader reader(p.gifPath);
        const QImage first = reader.read();
        QImage middle;
        for (int i = 1; i <= 22; ++i) {
            middle = reader.read();
        }
        QVERIFY(!middle.isNull());
        const QRect shot = AnimatedFrames(p.shot, p.effects, p.style).layout().shot;
        QVERIFY(first.copy(shot) != middle.copy(shot));
    }

    // Draw-in annotations reach the GIF: absent in the first frame, present
    // in the hold, gone again by the last frame
    void gifDrawsAnnotationsIn()
    {
        AnimExportParams p;
        p.shot = sampleShot();
        p.effects = settings();
        p.style = AnimStyle::Aurora;
        p.durationSec = 3;
        p.fps = 15;
        p.annotations = std::make_shared<BoxLayer>();
        p.annotationMode = AnnotationMode::DrawIn;
        p.gifPath = m_dir.filePath(QStringLiteral("drawin.gif"));
        QString error;
        QVERIFY2(exportAnimationBlocking(p, {}, nullptr, &error), qPrintable(error));
        AnimatedFrames frames(p.shot, p.effects, p.style);
        // The middle of the box on the canvas
        const QPoint at = frames.layout().shot.topLeft() + QPoint(180, 110);
        QImageReader reader(p.gifPath);
        QList<QImage> all;
        for (QImage img = reader.read(); !img.isNull(); img = reader.read()) {
            all << img;
        }
        QCOMPARE(all.size(), 45);
        auto greenish = [](QRgb c) {
            return qGreen(c) > 120 && qRed(c) < 80 && qBlue(c) < 110;
        };
        QVERIFY2(!greenish(all.first().pixel(at)), "box drawn in frame 0");
        QVERIFY2(greenish(all.at(22).pixel(at)),
                 qPrintable(QString::number(all.at(22).pixel(at), 16)));
        QVERIFY2(!greenish(all.last().pixel(at)), "box still drawn at the loop end");
    }

    // Opt-in: real annotations drawing themselves in, as MP4 and GIF
    void saveAnnotatedSampleLoop()
    {
        const QString dir = qEnvironmentVariable("PIXIE_ANIM_SAMPLES");
        if (dir.isEmpty()) {
            QSKIP("Set PIXIE_ANIM_SAMPLES to a folder to save a sample loop");
        }
        QDir().mkpath(dir);
        const PreparedAnimSource prepared = testtools::annotatedSample();

        AnimExportParams p;
        p.shot = prepared.base;
        p.annotations = prepared.annotations;
        p.annotationMode = AnnotationMode::DrawIn;
        p.effects = settings();
        p.effects.cornerRadius = 12;
        p.effects.palette = beautifypalette::presets().at(0).stops;
        p.style = AnimStyle::Flow;
        p.motion = ShotMotion::Tilt;
        p.durationSec = 5;
        p.fps = 30;
        p.mp4Path = dir + QStringLiteral("/drawin-tilt-flow.mp4");
        p.gifPath = dir + QStringLiteral("/drawin-tilt-flow.gif");
        AnimExportResult result;
        QString error;
        if (!exportAnimationBlocking(p, {}, &result, &error)) {
            if (result.encoderUnavailable) {
                p.mp4Path.clear();
                QVERIFY2(exportAnimationBlocking(p, {}, &result, &error),
                         qPrintable(error));
                QSKIP("No H.264 encoder: only the GIF was written");
            }
            QFAIL(qPrintable(error));
        }
        qInfo("draw-in mp4 %lld bytes, gif %lld bytes",
              QFileInfo(p.mp4Path).size(),
              QFileInfo(p.gifPath).size());
    }

    // Opt-in: writes a 5 s Tilt + Sunset Pop + Flow loop as MP4 and GIF
    void saveSampleLoop()
    {
        const QString dir = qEnvironmentVariable("PIXIE_ANIM_SAMPLES");
        if (dir.isEmpty()) {
            QSKIP("Set PIXIE_ANIM_SAMPLES to a folder to save a sample loop");
        }
        QDir().mkpath(dir);
        AnimExportParams p;
        QImage shot(QSize(900, 560), QImage::Format_ARGB32_Premultiplied);
        shot.fill(QColor(245, 246, 250));
        {
            QPainter painter(&shot);
            painter.fillRect(QRect(0, 0, 900, 36), QColor(228, 230, 236));
            for (int i = 0; i < 8; ++i) {
                painter.fillRect(QRect(40, 70 + 34 * i, 700 - (i * 97) % 260, 12),
                                 QColor(90, 96, 110));
            }
            painter.fillRect(QRect(40, 360, 820, 160), QColor(70, 130, 220));
        }
        shot.setDevicePixelRatio(2.0);
        p.shot = shot;
        p.effects = settings();
        p.effects.cornerRadius = 12;
        p.effects.palette = beautifypalette::presets().at(0).stops;
        p.style = AnimStyle::Flow;
        p.motion = ShotMotion::Tilt;
        p.durationSec = 5;
        p.fps = 30;
        p.mp4Path = dir + QStringLiteral("/tilt-sunset-flow.mp4");
        p.gifPath = dir + QStringLiteral("/tilt-sunset-flow.gif");
        AnimExportResult result;
        QString error;
        if (!exportAnimationBlocking(p, {}, &result, &error)) {
            if (result.encoderUnavailable) {
                p.mp4Path.clear();
                QVERIFY2(exportAnimationBlocking(p, {}, &result, &error),
                         qPrintable(error));
                QSKIP("No H.264 encoder: only the GIF was written");
            }
            QFAIL(qPrintable(error));
        }
        // Silk as a GIF too: its fine strands must stay smooth in 256 colours
        AnimExportParams silk = p;
        silk.style = AnimStyle::Silk;
        silk.mp4Path.clear();
        silk.gifPath = dir + QStringLiteral("/tilt-sunset-silk.gif");
        QVERIFY2(exportAnimationBlocking(silk, {}, nullptr, &error), qPrintable(error));
        qInfo("silk gif %lld bytes", QFileInfo(silk.gifPath).size());
        qInfo("mp4 %lld bytes %dx%d, gif %lld bytes %dx%d",
              QFileInfo(p.mp4Path).size(),
              result.mp4Size.width(),
              result.mp4Size.height(),
              QFileInfo(p.gifPath).size(),
              result.gifSize.width(),
              result.gifSize.height());
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
