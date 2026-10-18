// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "flameshot.h"
#include "core/flameshotdaemon.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "qhotkey.h"
#endif

#if defined(Q_OS_MACOS)
#include <QWindow>
#include <objc/message.h>

namespace {

constexpr long NSApplicationActivationPolicyRegular = 0;
constexpr long NSApplicationActivationPolicyAccessory = 1;

void setActivationPolicy(long policy)
{
    auto sharedApp = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend);
    auto setPolicy = reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend);
    id nsApp = sharedApp(reinterpret_cast<id>(objc_getClass("NSApplication")),
                         sel_registerName("sharedApplication"));
    setPolicy(nsApp, sel_registerName("setActivationPolicy:"), policy);
}

void setActivationPolicyRegular()
{
    setActivationPolicy(NSApplicationActivationPolicyRegular);
}

void setActivationPolicyAccessory()
{
    setActivationPolicy(NSApplicationActivationPolicyAccessory);
}

constexpr const char* visibleInDockProperty = "_visibleInDock";

} // namespace

#include <CoreGraphics/CoreGraphics.h>
#endif

#include "config/cacheutils.h"
#include "config/configresolver.h"
#include "config/configwindow.h"
#include "core/actionrunner.h"
#include "core/capturedelay.h"
#include "core/capturefirstrequest.h"
#include "core/hotkeyutils.h"
#include "core/qguiappcurrentscreen.h"
#include "core/recordingcontroller.h"
#include "core/scrollcapturecontroller.h"
#include "platform/screenrecorder.h"
#include "tools/beautify/beautifyaction.h"
#include "utils/abstractlogger.h"
#include "utils/capturehistory.h"
#include "utils/confighandler.h"
#include "utils/imageeffects.h"
#include "utils/screengrabber.h"
#include "utils/screenshotsaver.h"
#include "widgets/capture/capturewidget.h"
#include "platform/windowspace.h"
#include "widgets/editorvisibility.h"
#include "widgets/editorwindow.h"
#include "widgets/capturehistorywindow.h"
#include "widgets/capturelauncher.h"
#include "widgets/capturecountdown.h"
#include "widgets/capturetoast.h"
#include "widgets/editedthumbnail.h"
#include "widgets/infowindow.h"

#ifdef ENABLE_IMGUR
#include "tools/imgupload/imguploadermanager.h"
#include "tools/imgupload/storages/imguploaderbase.h"
#include "widgets/imguploaddialog.h"
#include "widgets/uploadhistory.h"
#endif

#include <QApplication>
#include <QDateTime>
#include <QBuffer>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QScopeGuard>
#include <QUrl>
#include <QUuid>
#include <QVersionNumber>

#if defined(Q_OS_MACOS)
#include <QScreen>
#endif

