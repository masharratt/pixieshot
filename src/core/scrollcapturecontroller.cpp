// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollcapturecontroller.h"
#include "core/recordinggeometry.h"
#include "platform/screenrecorder.h"
#include "platform/scrollcapture.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "widgets/scrollcapturebar.h"

#include <QGuiApplication>
#include <QScreen>

namespace {

const int kControlGap = 8;
// The overlay and the bar need a moment to leave and reach the screen
// before the window list for the grab is read
const int kStartDelayMs = 200;
// About nine grabs a second while the user scrolls
const int kManualIntervalMs = 110;
// Time for the content to redraw after a synthetic scroll
const int kAutoSettleMs = 180;
// Frames in a row without movement that mean the end was reached
const int kAutoUnchangedLimit = 3;
// A grab that has not answered by then counts as a hang
const int kGrabTimeoutMs = 3000;
// Grabs in a row that may fail before the capture gives up
const int kMaxFailedGrabs = 3;
const int kPreviewIntervalMs = 300;
const QSize kPreviewBound(40, 64);

} // namespace

ScrollCaptureController::ScrollCaptureController(QObject* parent)
  : QObject(parent)
  , m_autoStop(kAutoUnchangedLimit)
{
    m_tick.setInterval(kManualIntervalMs);
    connect(&m_tick, &QTimer::timeout, this, &ScrollCaptureController::grabNow);
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(kGrabTimeoutMs);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        fail(tr("The screen grab stopped answering."));
    });
}

ScrollCaptureController::~ScrollCaptureController() = default;

ScrollState ScrollCaptureController::state() const
{
    return m_machine.state();
}

bool ScrollCaptureController::isCurrent(quint64 session) const
{
    return session == m_session &&
           m_machine.state() == ScrollState::Scrolling;
}

void ScrollCaptureController::trigger()
{
    switch (onScrollHotkey(m_machine.state())) {
        case ScrollHotkeyEffect::StartPicking:
            if (m_machine.transition(ScrollState::Picking)) {
                emit pickRequested();
            }
            break;
        case ScrollHotkeyEffect::Finish:
            finish();
            break;
        case ScrollHotkeyEffect::Ignore:
            break;
    }
}

void ScrollCaptureController::pickAbandoned()
{
    if (m_machine.state() == ScrollState::Picking) {
        m_machine.transition(ScrollState::Idle);
    }
}

void ScrollCaptureController::areaPicked(const QRect& globalRect)
{
    if (m_machine.state() != ScrollState::Picking) {
        return;
    }
    if (globalRect.isEmpty()) {
        fail(tr("No area was selected."));
        return;
    }
    m_machine.transition(ScrollState::Scrolling);
    const quint64 session = ++m_session;
    m_rect = globalRect;
    m_lastFrame = QImage();
    m_grabInFlight = false;
    m_failedGrabs = 0;
    m_pixelRatio = 1.0;
    scrollstitch::Options options;
    options.maxHeight = ConfigHandler().scrollCaptureMaxHeight();
    m_stitcher = std::make_unique<scrollstitch::ScrollStitcher>(options);

    m_bar = new ScrollCaptureBar();
    m_bar->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_bar,
            &ScrollCaptureBar::doneRequested,
            this,
            &ScrollCaptureController::finish);
    connect(m_bar,
            &ScrollCaptureBar::cancelRequested,
            this,
            &ScrollCaptureController::cancel);
    connect(m_bar,
            &ScrollCaptureBar::autoRequested,
            this,
            &ScrollCaptureController::toggleAuto);
    QScreen* screen = QGuiApplication::screenAt(m_rect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    m_bar->move(controlPosition(
      m_rect, m_bar->size(), screen->availableGeometry(), kControlGap));
    m_bar->show();

    m_grabber = createAreaGrabber();
    QTimer::singleShot(kStartDelayMs, this, [this, session]() {
        if (!isCurrent(session) || !m_grabber) {
            return;
        }
        const QList<quint32> exclude = { nativeWindowId(m_bar) };
        m_grabber->prepare(m_rect, exclude, [this, session](QString error) {
            if (isCurrent(session)) {
                onReady(error);
            }
        });
    });
}

