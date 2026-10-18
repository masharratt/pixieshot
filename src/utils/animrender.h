// SPDX-License-Identifier: GPL-3.0-or-later
// The one place the "Animate" frames are set up: the export (MP4 and GIF)
// and the preview in the editor both get their AnimatedFrames here, so the
// preview at t is the export at t, only smaller. Pure C++, no encoders.

#pragma once

#include "utils/animatedbg.h"
#include "utils/animatedframe.h"
#include "utils/annotationlayer.h"
#include "utils/imageeffects.h"
#include "utils/shotmotion.h"

#include <QImage>
#include <QJsonObject>

#include <cstdint>
#include <memory>

// cfn: MP4 long side capped at 1920 (frames are scaled down to fit), raise
// it if people ask for 4K loops; render time and file size grow with area
inline constexpr int kAnimMp4MaxLongSide = 1920;
// GIF is 256 colours and has no inter-frame compression here, so it is
// kept smaller and slower to stay a sensible size
inline constexpr int kAnimGifMaxLongSide = 960;
inline constexpr int kAnimGifMaxFps = 15;

enum class AnimTarget
{
    Mp4,
    Gif,
};

// What the frames are drawn from, the same for every file of one export
struct AnimRenderParams
{
    QImage shot; // the capture, its devicePixelRatio sizes the effects
    // Annotations on the shot (null: none); built on the GUI thread by
    // animannotations::prepare, then only drawn with
    std::shared_ptr<AnnotationLayer> annotations;
    AnnotationMode annotationMode = AnnotationMode::DrawIn;
    EffectSettings effects;
    AnimStyle style = AnimStyle::Aurora;
    ShotMotion motion = ShotMotion::None;
    uint32_t seed = animatedbg::kDefaultSeed;
    int durationSec = 5;
};

// The choices made per export (the preview's pickers), as ConfigHandler
// stores them: animStyle, animMotion, animFormat (0 MP4, 1 GIF, 2 both)
struct AnimChoice
{
    int style = 0;
    int motion = 0;
    int format = 0;

    bool operator==(const AnimChoice& other) const
    {
        return style == other.style && motion == other.motion &&
               format == other.format;
    }
};

// Long side cap of a target's files
int animMaxLongSide(AnimTarget target);
// The target a preview of `format` shows: GIF only looks like the GIF,
// anything with an MP4 like the MP4
AnimTarget animPreviewTarget(int format);

// The frames an export of `target` encodes. maxLongSide > 0 draws the same
// canvas smaller (the preview); 0 uses the target's own cap. A GIF has no
// grain (the dither breaks up banding, and noise defeats its compression)
// and odd MP4 sizes grow by one pixel (H.264).
AnimatedFrames animFramesFor(const AnimRenderParams& params,
                             AnimTarget target,
                             int maxLongSide = 0);

// The choice stored in a project's Animate settings (see CaptureProject),
// each missing key taken from `fallback`
AnimChoice animChoiceFrom(const QJsonObject& settings,
                          const AnimChoice& fallback);
// Writes the choice into Animate settings, keeping every other key
void writeAnimChoice(QJsonObject& settings, const AnimChoice& choice);
