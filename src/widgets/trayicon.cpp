#include "trayicon.h"
#include "core/capturefirstrequest.h"
#include "core/capturerequest.h"
#include "core/flameshot.h"
#include "core/flameshotdaemon.h"
#include "core/qguiappcurrentscreen.h"
#include "platform/screenrecorder.h"
#include "utils/confighandler.h"
#include "utils/globalvalues.h"
#include "widgets/capturehistorywindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#if defined(Q_OS_MACOS)
#include <QOperatingSystemVersion>
#endif

TrayIcon::TrayIcon(QObject* parent)
  : QSystemTrayIcon(parent)
  , m_screenMenu(nullptr)
{
    initMenu();
    initScreenMenu();

    setToolTip(QStringLiteral("Pixieshot"));
#if defined(Q_OS_MACOS)
    // Because of the following issues on MacOS "Catalina":
    // https://bugreports.qt.io/browse/QTBUG-86393
    // https://developer.apple.com/forums/thread/126072
    auto currentMacOsVersion = QOperatingSystemVersion::current();
    if (currentMacOsVersion >= QOperatingSystemVersion::MacOSBigSur) {
        setContextMenu(m_menu);
    }
#else
    setContextMenu(m_menu);
#endif
    QIcon icon =
      QIcon::fromTheme("flameshot-tray", QIcon(GlobalValues::trayIconPath()));

#if defined(Q_OS_MACOS)
    if (currentMacOsVersion >= QOperatingSystemVersion::MacOSBigSur) {
        icon.setIsMask(true);
    }
#endif

    setIcon(icon);

#if defined(Q_OS_MACOS)
    if (currentMacOsVersion < QOperatingSystemVersion::MacOSBigSur) {
        // Because of the following issues on MacOS "Catalina":
        // https://bugreports.qt.io/browse/QTBUG-86393
        // https://developer.apple.com/forums/thread/126072
        auto trayIconActivated = [this](QSystemTrayIcon::ActivationReason r) {
            if (m_menu->isVisible()) {
                m_menu->hide();
            } else {
                m_menu->popup(QCursor::pos());
            }
        };
        connect(this, &QSystemTrayIcon::activated, this, trayIconActivated);
    }
#else
    connect(this, &TrayIcon::activated, this, [this](ActivationReason r) {
        if (r == Trigger) {
            startGuiCapture();
        }
    });
#endif

#ifdef Q_OS_WIN
    // Ensure proper removal of tray icon when program quits on Windows.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &TrayIcon::hide);
#endif

    show(); // TODO needed?

    if (ConfigHandler().showStartupLaunchMessage()) {
        showMessage(
          "Pixieshot",
          QObject::tr(
            "Hello, I'm here! Click icon in the tray to take a screenshot or "
            "click with a right button to see more options."),
          icon,
          3000);
    }

    connect(ConfigHandler::getInstance(),
            &ConfigHandler::fileChanged,
            this,
            [this]() { updateCaptureActionShortcut(); });
}

TrayIcon::~TrayIcon()
{
    delete m_menu;
}

#if !defined(DISABLE_UPDATE_CHECKER)
QAction* TrayIcon::appUpdates()
{
    return m_appUpdates;
}
#endif

