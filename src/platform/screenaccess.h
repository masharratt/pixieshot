// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Screen Recording permission check shared by the capture overlay, screen
// recording and scrolling capture.
//
// macOS treats a renamed app as new. Without the permission the overlay would
// show only the wallpaper, which looks as if every window had vanished, so
// each entry point calls this first.
//
// Returns true when capturing may go ahead. When it returns false the user
// has already been asked (the system prompt, once per run) and shown a message
// that says how to grant access; the caller just stops. Always true outside
// macOS.
bool ensureScreenCaptureAccess();