void ScrollCaptureController::onReady(const QString& error)
{
    if (!error.isEmpty()) {
        fail(error);
        return;
    }
    if (m_bar) {
        m_bar->setReady(true);
        m_bar->setStatus(
          tr("Scroll the content now, then press Done. Auto scrolls for you."));
    }
    m_previewClock.start();
    grabNow();
    m_tick.start();
}

void ScrollCaptureController::grabNow()
{
    if (m_grabInFlight || m_machine.state() != ScrollState::Scrolling ||
        !m_grabber) {
        return;
    }
    m_grabInFlight = true;
    m_watchdog.start();
    const quint64 session = m_session;
    m_grabber->grab([this, session](QImage image) {
        if (isCurrent(session)) {
            onFrame(image);
        }
    });
}

void ScrollCaptureController::onFrame(const QImage& frame)
{
    m_grabInFlight = false;
    m_watchdog.stop();
    if (frame.isNull()) {
        if (++m_failedGrabs >= kMaxFailedGrabs) {
            fail(tr("Could not grab the selected area."));
        } else if (m_machine.mode() == ScrollMode::Auto) {
            scrollAndGrab();
        }
        return;
    }
    m_failedGrabs = 0;

    scrollstitch::Step step = scrollstitch::Step::NoChange;
    if (frame != m_lastFrame) {
        m_lastFrame = frame;
        m_pixelRatio = frame.devicePixelRatio();
        step = m_stitcher->add(frame);
    }
    if (m_machine.mode() == ScrollMode::Auto) {
        handleAutoStep(step);
    } else {
        handleManualStep(step);
    }
}

void ScrollCaptureController::handleManualStep(scrollstitch::Step step)
{
    using scrollstitch::Step;
    switch (step) {
        case Step::Started:
        case Step::Appended:
            showProgress(false);
            if (m_bar) {
                m_bar->setStatus(tr("Keep scrolling, then press Done."));
            }
            break;
        case Step::NoMatch:
            if (m_bar) {
                m_bar->setStatus(
                  tr("Lost track of the content. Scroll back up a little."));
            }
            break;
        case Step::MaxHeight:
            m_tick.stop();
            showProgress(true);
            if (m_bar) {
                m_bar->setStatus(tr("Height limit reached. Press Done."));
            }
            break;
        case Step::NoChange:
        case Step::ScrolledUp:
            break;
    }
}

void ScrollCaptureController::handleAutoStep(scrollstitch::Step step)
{
    using scrollstitch::Step;
    if (step == Step::Appended || step == Step::Started) {
        m_autoMoved = true;
        showProgress(false);
    }
    // The wheel direction a synthetic event takes is not documented to be
    // the same everywhere; content moving up first means it is reversed.
    if (step == Step::ScrolledUp && !m_autoMoved && !m_autoFlipped) {
        m_autoFlipped = true;
        m_scrollSign = -m_scrollSign;
        m_autoStop = scrollstitch::AutoScrollStop(kAutoUnchangedLimit);
        scrollAndGrab();
        return;
    }
    if (!m_autoStop.shouldStop(step)) {
        scrollAndGrab();
        return;
    }
    if (step == Step::NoMatch) {
        stopAuto(tr("Auto scroll moved too far to follow. Scroll by hand "
                    "or press Done."));
        return;
    }
    // End of the content or the height limit: the capture is complete
    finish();
}

void ScrollCaptureController::toggleAuto()
{
    if (m_machine.state() != ScrollState::Scrolling) {
        return;
    }
    if (m_machine.mode() == ScrollMode::Auto) {
        stopAuto(tr("Auto scroll stopped. Scroll by hand or press Done."));
        return;
    }
    startAuto();
}

