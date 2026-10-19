// SPDX-License-Identifier: GPL-3.0-or-later
// Builds what the "Animate" frames draw from an AnimSource: the base with
// the objects that never animate painted in (pixelate and invert, so hidden
// content is never shown, not even for a frame), and an AnnotationLayer
// with every other object, drawn through CaptureTool::processReveal.

#pragma once

#include "utils/annotationlayer.h"

#include <QImage>
#include <QPainterPath>
#include <memory>

struct PreparedAnimSource
{
    QImage base; // devicePixelRatio set; static objects painted in
    std::shared_ptr<AnnotationLayer> annotations; // null without objects
    QVariantMap effectOverlay; // AnimSource::effectOverlay, passed through
};

namespace animannotations {

// Decodes and draws with tools, so call it on the GUI thread. The layer it
// returns may then be used (one thread at a time) from a worker thread.
PreparedAnimSource prepare(const AnimSource& source);

// The start of `path` up to `fraction` (0..1) of its length, as lines.
// Subpaths are walked in order; 0 is empty, 1 is the whole path.
QPainterPath trimPath(const QPainterPath& path, double fraction);

} // namespace animannotations
