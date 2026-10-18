// SPDX-License-Identifier: GPL-3.0-or-later

#include "animrender.h"

namespace {

const auto kStyleKey = QStringLiteral("animStyle");
const auto kMotionKey = QStringLiteral("animMotion");
const auto kFormatKey = QStringLiteral("animFormat");

} // namespace

int animMaxLongSide(AnimTarget target)
{
    return target == AnimTarget::Gif ? kAnimGifMaxLongSide
                                     : kAnimMp4MaxLongSide;
}

AnimTarget animPreviewTarget(int format)
{
    return format == 1 ? AnimTarget::Gif : AnimTarget::Mp4;
}

AnimatedFrames animFramesFor(const AnimRenderParams& params,
                             AnimTarget target,
                             int maxLongSide)
{
    const bool gif = target == AnimTarget::Gif;
    EffectSettings effects = params.effects;
    if (gif) {
        effects.grain = 0;
    }
    AnimatedFrames frames(params.shot,
                          effects,
                          params.style,
                          params.seed,
                          maxLongSide > 0 ? maxLongSide
                                          : animMaxLongSide(target),
                          !gif);
    frames.setMotion(params.motion, params.durationSec);
    frames.setAnnotations(params.annotations, params.annotationMode);
    return frames;
}

AnimChoice animChoiceFrom(const QJsonObject& settings,
                          const AnimChoice& fallback)
{
    AnimChoice choice;
    choice.style = settings.value(kStyleKey).toInt(fallback.style);
    choice.motion = settings.value(kMotionKey).toInt(fallback.motion);
    choice.format = settings.value(kFormatKey).toInt(fallback.format);
    return choice;
}

void writeAnimChoice(QJsonObject& settings, const AnimChoice& choice)
{
    settings.insert(kStyleKey, choice.style);
    settings.insert(kMotionKey, choice.motion);
    settings.insert(kFormatKey, choice.format);
}
