// SPDX-License-Identifier: GPL-3.0-or-later

#include "animexportjob.h"
#include "core/flameshot.h"
#include "core/flameshotdaemon.h"
#include "platform/animexport.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "utils/filenamehandler.h"
#include "utils/outputpath.h"
#include "widgets/recordingcontrol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

void AnimExportJob::start(const QImage& shot, const QString& nextTo)
{
    if (shot.isNull()) {
        return;
    }
    // Owned by the app until it finishes, then deletes itself
    auto* job = new AnimExportJob(QCoreApplication::instance());
    job->run(shot, nextTo);
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

void AnimExportJob::run(const QImage& shot, const QString& nextTo)
{
    ConfigHandler config;
    AnimExportParams params;
    params.shot = shot;
    params.effects = config.effectSettings();
    params.style = animatedbg::styleFrom(config.animStyle());
    params.durationSec = config.animDurationSec();
    params.fps = config.animFps();
    const int format = config.animFormat();
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
    // History entry and toast like a recording. The MP4 is its own edit
    // source; a GIF has none (the toast hides Edit).
    if (!m_mp4.isEmpty()) {
        emit Flameshot::instance()->recordingSaved(
          m_mp4, QStringLiteral("mp4"), mp4Size, m_mp4);
    }
    if (!m_gif.isEmpty()) {
        emit Flameshot::instance()->recordingSaved(
          m_gif, QStringLiteral("gif"), gifSize, QString());
    }
    const QString copied = !m_mp4.isEmpty() ? m_mp4 : m_gif;
    FlameshotDaemon::copyToClipboard(QUrl::fromLocalFile(copied).toString(),
                                     tr("Animation saved, file URL copied."));
    deleteLater();
}
