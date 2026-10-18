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

void fillStyles(QComboBox* combo)
{
    combo->addItem(tr("Aurora"), int(AnimStyle::Aurora));
    combo->addItem(tr("Gradient shift"), int(AnimStyle::GradientShift));
    combo->addItem(tr("Blobs"), int(AnimStyle::Blobs));
    combo->addItem(tr("Twinkle"), int(AnimStyle::Twinkle));
    combo->addItem(tr("Flow"), int(AnimStyle::Flow));
    combo->addItem(tr("Silk"), int(AnimStyle::Silk));
}

void fillMotions(QComboBox* combo)
{
    combo->addItem(tr("None"), int(ShotMotion::None));
    combo->addItem(tr("Tilt and light sweep"), int(ShotMotion::Tilt));
    combo->addItem(tr("Float"), int(ShotMotion::Float));
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
