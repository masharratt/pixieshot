// SPDX-License-Identifier: GPL-3.0-or-later

#include "animatetool.h"

AnimateTool::AnimateTool(QObject* parent)
  : AbstractActionTool(parent)
{}

bool AnimateTool::closeOnButtonPressed() const
{
    return true;
}

QIcon AnimateTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "animate.svg");
}

QString AnimateTool::name() const
{
    return tr("Animate");
}

CaptureTool::Type AnimateTool::type() const
{
    return CaptureTool::TYPE_ANIMATE;
}

QString AnimateTool::description() const
{
    return tr("Export with an animated background");
}

CaptureTool* AnimateTool::copy(QObject* parent)
{
    return new AnimateTool(parent);
}

void AnimateTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
    // The widget decides: finish and export (overlay) or export in place
    // (windowed editor)
    emit requestAction(REQ_ANIMATE);
}
