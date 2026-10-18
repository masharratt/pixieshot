// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "penciltool.h"
#include "tools/animannotations.h"

#include <QPainterPath>
#include <QPainter>

PencilTool::PencilTool(QObject* parent)
  : AbstractPathTool(parent)
{}

QIcon PencilTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "pencil.svg");
}
QString PencilTool::name() const
{
    return tr("Pencil");
}

CaptureTool::Type PencilTool::type() const
{
    return CaptureTool::TYPE_PENCIL;
}

QString PencilTool::description() const
{
    return tr("Set the Pencil as the paint tool");
}

CaptureTool* PencilTool::copy(QObject* parent)
{
    auto* tool = new PencilTool(parent);
    copyParams(this, tool);
    return tool;
}

void PencilTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    painter.setPen(QPen(m_color, size()));
    painter.drawPolyline(m_points.data(), m_points.size());
}

// Draws along the stroke, from where the pen went down
void PencilTool::processReveal(QPainter& painter,
                               const QPixmap& pixmap,
                               double progress)
{
    if (progress <= 0.0 || progress >= 1.0 || m_points.size() < 2) {
        CaptureTool::processReveal(painter, pixmap, progress);
        return;
    }
    QPainterPath path(m_points.first());
    for (int i = 1; i < m_points.size(); ++i) {
        path.lineTo(m_points[i]);
    }
    painter.setPen(QPen(m_color, size()));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(animannotations::trimPath(path, progress));
}

void PencilTool::paintMousePreview(QPainter& painter,
                                   const CaptureContext& context)
{
    painter.setPen(QPen(context.color, context.toolSize + 2));
    painter.drawLine(context.mousePos, context.mousePos);
}

void PencilTool::drawStart(const CaptureContext& context)
{
    m_color = context.color;
    onSizeChanged(context.toolSize);
    m_points.append(context.mousePos);
    m_pathArea.setTopLeft(context.mousePos);
    m_pathArea.setBottomRight(context.mousePos);
}

void PencilTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
