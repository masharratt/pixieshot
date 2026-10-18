// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/screenaccess.h"

#include <QMessageBox>
#include <QObject>

#import <CoreGraphics/CoreGraphics.h>

bool ensureScreenCaptureAccess()
{
    if (CGPreflightScreenCaptureAccess()) {
        return true;
    }
    // The system prompt only appears once per run; later calls just explain
    static bool asked = false;
    if (!asked) {
        asked = true;
        CGRequestScreenCaptureAccess();
    }
    QMessageBox::warning(
      nullptr,
      QObject::tr("Screen Recording permission needed"),
      QObject::tr("Pixieshot needs Screen Recording permission: System "
                  "Settings → Privacy & Security → Screen & System Audio "
                  "Recording, then restart Pixieshot."));
    return false;
}
