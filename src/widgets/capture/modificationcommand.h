// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "capturetoolobjects.h"
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QUndoCommand>

// Everything that makes up the windowed editor's canvas. Copies deep-copy
// the annotations (CaptureToolObjects is a QObject with its own operator=).
struct EditCanvasState
{
    QPixmap canvas;
    QRect imageLogical;
    QRect imageDevice;
    QSize canvasLogical;
    CaptureToolObjects objects;

    EditCanvasState() = default;
    EditCanvasState(const EditCanvasState& other) { *this = other; }
    EditCanvasState& operator=(const EditCanvasState& other)
    {
        canvas = other.canvas;
        imageLogical = other.imageLogical;
        imageDevice = other.imageDevice;
        canvasLogical = other.canvasLogical;
        objects = other.objects;
        return *this;
    }
};

class CaptureWidget;

class ModificationCommand : public QUndoCommand
{
public:
    ModificationCommand(CaptureWidget* captureWidget,
                        const CaptureToolObjects& captureToolObjects,
                        const CaptureToolObjects& captureToolObjectsBackup);

    virtual void undo() override;
    virtual void redo() override;

private:
    CaptureToolObjects m_captureToolObjects;
    CaptureToolObjects m_captureToolObjectsBackup;
    CaptureWidget* m_captureWidget;
};

// Windowed editor: one click of Expand canvas. Redo adds the step on every
// edge, undo removes it again. Annotations are shifted by the same step, so
// the stack of earlier commands stays valid.
class ExpandCanvasCommand : public QUndoCommand
{
public:
    ExpandCanvasCommand(CaptureWidget* captureWidget, int stepLogical);

    void undo() override;
    void redo() override;

private:
    CaptureWidget* m_captureWidget;
    int m_stepLogical;
};

// Windowed editor: Beautify applied in place. The image and its annotations
// were flattened and beautified; undo puts back the canvas and the editable
// annotations exactly as they were.
class BeautifyEditCommand : public QUndoCommand
{
public:
    // id() of this command, so a second Beautify press can recognise it
    static constexpr int kId = 0x6265; // "be"

    // shotSource: where the un-beautified pixels sit in the beautified
    // image (device pixels, relative to the image)
    BeautifyEditCommand(CaptureWidget* captureWidget,
                        const EditCanvasState& before,
                        const EditCanvasState& after,
                        const QRect& shotSource);

    const EditCanvasState& before() const { return m_before; }
    const QRect& shotSource() const { return m_shotSource; }

    int id() const override { return kId; }
    void undo() override;
    void redo() override;

private:
    CaptureWidget* m_captureWidget;
    EditCanvasState m_before;
    EditCanvasState m_after;
    QRect m_shotSource;
};
