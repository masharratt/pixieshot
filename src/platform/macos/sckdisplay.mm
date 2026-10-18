// SPDX-License-Identifier: GPL-3.0-or-later

#define QT_NO_KEYWORDS

#include "platform/macos/sckdisplay.h"
#include "core/recordinggeometry.h"

#import <AppKit/AppKit.h>

#if !__has_feature(objc_arc)
#error "Compile this file with -fobjc-arc"
#endif

SCKDisplayArea sckDisplayArea(SCShareableContent* content, CGRect globalLogical)
{
    SCKDisplayArea result;
    CGFloat bestArea = 0;
    for (SCDisplay* d in content.displays) {
        CGRect inter = CGRectIntersection(d.frame, globalLogical);
        CGFloat area =
          CGRectIsNull(inter) ? 0 : inter.size.width * inter.size.height;
        if (area > bestArea) {
            bestArea = area;
            result.display = d;
        }
    }
    if (result.display == nil) {
        return result;
    }
    SCDisplay* display = result.display;
    const CGRect area = CGRectIntersection(display.frame, globalLogical);
    result.local = displayLocalRect(
      QRect(qRound(area.origin.x),
            qRound(area.origin.y),
            qRound(area.size.width),
            qRound(area.size.height)),
      QRect(qRound(display.frame.origin.x),
            qRound(display.frame.origin.y),
            qRound(display.frame.size.width),
            qRound(display.frame.size.height)));

    // AppKit is main-thread only; this runs on a ScreenCaptureKit queue
    __block double scale = 0;
    const CGDirectDisplayID displayID = display.displayID;
    void (^lookup)(void) = ^{
        for (NSScreen* screen in NSScreen.screens) {
            NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
            if (number.unsignedIntValue == displayID) {
                scale = screen.backingScaleFactor;
                break;
            }
        }
    };
    if ([NSThread isMainThread]) {
        lookup();
    } else {
        dispatch_sync(dispatch_get_main_queue(), lookup);
    }
    result.scale = scale;
    return result;
}

NSArray<SCWindow*>* sckWindowsWithIds(SCShareableContent* content,
                                      NSArray<NSNumber*>* ids)
{
    NSMutableArray<SCWindow*>* windows = [NSMutableArray array];
    for (SCWindow* w in content.windows) {
        if ([ids containsObject:@(w.windowID)]) {
            [windows addObject:w];
        }
    }
    return windows;
}
