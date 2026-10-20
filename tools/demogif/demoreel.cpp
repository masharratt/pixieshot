// SPDX-License-Identifier: GPL-3.0-or-later

#include "demoreel.h"

#include "utils/animrender.h"
#include "utils/beautifylayout.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

QVariantMap look(std::initializer_list<std::pair<QString, QVariant>> extra)
{
    // Every scene is 16:9 so they share one canvas and can crossfade, with
    // less padding than the packs so the shot reads at README size
    QVariantMap m{ { QStringLiteral("effectAspect"), 2 },
                   { QStringLiteral("effectPadding"), 40 } };
    for (const auto& [key, value] : extra) {
        m.insert(key, value);
    }
    return m;
}

QImage fitTo(const QImage& image, QSize size)
{
    QImage out = image.size() == size
                   ? image
                   : image.scaled(
                       size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    out.setDevicePixelRatio(1.0);
    return out.convertToFormat(QImage::Format_RGB32);
}

} // namespace

QList<DemoScene> demoreel::scenes()
{
    QList<DemoScene> list;
    {
        // Annotations draw themselves in once per loop, here
        DemoScene s;
        s.caption = QStringLiteral("Mesh gradient · Glass · Orbit");
        s.pack = QStringLiteral("sunset-pop.pixiestyle");
        s.look = look({ { QStringLiteral("effectGlass"), true } });
        s.style = AnimStyle::Mesh;
        s.motion = ShotMotion::Orbit;
        s.annotations = AnnotationMode::DrawIn;
        s.seconds = 3.8;
        list.append(s);
    }
    {
        DemoScene s;
        s.caption = QStringLiteral("Glow grid · Tilt");
        s.pack = QStringLiteral("midnight-glass.pixiestyle");
        s.look = look({ { QStringLiteral("effectGlass"), false } });
        s.style = AnimStyle::Grid;
        s.motion = ShotMotion::Tilt;
        s.seconds = 3.0;
        list.append(s);
    }
    {
        DemoScene s;
        s.caption = QStringLiteral("Bokeh · Float");
        s.pack = QStringLiteral("ocean.pixiestyle");
        s.look = look({});
        s.style = AnimStyle::Bokeh;
        s.motion = ShotMotion::Float;
        s.seconds = 3.0;
        list.append(s);
    }
    {
        DemoScene s;
        s.caption = QStringLiteral("Beautify · Album Art");
        s.pack = QStringLiteral("album-art.pixiestyle");
        s.look = look({});
        s.still = true;
        s.seconds = 2.6;
        list.append(s);
    }
    return list;
}

DemoReel::DemoReel(const QImage& shot,
                   std::shared_ptr<AnnotationLayer> annotations,
                   const QList<DemoScene>& scenes,
                   const Resolve& resolve,
                   QSize size,
                   double fadeSec)
  : m_size(size)
  , m_fadeSec(fadeSec)
{
    // The shot with every annotation in it, for the still scenes
    QImage annotated;
    double start = 0.0;
    for (const DemoScene& scene : scenes) {
        Built built;
        built.scene = scene;
        built.start = start;
        start += scene.seconds - fadeSec;
        const EffectSettings settings = resolve(scene);
        if (scene.still) {
            if (annotated.isNull()) {
                annotated =
                  shot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                annotated.setDevicePixelRatio(shot.devicePixelRatio());
                if (annotations) {
                    QPainter painter(&annotated);
                    painter.setRenderHint(QPainter::Antialiasing);
                    for (int i = 0; i < annotations->count(); ++i) {
                        annotations->paint(painter, i, 1.0);
                    }
                }
            }
            const QImage still = imageeffects::applyEffects(annotated, settings);
            const BeautifyLayout layout = beautifyLayout(
              annotated.size(), settings, annotated.devicePixelRatio());
            built.still = fitTo(still, size);
            built.stillShot = toOutput(layout.shot, still.size());
        } else {
            // The export's own frame setup (GIF: no grain), at our size
            AnimRenderParams params;
            params.shot = shot;
            params.annotations = annotations;
            params.annotationMode = scene.annotations;
            params.effects = settings;
            params.style = scene.style;
            params.motion = scene.motion;
            built.frames.emplace(
              animFramesFor(params, AnimTarget::Gif, size.width()));
            // Scenes are not whole seconds long
            built.frames->setMotion(scene.motion, scene.seconds);
        }
        m_scenes.append(std::move(built));
    }
    m_durationSec = start;
}

int DemoReel::frameCount(int fps) const
{
    return std::max(1, int(std::lround(m_durationSec * fps)));
}

double DemoReel::frameTime(int index, int fps) const
{
    return index * m_durationSec / frameCount(fps);
}