Flameshot::Flameshot()
  : m_haveExternalWidget(false)
  , m_captureWindow(nullptr)
{
    QString StyleSheet = CaptureButton::globalStyleSheet();
    qApp->setStyleSheet(StyleSheet);

    // Every successful save is recorded in the capture history
    connect(this,
            &Flameshot::captureSaved,
            this,
            [this](const QString& path, const QPixmap& capture) {
                CaptureHistory history(CaptureHistory::defaultIndexPath());
                HistoryEntry entry;
                entry.timestamp = QDateTime::currentDateTimeUtc();
                entry.path = path;
                entry.size = capture.size();
                entry.kind = QStringLiteral("image");
                entry.project = storePendingProject(history.projectDir());
                if (history.append(entry)) {
                    history.trimTo(ConfigHandler().captureHistoryMax());
                }
            });

    // Editor windows are ordinary document windows: they stay on screen,
    // behind the other app, when the user switches away. While the app is
    // inactive it gets a Dock icon, so Cmd+Tab and the Dock can return to them.
    connect(qApp,
            &QGuiApplication::applicationStateChanged,
            this,
            [this](Qt::ApplicationState) { updateEditorDockIcon(); });

    // Screen recording: the controller asks for the area picker and reports
    // the saved file, which is recorded in the history and shown in a toast.
    m_recording = new RecordingController(this);
    connect(m_recording,
            &RecordingController::pickRequested,
            this,
            [this](CaptureRequest::RecordMode mode) {
                if (gui(buildRecordRequest(mode)) == nullptr) {
                    m_recording->pickAbandoned();
                }
            });
    connect(this,
            &Flameshot::captureFailed,
            m_recording,
            &RecordingController::pickAbandoned);
    connect(m_recording,
            &RecordingController::recordingSaved,
            this,
            &Flameshot::recordingSaved);
    connect(this,
            &Flameshot::recordingSaved,
            this,
            [](const QString& path,
               const QString& kind,
               const QSize& size,
               const QString& editSource) {
                CaptureHistory history(CaptureHistory::defaultIndexPath());
                HistoryEntry entry;
                entry.timestamp = QDateTime::currentDateTimeUtc();
                entry.path = path;
                entry.size = size;
                entry.kind = kind;
                if (history.append(entry)) {
                    history.trimTo(ConfigHandler().captureHistoryMax());
                }
                // The toast offers "Edit recording" and owns editSource; with
                // no toast there is nothing to edit from, so a temp source
                // goes now
                if (!CaptureToast::showForRecording(
                      path,
                      kind,
                      editSource,
                      ConfigHandler().captureToastSeconds())) {
                    RecordingController::discardEditSource(editSource, path);
                }
            });

    // Scrolling capture: the controller asks for the area picker and hands
    // back the tall image, which is exported like any capture.
    m_scrolling = new ScrollCaptureController(this);
    connect(m_scrolling, &ScrollCaptureController::pickRequested, this, [this]() {
        if (gui(buildScrollCaptureRequest()) == nullptr) {
            m_scrolling->pickAbandoned();
        }
    });
    connect(this,
            &Flameshot::captureFailed,
            m_scrolling,
            &ScrollCaptureController::pickAbandoned);
    connect(m_scrolling,
            &ScrollCaptureController::captured,
            this,
            [this](const QPixmap& image, const QRect& globalRect) {
                const CaptureRequest req = buildScrollResultRequest(
                  globalRect,
                  ConfigHandler().savePath(),
                  QStandardPaths::writableLocation(
                    QStandardPaths::PicturesLocation));
                QRect selection(QPoint(0, 0), globalRect.size());
                exportCapture(image, selection, req);
            });

#if defined(Q_OS_MACOS)
    // Request Screen Recording permission via the proper CoreGraphics API
    if (!CGPreflightScreenCaptureAccess()) {
        CGRequestScreenCaptureAccess();
    }
#endif
#if (defined(Q_OS_MACOS) || defined(Q_OS_WIN))
    // Global shortcuts for MacOS or Windows. Changes in the config file are
    // applied without a restart; only changed key sequences are touched.
    m_hotkeyDebounce = new QTimer(this);
    m_hotkeyDebounce->setSingleShot(true);
    m_hotkeyDebounce->setInterval(300);
    connect(m_hotkeyDebounce, &QTimer::timeout, this, &Flameshot::syncHotkeys);
    connect(ConfigHandler::getInstance(),
            &ConfigHandler::fileChanged,
            m_hotkeyDebounce,
            qOverload<>(&QTimer::start));
    syncHotkeys();
#endif
}

#if (defined(Q_OS_MACOS) || defined(Q_OS_WIN))
namespace {

// Shortcut names registered as global hotkeys on this platform
QStringList globalHotkeyNames()
{
    QStringList names = ConfigHandler::workflowHotkeys();
#if defined(Q_OS_MACOS)
    names << QStringLiteral("SCREENSHOT_HISTORY");
#endif
    if (screenRecordingSupported()) {
        names << QStringLiteral("RECORD_MP4") << QStringLiteral("RECORD_GIF")
              << QStringLiteral("SCROLL_CAPTURE");
    }
    return names;
}

} // namespace

