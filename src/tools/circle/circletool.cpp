// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "circletool.h"
#include "tools/animannotations.h"

#include <QPainterPath>

#include <QPainter>

CircleTool::CircleTool(QObject* parent)
  : AbstractTwoPointTool(parent)
{
    m_supportsDiagonalAdj = true;
}

QIcon CircleTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "circle-outline.svg");
}
QString CircleTool::name() const
{
    return tr("Circle");
}

CaptureTool::Type CircleTool::type() const
{
    return CaptureTool::TYPE_CIRCLE;
}

QString CircleTool::description() const
{
    return tr("Set the Circle as the paint tool");
}

CaptureTool* CircleTool::copy(QObject* parent)
{
    auto* tool = new CircleTool(parent);
    copyParams(this, tool);
    return tool;
}

void CircleTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    painter.setPen(QPen(color(), size()));
    painter.drawEllipse(QRect(points().first, points().second));
}

// The outline draws itself around
void CircleTool::processReveal(QPainter& painter,
                               const QPixmap& pixmap,
                               double progress)
{
    if (progress <= 0.0 || progress >= 1.0) {
        CaptureTool::processReveal(painter, pixmap, progress);
        return;
    }
    QPainterPath path;
    path.addEllipse(QRectF(QRect(points().first, points().second)));
    painter.setPen(QPen(color(), size()));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(animannotations::trimPath(path, progress));
}

void CircleTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
