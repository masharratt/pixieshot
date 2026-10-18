// SPDX-License-Identifier: GPL-3.0-or-later
// A UI-like screenshot with real annotations (box, bent arrow, counter,
// text), prepared for Animate. Plain C++ so Objective-C++ tests built with
// QT_NO_KEYWORDS can use it without the tool headers.

#pragma once

#include "tools/animannotations.h"

namespace testtools {

// 900 x 560 device pixels at dpr 2, four annotations in creation order
PreparedAnimSource annotatedSample();

} // namespace testtools