void Flameshot::syncHotkeys()
{
    ConfigHandler config;
    QMap<QString, QString> now;
    for (const QString& name : globalHotkeyNames()) {
        now.insert(name, config.shortcut(name));
    }

    for (const QString& name : changedShortcuts(m_hotkeySequences, now)) {
        if (!now.contains(name)) {
            continue;
        }
        QHotkey* hotkey = m_hotkeys.value(name, nullptr);
        if (hotkey == nullptr) {
            hotkey = new QHotkey(this);
            m_hotkeys.insert(name, hotkey);
            connect(hotkey, &QHotkey::activated, qApp, [this, name]() {
                onHotkeyActivated(name);
            });
        }

        const QKeySequence sequence(now.value(name));
        if (sequence.isEmpty()) {
            hotkey->setRegistered(false);
            continue;
        }
        hotkey->setShortcut(sequence, true);
        if (!hotkey->isRegistered()) {
            QString msg = tr("Could not register the global shortcut %1 for "
                             "%2. Another app may be using it.")
                            .arg(sequence.toString(), name);
#if defined(Q_OS_MACOS)
            if (isOptionOnlyShortcut(sequence)) {
                msg += QLatin1Char(' ') +
                       tr("macOS does not allow shortcuts that only use "
                          "Option or Option+Shift.");
            }
#endif
            AbstractLogger::warning() << msg;
        }
    }
    m_hotkeySequences = now;
}

void Flameshot::onHotkeyActivated(const QString& name)
{
    if (name == QLatin1String("SCREENSHOT_HISTORY")) {
#if ENABLE_IMGUR
        history();
#else
        CaptureHistoryWindow::showWindow();
#endif
        return;
    }

    if (name == QLatin1String("SCROLL_CAPTURE")) {
        toggleScrollCapture();
        return;
    }

    const CaptureRequest::RecordMode recordMode = recordModeForHotkey(name);
    if (recordMode != CaptureRequest::RecordNone) {
        toggleRecording(recordMode);
        return;
    }

    ConfigHandler config;
    // A hotkey with a workflow always runs its actions; TAKE_SCREENSHOT keeps
    // honouring the capture-first switch.
    if (name == QLatin1String("CAPTURE_AND_EDIT") || config.captureFirst()) {
        guiWithTimer(buildCaptureFirstRequest(
          name,
          config.savePath(),
          QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));
    } else {
        guiWithTimer();
    }
}
#endif

void Flameshot::toggleRecording(CaptureRequest::RecordMode mode)
{
    if (!screenRecordingSupported()) {
        return;
    }
    m_recording->trigger(mode);
}

void Flameshot::toggleScrollCapture()
{
    // Uses the same ScreenCaptureKit support as recording
    if (!screenRecordingSupported()) {
        return;
    }
    m_scrolling->trigger();
}

Flameshot* Flameshot::instance()
{
    static Flameshot c;
    return &c;
}

namespace {
// Time for the window server to drop the countdown before the screen is grabbed
const int kCountdownHideMs = 150;
} // namespace

CaptureWidget* Flameshot::guiWithTimer(const CaptureRequest& req)
{
    const int seconds = static_cast<int>(
      captureDelayMs(req.delay(), ConfigHandler().captureDelaySec()) / 1000);
    if (seconds <= 0 || req.delay() > 0) {
        // No timer configured, or the request carries its own delay
        return gui(req);
    }
    // A second press while counting must not start another countdown
    if (!m_countdown.transition(CountdownState::Counting)) {
        return nullptr;
    }
    auto* countdown = new CaptureCountdown(seconds);
    connect(countdown, &CaptureCountdown::cancelled, this, [this, countdown]() {
        m_countdown.transition(CountdownState::Cancelled);
        countdown->close();
        countdown->deleteLater();
        m_countdown.transition(CountdownState::Idle);
    });
    connect(
      countdown, &CaptureCountdown::finished, this, [this, countdown, req]() {
          m_countdown.transition(CountdownState::Capturing);
          // Remove the countdown from the screen before the grab
          countdown->close();
          countdown->deleteLater();
          QTimer::singleShot(kCountdownHideMs, this, [this, req]() {
              gui(req);
              m_countdown.transition(CountdownState::Idle);
          });
      });
    countdown->start();
    return nullptr;
}

