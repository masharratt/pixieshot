// SPDX-License-Identifier: GPL-3.0-or-later

#include "animannotations.h"
#include "tools/capturetool.h"
#include "utils/captureproject.h"

#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

#include <memory>
#include <vector>

namespace {

// The draw-in objects of one AnimSource, as live tools. The tools have no
// thread (moveToThread(nullptr)), so the export's worker thread may draw
// with and delete them.
class ToolAnnotationLayer : public AnnotationLayer
{
public:
    explicit ToolAnnotationLayer(std::vector<std::unique_ptr<CaptureTool>> tools)
      : m_tools(std::move(tools))
    {
        for (const auto& tool : m_tools) {
            tool->moveToThread(nullptr);
        }
    }

    int count() const override { return int(m_tools.size()); }

    void paint(QPainter& painter, int index, double progress) override
    {
        if (index < 0 || index >= count()) {
            return;
        }
        // Filters are baked into the base, so no draw-in tool reads the
        // pixels underneath: an empty pixmap is enough
        static const QPixmap kNone;
        painter.save();
        m_tools[size_t(index)]->processReveal(painter, kNone, progress);
        painter.restore();
    }

    QRectF bounds(int index) const override
    {
        if (index < 0 || index >= count()) {
            return {};
        }
        return QRectF(m_tools[size_t(index)]->boundingRect());
    }

private:
    std::vector<std::unique_ptr<CaptureTool>> m_tools;
};

} // namespace

namespace animannotations {

PreparedAnimSource prepare(const AnimSource& source)
{
    PreparedAnimSource out;
    out.base = source.base;
    out.base.setDevicePixelRatio(source.dpr > 0 ? source.dpr : 1.0);
    out.effectOverlay = source.effectOverlay;
    if (!source.hasAnnotations() || out.base.isNull()) {
        return out;
    }
    const QList<CaptureTool*> decoded =
      captureproject::decodeObjects(source.objects, QPoint(), nullptr, nullptr);
    std::vector<std::unique_ptr<CaptureTool>> drawIn;
    std::vector<std::unique_ptr<CaptureTool>> filters;
    for (CaptureTool* tool : decoded) {
        (tool->animatesIn() ? drawIn : filters).emplace_back(tool);
    }
    if (!filters.empty()) {
        // Like CaptureWidget::processPixmapWithTool, in creation order, each
        // seeing the pixels the previous ones left
        QPixmap pixmap = QPixmap::fromImage(out.base);
        for (const auto& tool : filters) {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            tool->process(painter, pixmap);
        }
        const QImage::Format format = out.base.format();
        out.base = pixmap.toImage().convertToFormat(format);
        out.base.setDevicePixelRatio(pixmap.devicePixelRatio());
    }
    if (!drawIn.empty()) {
        out.annotations = std::make_shared<ToolAnnotationLayer>(std::move(drawIn));
    }
    return out;
}

QPainterPath trimPath(const QPainterPath& path, double fraction)
{
    if (fraction <= 0.0 || path.isEmpty()) {
        return {};
    }
    const QList<QPolygonF> polygons = path.toSubpathPolygons();
    double total = 0.0;
    for (const QPolygonF& poly : polygons) {
        for (int i = 1; i < poly.size(); ++i) {
            total += QLineF(poly[i - 1], poly[i]).length();
        }
    }
    double left = std::min(1.0, fraction) * total;
    QPainterPath out;
    for (const QPolygonF& poly : polygons) {
        if (poly.isEmpty()) {
            continue;
        }
        out.moveTo(poly.first());
        for (int i = 1; i < poly.size(); ++i) {
            const QLineF seg(poly[i - 1], poly[i]);
            const double len = seg.length();
            if (len >= left) {
                out.lineTo(len > 0.0 ? seg.pointAt(left / len) : seg.p2());
                return out;
            }
            out.lineTo(poly[i]);
            left -= len;
        }
    }
    return out;
}

} // namespace animannotations
