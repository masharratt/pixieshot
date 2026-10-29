// SPDX-License-Identifier: GPL-3.0-or-later

#include "animpickers.h"
#include "utils/animatedbg.h"
#include "utils/shotmotion.h"

#include <QComboBox>
#include <QCoreApplication>

namespace animpickers {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("AnimPickers", text);
}

} // namespace

QString styleName(int value)
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
        case AnimStyle::Mesh:
            return tr("Mesh gradient");
        case AnimStyle::Bokeh:
            return tr("Bokeh");
        case AnimStyle::Waves:
            return tr("Waves");
        case AnimStyle::Grid:
            return tr("Glow grid");
        case AnimStyle::Prism:
            return tr("Prism light");
        case AnimStyle::Contours:
            return tr("Contours");
    }
    return {};
}

void fillStyles(QComboBox* combo)
{
    // Stored value order; new styles go at the end of the list
    for (int value = 0; value < kAnimStyleCount; ++value) {
        combo->addItem(styleName(value), value);
    }
}

void fillMotions(QComboBox* combo)
{
    combo->addItem(tr("None"), int(ShotMotion::None));
    combo->addItem(tr("Tilt and light sweep"), int(ShotMotion::Tilt));
    combo->addItem(tr("Float"), int(ShotMotion::Float));
    combo->addItem(tr("Orbit"), int(ShotMotion::Orbit));
    combo->setToolTip(tr("How the screenshot itself moves in the loop."));
}

void fillFormats(QComboBox* combo)
{
    combo->addItem(tr("MP4"), 0);
    combo->addItem(tr("GIF"), 1);
    combo->addItem(tr("MP4 and GIF"), 2);
}

void select(QComboBox* combo, int value)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace animpickers
