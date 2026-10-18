// SPDX-License-Identifier: GPL-3.0-or-later
// One "Animate" export from the button press to the saved files: names the
// files, shows progress, encodes off the GUI thread, then records each file
// in the history with a re-editable project and a toast, and copies its
// file URL. macOS only.

#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

#include "tools/animannotations.h"
#include "utils/animrender.h"
#include "utils/annotationlayer.h"

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

    // Exports `source` (the base with its devicePixelRatio, and its
    // annotations) with the current Beautify settings and `choice` (style,
    // motion, format). nextTo: a file whose folder and name the export
    // follows (the saved PNG), or a folder; empty uses the save folder. Call
    // on the GUI thread.
    static void start(const AnimSource& source,
                      const QString& nextTo,
                      const AnimChoice& choice);
    // The same with the saved Animate choice
    static void start(const AnimSource& source, const QString& nextTo);

    // The Animate choice in the config, and keeping a new one there
    static AnimChoice configuredChoice();
    static void rememberChoice(const AnimChoice& choice);

    // What the frames of an export of `prepared` are drawn from: `choice`
    // plus the current Beautify settings, annotation mode and loop length.
    // The preview uses this too, so it shows exactly what is exported.
    static AnimRenderParams paramsFor(const PreparedAnimSource& prepared,
                                      const AnimChoice& choice);

    // Free file name next to `nextTo` for an extension (claimed on disk)
    static QString outputPathFor(const QString& nextTo, const QString& extension);

private:
    explicit AnimExportJob(QObject* parent = nullptr);
    void run(const AnimSource& source,
             const QString& nextTo,
             const AnimChoice& choice);
    void finish(bool ok, const QString& error, QSize mp4Size, QSize gifSize);
    // Saves the re-editable project of one output file into the history's
    // project folder; its path, or empty when it could not be written
    QString storeProject() const;

    State m_state = State::Rendering;
    QString m_mp4;
    QString m_gif;
    AnimSource m_source;
    AnimChoice m_choice;
    QPointer<RecordingControl> m_status;
};
