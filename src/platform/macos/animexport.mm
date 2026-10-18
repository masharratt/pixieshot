// SPDX-License-Identifier: GPL-3.0-or-later
// "Animate" export. Frames come from AnimatedFrames (pure C++); this file
// only encodes them: H.264 through AVAssetWriter, GIF through ImageIO with
// the gifquant palette. Compiled with ARC. Runs on a worker thread.

#define QT_NO_KEYWORDS

#include "platform/animexport.h"
#include "platform/macos/gifwriter.h"
#include "utils/animatedframe.h"
#include "utils/gifquant.h"

#include <QCoreApplication>
#include <QMetaObject>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstring>
#include <thread>

// This file relies on automatic reference counting. Without it, objects held
// across asynchronous callbacks are freed early and the app crashes.
#if !__has_feature(objc_arc)
#error "Compile this file with -fobjc-arc"
#endif

namespace {

// H.264 bits per pixel per frame: the backgrounds are soft and the shot is
// static, so this is plenty and keeps a 5 s 1080p loop around 3 MB
constexpr double kBitsPerPixel = 0.08;
constexpr double kMinBitrate = 1.5e6;
constexpr double kMaxBitrate = 12e6;
// Palette samples per source (each background phase, and the shot)
constexpr int kSamplesPerSource = 12000;

NSString* toNSString(const QString& s)
{
    return [NSString stringWithUTF8String:s.toUtf8().constData()];
}

QString fromNSString(NSString* s)
{
    return s == nil ? QString() : QString::fromUtf8(s.UTF8String);
}

// Progress over both files, reported per frame
class Progress
{
public:
    Progress(const std::function<void(double)>& report, int total)
      : m_report(report)
      , m_total(std::max(1, total))
    {}
    void step()
    {
        ++m_done;
        if (m_report) {
            m_report(double(m_done) / m_total);
        }
    }

private:
    const std::function<void(double)>& m_report;
    int m_total;
    int m_done = 0;
};

enum class Mp4Result
{
    Ok,
    NoEncoder,
    Failed,
};

Mp4Result writeMp4(const AnimExportParams& p,
                   Progress& progress,
                   QSize* size,
                   QString* error)
{
    AnimatedFrames frames(
      p.shot, p.effects, p.style, p.seed, kAnimMp4MaxLongSide, true);
    frames.setMotion(p.motion, p.durationSec);
    const QSize px = frames.size();
    *size = px;
    const int fps = std::clamp(p.fps, 1, 120);
    const int count = animFrameCount(p.durationSec, fps);

    NSURL* url = [NSURL fileURLWithPath:toNSString(p.mp4Path)];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    NSError* nsError = nil;
    AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url
                                                     fileType:AVFileTypeMPEG4
                                                        error:&nsError];
    if (writer == nil) {
        *error = fromNSString(nsError.localizedDescription);
        return Mp4Result::Failed;
    }
    const double bitrate = std::clamp(
      double(px.width()) * px.height() * fps * kBitsPerPixel, kMinBitrate, kMaxBitrate);
    AVAssetWriterInput* input = [AVAssetWriterInput
      assetWriterInputWithMediaType:AVMediaTypeVideo
                     outputSettings:@{
                         AVVideoCodecKey : AVVideoCodecTypeH264,
                         AVVideoWidthKey : @(px.width()),
                         AVVideoHeightKey : @(px.height()),
                         AVVideoCompressionPropertiesKey : @{
                             AVVideoAverageBitRateKey : @(bitrate),
                             AVVideoMaxKeyFrameIntervalKey : @(fps * 2),
                             AVVideoProfileLevelKey :
                               AVVideoProfileLevelH264HighAutoLevel,
                         },
                         // Tag the colours so players do not shift them
                         AVVideoColorPropertiesKey : @{
                             AVVideoColorPrimariesKey :
                               AVVideoColorPrimaries_ITU_R_709_2,
                             AVVideoTransferFunctionKey :
                               AVVideoTransferFunction_ITU_R_709_2,
                             AVVideoYCbCrMatrixKey : AVVideoYCbCrMatrix_ITU_R_709_2,
                         },
                     }];
    input.expectsMediaDataInRealTime = NO;
    AVAssetWriterInputPixelBufferAdaptor* adaptor =
      [AVAssetWriterInputPixelBufferAdaptor
        assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                   sourcePixelBufferAttributes:@{
                                       (NSString*)kCVPixelBufferPixelFormatTypeKey :
                                         @(kCVPixelFormatType_32BGRA),
                                       (NSString*)kCVPixelBufferWidthKey :
                                         @(px.width()),
                                       (NSString*)kCVPixelBufferHeightKey :
                                         @(px.height()),
                                   }];
    [writer addInput:input];
    if (![writer startWriting]) {
        *error = fromNSString(writer.error.localizedDescription);
        return Mp4Result::NoEncoder;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];

