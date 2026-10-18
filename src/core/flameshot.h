// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "core/capturedelay.h"
#include "core/capturerequest.h"
#include "widgets/capture/capturewidget.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QVersionNumber>
#include <QWindow>

class ConfigWindow;
class InfoWindow;
class CaptureLauncher;
class EditorWindow;
class QTimer;
class QWidget;
class RecordingController;
class ScrollCaptureController;
#ifdef ENABLE_IMGUR
class UploadHistory;
#endif
#if (defined(Q_OS_MACOS) || defined(Q_OS_WIN))
class QHotkey;
#endif

enum ErrCode : uint8_t
{
    E_OK = 0,
    E_GENERAL,
    E_ABORTED,
    E_DBUSCONN,
    E_SIG_BASE = 128,
    E_SIGINT = E_SIG_BASE + 2,
    E_SIGTERM = E_SIG_BASE + 15,
};

class Flameshot : public QObject
{
    Q_OBJECT

public:
    enum Origin
    {
        CLI,
        DAEMON
    };

    static Flameshot* instance();

public slots:
    CaptureWidget* gui(
      const CaptureRequest& req = CaptureRequest::GRAPHICAL_MODE);
    // Region capture started by a hotkey or the tray: waits for the capture
    // timer (config captureDelaySec, shown as a countdown) before gui(). Returns
    // the widget when it starts at once, nullptr while counting down.
    CaptureWidget* guiWithTimer(
      const CaptureRequest& req = CaptureRequest::GRAPHICAL_MODE);
    void screen(CaptureRequest req, int const screenNumber = -1);
    void full(const CaptureRequest& req);
    void launcher();
    void config();

    void info();

#ifdef ENABLE_IMGUR
    void history();
#endif

    void openSavePath();

    QVersionNumber getVersion();

public:
    static void setOrigin(Origin origin);
    static Origin origin();
    void setExternalWidget(bool b);
    bool haveExternalWidget();

signals:
    void captureTaken(QPixmap p);
    // An editor window opened or closed (tray menu enable state)
    void editorWindowsChanged();
    void captureFailed();
    // selection uses exportCapture units (for pins); globalRect is the
    // captured area in global logical points.
    void captureSaved(const QString& path,
                      const QPixmap& capture,
                      const QRect& selection,
                      const QRect& globalRect);
    // A screen recording was saved. kind is "mp4" or "gif", size in pixels.
    // editSource is the MP4 to edit from (see RecordingController).
    void recordingSaved(const QString& path,
                        const QString& kind,
                        const QSize& size,
                        const QString& editSource);
    // An "Animate" export was saved (kind "mp4" or "gif"). project is its
    // re-editable capture project, empty when it could not be written.
    void animationSaved(const QString& path,
                        const QString& kind,
                        const QSize& size,
                        const QString& project);

public slots:
    // Record hotkey or menu item: picks an area, or stops a running recording
    void toggleRecording(CaptureRequest::RecordMode mode);
    // Scrolling capture hotkey or menu item: picks an area, or finishes the
    // capture in progress
    void toggleScrollCapture();
    void requestCapture(const CaptureRequest& request);
    // Reopen a saved capture in its own editor window. Saving overwrites the
    // file at path. capture is in device pixels and carries its own device
    // pixel ratio. Editor windows are independent of the capture overlay.
    void editSavedCapture(const QString& path, const QPixmap& capture);
    // Reopen a capture from its project: the base image with every
    // annotation still editable. Saving overwrites the file at path.
    void editProject(const QString& path, const CaptureProject& project);
    // Edit again: opens the history's project for path when there is one;
    // when there is none, or it cannot be read (the user is told why), opens
    // the flat image like editSavedCapture.
    void editCaptureAgain(const QString& path, const QPixmap& capture);
    // Edit on an "Animate" output's toast: opens its project in the editor
    // (annotations movable, Animate preset to how it was made). The user is
    // told when the project cannot be read.
    void editAnimation(const QString& path, const QString& projectPath);
    // Brings every open editor window to the front (tray menu)
    void showEditorWindows();
    bool hasEditorWindows() const;
    // Adds or removes the Dock icon (Cmd+Tab entry) for open editors
    void updateEditorDockIcon();
    void exportCapture(const QPixmap& p,
                       QRect& selection,
                       const CaptureRequest& req);

private:
    Flameshot();
    bool resolveAnyConfigErrors();
    void openEditorWindow(EditorWindow* editor);
    // Where an animation's editor saves its still: same folder and name, PNG
    static QString stillPathFor(const QString& animationPath);
    // Writes the pending project into the history's project folder; the
    // file path, or empty when there is nothing to write or it failed
    QString storePendingProject(const QString& projectDir) const;
    void runWorkflow(const QPixmap& capture,
                     const QRect& selection,
                     const CaptureRequest& req,
                     bool effectsApplied,
                     bool effectsNeedAlpha);

    // class members
    static Origin m_origin;
    bool m_haveExternalWidget;

    QPointer<CaptureWidget> m_captureWindow;
    QList<QPointer<EditorWindow>> m_editorWindows;
    QPointer<InfoWindow> m_infoWindow;
    QPointer<CaptureLauncher> m_launcherWindow;
    QPointer<ConfigWindow> m_configWindow;
    RecordingController* m_recording = nullptr;
    ScrollCaptureController* m_scrolling = nullptr;
    CountdownStateMachine m_countdown;
    // Project of the capture being exported; set only during exportCapture
    // so every save it makes records it in the history
    CaptureProject m_pendingProject;

#if defined(Q_OS_MACOS)
public:
    void showDockIcon(QWidget* window);

private:
    void onWindowVisibilityChanged(QWindow::Visibility newVisibility);
    int m_dockIconVisibleCount = 0;
    bool m_editorDockIcon = false;
#endif

#if (defined(Q_OS_MACOS) || defined(Q_OS_WIN))
    // Global hotkeys by shortcut name. Entries are created once and only
    // re-registered when their key sequence changes in the config.
    void syncHotkeys();
    void onHotkeyActivated(const QString& name);
    QMap<QString, QHotkey*> m_hotkeys;
    QMap<QString, QString> m_hotkeySequences;
    QTimer* m_hotkeyDebounce = nullptr;
#endif
};
