// SPDX-License-Identifier: GPL-3.0-or-later
// What the "Animate" export needs to know about annotations: the base image
// without them and the objects as the project file stores them (AnimSource),
// and, once prepared, something that draws object i part of the way through
// its reveal (AnnotationLayer). Keeps utils/animatedframe free of the tool
// classes; tools/animannotations builds the layer from real tools.

#pragma once

#include <QImage>
#include <QJsonArray>
#include <QRectF>

class QPainter;

// Overlay and editor both hand Animate one of these
struct AnimSource
{
    QImage base;        // device pixels, no annotations, no effects
    QJsonArray objects; // captureproject JSON, bottom first, logical points
                        // relative to the base's top-left
    qreal dpr = 1.0;

    bool hasAnnotations() const { return !objects.isEmpty(); }
};

enum class AnnotationMode
{
    Static = 0, // drawn into the shot for the whole loop
    DrawIn = 1, // each object draws itself in, in creation order
};

class AnnotationLayer
{
public:
    virtual ~AnnotationLayer() = default;
    // Objects that draw themselves in, in creation order
    virtual int count() const = 0;
    // Draws object `index` `progress` (0..1) of the way through its reveal.
    // The painter is in the base image's logical coordinates; its opacity
    // (fade, pulse) is the caller's and is left as it was.
    virtual void paint(QPainter& painter, int index, double progress) = 0;
    // Logical rectangle the finished object covers
    virtual QRectF bounds(int index) const = 0;
};

namespace animannotations {

// Clamps a stored number to a mode
inline AnnotationMode modeFrom(int value)
{
    return value <= 0 ? AnnotationMode::Static : AnnotationMode::DrawIn;
}

} // namespace animannotations