void ScrollCaptureController::startAuto()
{
    if (!scrollEventsAllowed(false)) {
        // Ask macOS to show its permission dialog, then carry on by hand
        scrollEventsAllowed(true);
        if (m_bar) {
            m_bar->setStatus(
              tr("Auto needs Accessibility permission: allow Pixieshot in "
                 "System Settings, Privacy & Security, Accessibility. Scroll "
                 "by hand meanwhile."));
        }
        return;
    }
    if (m_stitcher->isFull() || !m_machine.setMode(ScrollMode::Auto)) {
        return;
    }
    m_tick.stop();
    m_autoStop = scrollstitch::AutoScrollStop(kAutoUnchangedLimit);
    m_autoMoved = false;
    m_autoFlipped = false;
    if (m_bar) {
        m_bar->setAutoRunning(true);
        m_bar->setStatus(tr("Scrolling automatically..."));
    }
    // A manual grab still on its way is taken as the first auto frame
    if (!m_grabInFlight) {
        scrollAndGrab();
    }
}

void ScrollCaptureController::stopAuto(const QString& status)
{
    m_machine.setMode(ScrollMode::Manual);
    if (m_bar) {
        m_bar->setAutoRunning(false);
        m_bar->setStatus(status);
    }
    m_tick.start();
}

void ScrollCaptureController::scrollAndGrab()
{
    // Half the area per step keeps a wide overlap between frames
    const int points = qMax(20, m_rect.height() / 2);
    if (!postScrollDown(m_rect.center(), m_rect, m_scrollSign * points)) {
        stopAuto(tr("Could not send scroll events. Scroll by hand or press "
                    "Done."));
        return;
    }
    const quint64 session = m_session;
    QTimer::singleShot(kAutoSettleMs, this, [this, session]() {
        if (isCurrent(session) && m_machine.mode() == ScrollMode::Auto) {
            grabNow();
        }
    });
}

void ScrollCaptureController::showProgress(bool force)
{
    if (!m_bar || !m_stitcher) {
        return;
    }
    if (!force && m_previewClock.isValid() &&
        m_previewClock.elapsed() < kPreviewIntervalMs) {
        // Height is cheap; the scaled preview is refreshed less often
        m_bar->setStitched(m_stitcher->height(), QImage());
        return;
    }
    m_previewClock.restart();
    const qreal dpr = m_bar->devicePixelRatioF();
    QImage preview = m_stitcher->preview(kPreviewBound * dpr);
    preview.setDevicePixelRatio(dpr);
    m_bar->setStitched(m_stitcher->height(), preview);
}

void ScrollCaptureController::finish()
{
    if (m_machine.state() != ScrollState::Scrolling) {
        return;
    }
    const QImage image = m_stitcher ? m_stitcher->result() : QImage();
    if (image.isNull()) {
        fail(tr("Nothing was captured yet."));
        return;
    }
    m_machine.transition(ScrollState::Done);
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(m_pixelRatio);
    const QRect globalRect(
      m_rect.topLeft(),
      QSize(m_rect.width(), qRound(image.height() / m_pixelRatio)));
    cleanup();
    m_machine.transition(ScrollState::Idle);
    emit captured(pixmap, globalRect);
}

void ScrollCaptureController::cancel()
{
    if (!m_machine.transition(ScrollState::Cancelled)) {
        return;
    }
    cleanup();
    m_machine.transition(ScrollState::Idle);
}

void ScrollCaptureController::fail(const QString& message)
{
    if (!m_machine.transition(ScrollState::Failed, message)) {
        return;
    }
    AbstractLogger::error() << tr("Scrolling capture failed: %1").arg(message);
    cleanup();
    m_machine.transition(ScrollState::Idle);
}

void ScrollCaptureController::cleanup()
{
    ++m_session;
    m_tick.stop();
    m_watchdog.stop();
    m_grabInFlight = false;
    if (m_bar) {
        m_bar->close();
    }
    m_bar.clear();
    m_stitcher.reset();
    m_lastFrame = QImage();
    m_scrollSign = 1;
    // The native grabber may still answer a pending grab; its callback is
    // dropped by the session check, so it can go now
    m_grabber.reset();
}
