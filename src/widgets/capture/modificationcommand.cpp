// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "modificationcommand.h"
#include "capturewidget.h"

ModificationCommand::ModificationCommand(
  CaptureWidget* captureWidget,
  const CaptureToolObjects& captureToolObjects,
  const CaptureToolObjects& captureToolObjectsBackup)
  : m_captureWidget(captureWidget)
{
    m_captureToolObjects = captureToolObjects;
    m_captureToolObjectsBackup = captureToolObjectsBackup;
}

void ModificationCommand::undo()
{
    m_captureWidget->setCaptureToolObjects(m_captureToolObjectsBackup);
}

void ModificationCommand::redo()
{
    m_captureWidget->setCaptureToolObjects(m_captureToolObjects);
}

ExpandCanvasCommand::ExpandCanvasCommand(CaptureWidget* captureWidget,
                                         int stepLogical)
  : m_captureWidget(captureWidget)
  , m_stepLogical(stepLogical)
{
    setText(QObject::tr("Expand canvas"));
}

void ExpandCanvasCommand::undo()
{
    m_captureWidget->applyCanvasExpansion(-m_stepLogical);
}

void ExpandCanvasCommand::redo()
{
    m_captureWidget->applyCanvasExpansion(m_stepLogical);
}

BeautifyEditCommand::BeautifyEditCommand(CaptureWidget* captureWidget,
                                         const EditCanvasState& before,
                                         const EditCanvasState& after,
                                         const QRect& shotSource)
  : m_captureWidget(captureWidget)
  , m_before(before)
  , m_after(after)
  , m_shotSource(shotSource)
{
    setText(QObject::tr("Beautify"));
}

void BeautifyEditCommand::undo()
{
    m_captureWidget->restoreEditCanvasState(m_before);
}

void BeautifyEditCommand::redo()
{
    m_captureWidget->restoreEditCanvasState(m_after);
}
