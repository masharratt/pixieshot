// SPDX-License-Identifier: GPL-3.0-or-later
// Trim and Beautify export for screen recordings with AVAssetExportSession.
// Trim alone copies the frames (passthrough). Beautify re-encodes through a
// Core Image video composition: the static parts of the canvas (background,
// glass bezel, shadow, border) are drawn once by imageeffects, and each
// frame is placed on top at the spot beautifyLayout() gives, rounded like the
// still. Backgrounds made from the shot (From screenshot, auto colors) use
// the first exported frame.
// Compiled with ARC.

#define QT_NO_KEYWORDS

#include "platform/videoedit.h"
#include "platform/videothumbnail.h"
#include "utils/beautifylayout.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPainter>

#import <AVFoundation/AVFoundation.h>
#import <CoreImage/CoreImage.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <thread>

// This file relies on automatic reference counting. Without it, objects held
// across asynchronous callbacks are freed early and the app crashes.
#if !__has_feature(objc_arc)
#error "Compile this file with -fobjc-arc"
#endif

namespace {

NSString* toNSString(const QString& s)
{
    return [NSString stringWithUTF8String:s.toUtf8().constData()];
}

QString fromNSString(NSString* s)
{
    return s == nil ? QString() : QString::fromUtf8(s.UTF8String);
}

// Loads duration and tracks, blocking the (worker) thread. nil on failure.
AVURLAsset* loadAsset(const QString& path)
{
    AVURLAsset* asset =
      [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:toNSString(path)]
                          options:@{
                              AVURLAssetPreferPreciseDurationAndTimingKey : @YES
                          }];
    dispatch_semaphore_t loaded = dispatch_semaphore_create(0);
    [asset loadValuesAsynchronouslyForKeys:@[ @"duration", @"tracks" ]
                         completionHandler:^{
                             dispatch_semaphore_signal(loaded);
                         }];
    dispatch_semaphore_wait(loaded, DISPATCH_TIME_FOREVER);
    NSError* error = nil;
    if ([asset statusOfValueForKey:@"duration" error:&error] !=
          AVKeyValueStatusLoaded ||
        [asset statusOfValueForKey:@"tracks" error:&error] !=
          AVKeyValueStatusLoaded) {
        return nil;
    }
    return asset;
}

