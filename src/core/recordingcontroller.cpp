// SPDX-License-Identifier: GPL-3.0-or-later

#include "recordingcontroller.h"
#include "core/recordinggeometry.h"
#include "platform/gifencoder.h"
#include "platform/screenrecorder.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "utils/filenamehandler.h"
#include "utils/outputpath.h"
#include "widgets/recordingcontrol.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

namespace {

const int kControlGap = 8;
// The overlay and the control window need a moment to leave and reach the
// screen before the stream decides what to exclude.
const int kStartDelayMs = 200;

const QString kMp4 = QStringLiteral("mp4");
const QString kGif = QStringLiteral("gif");

} // namespace

RecordingController::RecordingController(QObject* parent)
  : QObject(parent)
{}

RecordingController::~RecordingController() = default;

RecState RecordingController::state() const
{
    return m_machine.state();
}

void RecordingController::trigger(CaptureRequest::RecordMode mode)
{
    switch (onHotkey(m_machine.state())) {
        case HotkeyEffect::StartSelecting:
            if (m_machine.transition(RecState::Selecting)) {
                m_mode = mode;
                emit pickRequested(mode);
            }
            break;
        case HotkeyEffect::Stop:
            stop();
            break;
        case HotkeyEffect::Ignore:
            break;
    }
}

void RecordingController::pickAbandoned()
{
    if (m_machine.state() == RecState::Selecting) {
        m_machine.transition(RecState::Idle);
    }
}

void RecordingController::areaPicked(const QRect& globalRect)
{
    if (!m_machine.transition(RecState::Starting)) {
        return;
    }
    if (globalRect.isEmpty()) {
        fail(tr("No area was selected."));
        return;
    }
    m_rect = globalRect;
    m_tempPath = QDir(QStandardPaths::writableLocation(
                        QStandardPaths::TempLocation))
                   .filePath(QStringLiteral("flameshot-recording-%1.mp4")
                               .arg(QDateTime::currentMSecsSinceEpoch()));

    m_control = new RecordingControl();
    m_control->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_control,
            &RecordingControl::stopRequested,
            this,
            &RecordingController::stop);
    connect(m_control,
            &RecordingControl::cancelRequested,
            this,
            &RecordingController::cancel);
    QScreen* screen = QGuiApplication::screenAt(m_rect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    m_control->move(controlPosition(
      m_rect, m_control->size(), screen->availableGeometry(), kControlGap));
    m_control->show();

    m_recorder = createScreenRecorder();
    QTimer::singleShot(kStartDelayMs, this, [this]() { begin(); });
}

void RecordingController::begin()
{
    if (m_machine.state() != RecState::Starting || !m_recorder) {
        return;
    }
    const QList<quint32> exclude = { nativeWindowId(m_control) };
    m_recorder->start(
      m_rect,
      m_tempPath,
      exclude,
      recordingOptionsFor(ConfigHandler().recordShowClicks()),
      [this](QString error) { fail(error); },
      [this]() {
          if (m_machine.transition(RecState::Recording) && m_control) {
              m_control->startClock();
          }
      });
}

void RecordingController::stop()
{
    if (!m_machine.transition(RecState::Stopping)) {
        return;
    }
    if (m_control) {
        m_control->showBusy(tr("Saving..."));
    }
    m_recorder->stop([this](bool ok, QString) { onStopped(ok); });
}

void RecordingController::cancel()
{
    if (!m_machine.transition(RecState::Cancelled)) {
        return;
    }
    if (m_recorder) {
        m_recorder->cancel();
    }
    cleanup();
    m_machine.transition(RecState::Idle);
}

void RecordingController::onStopped(bool ok)
{
    if (m_machine.state() != RecState::Stopping) {
        return;
    }
    if (!ok || !QFile::exists(m_tempPath)) {
        fail(tr("The recording could not be saved."));
        return;
    }
    if (m_mode == CaptureRequest::RecordGif) {
        startGifConversion();
        return;
    }

    const QString dest = nextOutputPath(kMp4);
    if (!QFile::rename(m_tempPath, dest)) {
        // Another volume: copy, then drop the temp file
        if (!QFile::copy(m_tempPath, dest)) {
            fail(tr("Could not save the recording to %1.").arg(dest));
            return;
        }
        QFile::remove(m_tempPath);
    }
    m_machine.transition(RecState::Done);
    finish(dest, kMp4, dest);
}

void RecordingController::startGifConversion()
{
    m_machine.transition(RecState::Converting);
    if (m_control) {
        m_control->showBusy(tr("Making GIF..."));
    }
    ConfigHandler config;
    GifEncodeParams params;
    params.inputMp4 = m_tempPath;
    params.outputGif = nextOutputPath(kGif);
    params.fps = config.gifFps();
    params.maxWidth = config.gifMaxWidth();
    encodeGifAsync(params, [this, params](bool ok, QString error) {
        if (!ok) {
            QFile::remove(params.inputMp4);
            fail(tr("GIF conversion failed: %1").arg(error));
            return;
        }
        // The temp MP4 is kept so the GIF can be trimmed and re-made from
        // it; the receiver of recordingSaved owns it from here
        m_machine.transition(RecState::Done);
        finish(params.outputGif, kGif, params.inputMp4);
    });
}

void RecordingController::finish(const QString& path,
                                 const QString& kind,
                                 const QString& editSource)
{
    QSize size;
    if (kind == kGif) {
        size = QImageReader(path).size();
    } else {
        QScreen* screen = QGuiApplication::screenAt(m_rect.center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        size = outputPixels(m_rect.size(), screen->devicePixelRatio());
    }
    cleanup();
    m_machine.transition(RecState::Idle);
    // The temp file now belongs to the receiver, so fail() must not touch it
    m_tempPath.clear();
    emit recordingSaved(path, kind, size, editSource);
}

void RecordingController::fail(const QString& message)
{
    if (!m_machine.transition(RecState::Failed, message)) {
        return;
    }
    AbstractLogger::error() << tr("Recording failed: %1").arg(message);
    if (m_recorder) {
        m_recorder->cancel();
    }
    if (!m_tempPath.isEmpty()) {
        QFile::remove(m_tempPath);
    }
    cleanup();
    m_machine.transition(RecState::Idle);
}

void RecordingController::cleanup()
{
    if (m_control) {
        m_control->close();
    }
    m_control.clear();
    // The native recorder may still be winding down on its own queues, so
    // keep it until the next recording replaces it.
}

void RecordingController::discardEditSource(const QString& editSource,
                                            const QString& savedPath)
{
    if (!editSource.isEmpty() && editSource != savedPath) {
        QFile::remove(editSource);
    }
}

QString RecordingController::nextOutputPath(const QString& extension)
{
    QString dir = ConfigHandler().savePath();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    QDir().mkpath(dir);
    // Reserved on disk: the real file only appears when the export finishes,
    // and a second export asking meanwhile must not get the same name
    return outputpath::reserve([&]() {
        return FileNameHandler().properScreenshotPath(dir, extension);
    });
}
