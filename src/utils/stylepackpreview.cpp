// SPDX-License-Identifier: GPL-3.0-or-later

#include "stylepackpreview.h"
#include "utils/animatedframe.h"
#include "utils/confighandler.h"

#include <QCoreApplication>
#include <QLinearGradient>
#include <QPainter>

namespace stylepackpreview {

namespace {

// Where in the loop the Animate frame is taken: mid-motion, so the style's
// light and colour show
constexpr double kAnimatePhase = 0.3;

QString tr(const char* text)
{
    return QCoreApplication::translate("StylePackPreview", text);
}

// The value a pack has for `key`, else the saved one
QVariant effective(const stylepack::Pack& pack, const char* key)
{
    const QString k = QString::fromLatin1(key);
    return pack.settings.contains(k) ? pack.settings.value(k)
                                     : ConfigHandler().value(k);
}

QString animStyleName(int value)
{
    switch (animatedbg::styleFrom(value)) {
        case AnimStyle::Aurora:
            return tr("Aurora");
        case AnimStyle::GradientShift:
            return tr("Gradient shift");
        case AnimStyle::Blobs:
            return tr("Blobs");
        case AnimStyle::Twinkle:
            return tr("Twinkle");
        case AnimStyle::Flow:
            return tr("Flow");
        case AnimStyle::Silk:
            return tr("Silk");
    }
    return {};
}

} // namespace

QImage sampleShot()
{
    static const QImage sample = [] {
        QImage img(1280, 800, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(245, 246, 250));
        QPainter p(&img);
        p.fillRect(QRect(0, 0, 1280, 40), QColor(228, 230, 236));
        const QColor dots[] = { QColor(255, 95, 87),
                                QColor(254, 188, 46),
                                QColor(40, 200, 64) };
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            p.setBrush(dots[i]);
            p.drawEllipse(QPoint(24 + 22 * i, 20), 7, 7);
        }
        p.fillRect(QRect(0, 40, 240, 760), QColor(236, 238, 244));
        for (int i = 0; i < 9; ++i) {
            p.fillRect(QRect(28, 80 + 44 * i, 140 - (i * 23) % 60, 12),
                       QColor(120, 126, 140));
        }
        p.fillRect(QRect(290, 80, 520, 30), QColor(36, 40, 52));
        for (int i = 0; i < 6; ++i) {
            p.fillRect(QRect(290, 140 + 30 * i, 900 - (i * 137) % 260, 12),
                       QColor(90, 96, 110));
        }
        QLinearGradient chart(290, 360, 1190, 720);
        chart.setColorAt(0, QColor(70, 130, 220));
        chart.setColorAt(1, QColor(240, 170, 60));
        p.setBrush(chart);
        p.drawRoundedRect(QRect(290, 340, 900, 360), 16, 16);
        p.setBrush(QColor(236, 72, 96));
        p.drawRoundedRect(QRect(1040, 730, 150, 44), 10, 10);
        return img;
    }();
    return sample;
}

EffectSettings settingsFor(const stylepack::Pack& pack)
{
    return ConfigHandler().effectSettings(pack.settings);
}

QImage renderBeautify(const stylepack::Pack& pack, int width)
{
    const QImage full =
      imageeffects::applyEffects(sampleShot(), settingsFor(pack));
    return full.scaledToWidth(width, Qt::SmoothTransformation)
      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage renderAnimate(const stylepack::Pack& pack, int width)
{
    const AnimStyle style =
      animatedbg::styleFrom(effective(pack, "animStyle").toInt());
    const AnimatedFrames frames(
      sampleShot(),
      AnimatedFrames::animatedSettings(settingsFor(pack)),
      style,
      animatedbg::kDefaultSeed,
      width);
    return frames.frame(kAnimatePhase)
      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QStringList chips(const stylepack::Pack& pack)
{
    QStringList out;
    switch (static_cast<BackgroundMode>(
      effective(pack, "effectBackground").toInt())) {
        case BackgroundMode::None:
            out << tr("No background");
            break;
        case BackgroundMode::Solid:
            out << tr("Solid");
            break;
        case BackgroundMode::Gradient:
            out << tr("Gradient");
            break;
        case BackgroundMode::FromScreenshot:
            out << tr("From screenshot");
            break;
    }
    if (effective(pack, "effectGlass").toBool()) {
        out << tr("Glass");
    }
    if (effective(pack, "effectGrain").toInt() > 0) {
        out << tr("Grain");
    }
    out << tr("Animate: %1")
             .arg(animStyleName(effective(pack, "animStyle").toInt()));
    return out;
}

} // namespace stylepackpreview