// The sync accessors are deprecated but safe once the keys are loaded
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
AVAssetTrack* videoTrack(AVAsset* asset)
{
    return [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
}

double durationOf(AVAsset* asset)
{
    return CMTimeGetSeconds(asset.duration);
}

QSize pixelsOf(AVAssetTrack* track)
{
    const CGSize size =
      CGSizeApplyAffineTransform(track.naturalSize, track.preferredTransform);
    return QSize(qRound(std::fabs(size.width)), qRound(std::fabs(size.height)));
}
#pragma clang diagnostic pop

// Physical pixel copy of an image as a Core Image image
CIImage* ciImageFrom(const QImage& image)
{
    QImage plain = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    plain.setDevicePixelRatio(1.0);
    NSData* bytes = [NSData dataWithBytes:plain.constBits()
                                   length:size_t(plain.sizeInBytes())];
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CIImage* result = [CIImage
      imageWithBitmapData:bytes
              bytesPerRow:size_t(plain.bytesPerLine())
                     size:CGSizeMake(plain.width(), plain.height())
                   format:kCIFormatBGRA8
               colorSpace:space];
    CGColorSpaceRelease(space);
    return result;
}

// Background, glass, shadow and border for the canvas, drawn once by
// imageeffects on a stand-in shot that the frames cover: the first frame
// when the background is made from the shot, black otherwise. A video has
// no alpha, so whatever is transparent ends up black.
CIImage* backdropFor(QSize sourcePx,
                     EffectSettings settings,
                     qreal dpr,
                     const QImage& firstFrame)
{
    settings.autoBalance = false; // frames are placed unbalanced
    QImage standIn(sourcePx, QImage::Format_ARGB32_Premultiplied);
    standIn.fill(Qt::black);
    if (!firstFrame.isNull()) {
        QPainter p(&standIn);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QImage frame = firstFrame;
        frame.setDevicePixelRatio(1.0);
        p.drawImage(standIn.rect(), frame);
    }
    standIn.setDevicePixelRatio(dpr);
    QImage drawn = imageeffects::applyEffects(standIn, settings);
    drawn.setDevicePixelRatio(1.0);
    QImage opaque(drawn.size(), QImage::Format_ARGB32_Premultiplied);
    opaque.fill(Qt::black);
    {
        QPainter p(&opaque);
        p.drawImage(0, 0, drawn);
    }
    return ciImageFrom(opaque);
}

// Core Image puts the origin at the bottom left
CGRect toCI(const QRect& r, int canvasHeight)
{
    return CGRectMake(
      r.x(), canvasHeight - r.y() - r.height(), r.width(), r.height());
}

AVVideoComposition* beautifyComposition(AVAsset* asset,
                                        QSize sourcePx,
                                        const EffectSettings& settings,
                                        qreal dpr,
                                        const QImage& firstFrame)
{
    const BeautifyLayout layout = beautifyLayout(sourcePx, settings, dpr);
    const QSize video = videoCanvasSize(layout.canvas);
    // Core Image's origin is the bottom left, so an extra row for the even
    // size goes below the still's canvas: lay out against the video height
    // and lift the backdrop by the difference
    const int canvasH = video.height();
    const int lift = video.height() - layout.canvas.height();
    const CGRect canvasRect = CGRectMake(0, 0, video.width(), video.height());
    const CGRect frameRect = toCI(layout.frame, canvasH);
    // Extending the edge pixels fills the extra column or row with the
    // background instead of black
    CIImage* backdrop = [[[backdropFor(sourcePx, settings, dpr, firstFrame)
      imageByApplyingTransform:CGAffineTransformMakeTranslation(0, lift)]
      imageByClampingToExtent] imageByCroppingToRect:canvasRect];
    CIImage* mask = nil;
    if (layout.cornerRadius > 0) {
        CIFilter* rounded = [CIFilter filterWithName:@"CIRoundedRectangleGenerator"];
        [rounded setValue:[CIVector vectorWithCGRect:toCI(layout.shot, canvasH)]
                   forKey:@"inputExtent"];
        [rounded setValue:@(layout.cornerRadius) forKey:@"inputRadius"];
        [rounded setValue:[CIColor whiteColor] forKey:@"inputColor"];
        mask = rounded.outputImage;
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    AVMutableVideoComposition* composition = [AVMutableVideoComposition
      videoCompositionWithAsset:asset
       applyingCIFiltersWithHandler:^(
         AVAsynchronousCIImageFilteringRequest* request) {
           CIImage* source = request.sourceImage;
           const CGRect extent = source.extent;
           CIImage* placed = [[source
             imageByApplyingTransform:CGAffineTransformMakeTranslation(
                                        frameRect.origin.x - extent.origin.x,
                                        frameRect.origin.y - extent.origin.y)]
             imageByCroppingToRect:frameRect];
           if (mask != nil) {
               placed = [placed
                 imageByApplyingFilter:@"CIBlendWithAlphaMask"
                   withInputParameters:@{
                       kCIInputBackgroundImageKey : [CIImage emptyImage],
                       kCIInputMaskImageKey : mask,
                   }];
           }
           CIImage* out = [[placed imageByCompositingOverImage:backdrop]
             imageByCroppingToRect:canvasRect];
           [request finishWithImage:out context:nil];
       }];
#pragma clang diagnostic pop
    composition.renderSize = canvasRect.size;
    return composition;
}

} // namespace

VideoInfo probeVideo(const QString& path)
{
    VideoInfo info;
    @autoreleasepool {
        AVURLAsset* asset = loadAsset(path);
        AVAssetTrack* track = asset == nil ? nil : videoTrack(asset);
        if (track == nil) {
            return info;
        }
        info.duration = durationOf(asset);
        info.pixels = pixelsOf(track);
        info.ok = info.duration > 0.0 && !info.pixels.isEmpty();
    }
    return info;
}

bool exportVideoBlocking(const VideoExportParams& p, QString* error)
{
    @autoreleasepool {
        AVURLAsset* asset = loadAsset(p.input);
        AVAssetTrack* track = asset == nil ? nil : videoTrack(asset);
        if (track == nil) {
            *error = QStringLiteral("Could not read the recording.");
            return false;
        }
        const double duration = durationOf(asset);
        if (!isValidTrim(p.range, duration)) {
            *error = QStringLiteral("The trim range is not valid.");
            return false;
        }

        NSString* preset = p.beautify ? AVAssetExportPresetHighestQuality
                                      : AVAssetExportPresetPassthrough;
        AVAssetExportSession* session =
          [[AVAssetExportSession alloc] initWithAsset:asset presetName:preset];
        if (session == nil) {
            *error = QStringLiteral("This recording cannot be exported.");
            return false;
        }
        NSURL* output = [NSURL fileURLWithPath:toNSString(p.output)];
        [[NSFileManager defaultManager] removeItemAtURL:output error:nil];
        session.outputURL = output;
        session.outputFileType = AVFileTypeMPEG4;
        const CMTime start = CMTimeMakeWithSeconds(p.range.start, 600);
        const CMTime end = CMTimeMakeWithSeconds(p.range.end, 600);
        session.timeRange = CMTimeRangeFromTimeToTime(start, end);
        if (p.beautify) {
            // Decoded only when the background needs the shot's pixels
            const QImage firstFrame =
              imageeffects::backgroundUsesShot(p.effects)
                ? videoFrameAt(p.input, p.range.start, QSize())
                : QImage();
            session.videoComposition = beautifyComposition(
              asset, pixelsOf(track), p.effects, p.dpr, firstFrame);
        }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [session exportAsynchronouslyWithCompletionHandler:^{
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        const bool ok = session.status == AVAssetExportSessionStatusCompleted;
        if (!ok) {
            *error = session.error != nil
                       ? fromNSString(session.error.localizedDescription)
                       : QStringLiteral("The export did not finish.");
            [[NSFileManager defaultManager] removeItemAtURL:output error:nil];
        }
#pragma clang diagnostic pop
        return ok;
    }
}

void exportVideoAsync(const VideoExportParams& params,
                      std::function<void(bool ok, QString error)> done)
{
    std::thread([params, done]() {
        QString error;
        const bool ok = exportVideoBlocking(params, &error);
        QMetaObject::invokeMethod(
          QCoreApplication::instance(),
          [done, ok, error]() { done(ok, error); },
          Qt::QueuedConnection);
    }).detach();
}