void TrayIcon::initMenu()
{
    m_menu = new QMenu();

    m_captureAction = new QAction(tr("&Take Screenshot"), this);

    updateCaptureActionShortcut();

    connect(m_captureAction, &QAction::triggered, this, [this]() {
#if defined(Q_OS_MACOS)
        auto currentMacOsVersion = QOperatingSystemVersion::current();
        if (currentMacOsVersion >= QOperatingSystemVersion::MacOSBigSur) {
            startGuiCapture();
        } else {
            // It seems it is not relevant for MacOS BigSur (Wait 400 ms to hide
            // the QMenu)
            QTimer::singleShot(400, this, [this]() { startGuiCapture(); });
        }
#else
    // Wait 400 ms to hide the QMenu
    QTimer::singleShot(400, this, [this]() {
        startGuiCapture();
    });
#endif
    });
    m_launcherAction = new QAction(tr("&Open Launcher"), this);
    connect(m_launcherAction,
            &QAction::triggered,
            Flameshot::instance(),
            &Flameshot::launcher);
    // Recording needs macOS 15+; the items do not exist elsewhere
    if (screenRecordingSupported()) {
        const struct
        {
            const char* text;
            CaptureRequest::RecordMode mode;
        } recordItems[] = {
            { QT_TR_NOOP("Record &MP4"), CaptureRequest::RecordMp4 },
            { QT_TR_NOOP("Record &GIF"), CaptureRequest::RecordGif },
        };
        for (const auto& item : recordItems) {
            auto* action = new QAction(tr(item.text), this);
            const auto mode = item.mode;
            connect(action, &QAction::triggered, this, [mode]() {
                // Let the menu close before the picker overlay opens
                QTimer::singleShot(400, Flameshot::instance(), [mode]() {
                    Flameshot::instance()->toggleRecording(mode);
                });
            });
            m_recordActions << action;
        }
        // Scrolling capture grabs with ScreenCaptureKit too
        auto* scrollAction = new QAction(tr("&Scrolling Capture"), this);
        connect(scrollAction, &QAction::triggered, this, []() {
            QTimer::singleShot(400, Flameshot::instance(), []() {
                Flameshot::instance()->toggleScrollCapture();
            });
        });
        m_recordActions << scrollAction;
    }
    auto* configAction = new QAction(tr("&Configuration"), this);
    connect(configAction,
            &QAction::triggered,
            Flameshot::instance(),
            &Flameshot::config);
    m_infoAction = new QAction(tr("&About"), this);
    connect(m_infoAction,
            &QAction::triggered,
            Flameshot::instance(),
            &Flameshot::info);

#if !defined(DISABLE_UPDATE_CHECKER)
    m_appUpdates = new QAction(tr("Check for updates"), this);
    connect(m_appUpdates,
            &QAction::triggered,
            FlameshotDaemon::instance(),
            &FlameshotDaemon::checkForUpdates);

    connect(FlameshotDaemon::instance(),
            &FlameshotDaemon::newVersionAvailable,
            this,
            [this](const QVersionNumber& version) {
                if (ConfigHandler().checkForUpdates()) {
                    QString newVersion =
                      tr("Download version %1").arg(version.toString());
                    m_appUpdates->setText(newVersion);
                    m_appUpdates->setVisible(true);

                    // hack to work around menu not updating when the text /
                    // visibility is modified Force menu refresh by removing and
                    // re-adding the action
                    m_menu->removeAction(m_appUpdates);
                    m_menu->insertAction(m_infoAction, m_appUpdates);
                }
            });
    updateCheckUpdatesMenuVisibility();
#endif

    QAction* quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

#ifdef ENABLE_IMGUR
    // recent screenshots
    QAction* recentAction = new QAction(tr("&Latest Uploads"), this);
    connect(recentAction,
            &QAction::triggered,
            Flameshot::instance(),
            &Flameshot::history);
#endif
    auto* captureHistoryAction = new QAction(tr("Capture &History"), this);
    connect(captureHistoryAction,
            &QAction::triggered,
            this,
            []() { CaptureHistoryWindow::showWindow(); });
    auto* openSavePathAction = new QAction(tr("&Open Save Path"), this);
    connect(openSavePathAction,
            &QAction::triggered,
            Flameshot::instance(),
            &Flameshot::openSavePath);

    m_menu->addAction(m_captureAction);
    m_menu->addAction(m_launcherAction);
#if defined(Q_OS_MACOS)
    // Brings open editor windows to the front, e.g. from behind other apps
    m_showEditorsAction = new QAction(tr("Show &Edit Windows"), this);
    m_showEditorsAction->setEnabled(
      Flameshot::instance()->hasEditorWindows());
    connect(m_showEditorsAction,
            &QAction::triggered,
            Flameshot::instance(),
            &Flameshot::showEditorWindows);
    connect(Flameshot::instance(),
            &Flameshot::editorWindowsChanged,
            this,
            [this]() {
                m_showEditorsAction->setEnabled(
                  Flameshot::instance()->hasEditorWindows());
            });
    m_menu->addAction(m_showEditorsAction);
#endif
    m_menu->addActions(m_recordActions);
    m_menu->addSeparator();
#ifdef ENABLE_IMGUR
    m_menu->addAction(recentAction);
#endif
    m_menu->addAction(captureHistoryAction);
    m_menu->addAction(openSavePathAction);
    m_menu->addSeparator();
    m_menu->addAction(configAction);
    m_menu->addSeparator();
#if !defined(DISABLE_UPDATE_CHECKER)
    m_menu->addAction(m_appUpdates);
#endif
    m_menu->addAction(m_infoAction);
    m_menu->addSeparator();
    m_menu->addAction(quitAction);
}

