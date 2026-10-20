// SPDX-License-Identifier: GPL-3.0-or-later

#include "animatedframe.h"
#include "backgroundfx.h"
#include "glassframe.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
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

// A copy QPainter can draw on with alpha, devicePixelRatio kept
QImage paintable(const QImage& src)
{
    if (src.format() == QImage::Format_RGB32 ||
        src.format() == QImage::Format_ARGB32_Premultiplied) {
        return src.copy();
    }
    QImage out = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(src.devicePixelRatio());
    return out;
}

int longSide(QSize s)
{
    return std::max(s.width(), s.height());
}

// Transparent border around the moving layer, so its straight edges are
// smoothed by the transform like its rounded corners
constexpr int kLayerMargin = 2;

// The light band: white, soft edged, leaning kSweepSlant from vertical
constexpr double kSweepSlant = 15.0 * M_PI / 180.0;
constexpr double kSweepAlpha = 0.2;
constexpr double kSweepWidth = 0.22; // of the shot width, across the band

// Paints the band at `pos` (0 left of the shot, 1 right of it) over the
// shot's face, clipped to its rounded rect, never changing its alpha
void paintSweep(QPainter& p, const QRectF& shot, qreal radius, double pos)
{
    const double band = std::max(8.0, kSweepWidth * shot.width());
    const double cs = std::cos(kSweepSlant);
    const double sn = std::sin(kSweepSlant);
    // Half the band's horizontal extent over the shot's height
    const double reach = band / 2.0 / cs + shot.height() / 2.0 * sn / cs;
    const double cx = shot.left() - reach + pos * (shot.width() + 2.0 * reach);
    const QPointF centre(cx, shot.center().y());
    // Across the band: perpendicular to a line leaning right at the top
    const QPointF across(cs * band / 2.0, sn * band / 2.0);
    QLinearGradient g(centre - across, centre + across);
    auto white = [](double a) { return QColor(255, 255, 255, int(std::lround(a * 255))); };
    g.setColorAt(0.0, white(0.0));
    g.setColorAt(0.3, white(kSweepAlpha * 0.45));
    g.setColorAt(0.5, white(kSweepAlpha));
    g.setColorAt(0.7, white(kSweepAlpha * 0.45));
    g.setColorAt(1.0, white(0.0));
    const QRectF area = QRectF(cx - reach, shot.top(), 2.0 * reach, shot.height()) & shot;
    if (area.isEmpty()) {
        return;
    }
    QPainterPath clip;
    clip.addRoundedRect(shot, radius, radius);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(clip);
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    p.fillRect(area, g);
    p.restore();
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
    m_stops = animatedbg::stopsFor(settings, source);
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
    m_source = source;
    if (m_settings.borderPx > 0) {
        const beautifygeometry::Step step =
          beautifygeometry::borderStep(source.size(), m_settings.borderPx, m_dpr);
        m_shotOffset = step.origin.x() > 0 ? step.origin : QPoint();
    }
    const QImage layer = layerFrom(source);
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
    m_shot = layer;
}

QImage AnimatedFrames::layerFrom(const QImage& source) const
{
    QImage layer = source;
    if (m_settings.borderPx > 0) {
        layer = imageeffects::addBorder(layer, m_settings.borderPx, m_settings.borderColor);
    }
    if (m_settings.cornerRadius > 0) {
        layer = imageeffects::roundCorners(layer, m_settings.cornerRadius);
    }
    return physical(layer).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void AnimatedFrames::setAnnotations(std::shared_ptr<AnnotationLayer> layer,
                                    AnnotationMode mode)
{
    m_annotations = (layer && layer->count() > 0) ? std::move(layer) : nullptr;
    m_annotationMode = mode;
    m_plan = {};
    if (m_source.isNull()) {
        return;
    }
    if (!m_annotations) {
        m_shot = layerFrom(m_source);
        return;
    }
    if (mode == AnnotationMode::DrawIn) {
        m_plan = animtimeline::plan(m_durationSec, m_annotations->count());
        m_shot = layerFrom(m_source);
        return;
    }
    // Static: drawn once into the shot, exactly like the flattened still
    QImage drawn = paintable(m_source);
    {
        QPainter p(&drawn);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < m_annotations->count(); ++i) {
            m_annotations->paint(p, i, 1.0);
        }
    }
    m_shot = layerFrom(drawn);
}

QImage AnimatedFrames::shotLayerAt(double tSec) const
{
    if (!m_annotations || !m_plan.drawsIn()) {
        return m_shot;
    }
    QImage drawn;
    for (int i = 0; i < m_annotations->count(); ++i) {
        const double progress = animtimeline::progress(m_plan, i, tSec);
        const double opacity = animtimeline::opacity(m_plan, i, tSec);
        if (progress <= 0.0 || opacity <= 0.0) {
            continue;
        }
        if (drawn.isNull()) {
            drawn = paintable(m_source);
        }
        QPainter p(&drawn);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(opacity);
        m_annotations->paint(p, i, progress);
    }
    // Nothing shows before the first reveal and after the fade
    return drawn.isNull() ? m_shot : layerFrom(drawn);
}

QImage AnimatedFrames::shotLayerShown() const
{
    return m_plan.drawsIn() ? shotLayerAt(m_plan.revealEnd()) : m_shot;
}

void AnimatedFrames::setMotion(ShotMotion motion, double durationSec)
{
    m_motion = motion;
    m_durationSec = durationSec;
    if (m_plan.drawsIn()) {
        m_plan = animtimeline::plan(m_durationSec, m_plan.count);
    }
}

