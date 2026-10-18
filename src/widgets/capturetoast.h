// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

// Small always-on-top preview shown after a capture-first save. It never takes
// keyboard focus, hides itself after a delay and pauses while hovered.
class CaptureToast : public QWidget
{
    Q_OBJECT

public:
    // Creates, stacks and shows a toast. Does nothing when seconds <= 0.
    static void showFor(const QString& path,
                        const QPixmap& capture,
                        const QRect& selection,
                        const QRect& globalRect,
                        int seconds);
    // Toast for a saved screen recording (kind "mp4" or "gif"): first-frame
    // thumbnail with Edit, Copy path and Show in Finder. Edit opens the
    // "Edit recording" window on editSource (see RecordingController). The
    // toast owns a temp editSource and removes it on close unless it was
    // handed to the editor. Returns false (and owns nothing) when no toast
    // is shown because seconds <= 0. With no editSource there is no Edit.
    static bool showForRecording(const QString& path,
                                 const QString& kind,
                                 const QString& editSource,
                                 int seconds);
    // Toast for an "Animate" output: like a recording, but Edit reopens the
    // capture editor from projectPath (annotations movable). With no
    // projectPath there is no Edit. Returns false when seconds <= 0.
    static bool showForAnimation(const QString& path,
                                 const QString& kind,
                                 const QString& projectPath,
                                 int seconds);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    CaptureToast(const QString& path,
                 const QPixmap& capture,
                 const QRect& selection,
                 const QRect& globalRect,
                 int seconds,
                 bool recording = false);

    static void restack();
    // A recording-style toast for a saved video or GIF, not shown yet
    static CaptureToast* createForVideo(const QString& path,
                                        const QString& kind,
                                        int seconds);
    void showStacked();
    void openFromThumbnail();

    QString m_path;
    QPixmap m_capture;
    QRect m_selection;  // exportCapture units, used for pins
    QRect m_globalRect; // global logical points
    QTimer m_timer;
    QWidget* m_thumb = nullptr;
    QWidget* m_editButton = nullptr; // recordings only
    bool m_recording = false;
    QString m_kind;       // recordings: "mp4" or "gif"
    QString m_editSource; // recordings: MP4 to edit from, until handed off
    QString m_projectPath; // Animate output: the project Edit opens

    // Newest first, matching toastStackGeometry()
    static QList<QPointer<CaptureToast>> s_toasts;
};
