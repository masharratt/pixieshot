// SPDX-License-Identifier: GPL-3.0-or-later

#include "demoshot.h"

#include "config/configwindow.h"
#include "demoreel.h"
#include "tools/abstracttwopointtool.h"
#include "tools/capturecontext.h"
#include "tools/capturetool.h"
#include "tools/drawabletools.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"
#include "utils/stylepack.h"

#include <QComboBox>
#include <QDir>
#include <QGroupBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTest>

#include <memory>
#include <vector>

namespace {

// Pixieshot pink and violet, as in the banner
const QColor kPink(236, 72, 153);
const QColor kViolet(139, 92, 246);

CaptureContext contextAt(const QPoint& at, const QColor& color, int size)
{
    CaptureContext ctx;
    ctx.color = color;
    ctx.toolSize = size;
    ctx.mousePos = at;
    ctx.circleCount = 1;
    ctx.fullscreen = false;
    return ctx;
}

// Drawn the way the editor does it: press, drag, release
std::unique_ptr<CaptureTool> drag(CaptureTool::Type type,
                                  const QPoint& from,
                                  const QPoint& to,
                                  const QColor& color,
                                  int size)
{
    std::unique_ptr<CaptureTool> tool(createDrawableTool(type));
    tool->drawStart(contextAt(from, color, size));
    tool->drawMove(to);
    tool->drawEnd(to);
    return tool;
}

std::unique_ptr<CaptureTool> text(const QPoint& at,
                                  const QString& words,
                                  const QColor& color,
                                  int size)
{
    std::unique_ptr<CaptureTool> tool(
      createDrawableTool(CaptureTool::TYPE_TEXT));
    tool->drawStart(contextAt(at, color, size));
    QMetaObject::invokeMethod(tool.get(), "updateText", Q_ARG(QString, words));
    tool->onSizeChanged(size);
    tool->drawEnd(at);
    return tool;
}

// Where the annotations go, in the window's logical points
struct Targets
{
    QRect thumbs; // the style pack's Beautify and Animate previews
    QRect apply;  // its Apply button
};

// A box round the previews, numbered, a note and an arrow to Apply
PreparedAnimSource annotate(const QImage& shot, const Targets& at)
{
    const QRect box = at.thumbs.adjusted(-10, -3, 10, 6);
    const QPoint note(box.right() + 34, box.center().y() + 10);
    // The counter sits on the box's left edge, clear of the pack picker
    const QPoint number(box.left(), box.center().y());
    std::vector<std::unique_ptr<CaptureTool>> objects;
    objects.push_back(drag(
      CaptureTool::TYPE_SELECTION, box.topLeft(), box.bottomRight(), kPink, 4));
    objects.push_back(
      drag(CaptureTool::TYPE_CIRCLECOUNT, number, number, kViolet, 7));
    const QPoint tail(note.x() + 60, note.y() - 14);
    const QPoint head(at.apply.center().x(), at.apply.bottom() + 6);
    std::unique_ptr<CaptureTool> arrow =
      drag(CaptureTool::TYPE_ARROW, tail, head, kPink, 5);
    static_cast<AbstractTwoPointTool*>(arrow.get())
      ->setBendHandle(QPointF((tail + head) / 2 + QPoint(36, 6)));
    objects.push_back(std::move(arrow));
    objects.push_back(text(note, QStringLiteral("One click ✨"), kPink, 14));

    QList<QPointer<CaptureTool>> tools;
    for (const auto& tool : objects) {
        tools.append(tool.get());
    }
    AnimSource source;
    source.base = shot;
    source.dpr = shot.devicePixelRatio();
    source.objects = captureproject::encodeObjects(tools, QPoint());
    return animannotations::prepare(source);
}

QRect rectIn(const QWidget* widget, const QWidget* window)
{
    return widget == nullptr
             ? QRect()
             : QRect(widget->mapTo(window, QPoint()), widget->size());
}

} // namespace

namespace demoshot {

PreparedAnimSource settingsShot(int tab)
{
    auto* window = new ConfigWindow();
    window->resize(820, 470);
    window->show();
    auto* tabs = window->findChild<QTabWidget*>();
    if (tabs != nullptr) {
        tabs->setCurrentIndex(qBound(0, tab, tabs->count() - 1));
    }
    QTest::qWait(100);
    // The most colourful built-in pack in the previews
    for (auto* combo : window->findChildren<QComboBox*>()) {
        const int pack =
          combo->findText(QStringLiteral("Sunset Pop"), Qt::MatchStartsWith);
        if (pack >= 0 && combo->isVisible()) {
            combo->setCurrentIndex(pack);
        }
    }
    // Scroll the Effects box to the top of the page
    auto* area =
      qobject_cast<QScrollArea*>(tabs != nullptr ? tabs->currentWidget() : nullptr);
    for (auto* box : window->findChildren<QGroupBox*>()) {
        if (area != nullptr && box->isVisible() &&
            box->title() == QObject::tr("Effects")) {
            area->verticalScrollBar()->setValue(
              box->mapTo(area->widget(), QPoint()).y() - 10);
        }
    }
    QTest::qWait(150);
    Targets targets;
    const auto* beautify =
      window->findChild<QWidget*>(QStringLiteral("stylePackBeautify"));
    const auto* animate =
      window->findChild<QWidget*>(QStringLiteral("stylePackAnimate"));
    targets.thumbs = rectIn(beautify, window) | rectIn(animate, window);
    for (auto* button : window->findChildren<QPushButton*>()) {
        if (button->isVisible() && button->text() == QObject::tr("Apply")) {
            targets.apply = rectIn(button, window);
        }
    }
    const QImage shot = window->grab().toImage();
    window->close(); // deletes itself (WA_DeleteOnClose)
    if (targets.thumbs.isEmpty() || targets.apply.isEmpty()) {
        qWarning("demogif: style pack row not found, no annotations");
        PreparedAnimSource bare;
        bare.base = shot;
        return bare;
    }
    return annotate(shot, targets);
}

EffectSettings settingsFor(const DemoScene& scene)
{
    QVariantMap overlay;
    if (!scene.pack.isEmpty()) {
        const stylepack::ParseResult pack = stylepack::loadFile(
          QDir(stylepack::builtInDir()).filePath(scene.pack));
        if (pack.ok) {
            overlay = pack.pack.settings;
        } else {
            qWarning("demogif: style pack %s: %s",
                     qPrintable(scene.pack),
                     qPrintable(pack.error));
        }
    }
    for (auto it = scene.look.cbegin(); it != scene.look.cend(); ++it) {
        overlay.insert(it.key(), it.value());
    }
    return ConfigHandler().effectSettings(overlay);
}

} // namespace demoshot