DemoReel::Mix DemoReel::mixAt(double tSec) const
{
    Mix mix;
    if (m_scenes.isEmpty() || m_durationSec <= 0) {
        return mix;
    }
    double t = std::fmod(tSec, m_durationSec);
    if (t < 0) {
        t += m_durationSec;
    }
    int i = 0;
    while (i + 1 < m_scenes.size() && m_scenes.at(i + 1).start <= t) {
        ++i;
    }
    const double own = t - m_scenes.at(i).start;
    if (own >= m_fadeSec) {
        mix.a = i;
        mix.aSec = own;
        return mix;
    }
    // Fading in over the scene before it (the first over the last)
    const int prev = i > 0 ? i - 1 : int(m_scenes.size()) - 1;
    const double prevStart =
      i > 0 ? m_scenes.at(prev).start : m_scenes.at(prev).start - m_durationSec;
    mix.a = prev;
    mix.aSec = t - prevStart;
    mix.b = i;
    mix.bSec = own;
    mix.weight = own / m_fadeSec;
    return mix;
}

QImage DemoReel::sceneFrame(int index, double sceneSec) const
{
    QImage image = sceneImage(index, sceneSec);
    drawCaption(image, m_scenes.at(index).scene.caption, 1.0);
    return image;
}

QImage DemoReel::sceneImage(int index, double sceneSec) const
{
    const Built& built = m_scenes.at(index);
    if (built.frames) {
        return fitTo(built.frames->frame(sceneSec / built.scene.seconds),
                     m_size);
    }
    return built.still;
}

QImage DemoReel::frameAt(double tSec) const
{
    if (m_scenes.isEmpty()) {
        return {};
    }
    const Mix mix = mixAt(tSec);
    if (mix.b < 0 || mix.weight <= 0) {
        return sceneFrame(mix.a, mix.aSec);
    }
    QImage image = sceneImage(mix.a, mix.aSec);
    {
        QPainter painter(&image);
        painter.setOpacity(mix.weight);
        painter.drawImage(0, 0, sceneImage(mix.b, mix.bSec));
    }
    // Captions hand over instead of crossfading: the old one fades out in
    // the first half, the new one in during the second, never both at once
    if (mix.weight < 0.5) {
        drawCaption(
          image, m_scenes.at(mix.a).scene.caption, 1.0 - 2.0 * mix.weight);
    } else {
        drawCaption(
          image, m_scenes.at(mix.b).scene.caption, 2.0 * mix.weight - 1.0);
    }
    return image;
}

QRect DemoReel::shotBoundsAt(double tSec) const
{
    if (m_scenes.isEmpty()) {
        return {};
    }
    const auto bounds = [this](int index, double sec) {
        const Built& built = m_scenes.at(index);
        if (!built.frames) {
            return built.stillShot;
        }
        const AnimatedFrames& f = *built.frames;
        return toOutput(f.shotBounds(f.poseAt(sec / built.scene.seconds)),
                        f.size());
    };
    const Mix mix = mixAt(tSec);
    QRect rect = bounds(mix.a, mix.aSec);
    if (mix.b >= 0) {
        // Two different shots overlap: only where both are still the shot
        rect &= bounds(mix.b, mix.bSec);
    }
    return rect;
}

QImage DemoReel::shotShown(int index) const
{
    const Built& built = m_scenes.at(index);
    return built.frames ? built.frames->shotLayerShown() : built.still;
}

QRect DemoReel::toOutput(const QRect& canvasRect, QSize canvas) const
{
    if (canvas == m_size || canvas.isEmpty()) {
        return canvasRect;
    }
    const double sx = double(m_size.width()) / canvas.width();
    const double sy = double(m_size.height()) / canvas.height();
    return QRectF(canvasRect.x() * sx,
                  canvasRect.y() * sy,
                  canvasRect.width() * sx,
                  canvasRect.height() * sy)
      .toAlignedRect()
      .intersected(QRect(QPoint(), m_size));
}

void DemoReel::drawCaption(QImage& image,
                           const QString& caption,
                           double opacity) const
{
    if (caption.isEmpty() || opacity <= 0.0) {
        return;
    }
    // A small frosted pill in the bottom-left corner, sized to the frame
    const int h = image.height();
    QFont font;
    font.setPixelSize(std::max(8, int(std::lround(h * 0.032))));
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    const int padX = std::max(4, h / 45);
    const int padY = std::max(2, h / 90);
    const int margin = std::max(4, h / 28);
    const QRect text(0, 0, metrics.horizontalAdvance(caption), metrics.height());
    QRect pill(0, 0, text.width() + 2 * padX, text.height() + 2 * padY);
    pill.moveBottomLeft(QPoint(margin, h - margin));
    QPainter painter(&image);
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(pill), pill.height() / 2.0, pill.height() / 2.0);
    painter.fillPath(path, QColor(12, 12, 24, 150));
    painter.setPen(QColor(255, 255, 255, 80));
    painter.drawPath(path);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 235));
    painter.drawText(pill, Qt::AlignCenter, caption);
}
