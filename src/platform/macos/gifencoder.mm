// SPDX-License-Identifier: GPL-3.0-or-later
// MP4 to GIF conversion with AVAssetImageGenerator and ImageIO. Compiled with
// ARC. Runs on a worker thread.

#define QT_NO_KEYWORDS

#include "platform/gifencoder.h"
#include "core/gifplan.h"
#include "platform/macos/gifwriter.h"

#include <QCoreApplication>
#include <QMetaObject>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

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

// One frame at `seconds`, or NULL. Blocks the (worker) thread.
CGImageRef copyFrame(AVAssetImageGenerator* generator, double seconds)
{
    __block CGImageRef result = NULL;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [generator
      generateCGImageAsynchronouslyForTime:CMTimeMakeWithSeconds(seconds, 600)
                         completionHandler:^(CGImageRef image,
                                             CMTime,
                                             NSError*) {
                             if (image != NULL) {
                                 result = CGImageRetain(image);
                             }
                             dispatch_semaphore_signal(done);
                         }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    return result;
}

bool encodeBlocking(const GifEncodeParams& p, QString* error)
{
    @autoreleasepool {
        NSURL* input = [NSURL fileURLWithPath:toNSString(p.inputMp4)];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:input options:nil];

        // The sync accessors are deprecated but are the only blocking route
        // that is safe on a worker thread without more plumbing.
        dispatch_semaphore_t loaded = dispatch_semaphore_create(0);
        [asset loadValuesAsynchronouslyForKeys:@[ @"duration" ]
                             completionHandler:^{
                                 dispatch_semaphore_signal(loaded);
                             }];
        dispatch_semaphore_wait(loaded, DISPATCH_TIME_FOREVER);
        NSError* loadError = nil;
        if ([asset statusOfValueForKey:@"duration" error:&loadError] !=
            AVKeyValueStatusLoaded) {
            *error = QStringLiteral("Could not read the recording.");
            return false;
        }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const double duration = CMTimeGetSeconds(asset.duration);
#pragma clang diagnostic pop

        AVAssetImageGenerator* generator =
          [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
        generator.appliesPreferredTrackTransform = YES;
        generator.requestedTimeToleranceBefore = kCMTimeZero;
        generator.requestedTimeToleranceAfter = kCMTimeZero;

        // Full size first frame tells the source size for the plan
        CGImageRef first = copyFrame(generator, 0.0);
        if (first == NULL) {
            *error = QStringLiteral("Could not read the first frame.");
            return false;
        }
        const QSize source(static_cast<int>(CGImageGetWidth(first)),
                           static_cast<int>(CGImageGetHeight(first)));
        CGImageRelease(first);

        const GifPlan plan = planGif(duration, source, p.fps, p.maxWidth);
        generator.maximumSize =
          CGSizeMake(plan.size.width(), plan.size.height());

        GifWriter gif(p.outputGif, static_cast<size_t>(plan.frameTimes.size()));
        if (!gif.isOpen()) {
            *error = QStringLiteral("Could not create the GIF file.");
            return false;
        }

        // A frame that cannot be decoded repeats the previous one so the
        // frame count declared above still matches and timing is kept.
        CGImageRef last = NULL;
        for (double t : plan.frameTimes) {
            @autoreleasepool {
                CGImageRef image = copyFrame(generator, t);
                if (image == NULL) {
                    image = last;
                    if (image != NULL) {
                        CGImageRetain(image);
                    }
                }
                if (image == NULL) {
                    *error = QStringLiteral("Could not decode a frame.");
                    gif.discard();
                    return false;
                }
                gif.add(image, plan.frameDelay);
                if (last != NULL) {
                    CGImageRelease(last);
                }
                last = image;
            }
        }
        if (last != NULL) {
            CGImageRelease(last);
        }
        const bool ok = gif.finish();
        if (!ok) {
            *error = QStringLiteral("Could not write the GIF file.");
        }
        return ok;
    }
}

} // namespace

void encodeGifAsync(const GifEncodeParams& params,
                    std::function<void(bool ok, QString error)> done)
{
    std::thread([params, done]() {
        QString error;
        const bool ok = encodeBlocking(params, &error);
        QMetaObject::invokeMethod(
          QCoreApplication::instance(),
          [done, ok, error]() { done(ok, error); },
          Qt::QueuedConnection);
    }).detach();
}