CaptureWidget* Flameshot::gui(const CaptureRequest& req)
{
    if (!resolveAnyConfigErrors()) {
        return nullptr;
    }

    CaptureRequest request = req;
    if (shouldApplyLastRegion(request, ConfigHandler().saveLastRegion())) {
        request.setInitialSelection(getLastRegion());
    }

#if defined(Q_OS_MACOS)
    // This is required on MacOS because of Mission Control. If you'll switch to
    // another Desktop you cannot take a new screenshot from the tray, you have
    // to switch back to the Flameshot Desktop manually. It is not obvious and a
    // large number of users are confused and report a bug.
    if (m_captureWindow != nullptr) {
        m_captureWindow->close();
        delete m_captureWindow;
        m_captureWindow = nullptr;
    }
#endif

    if (nullptr == m_captureWindow) {
        // TODO is this unnecessary now?
        int timeout = 5000; // 5 seconds
        const int delay = 100;
        QWidget* modalWidget = nullptr;
        for (; timeout >= 0; timeout -= delay) {
            modalWidget = qApp->activeModalWidget();
            if (nullptr == modalWidget) {
                break;
            }
            modalWidget->close();
            modalWidget->deleteLater();
            QThread::msleep(delay);
        }
        if (0 == timeout) {
            QMessageBox::warning(
              nullptr, tr("Error"), tr("Unable to close active modal widgets"));
            return nullptr;
        }

        m_captureWindow = new CaptureWidget(request);

#ifdef Q_OS_WIN
        m_captureWindow->show();
#elif defined(Q_OS_MACOS)
        if (shouldUseNativeFullscreen(request,
                                      ConfigHandler().useNativeFullscreen())) {
            m_captureWindow->showFullScreen();
        } else {
            m_captureWindow->show();
        }
        m_captureWindow->activateWindow();
        m_captureWindow->raise();
#else
        m_captureWindow->showFullScreen();
//        m_captureWindow->show(); // For CaptureWidget Debugging under Linux
#endif
        return m_captureWindow;
    } else {
        emit captureFailed();
        return nullptr;
    }
}

void Flameshot::screen(CaptureRequest req, const int screenNumber)
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    bool ok = false;
    QPixmap p;
    QRect geometry;

    if (screenNumber < 0) {
        ScreenGrabber grabber;
        p = grabber.grabEntireDesktop(ok);
        if (ok) {
            QScreen* selectedScreen = grabber.getSelectedScreen();
            if (selectedScreen) {
                geometry = ScreenGrabber().screenGeometry(selectedScreen);
            } else {
                ok = false;
            }
        }
    } else if (screenNumber >= qApp->screens().count()) {
        AbstractLogger() << QObject::tr(
          "Requested screen exceeds screen count");
        ok = false;
    } else {
        // Specific screen number provided - use grabScreen to bypass selector
        QScreen* screen = qApp->screens()[screenNumber];
        p = ScreenGrabber().grabScreen(screen, ok);
        if (ok) {
            geometry = ScreenGrabber().screenGeometry(screen);
        }
    }

    if (ok) {
        QRect region = req.initialSelection();
        if (region.isNull()) {
            region = geometry;
        } else {
            QRect screenGeom = geometry;
            screenGeom.moveTopLeft({ 0, 0 });
            region = region.intersected(screenGeom);
            p = p.copy(region);
        }
        if (req.tasks() & CaptureRequest::PIN) {
            // change geometry for pin task
            req.addPinTask(region);
        }
        exportCapture(p, geometry, req);
    } else {
        emit captureFailed();
    }
}

void Flameshot::full(const CaptureRequest& req)
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    bool ok = true;
    QPixmap p(ScreenGrabber().grabFullDesktop(ok));
    if (ok) {
        QRect selection; // `flameshot full` does not support region selection
        exportCapture(p, selection, req);
    } else {
        emit captureFailed();
    }
}

void Flameshot::launcher()
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    if (m_launcherWindow == nullptr) {
        m_launcherWindow = new CaptureLauncher();
    }
    m_launcherWindow->show();
#if defined(Q_OS_MACOS)
    showDockIcon(m_launcherWindow);
#endif
}

void Flameshot::config()
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    if (m_configWindow == nullptr) {
        m_configWindow = new ConfigWindow();
        m_configWindow->show();
        // Call show() first, otherwise the correct geometry cannot be fetched
        // for centering the window on the screen
        QRect position = m_configWindow->frameGeometry();
        QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
        position.moveCenter(currentScreen->availableGeometry().center());
        m_configWindow->move(position.topLeft());
#if defined(Q_OS_MACOS)
        showDockIcon(m_configWindow);
#endif
    }
}

void Flameshot::info()
{
    if (m_infoWindow == nullptr) {
        m_infoWindow = new InfoWindow();
#if defined(Q_OS_MACOS)
        showDockIcon(m_infoWindow);
#endif
    }
}

