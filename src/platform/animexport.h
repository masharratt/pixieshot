// SPDX-License-Identifier: GPL-3.0-or-later
// "Animate" export: a still capture over a looping animated background,
// written as H.264 MP4 (AVAssetWriter) and/or GIF (ImageIO). macOS only.

#pragma once

#include "utils/animatedbg.h"
#include "utils/imageeffects.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>

// cfn: MP4 long side capped at 1920 (frames are scaled down to fit), raise
// it if people ask for 4K loops; render time and file size grow with area
inline constexpr int kAnimMp4MaxLongSide = 1920;
// GIF is 256 colours and has no inter-frame compression here, so it is
// kept smaller and slower to stay a sensible size
inline constexpr int kAnimGifMaxLongSide = 960;
inline constexpr int kAnimGifMaxFps = 15;

struct AnimExportParams
{
    QImage shot; // the capture, its devicePixelRatio sizes the effects
    EffectSettings effects;
    AnimStyle style = AnimStyle::Aurora;
    uint32_t seed = animatedbg::kDefaultSeed;
    int durationSec = 5;
    int fps = 30;      // MP4; the GIF uses min(fps, kAnimGifMaxFps)
    QString mp4Path;   // empty: no MP4
    QString gifPath;   // empty: no GIF
};

struct AnimExportResult
{
    QSize mp4Size;
    QSize gifSize;
    int gifFrames = 0;
    // The system H.264 encoder could not be reached (e.g. a sandbox)
    bool encoderUnavailable = false;
};

// Frame counts for a loop: every frame is a distinct phase, the loop's end
// is the next start, so there are exactly seconds * fps frames
int animFrameCount(int durationSec, int fps);
int animGifFps(int fps);

// Writes the requested files, replacing existing ones; a failed file is
// removed. progress (0..1) runs on the calling thread. Blocks: call from a
// worker thread or a test.
bool exportAnimationBlocking(const AnimExportParams& params,
                             const std::function<void(double)>& progress,
                             AnimExportResult* result,
                             QString* error);

// exportAnimationBlocking on a worker thread; progress and done run on the
// Qt main thread
void exportAnimationAsync(
  const AnimExportParams& params,
  std::function<void(double)> progress,
  std::function<void(bool ok, QString error, AnimExportResult result)> done);
