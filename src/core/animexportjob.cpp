// SPDX-License-Identifier: GPL-3.0-or-later

#include "animexportjob.h"
#include "core/flameshot.h"
#include "core/flameshotdaemon.h"
#include "platform/animexport.h"
#include "tools/animannotations.h"
#include "utils/abstractlogger.h"
#include "utils/capturehistory.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"
#include "utils/filenamehandler.h"
#include "utils/outputpath.h"
#include "utils/stylepack.h"
#include "widgets/recordingcontrol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>

namespace {

const int kStatusMargin = 24;

} // namespace

AnimExportJob::AnimExportJob(QObject* parent)
  : QObject(parent)
{}

void AnimExportJob::start(const AnimSource& source,
                          const QString& nextTo,
                          const AnimChoice& choice)
{
    if (source.base.isNull()) {
        return;
    }
    // Owned by the app until it finishes, then deletes itself
    auto* job = new AnimExportJob(QCoreApplication::instance());
    job->run(source, nextTo, choice);
}

void AnimExportJob::start(const AnimSource& source, const QString& nextTo)
{
    start(source, nextTo, configuredChoice());
}

AnimChoice AnimExportJob::configuredChoice()
{
    ConfigHandler config;
    AnimChoice choice;
    choice.style = config.animStyle();
    choice.motion = config.animMotion();
    choice.format = config.animFormat();
    return choice;
}

void AnimExportJob::rememberChoice(const AnimChoice& choice)
{
    ConfigHandler config;
    config.setAnimStyle(choice.style);
    config.setAnimMotion(choice.motion);
    config.setAnimFormat(choice.format);
}

AnimRenderParams AnimExportJob::paramsFor(const PreparedAnimSource& prepared,
                                          const AnimChoice& choice)
{
    ConfigHandler config;
    AnimRenderParams params;
    params.shot = prepared.base;
    params.annotations = prepared.annotations;
    params.annotationMode = animannotations::modeFrom(config.animAnnotations());
    params.effects = config.effectSettings();
    params.style = animatedbg::styleFrom(choice.style);
    params.motion = shotmotion::motionFrom(choice.motion);
    params.durationSec = config.animDurationSec();
    return params;
}

QString AnimExportJob::outputPathFor(const QString& nextTo, const QString& extension)
{
    QString base = nextTo;
    if (base.isEmpty()) {
        base = ConfigHandler().savePath();
        if (base.isEmpty()) {
            base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        }
    }
    // A file (the saved PNG) gives its folder; a folder may not exist yet
    const QFileInfo info(base);
    const bool isFolder = info.isDir() || (!info.exists() && info.suffix().isEmpty());
    QDir().mkpath(isFolder ? base : info.absolutePath());
    // Reserved on disk: the file only appears when encoding ends
    return outputpath::reserve([&]() {
        return FileNameHandler().properScreenshotPath(base, extension);
    });
}

void AnimExportJob::run(const AnimSource& source,
                        const QString& nextTo,
                        const AnimChoice& choice)
{
    m_source = source;
    m_choice = choice;
    AnimExportParams params;
    // Tools decode and pixelate here, on the GUI thread; the worker only
    // draws with the prepared layer
    static_cast<AnimRenderParams&>(params) =
      paramsFor(animannotations::prepare(source), choice);
    params.fps = ConfigHandler().animFps();
    const int format = choice.format;
    if (format == 0 || format == 2) {
        m_mp4 = outputPathFor(nextTo, QStringLiteral("mp4"));
        params.mp4Path = m_mp4;
    }
    if (format == 1 || format == 2) {
        m_gif = outputPathFor(nextTo, QStringLiteral("gif"));
        params.gifPath = m_gif;
    }

    // Progress pill in the corner of the screen the pointer is on
    m_status = new RecordingControl();
    m_status->setAttribute(Qt::WA_DeleteOnClose);
    m_status->showBusy(tr("Animating... 0%"));
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect area = screen->availableGeometry();
    m_status->move(area.right() - m_status->width() - kStatusMargin,
                   area.bottom() - m_status->height() - kStatusMargin);
    m_status->show();

    m_state = State::Rendering;
    QPointer<AnimExportJob> self(this);
    exportAnimationAsync(
      params,
      [self](double fraction) {
          if (self && self->m_status) {
              self->m_status->showBusy(
                tr("Animating... %1%").arg(int(fraction * 100)));
          }
      },
      [self](bool ok, QString error, AnimExportResult result) {
          if (self) {
              self->finish(ok, error, result.mp4Size, result.gifSize);
          }
      });
}

void AnimExportJob::finish(bool ok,
                           const QString& error,
                           QSize mp4Size,
                           QSize gifSize)
{
    if (m_status) {
        m_status->close();
    }
    if (!ok) {
        m_state = State::Failed;
        // The reserved names are empty placeholders
        for (const QString& path : { m_mp4, m_gif }) {
            if (!path.isEmpty() && QFileInfo(path).size() == 0) {
                QFile::remove(path);
            }
        }
        AbstractLogger::error() << tr("Animated export failed: %1").arg(error);
        deleteLater();
        return;
    }
    m_state = State::Saved;
    // History entry and toast. Each file gets its own copy of the project
    // (the history deletes a project with its entry), and Edit on the toast
    // reopens the editor from it.
    if (!m_mp4.isEmpty()) {
        emit Flameshot::instance()->animationSaved(
          m_mp4, QStringLiteral("mp4"), mp4Size, storeProject());
    }
    if (!m_gif.isEmpty()) {
        emit Flameshot::instance()->animationSaved(
          m_gif, QStringLiteral("gif"), gifSize, storeProject());
    }
    const QString copied = !m_mp4.isEmpty() ? m_mp4 : m_gif;
    FlameshotDaemon::copyToClipboard(QUrl::fromLocalFile(copied).toString(),
                                     tr("Animation saved, file URL copied."));
    deleteLater();
}

QString AnimExportJob::storeProject() const
{
    CaptureProject project;
    project.base = m_source.base;
    project.base.setDevicePixelRatio(m_source.dpr);
    project.objects = m_source.objects;
    // Every Animate and Beautify setting this export used
    project.anim = QJsonObject::fromVariantMap(
      stylepack::fromCurrentConfig(QString()).settings);
    writeAnimChoice(project.anim, m_choice);
    // cfn: synchronous PNG encode on the UI thread after each Animate export,
    // move to a worker thread if large captures make the toast lag
    QString error;
    const QString path = captureproject::saveInFolder(
      CaptureHistory(CaptureHistory::defaultIndexPath()).projectDir(),
      project,
      &error);
    if (path.isEmpty()) {
        AbstractLogger::warning(AbstractLogger::Stderr)
          << tr("Could not keep this animation editable: %1").arg(error);
    }
    return path;
}