#ifdef ENABLE_IMGUR
void Flameshot::history()
{
    static UploadHistory* historyWidget = nullptr;
    if (historyWidget == nullptr) {
        historyWidget = new UploadHistory;
        historyWidget->loadHistory();
        connect(historyWidget, &QObject::destroyed, this, []() {
            historyWidget = nullptr;
        });
    }

    historyWidget->show();
    // Call show() first, otherwise the correct geometry cannot be fetched
    // for centering the window on the screen
    QRect position = historyWidget->frameGeometry();
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    position.moveCenter(currentScreen->availableGeometry().center());
    historyWidget->move(position.topLeft());

#if defined(Q_OS_MACOS)
    showDockIcon(historyWidget);
#endif
}
#endif

#if defined(Q_OS_MACOS)
void Flameshot::onWindowVisibilityChanged(QWindow::Visibility newVisibility)
{
    auto* qw = qobject_cast<QWindow*>(sender());
    if (!qw) {
        return;
    }

    if (newVisibility == QWindow::Hidden) {
        qw->setProperty(visibleInDockProperty, false);
        --m_dockIconVisibleCount;
        if (m_dockIconVisibleCount == 0 && !m_editorDockIcon) {
            setActivationPolicyAccessory();
        }
    } else {
        bool windowTrackedInDock = qw->property(visibleInDockProperty).toBool();
        if (!windowTrackedInDock) {
            qw->setProperty(visibleInDockProperty, true);
            ++m_dockIconVisibleCount;
            setActivationPolicyRegular();
        }
    }
}

void Flameshot::showDockIcon(QWidget* w)
{
    QWindow* qw = w->windowHandle();
    if (!qw) {
        return;
    }

    connect(qw,
            &QWindow::visibilityChanged,
            this,
            &Flameshot::onWindowVisibilityChanged);
}
#endif

void Flameshot::openSavePath()
{
    QString savePath = ConfigHandler().savePath();
    if (!savePath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
    }
}

QVersionNumber Flameshot::getVersion()
{
    return QVersionNumber::fromString(
      QStringLiteral(APP_VERSION).replace("v", ""));
}

void Flameshot::setOrigin(Origin origin)
{
    m_origin = origin;
}

Flameshot::Origin Flameshot::origin()
{
    return m_origin;
}

/**
 * @brief Prompt the user to resolve config errors if necessary.
 * @return Whether errors were resolved.
 */
bool Flameshot::resolveAnyConfigErrors()
{
    bool resolved = true;
    ConfigHandler confighandler;
    if (!confighandler.checkUnrecognizedSettings() ||
        !confighandler.checkSemantics()) {
        auto* resolver = new ConfigResolver();
        QObject::connect(
          resolver, &ConfigResolver::rejected, [resolver, &resolved]() {
              resolved = false;
              resolver->deleteLater();
              if (origin() == CLI) {
                  exit(1);
              }
          });
        QObject::connect(
          resolver, &ConfigResolver::accepted, [resolver, &resolved]() {
              resolved = true;
              resolver->close();
              resolver->deleteLater();
              // Ensure that the dialog is closed before starting capture
              qApp->processEvents();
          });
        resolver->exec();
        qApp->processEvents();
    }
    return resolved;
}

void Flameshot::requestCapture(const CaptureRequest& request)
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    switch (request.captureMode()) {
        case CaptureRequest::FULLSCREEN_MODE:
            QTimer::singleShot(request.delay(),
                               [this, request] { full(request); });
            break;
        case CaptureRequest::SCREEN_MODE: {
            int&& number = request.data().toInt();
            QTimer::singleShot(request.delay(), [this, request, number]() {
                screen(request, number);
            });
            break;
        }
        case CaptureRequest::GRAPHICAL_MODE: {
            QTimer::singleShot(
              request.delay(), this, [this, request]() { gui(request); });
            break;
        }
        default:
            emit captureFailed();
            break;
    }
}