    auto fail = [&](const QString& message) {
        [writer cancelWriting];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
        *error = message;
        return Mp4Result::Failed;
    };
    for (int i = 0; i < count; ++i) {
        @autoreleasepool {
            // Format_RGB32 is B, G, R, A in memory: the buffer's layout
            const QImage frame = frames.frame(double(i) / count);
            while (!input.readyForMoreMediaData) {
                if (writer.status != AVAssetWriterStatusWriting) {
                    return fail(fromNSString(writer.error.localizedDescription));
                }
                [NSThread sleepForTimeInterval:0.002];
            }
            CVPixelBufferRef buffer = NULL;
            CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buffer);
            if (buffer == NULL) {
                return fail(QStringLiteral("Could not get a video frame buffer."));
            }
            CVPixelBufferLockBaseAddress(buffer, 0);
            auto* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
            const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
            const size_t rowBytes = size_t(px.width()) * 4;
            for (int y = 0; y < px.height(); ++y) {
                std::memcpy(base + y * stride, frame.constScanLine(y), rowBytes);
            }
            CVPixelBufferUnlockBaseAddress(buffer, 0);
            const BOOL appended = [adaptor appendPixelBuffer:buffer
                                        withPresentationTime:CMTimeMake(i, fps)];
            CVPixelBufferRelease(buffer);
            if (!appended) {
                return fail(fromNSString(writer.error.localizedDescription));
            }
            progress.step();
        }
    }
    [input markAsFinished];
    // The loop's length is exactly count / fps: the last frame lasts 1/fps
    [writer endSessionAtSourceTime:CMTimeMake(count, fps)];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{
        dispatch_semaphore_signal(done);
    }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (writer.status != AVAssetWriterStatusCompleted) {
        return fail(fromNSString(writer.error.localizedDescription));
    }
    return Mp4Result::Ok;
}

// An indexed CGImage that shares the palette, so ImageIO writes these
// colours instead of picking its own per frame
CGImageRef indexedImage(const QImage& indexed, CGColorSpaceRef space)
{
    NSData* bytes = [NSData dataWithBytes:indexed.constBits()
                                   length:size_t(indexed.sizeInBytes())];
    CGDataProviderRef provider =
      CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes);
    CGImageRef image = CGImageCreate(size_t(indexed.width()),
                                     size_t(indexed.height()),
                                     8,
                                     8,
                                     size_t(indexed.bytesPerLine()),
                                     space,
                                     kCGImageAlphaNone,
                                     provider,
                                     NULL,
                                     false,
                                     kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    return image;
}

