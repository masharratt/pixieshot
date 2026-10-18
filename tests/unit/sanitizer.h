// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// True when the test binary is built with AddressSanitizer or
// UndefinedBehaviorSanitizer (scripts/asan-check.sh). Timing bounds are
// meaningless there, so performance tests skip instead of failing.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define PIXIE_SANITIZED 1
#endif
#endif
#if !defined(PIXIE_SANITIZED) && defined(__SANITIZE_ADDRESS__)
#define PIXIE_SANITIZED 1
#endif

#ifdef PIXIE_SANITIZED
#define SKIP_TIMING_UNDER_SANITIZER()                                          \
    QSKIP("Timing bound not meaningful in a sanitizer build")
#else
#define SKIP_TIMING_UNDER_SANITIZER()                                          \
    do {                                                                       \
    } while (false)
#endif