QImage AnimatedFrames::frame(double phase) const
{
    return frameAt(phase, poseAt(phase));
}

shotmotion::Pose AnimatedFrames::poseAt(double phase) const
{
    if (!m_plan.drawsIn()) {
        return shotmotion::poseAt(m_motion, phase, m_durationSec);
    }
    return shotmotion::poseAt(m_motion,
                              animtimeline::motionPhase(m_plan, phase),
                              animtimeline::motionDurationSec(m_plan));
}

QRectF AnimatedFrames::shotArea(const shotmotion::Pose& pose) const
{
    const QTransform t = shotmotion::transformFor(
      m_motion, pose.amount, QRectF(m_layout.bezel), m_dpr);
    return t.isIdentity() ? QRectF(m_layout.shot) : t.mapRect(QRectF(m_layout.shot));
}

QRect AnimatedFrames::shotBounds(const shotmotion::Pose& pose) const
{
    const QTransform t = shotmotion::transformFor(
      m_motion, pose.amount, QRectF(m_layout.bezel), m_dpr);
    if (t.isIdentity()) {
        return m_layout.shot;
    }
    // One pixel more for the smoothing at the edges
    return shotArea(pose)
      .toAlignedRect()
      .adjusted(-1, -1, 1, 1)
      .intersected(QRect(QPoint(0, 0), m_size));
}

QRect AnimatedFrames::annotationBounds(int index, const shotmotion::Pose& pose) const
{
    if (!m_annotations || index < 0 || index >= m_annotations->count()) {
        return {};
    }
    const QRectF logical = m_annotations->bounds(index);
    const QRectF onCanvas(
      QPointF(logical.topLeft() * m_dpr) + m_shotOffset + m_layout.shot.topLeft(),
      logical.size() * m_dpr);
    const QTransform t = shotmotion::transformFor(
      m_motion, pose.amount, QRectF(m_layout.bezel), m_dpr);
    return (t.isIdentity() ? onCanvas : t.mapRect(onCanvas)).toAlignedRect();
}

QImage AnimatedFrames::frameAt(double phase, const shotmotion::Pose& pose) const
{
    if (m_size.isEmpty()) {
        return {};
    }
    const QImage work =
      animatedbg::renderWorkStops(m_style, m_stops, m_size, phase, m_seed);
    QImage out =
      work.size() == m_size
        ? work
        : work.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB32);
    // Static grain: the same noise at the same pixel in every frame, so it
    // hides banding without shimmering or costing the encoder bits
    backgroundfx::addGrain(out, m_settings.grain);

    const double t = (phase - std::floor(phase)) * m_durationSec;
    const QImage shotLayer = shotLayerAt(t);
    const QTransform move = shotmotion::transformFor(
      m_motion, pose.amount, QRectF(m_layout.bezel), m_dpr);
    if (move.isIdentity() && pose.sweep < 0) {
        // At rest: exactly the still's layers, untouched by any resampling
        {
            QPainter p(&out);
            if (!m_shadow.isNull()) {
                p.drawImage(m_shadowPos, m_shadow);
            }
        }
        if (m_glass) {
            // Refracts the background alone (as the still does), sampled
            // from the small work image: the bezel blurs it heavily anyway
            glassframe::drawBezel(out,
                                  work,
                                  m_layout.bezel,
                                  m_layout.bezelRadius,
                                  m_layout.shot,
                                  m_layout.cornerRadius,
                                  m_dpr,
                                  shotLayer);
        }
        QPainter p(&out);
        p.drawImage(m_layout.shot.topLeft(), shotLayer);
        return out;
    }

    // Moving: the bezel and shot become one layer, drawn once through the
    // transform from full-resolution pixels; the shadow slides under it
    QPainter p(&out);
    if (!m_shadow.isNull()) {
        const QPointF shift = shotmotion::shadowShift(m_motion, pose.amount, m_dpr);
        p.drawImage(m_shadowPos + shift.toPoint(), m_shadow);
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(move);
    if (!m_glass && pose.sweep < 0) {
        p.drawImage(m_layout.shot.topLeft(), shotLayer);
        return out;
    }
    // Room for the smoothing at the edges and the glass's own drop shadow
    const int margin =
      kLayerMargin +
      (m_glass ? glassframe::outset(m_layout.bezel, m_layout.shot, m_dpr) : 0);
    const QRect area = m_layout.bezel.adjusted(-margin, -margin, margin, margin);
    const QPoint origin = area.topLeft();
    QImage layer(area.size(), QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    if (m_glass) {
        // The background behind the layer, cut from the work image
        const double kx = double(work.width()) / m_size.width();
        const double ky = double(work.height()) / m_size.height();
        const QRect behind =
          QRect(QPoint(int(std::floor(area.left() * kx)), int(std::floor(area.top() * ky))),
                QPoint(int(std::ceil((area.right() + 1) * kx)) - 1,
                       int(std::ceil((area.bottom() + 1) * ky)) - 1))
            .intersected(work.rect());
        glassframe::drawBezel(layer,
                              work.copy(behind),
                              m_layout.bezel.translated(-origin),
                              m_layout.bezelRadius,
                              m_layout.shot.translated(-origin),
                              m_layout.cornerRadius,
                              m_dpr,
                              shotLayer);
    }
    {
        QPainter lp(&layer);
        const QRect shot = m_layout.shot.translated(-origin);
        lp.drawImage(shot.topLeft(), shotLayer);
        if (pose.sweep >= 0) {
            paintSweep(lp, QRectF(shot), m_layout.cornerRadius, pose.sweep);
        }
    }
    p.drawImage(origin, layer);
    return out;
}