bool writeGif(const AnimExportParams& p,
              Progress& progress,
              QSize* size,
              int* frameCount,
              QString* error)
{
    // No grain: the ordered dither already breaks up banding, and noise
    // defeats the GIF's LZW compression (it roughly doubles the file)
    EffectSettings effects = p.effects;
    effects.grain = 0;
    AnimatedFrames frames(
      p.shot, effects, p.style, p.seed, kAnimGifMaxLongSide, false);
    frames.setMotion(p.motion, p.durationSec);
    *size = frames.size();
    const int count = animFrameCount(p.durationSec, animGifFps(p.fps));
    *frameCount = count;

    // One palette for the whole loop: the background at four phases and
    // the shot, sampled equally so neither crowds the other out
    QVector<QRgb> samples;
    for (double phase : { 0.0, 0.25, 0.5, 0.75 }) {
        samples += gifquant::samplePixels(frames.frame(phase), kSamplesPerSource);
    }
    samples += gifquant::samplePixels(frames.shotLayer(), 4 * kSamplesPerSource);
    const gifquant::Palette palette = gifquant::Palette::fromSamples(samples);
    QByteArray table;
    for (QRgb c : palette.colors()) {
        table.append(char(qRed(c)));
        table.append(char(qGreen(c)));
        table.append(char(qBlue(c)));
    }
    CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorSpaceRef space = CGColorSpaceCreateIndexed(
      rgb,
      size_t(palette.colors().size() - 1),
      reinterpret_cast<const unsigned char*>(table.constData()));
    CGColorSpaceRelease(rgb);

    const QList<int> delays = gifquant::frameDelaysCs(count, p.durationSec);
    GifWriter gif(p.gifPath, size_t(count));
    bool ok = gif.isOpen();
    if (!ok) {
        *error = QStringLiteral("Could not create the GIF file.");
    }
    for (int i = 0; ok && i < count; ++i) {
        @autoreleasepool {
            // The shot is not dithered: at rest its pixels stay the same in
            // every frame, and moving it would make the dither crawl
            const double phase = double(i) / count;
            const QImage indexed = gifquant::quantize(
              frames.frame(phase),
              palette,
              frames.shotBounds(
                shotmotion::poseAt(p.motion, phase, p.durationSec)));
            CGImageRef image = indexedImage(indexed, space);
            if (image == NULL) {
                *error = QStringLiteral("Could not make a GIF frame.");
                ok = false;
                break;
            }
            gif.add(image, delays.at(i) / 100.0);
            CGImageRelease(image);
            progress.step();
        }
    }
    CGColorSpaceRelease(space);
    if (!ok) {
        gif.discard();
        return false;
    }
    if (!gif.finish()) {
        *error = QStringLiteral("Could not write the GIF file.");
        return false;
    }
    return true;
}

} // namespace

int animFrameCount(int durationSec, int fps)
{
    return std::max(1, durationSec * fps);
}

int animGifFps(int fps)
{
    return std::clamp(fps, 1, kAnimGifMaxFps);
}

bool exportAnimationBlocking(const AnimExportParams& p,
                             const std::function<void(double)>& report,
                             AnimExportResult* result,
                             QString* error)
{
    AnimExportResult local;
    AnimExportResult& r = result != nullptr ? *result : local;
    if (p.shot.isNull()) {
        *error = QStringLiteral("There is no image to animate.");
        return false;
    }
    const bool mp4 = !p.mp4Path.isEmpty();
    const bool gif = !p.gifPath.isEmpty();
    const int total = (mp4 ? animFrameCount(p.durationSec, p.fps) : 0) +
                      (gif ? animFrameCount(p.durationSec, animGifFps(p.fps)) : 0);
    Progress progress(report, total);
    @autoreleasepool {
        if (mp4) {
            const Mp4Result written = writeMp4(p, progress, &r.mp4Size, error);
            if (written != Mp4Result::Ok) {
                r.encoderUnavailable = written == Mp4Result::NoEncoder;
                return false;
            }
        }
        if (gif && !writeGif(p, progress, &r.gifSize, &r.gifFrames, error)) {
            if (mp4) {
                // All or nothing, so a retry does not leave a stray file
                [[NSFileManager defaultManager]
                  removeItemAtURL:[NSURL fileURLWithPath:toNSString(p.mp4Path)]
                            error:nil];
            }
            return false;
        }
    }
    return true;
}

void exportAnimationAsync(
  const AnimExportParams& params,
  std::function<void(double)> progress,
  std::function<void(bool ok, QString error, AnimExportResult result)> done)
{
    std::thread([params, progress, done]() {
        // Coalesced: one queued update per whole percent
        int lastPercent = -1;
        const std::function<void(double)> report = [&](double fraction) {
            const int percent = int(fraction * 100);
            if (percent == lastPercent || !progress) {
                return;
            }
            lastPercent = percent;
            QMetaObject::invokeMethod(
              QCoreApplication::instance(),
              [progress, fraction]() { progress(fraction); },
              Qt::QueuedConnection);
        };
        AnimExportResult result;
        QString error;
        const bool ok = exportAnimationBlocking(params, report, &result, &error);
        QMetaObject::invokeMethod(
          QCoreApplication::instance(),
          [done, ok, error, result]() { done(ok, error, result); },
          Qt::QueuedConnection);
    }).detach();
}
