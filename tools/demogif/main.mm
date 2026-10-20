// SPDX-License-Identifier: GPL-3.0-or-later
// pixie_demogif: writes the README demo GIF. Frames come from DemoReel (the
// Animate renderer); they go through the export's GIF path: gifquant
// palettes and dither, ImageIO through GifWriter. Run by
// scripts/make-demo-gif.sh.
//
//   pixie_demogif <out.gif> [--samples <dir>] [--tab <n>] [--fps <n>]
//
// Settings, history and caches are pointed at a temp dir before Qt starts,
// so the user's own Pixieshot settings are never read or written.

#define QT_NO_KEYWORDS

#include "demoreel.h"
#include "demoshot.h"
#include "platform/macos/gifwriter.h"
#include "utils/appidentity.h"
#include "utils/gifquant.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#if !__has_feature(objc_arc)
#error "Compile this file with -fobjc-arc"
#endif

namespace {

constexpr int kSamplesPerFrame = 16000;

// One palette per scene: several moments of it and the shot with every
// annotation shown, so neither crowds the other out
gifquant::Palette scenePalette(const DemoReel& reel,
                               const DemoScene& scene,
                               int index)
{
    QVector<QRgb> samples;
    for (double f : { 0.1, 0.35, 0.6, 0.85 }) {
        samples += gifquant::samplePixels(
          reel.sceneFrame(index, f * scene.seconds), kSamplesPerFrame);
    }
    samples +=
      gifquant::samplePixels(reel.shotShown(index), 3 * kSamplesPerFrame);
    return gifquant::Palette::fromSamples(samples);
}

int fail(const char* message)
{
    std::fprintf(stderr, "pixie_demogif: %s\n", message);
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        return fail("no temp dir");
    }
    const QByteArray home = tmp.path().toUtf8();
    qputenv("HOME", home);
    qputenv("CFFIXED_USER_HOME", home);
    qputenv("XDG_CONFIG_HOME", home + "/.config");
    qputenv("XDG_CACHE_HOME", home + "/.cache");
    qputenv("XDG_DATA_HOME", home + "/.local/share");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // A Retina grab, as a real capture on a Mac would be
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        qputenv("QT_SCALE_FACTOR", "2");
    }
    QSettings::setPath(
      QSettings::IniFormat, QSettings::UserScope, tmp.path() + "/.config");

    QApplication app(argc, argv);
    AppIdentity::apply();
    app.setQuitOnLastWindowClosed(false);

    QString out;
    QString samplesDir;
    int tab = 4; // Workflows: the style packs
    int fps = demoreel::kFps;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--samples") && i + 1 < args.size()) {
            samplesDir = args.at(++i);
        } else if (a == QLatin1String("--tab") && i + 1 < args.size()) {
            tab = args.at(++i).toInt();
        } else if (a == QLatin1String("--fps") && i + 1 < args.size()) {
            fps = qBound(1, args.at(++i).toInt(), 30);
        } else if (out.isEmpty()) {
            out = a;
        }
    }
    if (out.isEmpty()) {
        return fail("usage: pixie_demogif <out.gif> [--samples <dir>] "
                    "[--tab <n>] [--fps <n>]");
    }

    const PreparedAnimSource source = demoshot::settingsShot(tab);
    if (source.base.isNull()) {
        return fail("could not grab the Settings window");
    }
    const QList<DemoScene> scenes = demoreel::scenes();
    const DemoReel reel(
      source.base, source.annotations, scenes, demoshot::settingsFor);
    const int count = reel.frameCount(fps);

    if (!samplesDir.isEmpty()) {
        QDir().mkpath(samplesDir);
        source.base.save(QDir(samplesDir).filePath("base-shot.png"));
        for (int i = 0; i < scenes.size(); ++i) {
            reel.sceneFrame(i, scenes.at(i).seconds * 0.6)
              .save(QDir(samplesDir).filePath(
                QStringLiteral("scene-%1.png").arg(i + 1)));
        }
        reel.frameAt(scenes.at(0).seconds - demoreel::kFadeSec / 2)
          .save(QDir(samplesDir).filePath("crossfade.png"));
        reel.frameAt(0.0).save(QDir(samplesDir).filePath("loop-start.png"));
    }

    QList<gifquant::Palette> palettes;
    QList<CGColorSpaceRef> spaces;
    for (int i = 0; i < scenes.size(); ++i) {
        palettes.append(scenePalette(reel, scenes.at(i), i));
        spaces.append(gifwriter::indexedColorSpace(palettes.last().colors()));
    }

    QDir().mkpath(QFileInfo(out).absolutePath());
    const QList<int> delays = gifquant::frameDelaysCs(count, reel.durationSec());
    GifWriter gif(out, size_t(count));
    bool ok = gif.isOpen();
    for (int i = 0; ok && i < count; ++i) {
        @autoreleasepool {
            const double t = reel.frameTime(i, fps);
            const QImage frame = reel.frameAt(t);
            const DemoReel::Mix mix = reel.mixAt(t);
            CGColorSpaceRef space = NULL;
            QImage indexed;
            if (mix.b < 0) {
                indexed = gifquant::quantize(
                  frame, palettes.at(mix.a), reel.shotBoundsAt(t));
                space = CGColorSpaceRetain(spaces.at(mix.a));
            } else {
                // A crossfade has colours of both scenes: its own palette
                const gifquant::Palette own = gifquant::Palette::fromSamples(
                  gifquant::samplePixels(frame, 4 * kSamplesPerFrame));
                indexed = gifquant::quantize(frame, own, reel.shotBoundsAt(t));
                space = gifwriter::indexedColorSpace(own.colors());
            }
            // A few frames as the GIF has them (palette and dither)
            if (!samplesDir.isEmpty() && i % (count / 5) == 0) {
                indexed.save(QDir(samplesDir).filePath(
                  QStringLiteral("gif-frame-%1.png").arg(i, 3, 10, QChar('0'))));
            }
            CGImageRef image = gifwriter::indexedImage(indexed, space);
            CGColorSpaceRelease(space);
            if (image == NULL) {
                ok = false;
                break;
            }
            gif.add(image, delays.at(i) / 100.0);
            CGImageRelease(image);
        }
    }
    for (CGColorSpaceRef space : spaces) {
        CGColorSpaceRelease(space);
    }
    if (!ok) {
        gif.discard();
        return fail("could not make a GIF frame");
    }
    if (!gif.finish()) {
        return fail("could not write the GIF file");
    }
    std::printf("%s: %dx%d, %d frames, %.1f s, %.2f MB\n",
                qPrintable(out),
                reel.size().width(),
                reel.size().height(),
                count,
                reel.durationSec(),
                QFileInfo(out).size() / (1024.0 * 1024.0));
    return 0;
}