namespace {

// JPEG cannot hold transparency, so such captures are saved as PNG
QString pngOverrideForAlpha(bool needsAlpha)
{
    if (!needsAlpha) {
        return {};
    }
    const QString ext = ConfigHandler().saveAsFileExtension().toLower();
    if (ext != QLatin1String("jpg") && ext != QLatin1String("jpeg")) {
        return {};
    }
    AbstractLogger::info()
      << QObject::tr("Effects need transparency, saving this capture as "
                     "PNG instead of JPEG.");
    return QStringLiteral("png");
}

// Real side effects of the after-capture actions
class FlameshotActionSink : public ActionSink
{
public:
    // effectsApplied: the capture already went through the effects (beautify),
    // so the Apply Effects action must not run them a second time
    FlameshotActionSink(Flameshot* flameshot,
                        const CaptureRequest& req,
                        const QRect& selection,
                        bool effectsApplied,
                        bool effectsNeedAlpha)
      : m_flameshot(flameshot)
      , m_req(req)
      , m_selection(selection)
      , m_effectsApplied(effectsApplied)
      , m_effectsNeedAlpha(effectsNeedAlpha)
    {}

    QString save(const QPixmap& pixmap) override
    {
        const QString dir =
          m_req.path().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            : m_req.path();
        QString savedPath;
        if (!saveToFilesystem(pixmap,
                              dir,
                              QString(),
                              &savedPath,
                              m_req.overwriteExisting(),
                              saveExtensionOverride())) {
            return {};
        }
        // History and other listeners rely on this firing for every save
        emit m_flameshot->captureSaved(
          savedPath, pixmap, m_selection, m_req.capturedGlobalRect());
        return savedPath;
    }

    void copyImage(const QPixmap& pixmap) override
    {
        FlameshotDaemon::copyToClipboard(pixmap);
    }

    void copyText(const QString& text) override
    {
        FlameshotDaemon::copyToClipboard(
          text, QObject::tr("Path copied to clipboard."));
    }

    void openEditor(const QString& path,
                    const QPixmap& pixmap,
                    const QRect& globalRect) override
    {
        // The capture window may still be shutting down when this runs, and
        // gui() refuses to open while it exists, so wait for the event loop.
        QPointer<Flameshot> flameshot(m_flameshot);
        Q_UNUSED(globalRect)
        QTimer::singleShot(0, m_flameshot, [flameshot, path, pixmap]() {
            if (flameshot) {
                flameshot->editSavedCapture(path, pixmap);
            }
        });
    }

    void pin(const QPixmap& pixmap, const QRect& selection) override
    {
        FlameshotDaemon::createPin(pixmap, selection);
    }

    void showToast(const QString& path,
                   const QPixmap& pixmap,
                   const QRect& selection,
                   const QRect& globalRect) override
    {
        CaptureToast::showFor(path,
                              pixmap,
                              selection,
                              globalRect,
                              ConfigHandler().captureToastSeconds());
    }

    QPixmap applyEffects(const QPixmap& pixmap) override
    {
        if (m_effectsApplied) {
            return pixmap;
        }
        // Only border, corners and shadow, as before Beautify existed. The
        // Beautify extras (padding, background, ...) apply through
        // "Beautify every capture" or the Beautify button, which set
        // m_effectsApplied.
        const EffectSettings settings =
          imageeffects::basicEffects(ConfigHandler().effectSettings());
        m_effectsNeedAlpha = imageeffects::needsAlpha(settings);
        return QPixmap::fromImage(
          imageeffects::applyEffects(pixmap.toImage(), settings));
    }

private:
    QString saveExtensionOverride() const
    {
        return pngOverrideForAlpha(m_effectsNeedAlpha);
    }

    Flameshot* m_flameshot;
    const CaptureRequest& m_req;
    QRect m_selection;
    bool m_effectsApplied;
    bool m_effectsNeedAlpha;
};

} // namespace

void Flameshot::runWorkflow(const QPixmap& capture,
                            const QRect& selection,
                            const CaptureRequest& req,
                            bool effectsApplied,
                            bool effectsNeedAlpha)
{
    FlameshotActionSink sink(
      this, req, selection, effectsApplied, effectsNeedAlpha);
    CaptureResult result;
    result.pixmap = capture;
    result.selection = selection;
    result.globalRect = req.capturedGlobalRect();
    const RunReport report = ActionRunner(sink).run(
      ConfigHandler().workflowActions(req.workflow()), result);
    if (!report.skipped.isEmpty()) {
        QStringList names;
        for (CaptureAction a : report.skipped) {
            names << toName(a);
        }
        AbstractLogger::warning(AbstractLogger::Stderr)
          << tr("Skipped after-capture actions: %1").arg(names.join(", "));
    }
}

