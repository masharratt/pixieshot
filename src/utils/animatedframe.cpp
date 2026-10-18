// SPDX-License-Identifier: GPL-3.0-or-later

#include "animatedframe.h"
#include "backgroundfx.h"
#include "glassframe.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

// Padding used when the settings leave no room for the background (the
// default of the effectPadding option)
constexpr int kFallbackPadding = 64;

QImage physical(const QImage& src)
{
    QImage plain = src;
    plain.setDevicePixelRatio(1.0);
    return plain;
}

int longSide(QSize s)
{
    return std::max(s.width(), s.height());
}

} // namespace

EffectSettings AnimatedFrames::animatedSettings(EffectSettings settings)
{
    if (settings.paddingPx <= 0 && settings.aspect == 0) {
        settings.paddingPx = kFallbackPadding;
    }
    // The animation is the background; the mode only chose its colours
    settings.backgroundMode = BackgroundMode::Gradient;
    settings.autoColors = false;
    return settings;
}

AnimatedFrames::AnimatedFrames(const QImage& shot,
                               const EffectSettings& settings,
                               AnimStyle style,
                               uint32_t seed,
                               int maxLongSide,
                               bool evenSize)
  : m_settings(animatedSettings(settings))
  , m_style(style)
  , m_seed(seed)
{
    if (shot.isNull()) {
        return;
    }
    QImage source = settings.autoBalance ? imageeffects::autoBalance(shot) : shot;
    m_colors = animatedbg::colorsFor(settings, source);
    m_dpr = source.devicePixelRatio();
    m_layout = beautifyLayout(source.size(), m_settings, m_dpr);

    // Scaling the shot and its pixel ratio together scales every effect
    // size with it, so the smaller canvas is the same picture
    if (maxLongSide > 0 && longSide(m_layout.canvas) > maxLongSide) {
        const QImage original = physical(source);
        const qreal originalDpr = m_dpr;
        double k = double(maxLongSide) / longSide(m_layout.canvas);
        for (int attempt = 0; attempt < 20; ++attempt) {
            const QSize size(std::max(1, int(std::lround(original.width() * k))),
                             std::max(1, int(std::lround(original.height() * k))));
            m_dpr = originalDpr * k;
            m_layout = beautifyLayout(size, m_settings, m_dpr);
            const QSize out = evenSize ? videoCanvasSize(m_layout.canvas)
                                       : m_layout.canvas;
            if (longSide(out) <= maxLongSide) {
                source = original.scaled(
                  size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                source.setDevicePixelRatio(m_dpr);
                break;
            }
            k *= 0.995; // rounding pushed it over: shrink a little more
        }
    }
    m_size = evenSize ? videoCanvasSize(m_layout.canvas) : m_layout.canvas;

    // The static layers, the same calls as imageeffects::applyEffects
    QImage layer = source;
    if (m_settings.borderPx > 0) {
        layer = imageeffects::addBorder(layer, m_settings.borderPx, m_settings.borderColor);
    }
    if (m_settings.cornerRadius > 0) {
        layer = imageeffects::roundCorners(layer, m_settings.cornerRadius);
    }
    m_glass = m_layout.bezel != m_layout.shot;
    if (m_settings.shadow) {
        m_shadow = m_glass ? imageeffects::glassShadow(m_layout.bezel.size(),
                                                       m_layout.bezelRadius,
                                                       m_settings,
                                                       m_dpr)
                           : imageeffects::shadowLayer(layer,
                                                       m_settings.shadowBlur,
                                                       m_settings.shadowOffset,
                                                       m_settings.shadowColor);
        m_shadow.setDevicePixelRatio(1.0);
        const beautifygeometry::Step step = beautifygeometry::shadowStep(
          m_layout.bezel.size(), m_settings.shadowBlur, m_settings.shadowOffset, m_dpr);
        m_shadowPos = m_layout.bezel.topLeft() - step.origin;
    }
    m_shot = physical(layer).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage AnimatedFrames::frame(double phase) const
{
    if (m_size.isEmpty()) {
        return {};
    }
    const QImage work =
      animatedbg::renderWork(m_style, m_colors, m_size, phase, m_seed);
    QImage out =
      work.size() == m_size
        ? work
        : work.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB32);
    // Static grain: the same noise at the same pixel in every frame, so it
    // hides banding without shimmering or costing the encoder bits
    backgroundfx::addGrain(out, m_settings.grain);
    {
        QPainter p(&out);
        if (!m_shadow.isNull()) {
            p.drawImage(m_shadowPos, m_shadow);
        }
    }
    if (m_glass) {
        // Refracts the background alone (as the still does), sampled from
        // the small work image: the bezel blurs it heavily anyway
        glassframe::drawBezel(out,
                              work,
                              m_layout.bezel,
                              m_layout.bezelRadius,
                              m_layout.shot,
                              m_layout.cornerRadius,
                              m_dpr);
    }
    {
        QPainter p(&out);
        p.drawImage(m_layout.shot.topLeft(), m_shot);
    }
    return out;
}