void TrayIcon::updateCaptureActionShortcut()
{
#if defined(Q_OS_MACOS)
    if (!m_captureAction) {
        return;
    }

    QString shortcut = ConfigHandler().shortcut("TAKE_SCREENSHOT");
    m_captureAction->setShortcut(QKeySequence(shortcut));
#endif
}

#if !defined(DISABLE_UPDATE_CHECKER)
void TrayIcon::updateCheckUpdatesMenuVisibility()
{
    if (m_appUpdates == nullptr) {
        return;
    }

    bool autoCheckEnabled = ConfigHandler().checkForUpdates();
    if (autoCheckEnabled) {
        // When auto-check is enabled, hide the menu item initially
        // It will be shown when a new version is available via a callback
        m_appUpdates->setVisible(false);
    } else {
        m_appUpdates->setVisible(true);
        m_appUpdates->setText(tr("Check for updates"));
    }
}
#endif

void TrayIcon::initScreenMenu()
{
#ifndef Q_OS_MACOS
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() <= 1) {
        return;
    }

    m_screenMenu = new QMenu(tr("Select Screen"));

    QList<QAction*> actions = m_menu->actions();
    int index = actions.indexOf(m_launcherAction);
    if (index >= 0 && index + 1 < actions.size()) {
        m_menu->insertMenu(actions[index + 1], m_screenMenu);
    } else {
        m_menu->addMenu(m_screenMenu);
    }

    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    int currentIndex = screens.indexOf(currentScreen);

    for (int i = 0; i < screens.size(); ++i) {
        QScreen* screen = screens[i];
        QRect geom = screen->geometry();
        QString screenDescription = tr("Monitor %1: %2 (%3x%4)")
                                      .arg(i + 1)
                                      .arg(screen->name())
                                      .arg(geom.width())
                                      .arg(geom.height());

        QAction* screenAction = m_screenMenu->addAction(screenDescription);
        connect(screenAction, &QAction::triggered, this, [this, i]() {
            // Wait and hide the menu
            QTimer::singleShot(
              100, this, [this, i]() { startGuiCaptureOnScreen(i); });
        });
    }
#endif
}

void TrayIcon::startGuiCapture()
{
    ConfigHandler config;
    auto* widget =
      config.captureFirst()
        ? Flameshot::instance()->guiWithTimer(buildCaptureFirstRequest(
            QStringLiteral("TAKE_SCREENSHOT"),
            config.savePath(),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)))
        : Flameshot::instance()->guiWithTimer();
#if !defined(DISABLE_UPDATE_CHECKER)
    if (widget != nullptr) {
        FlameshotDaemon::instance()->showUpdateNotificationIfAvailable(widget);
    }
#endif
}

void TrayIcon::startGuiCaptureOnScreen(int screenIndex)
{
    CaptureRequest req(CaptureRequest::GRAPHICAL_MODE, 400);
    req.setSelectedMonitor(screenIndex);
    Flameshot::instance()->requestCapture(req);
}