void Flameshot::exportCapture(const QPixmap& rawCapture,
                              QRect& selection,
                              const CaptureRequest& req)
{
    using CR = CaptureRequest;
    m_pendingProject = req.project();
    const auto clearProject =
      qScopeGuard([this]() { m_pendingProject = CaptureProject(); });
    if (req.recordMode() != CR::RecordNone) {
        // Area pick for a recording: nothing is exported as an image
        m_recording->areaPicked(req.capturedGlobalRect());
        return;
    }
    if (req.scrollCapture()) {
        // Area pick for a scrolling capture: the image comes later
        m_scrolling->areaPicked(req.capturedGlobalRect());
        return;
    }
    // Beautify replaces the capture for every task below
    QPixmap capture = rawCapture;
    const bool beautify = shouldBeautify((req.tasks() & CR::BEAUTIFY) != 0,
                                         ConfigHandler().beautifyAlways(),
                                         !req.editImage().isNull());
    // The project keeps the un-beautified base, so remember the effect for
    // "Edit again"
    m_pendingProject.beautified = beautify;
    bool needsAlpha = false;
    if (beautify) {
        const EffectSettings settings = ConfigHandler().effectSettings();
        needsAlpha = imageeffects::needsAlpha(settings);
        capture = QPixmap::fromImage(
          imageeffects::applyEffects(rawCapture.toImage(), settings));
    }
    int tasks = req.tasks(), mode = req.captureMode();
    QString path = req.path();

    if (tasks & CR::PRINT_GEOMETRY) {
        QTextStream(stdout)
          << selection.width() << "x" << selection.height() << "+"
          << selection.x() << "+" << selection.y() << "\n";
    }

    if (tasks & CR::PRINT_RAW) {
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        capture.save(&buffer, "PNG");
        if (QFile file; file.open(stdout, QIODevice::WriteOnly)) {
            file.write(byteArray);
            file.close();
        }
    }

    const bool hasWorkflow = !req.workflow().isEmpty();
    if (hasWorkflow) {
        // The hotkey's configured actions replace the SAVE and COPY tasks
        runWorkflow(capture, selection, req, beautify, needsAlpha);
    }

    if ((tasks & CR::SAVE) && !hasWorkflow) {
        if (req.path().isEmpty()) {
            saveToFilesystemGUI(capture, pngOverrideForAlpha(needsAlpha));
        } else {
            QString savedPath;
            if (saveToFilesystem(capture,
                                 path,
                                 QString(),
                                 &savedPath,
                                 req.overwriteExisting(),
                                 pngOverrideForAlpha(needsAlpha))) {
                emit captureSaved(
                  savedPath, capture, selection, req.capturedGlobalRect());
                // Only the editor window sets an edit image
                if (!req.editImage().isNull()) {
                    EditedThumbnail::showFor(
                      savedPath,
                      capture,
                      ConfigHandler().editedThumbnailSeconds());
                }
            }
        }
    }

    if ((tasks & CR::COPY) && !hasWorkflow) {
        FlameshotDaemon::copyToClipboard(capture);
    }

    if (tasks & CR::PIN) {
        FlameshotDaemon::createPin(capture, selection);
        if (mode == CR::SCREEN_MODE || mode == CR::FULLSCREEN_MODE) {
            AbstractLogger::info()
              << QObject::tr("Full screen screenshot pinned to screen");
        }
    }

#ifdef ENABLE_IMGUR
    if (tasks & CR::UPLOAD) {
        if (!ConfigHandler().uploadWithoutConfirmation()) {
            auto* dialog = new ImgUploadDialog();
            if (dialog->exec() == QDialog::Rejected) {
                return;
            }
        }

        ImgUploaderBase* widget = ImgUploaderManager().uploader(capture);
        widget->show();
        widget->activateWindow();
        // NOTE: lambda can't capture 'this' because it might be destroyed later
        CR::ExportTask tasks = tasks;
        QObject::connect(
          widget, &ImgUploaderBase::uploadOk, [=, this](const QUrl& url) {
              if (ConfigHandler().copyURLAfterUpload()) {
                  if (!(tasks & CR::COPY)) {
                      FlameshotDaemon::copyToClipboard(
                        url.toString(), tr("URL copied to clipboard."));
                  }
                  widget->showPostUploadDialog();
              }
          });
    }
#endif

    if (!(tasks & CR::UPLOAD)) {
        emit captureTaken(capture);
    }
}

