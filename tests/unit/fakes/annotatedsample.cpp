// SPDX-License-Identifier: GPL-3.0-or-later

#include "annotatedsample.h"
#include "drawobject.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"

#include <QPainter>
#include <QPointer>

#include <memory>

namespace testtools {

PreparedAnimSource annotatedSample()
{
    ConfigHandler cfg;
    cfg.setArrowStyle(0);
    cfg.setReverseArrow(false);
    cfg.setDrawCircleCounterOutline(true);
    QImage shot(QSize(900, 560), QImage::Format_ARGB32_Premultiplied);
    shot.fill(QColor(245, 246, 250));
    {
        QPainter painter(&shot);
        painter.fillRect(QRect(0, 0, 900, 36), QColor(228, 230, 236));
        for (int i = 0; i < 8; ++i) {
            painter.fillRect(QRect(40, 70 + 34 * i, 700 - (i * 97) % 260, 12),
                             QColor(90, 96, 110));
        }
        painter.fillRect(QRect(40, 360, 820, 160), QColor(70, 130, 220));
    }
    shot.setDevicePixelRatio(2.0);
    // Logical points (450 x 280)
    std::unique_ptr<CaptureTool> box(drawObject(
      CaptureTool::TYPE_SELECTION, QString(), QPoint(14, 28), QPoint(370, 166)));
    std::unique_ptr<CaptureTool> arrow(
      drawObject(CaptureTool::TYPE_ARROW, "bent", QPoint(400, 60), QPoint(330, 190)));
    std::unique_ptr<CaptureTool> counter(drawObject(
      CaptureTool::TYPE_CIRCLECOUNT, QString(), QPoint(40, 215), QPoint(40, 215)));
    std::unique_ptr<CaptureTool> text(
      drawObject(CaptureTool::TYPE_TEXT, "plain", QPoint(70, 210), QPoint(70, 210)));
    const QList<QPointer<CaptureTool>> tools = { box.get(), arrow.get(),
                                                 counter.get(), text.get() };
    AnimSource source;
    source.base = shot;
    source.dpr = 2.0;
    source.objects = captureproject::encodeObjects(tools, QPoint());
    return animannotations::prepare(source);
}

} // namespace testtools
