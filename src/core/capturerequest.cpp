// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "capturerequest.h"

#include <stdexcept>
#include <utility>

CaptureRequest::CaptureRequest(CaptureRequest::CaptureMode mode,
                               const uint delay,
                               QVariant data,
                               CaptureRequest::ExportTask tasks)
  : m_mode(mode)
  , m_delay(delay)
  , m_tasks(tasks)
  , m_data(std::move(data))
  , m_selectedMonitor(-1)
  , m_hasSelectedMonitor(false)
{}

CaptureRequest::CaptureMode CaptureRequest::captureMode() const
{
    return m_mode;
}

uint CaptureRequest::delay() const
{
    return m_delay;
}

QString CaptureRequest::path() const
{
    return m_path;
}

QVariant CaptureRequest::data() const
{
    return m_data;
}

CaptureRequest::ExportTask CaptureRequest::tasks() const
{
    return m_tasks;
}

QRect CaptureRequest::initialSelection() const
{
    return m_initialSelection;
}

QPixmap CaptureRequest::editImage() const
{
    return m_editImage;
}

CaptureProject CaptureRequest::project() const
{
    return m_project;
}

QVariantMap CaptureRequest::effectOverlay() const
{
    return m_effectOverlay;
}

void CaptureRequest::setEffectOverlay(const QVariantMap& overlay)
{
    m_effectOverlay = overlay;
}

void CaptureRequest::setProject(const CaptureProject& project)
{
    m_project = project;
}

bool CaptureRequest::overwriteExisting() const
{
    return m_overwriteExisting;
}

bool CaptureRequest::captureFirst() const
{
    return m_captureFirst;
}

QString CaptureRequest::workflow() const
{
    return m_workflow;
}

void CaptureRequest::setWorkflow(const QString& workflow)
{
    m_workflow = workflow;
}

CaptureRequest::RecordMode CaptureRequest::recordMode() const
{
    return m_recordMode;
}

void CaptureRequest::setRecordMode(RecordMode mode)
{
    m_recordMode = mode;
}

bool CaptureRequest::scrollCapture() const
{
    return m_scrollCapture;
}

void CaptureRequest::setScrollCapture(bool scrollCapture)
{
    m_scrollCapture = scrollCapture;
}

void CaptureRequest::setCaptureFirst(bool captureFirst)
{
    m_captureFirst = captureFirst;
}

void CaptureRequest::setEditImage(const QPixmap& image)
{
    m_editImage = image;
}

void CaptureRequest::setOverwriteExisting(bool overwrite)
{
    m_overwriteExisting = overwrite;
}

void CaptureRequest::addTask(CaptureRequest::ExportTask task)
{
    if (task == SAVE) {
        throw std::logic_error("SAVE task must be added using addSaveTask");
    }
    m_tasks |= task;
}

void CaptureRequest::removeTask(ExportTask task)
{
    ((int&)m_tasks) &= ~task;
}

void CaptureRequest::addSaveTask(const QString& path)
{
    m_tasks |= SAVE;
    // An edit of a saved capture must keep overwriting that file, even when
    // the editor's Save tool asks for a save without a path.
    if (path.isEmpty() && m_overwriteExisting && !m_path.isEmpty()) {
        return;
    }
    m_path = path;
}

void CaptureRequest::addPinTask(const QRect& pinWindowGeometry)
{
    m_tasks |= PIN;
    m_pinWindowGeometry = pinWindowGeometry;
}

void CaptureRequest::setInitialSelection(const QRect& selection)
{
    m_initialSelection = selection;
}

void CaptureRequest::setSelectedMonitor(int monitorIndex)
{
    m_selectedMonitor = monitorIndex;
    m_hasSelectedMonitor = true;
}

int CaptureRequest::selectedMonitor() const
{
    return m_selectedMonitor;
}

bool CaptureRequest::hasSelectedMonitor() const
{
    return m_hasSelectedMonitor;
}

QRect CaptureRequest::capturedGlobalRect() const
{
    return m_capturedGlobalRect;
}

void CaptureRequest::setCapturedGlobalRect(const QRect& rect)
{
    m_capturedGlobalRect = rect;
}
