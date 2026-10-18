// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "linetool.h"
#include "tools/animannotations.h"

#include "tools/curvemath.h"

#include <QPainter>
#include <QPainterPath>

LineTool::LineTool(QObject* parent)
  : AbstractTwoPointTool(parent)
{
    m_supportsOrthogonalAdj = true;
    m_supportsDiagonalAdj = true;
}

QIcon LineTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "line.svg");
}

QString LineTool::name() const
{
    return tr("Line");
}

CaptureTool::Type LineTool::type() const
{
    return CaptureTool::TYPE_DRAWER;
}

QString LineTool::description() const
{
    return tr("Set the Line as the paint tool");
}

CaptureTool* LineTool::copy(QObject* parent)
{
    auto* tool = new LineTool(parent);
    copyParams(this, tool);
    return tool;
}

void LineTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    painter.setPen(QPen(color(), size()));
    if (!isBent()) {
        painter.drawLine(points().first, points().second);
        return;
    }
    QPainterPath path(points().first);
    path.quadTo(*control(), points().second);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

// Draws from the start point to the end, along the bend if any
void LineTool::processReveal(QPainter& painter,
                             const QPixmap& pixmap,
                             double progress)
{
    if (progress <= 0.0 || progress >= 1.0) {
        CaptureTool::processReveal(painter, pixmap, progress);
        return;
    }
    QPainterPath path(points().first);
    if (isBent()) {
        path.quadTo(*control(), points().second);
    } else {
        path.lineTo(points().second);
    }
    painter.setPen(QPen(color(), size()));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(animannotations::trimPath(path, progress));
}

void LineTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
