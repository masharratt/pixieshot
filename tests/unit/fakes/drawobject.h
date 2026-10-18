// SPDX-License-Identifier: GPL-3.0-or-later
// Test helper: draws an annotation the way the editor does (press, drag,
// release), so tests get real tool objects. Needs the tool sources and
// fakes/fakeconfighandler.cpp linked in.

#pragma once

#include "tools/abstracttwopointtool.h"
#include "tools/capturecontext.h"
#include "tools/capturetool.h"
#include "tools/drawabletools.h"

#include <QMetaObject>
#include <QString>

namespace testtools {

inline CaptureContext context(const QPoint& at = QPoint(20, 30))
{
    CaptureContext ctx;
    ctx.color = QColor(200, 30, 40, 230);
    ctx.toolSize = 5;
    ctx.mousePos = at;
    ctx.circleCount = 3;
    ctx.fullscreen = false;
    return ctx;
}

// Draws an object of the given type from `from` to `to`, then any
// tool-specific edit named by `variant` ("bent", "curvedHead"; for text
// "plain" skips the italic/underline/right-aligned styling).
inline CaptureTool* drawObject(CaptureTool::Type type,
                               const QString& variant,
                               const QPoint& from = QPoint(20, 30),
                               const QPoint& to = QPoint(110, 90))
{
    CaptureTool* tool = createDrawableTool(type);
    if (!tool) {
        return nullptr;
    }
    CaptureContext ctx = context(from);
    if (type == CaptureTool::TYPE_TEXT) {
        tool->drawStart(ctx);
        QMetaObject::invokeMethod(
          tool, "updateText", Q_ARG(QString, QStringLiteral("Hello\nworld")));
        if (variant != QLatin1String("plain")) {
            QMetaObject::invokeMethod(
              tool, "updateFontItalic", Q_ARG(bool, true));
            QMetaObject::invokeMethod(
              tool, "updateFontUnderline", Q_ARG(bool, true));
            QMetaObject::invokeMethod(tool,
                                      "updateAlignment",
                                      Q_ARG(Qt::AlignmentFlag, Qt::AlignRight));
        }
        tool->onSizeChanged(7);
        tool->drawEnd(from + QPoint(-5, -10));
        return tool;
    }
    tool->drawStart(ctx);
    if (type == CaptureTool::TYPE_PENCIL || type == CaptureTool::TYPE_MARKER) {
        tool->drawMove(from + QPoint(20, 20));
        tool->drawMove(from + QPoint(50, 15));
        tool->drawMove(to);
    } else {
        tool->drawMove(to);
    }
    tool->drawEnd(to);
    if (variant == QLatin1String("bent")) {
        static_cast<AbstractTwoPointTool*>(tool)->setBendHandle(
          QPointF(from + QPoint(70, 0)));
    }
    if (variant == QLatin1String("curvedHead")) {
        QMetaObject::invokeMethod(tool, "setArrowStyle", Q_ARG(int, 1));
    }
    return tool;
}

} // namespace testtools
