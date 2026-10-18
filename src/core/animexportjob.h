// SPDX-License-Identifier: GPL-3.0-or-later
// One "Animate" export from the button press to the saved files: names the
// files, shows progress, encodes off the GUI thread, then records each file
// in the history with a toast and copies its file URL. macOS only.

#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

class RecordingControl;

class AnimExportJob : public QObject
{
    Q_OBJECT

public:
    // Lifecycle (see readme/state-machines.md): frames are rendered and
    // encoded together, so there is no separate encoding step
    enum class State
    {
        Rendering,
        Saved,
        Failed,
    };

    // Exports `shot` (with its devicePixelRatio) with the current Beautify
    // and Animate settings. nextTo: a file whose folder and name the export
    // follows (the saved PNG), or a folder; empty uses the save folder.
    static void start(const QImage& shot, const QString& nextTo);

    // Free file name next to `nextTo` for an extension (claimed on disk)
    static QString outputPathFor(const QString& nextTo, const QString& extension);

private:
    explicit AnimExportJob(QObject* parent = nullptr);
    void run(const QImage& shot, const QString& nextTo);
    void finish(bool ok, const QString& error, QSize mp4Size, QSize gifSize);

    State m_state = State::Rendering;
    QString m_mp4;
    QString m_gif;
    QPointer<RecordingControl> m_status;
};