QString Flameshot::storePendingProject(const QString& projectDir) const
{
    if (m_pendingProject.isEmpty()) {
        return {};
    }
    QDir().mkpath(projectDir);
    const QString path =
      projectDir + QLatin1Char('/') +
      QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
      QLatin1Char('-') +
      QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) +
      QLatin1Char('.') + captureproject::fileSuffix();
    // cfn: synchronous PNG encode on the UI thread after each annotated save,
    // move to a worker thread if large captures make the thumbnail lag
    QString error;
    if (!captureproject::save(path, m_pendingProject, &error)) {
        AbstractLogger::warning(AbstractLogger::Stderr)
          << tr("Could not keep this capture editable: %1").arg(error);
        return {};
    }
    return path;
}

void Flameshot::editSavedCapture(const QString& path, const QPixmap& capture)
{
    openEditorWindow(new EditorWindow(path, capture));
}

void Flameshot::editProject(const QString& path, const CaptureProject& project)
{
    QPixmap image = QPixmap::fromImage(project.base);
    image.setDevicePixelRatio(project.base.devicePixelRatio());
    openEditorWindow(
      new EditorWindow(path, image, project.objects, project.beautified));
}

void Flameshot::editCaptureAgain(const QString& path, const QPixmap& capture)
{
    const QString projectPath =
      CaptureHistory(CaptureHistory::defaultIndexPath()).projectFor(path);
    if (!projectPath.isEmpty()) {
        CaptureProject project;
        QString error;
        if (captureproject::load(projectPath, project, &error)) {
            editProject(path, project);
            return;
        }
        QMessageBox::warning(
          nullptr,
          tr("Edit Again"),
          tr("The annotations of this capture could not be restored, so it "
             "opens as a flat image.\n\n%1")
            .arg(error));
    }
    editSavedCapture(path, capture);
}

void Flameshot::openEditorWindow(EditorWindow* editor)
{
    m_editorWindows.append(editor);
    connect(editor, &QObject::destroyed, this, [this]() {
        m_editorWindows.removeAll(nullptr);
        emit editorWindowsChanged();
        updateEditorDockIcon();
    });
    emit editorWindowsChanged();
    // No showDockIcon() here: switching the app to a regular Dock app while
    // the user is in another app's full-screen Space makes macOS jump to the
    // desktop Space instead of showing the editor where the user is. The
    // Dock icon is added later, once the app is inactive
    // (updateEditorDockIcon).
    editor->present();
}

void Flameshot::showEditorWindows()
{
    const auto editors = m_editorWindows;
    for (const QPointer<EditorWindow>& editor : editors) {
        if (editor) {
            showOnActiveSpace(editor);
            if (editor->isMinimized()) {
                editor->showNormal();
            } else {
                editor->show();
            }
            editor->raise();
        }
    }
}

void Flameshot::updateEditorDockIcon()
{
#if defined(Q_OS_MACOS)
    m_editorWindows.removeAll(nullptr);
    const bool want =
      editorsWantDockIcon(static_cast<int>(m_editorWindows.size()),
                          qApp->applicationState() == Qt::ApplicationActive,
                          m_editorDockIcon);
    if (want == m_editorDockIcon) {
        return;
    }
    m_editorDockIcon = want;
    if (want) {
        setActivationPolicyRegular();
    } else if (m_dockIconVisibleCount == 0) {
        setActivationPolicyAccessory();
    }
#endif
}

bool Flameshot::hasEditorWindows() const
{
    for (const QPointer<EditorWindow>& editor : m_editorWindows) {
        if (editor) {
            return true;
        }
    }
    return false;
}

void Flameshot::setExternalWidget(bool b)
{
    m_haveExternalWidget = b;
}
bool Flameshot::haveExternalWidget()
{
    return m_haveExternalWidget;
}

// STATIC ATTRIBUTES
Flameshot::Origin Flameshot::m_origin = Flameshot::DAEMON;
