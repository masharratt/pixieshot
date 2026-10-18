// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/scrollcapturestate.h"
#include "utils/scrollstitch.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QTimer>

#include <memory>

class AreaGrabber;
class ScrollCaptureBar;

// Runs one scrolling capture from the area pick to the finished tall image.
// The legal order of steps is enforced by ScrollCaptureMachine.
//
// Manual mode grabs the area about nine times a second while the user
// scrolls; a grab identical to the previous one is dropped before stitching.
// Auto mode posts a scroll-wheel event, waits for the content to settle,
// grabs, and repeats until AutoScrollStop says the end or the height limit
// was reached, then finishes on its own.
class ScrollCaptureController : public QObject
{
    Q_OBJECT

public:
    explicit ScrollCaptureController(QObject* parent = nullptr);
    ~ScrollCaptureController() override;

    ScrollState state() const;

    // Hotkey or menu item: starts picking when idle, finishes while
    // scrolling, otherwise does nothing.
    void trigger();
    // The overlay settled on an area (global logical points)
    void areaPicked(const QRect& globalRect);
    // The overlay closed without an area
    void pickAbandoned();

signals:
    // Ask for the area picker to open
    void pickRequested();
    // The finished image (device pixels, its devicePixelRatio set) and the
    // area it stands for in global logical points
    void captured(const QPixmap& image, const QRect& globalRect);

private:
    void onReady(const QString& error);
    void grabNow();
    void onFrame(const QImage& frame);
    void handleManualStep(scrollstitch::Step step);
    void handleAutoStep(scrollstitch::Step step);
    void toggleAuto();
    void startAuto();
    void stopAuto(const QString& status);
    void scrollAndGrab();
    void grabAfterSettle();
    void showProgress(bool force);
    void finish();
    void cancel();
    void fail(const QString& message);
    void cleanup();
    bool isCurrent(quint64 session) const;

    ScrollCaptureMachine m_machine;
    QRect m_rect;
    QPointer<ScrollCaptureBar> m_bar;
    std::unique_ptr<AreaGrabber> m_grabber;
    std::unique_ptr<scrollstitch::ScrollStitcher> m_stitcher;
    scrollstitch::AutoScrollStop m_autoStop;
    QImage m_lastFrame;
    double m_pixelRatio = 1.0;
    // Bumped on every new capture and on cleanup, so callbacks from an old
    // capture are dropped
    quint64 m_session = 0;
    bool m_grabInFlight = false;
    int m_failedGrabs = 0;
    QTimer m_tick;
    QTimer m_watchdog;
    QElapsedTimer m_previewClock;
    // Auto mode: wheel direction, flipped once if the first move went up
    int m_scrollSign = 1;
    bool m_autoMoved = false;
    bool m_autoFlipped = false;
};
