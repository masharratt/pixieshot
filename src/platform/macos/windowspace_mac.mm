// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/windowspace.h"

#include <QGuiApplication>
#include <QWidget>

#import <AppKit/AppKit.h>

// This file relies on automatic reference counting. Without it, objects held
// across asynchronous callbacks are freed early and the app crashes.
#if !__has_feature(objc_arc)
#error "Compile this file with -fobjc-arc"
#endif


void showOnActiveSpace(QWidget* window)
{
    // winId() is an NSView only on the Cocoa platform plugin (not offscreen)
    if (!window || QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }
    // winId() creates the native window if it does not exist yet
    auto* view =
      (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* nsWindow = view.window;
    if (nsWindow) {
        nsWindow.collectionBehavior |=
          NSWindowCollectionBehaviorMoveToActiveSpace |
          NSWindowCollectionBehaviorFullScreenAuxiliary;
    }
    if (@available(macOS 14.0, *)) {
        [NSApp activate];
    } else {
        [NSApp activateIgnoringOtherApps:YES];
    }
}
