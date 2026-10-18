// SPDX-License-Identifier: GPL-3.0-or-later
// Platforms without a Screen Recording permission: capture is always allowed.

#include "platform/screenaccess.h"

bool ensureScreenCaptureAccess()
{
    return true;
}
