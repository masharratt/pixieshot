// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "utils/captureproject.h"

#include <QPixmap>
#include <QString>
#include <QVariant>
#include <QVariantMap>

class CaptureRequest
{
public:
    enum CaptureMode
    {
        FULLSCREEN_MODE,
        GRAPHICAL_MODE,
        SCREEN_MODE,
    };

    enum ExportTask
    {
        NO_TASK = 0,
        COPY = 1,
        SAVE = 2,
        PRINT_RAW = 4,
        PRINT_GEOMETRY = 8,
        PIN = 16,
        UPLOAD = 32,
        ACCEPT_ON_SELECT = 64,
        BEAUTIFY = 128,
        // Also export the capture over an animated background (macOS)
        ANIMATE = 256,
    };

    // Set on requests that only pick an area to record. No image is saved.
    enum RecordMode
    {
        RecordNone,
        RecordMp4,
        RecordGif,
    };

    CaptureRequest(CaptureMode mode,
                   const uint delay = 0,
                   QVariant data = QVariant(),
                   ExportTask tasks = NO_TASK);

    void setStaticID(uint id);

    uint id() const;
    uint delay() const;
    QString path() const;
    QVariant data() const;
    CaptureMode captureMode() const;
    ExportTask tasks() const;
    QRect initialSelection() const;
    // Image to edit in the windowed editor (null for normal captures). Its
    // device pixel ratio is the canvas ratio.
    QPixmap editImage() const;
    bool overwriteExisting() const;
    // Re-editable project of a finished annotated capture (empty otherwise)
    CaptureProject project() const;
    // Effect options that replace the saved ones when this request is
    // beautified (see ConfigHandler::effectSettings); empty uses the config
    QVariantMap effectOverlay() const;
    bool captureFirst() const;
    QRect capturedGlobalRect() const;
    // Name of the hotkey that started this request ("" for CLI and editor).
    QString workflow() const;
    RecordMode recordMode() const;
    // Set on requests that only pick the area of a scrolling capture
    bool scrollCapture() const;

    void addTask(ExportTask task);
    void removeTask(ExportTask task);
    void addSaveTask(const QString& path = QString());
    void addPinTask(const QRect& pinWindowGeometry);
    void setInitialSelection(const QRect& selection);
    // Open the windowed editor on this image instead of grabbing the screen.
    void setEditImage(const QPixmap& image);
    void setProject(const CaptureProject& project);
    void setEffectOverlay(const QVariantMap& overlay);
    // Save to the exact path given, replacing the file instead of numbering.
    void setOverwriteExisting(bool overwrite);
    // Marks a request made by the capture-first hotkey or tray path.
    void setCaptureFirst(bool captureFirst);
    void setCapturedGlobalRect(const QRect& rect);
    // When set, the after-capture actions configured for this hotkey replace
    // the SAVE and COPY tasks.
    void setWorkflow(const QString& workflow);
    void setRecordMode(RecordMode mode);
    void setScrollCapture(bool scrollCapture);
    void setSelectedMonitor(int monitorIndex);
    int selectedMonitor() const;
    bool hasSelectedMonitor() const;

private:
    CaptureMode m_mode;
    uint m_delay;
    QString m_path;
    ExportTask m_tasks;
    QVariant m_data;
    QRect m_pinWindowGeometry, m_initialSelection;
    QPixmap m_editImage;
    CaptureProject m_project;
    QVariantMap m_effectOverlay;
    bool m_overwriteExisting = false;
    bool m_captureFirst = false;
    QRect m_capturedGlobalRect;
    QString m_workflow;
    RecordMode m_recordMode = RecordNone;
    bool m_scrollCapture = false;
    int m_selectedMonitor;
    bool m_hasSelectedMonitor;

    CaptureRequest() {}
};

using eTask = CaptureRequest::ExportTask;

inline eTask operator|(const eTask& a, const eTask& b)
{
    return static_cast<eTask>(static_cast<int>(a) | static_cast<int>(b));
}

inline eTask operator&(const eTask& a, const eTask& b)
{
    return static_cast<eTask>(static_cast<int>(a) & static_cast<int>(b));
}

inline eTask& operator|=(eTask& a, const eTask& b)
{
    a = static_cast<eTask>(static_cast<int>(a) | static_cast<int>(b));
    return a;
}
