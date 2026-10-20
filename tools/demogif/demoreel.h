// SPDX-License-Identifier: GPL-3.0-or-later
// The README demo: a few "Animate" looks of one annotated shot, joined by
// soft crossfades into one seamless loop. Every scene is drawn by the export
// renderer (animFramesFor / AnimatedFrames) or, for the still, by
// imageeffects::applyEffects, so the demo shows exactly what the app makes.
// Pure C++ with no encoder; tools/demogif/main.mm writes the GIF.

#pragma once

#include "utils/animatedbg.h"
#include "utils/animatedframe.h"
#include "utils/annotationlayer.h"
#include "utils/imageeffects.h"
#include "utils/shotmotion.h"

#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

struct DemoScene
{
    QString caption;  // small chip in the corner, empty for none
    QString pack;     // style pack file in data/stylepacks, empty for none
    QVariantMap look; // style keys applied on top of the pack
    AnimStyle style = AnimStyle::Aurora;
    ShotMotion motion = ShotMotion::None;
    AnnotationMode annotations = AnnotationMode::Static;
    bool still = false; // plain Beautify still, no animated background
    double seconds = 3.0;
};

namespace demoreel {

inline constexpr int kWidth = 800;
inline constexpr int kHeight = 450; // 16:9, every scene uses that size preset
inline constexpr int kFps = 12;
inline constexpr double kFadeSec = 0.6;

// The scenes of the README GIF, in order
QList<DemoScene> scenes();

} // namespace demoreel

class DemoReel
{
public:
    // Effect settings for a scene (the generator: style pack + look through
    // ConfigHandler::effectSettings)
    using Resolve = std::function<EffectSettings(const DemoScene&)>;

    // shot: the base capture (with its devicePixelRatio), annotations drawn
    // on it (may be null). Every scene is drawn at `size`.
    DemoReel(const QImage& shot,
             std::shared_ptr<AnnotationLayer> annotations,
             const QList<DemoScene>& scenes,
             const Resolve& resolve,
             QSize size = QSize(demoreel::kWidth, demoreel::kHeight),
             double fadeSec = demoreel::kFadeSec);

    // Loop length: every scene, less one crossfade per scene (the last one
    // fades back into the first)
    double durationSec() const { return m_durationSec; }
    QSize size() const { return m_size; }
    int sceneCount() const { return int(m_scenes.size()); }
    // Frames for a loop at `fps`, frame i at i * durationSec / count
    int frameCount(int fps) const;
    double frameTime(int index, int fps) const;

    // What shows at loop time tSec (wraps): scene `a`, and while a
    // crossfade runs scene `b` over it at `weight` (0..1)
    struct Mix
    {
        int a = 0;
        double aSec = 0.0; // a's own time
        int b = -1;        // -1: no crossfade
        double bSec = 0.0;
        double weight = 0.0;
    };
    Mix mixAt(double tSec) const;

    // Opaque Format_RGB32 frame at loop time tSec
    QImage frameAt(double tSec) const;
    // Scene `index` alone at its own time, caption included
    QImage sceneFrame(int index, double sceneSec) const;
    // Output pixels the shot covers at tSec (not dithered by the GIF)
    QRect shotBoundsAt(double tSec) const;
    // The shot with every annotation shown, for palette samples
    QImage shotShown(int index) const;

private:
    struct Built
    {
        DemoScene scene;
        std::optional<AnimatedFrames> frames; // animated scenes
        QImage still;                         // still scenes, at m_size
        QRect stillShot;                      // where the shot sits in it
        double start = 0.0;                   // loop time it starts at
    };
    QRect toOutput(const QRect& canvasRect, QSize canvas) const;
    // The scene without its caption
    QImage sceneImage(int index, double sceneSec) const;
    void drawCaption(QImage& image, const QString& caption, double opacity) const;

    QSize m_size;
    double m_fadeSec;
    double m_durationSec = 0.0;
    QList<Built> m_scenes;
};
