// SPDX-License-Identifier: GPL-3.0-or-later
// Frames of the "Animate" export: the Beautify canvas with its background
// replaced by an animated one. Everything that does not move (the shot with
// its border and corners, the shadow) is drawn once; each frame only redraws
// the background and, with glass on, the bezel that refracts it. Geometry
// comes from beautifyLayout(), so frames line up with the still.

#pragma once

#include "utils/animatedbg.h"
#include "utils/beautifylayout.h"
#include "utils/imageeffects.h"
#include "utils/shotmotion.h"

#include <QImage>
#include <QPoint>
#include <QSize>
#include <cstdint>
#include <utility>

class AnimatedFrames
{
public:
    // shot: the raw capture (its devicePixelRatio sizes the effects).
    // maxLongSide > 0 scales the whole canvas down until its long side fits.
    // evenSize grows an odd axis by one pixel (H.264), see videoCanvasSize.
    AnimatedFrames(const QImage& shot,
                   const EffectSettings& settings,
                   AnimStyle style,
                   uint32_t seed = animatedbg::kDefaultSeed,
                   int maxLongSide = 0,
                   bool evenSize = false);

    // The settings an animation uses: with no padding and no size preset
    // there is no room for a background, so the default padding is used
    static EffectSettings animatedSettings(EffectSettings settings);

    QSize size() const { return m_size; }
    // Geometry at the output scale (canvas is before the even adjustment)
    const BeautifyLayout& layout() const { return m_layout; }
    // The shot with border and rounded corners, drawn at layout().shot
    const QImage& shotLayer() const { return m_shot; }
    const std::pair<QColor, QColor>& colors() const { return m_colors; }

    // Moves the shot layer (shot, border, glass bezel) with `motion` over a
    // loop of durationSec. Default: None, the shot stays where the still has
    // it.
    void setMotion(ShotMotion motion, double durationSec);
    ShotMotion motion() const { return m_motion; }
    double durationSec() const { return m_durationSec; }

    // The frame at phase 0..1 (1 is the same as 0). Opaque Format_RGB32.
    QImage frame(double phase) const;
    // The frame with the background at `phase` and the shot at `pose`
    QImage frameAt(double phase, const shotmotion::Pose& pose) const;
    // Canvas pixels the shot (with border, not bezel) can cover at `pose`
    QRect shotBounds(const shotmotion::Pose& pose) const;

private:
    EffectSettings m_settings;
    AnimStyle m_style;
    uint32_t m_seed;
    qreal m_dpr = 1.0;
    BeautifyLayout m_layout;
    QSize m_size;
    std::pair<QColor, QColor> m_colors;
    QImage m_shot;   // physical pixels, premultiplied
    QImage m_shadow; // shadow alone, null when off
    QPoint m_shadowPos;
    bool m_glass = false;
    QVector<QColor> m_stops;
    ShotMotion m_motion = ShotMotion::None;
    double m_durationSec = 5.0;
};
