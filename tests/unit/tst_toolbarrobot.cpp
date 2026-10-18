// SPDX-License-Identifier: GPL-3.0-or-later

// Toolbar robot. Regression test for the Animate crash (EXC_BAD_ACCESS in
// CaptureWidget::setState after a left click on the Animate button): for
// every toolbar button, in the overlay and in the windowed editor, a fresh
// CaptureWidget gets a real left click on that button, then the event loop
// runs (deferred deletes included). Run under AddressSanitizer, a button whose
// press frees the widget or the button before setState is done goes red.
//
// Safety: before any widget exists, HOME and every Qt / Foundation location
// are pointed at a temp dir, and the test refuses to run unless the settings,
// history, cache and Pictures paths all resolve inside it. The daemon is
// hosted in this process, so clipboard and pin requests never reach a
// running Pixieshot.

#include "core/animexportjob.h"
#include "core/capturerequest.h"
#include "core/flameshot.h"
#include "core/flameshotdaemon.h"
#include "tools/capturetool.h"
#include "utils/abstractlogger.h"
#include "utils/appidentity.h"
#include "utils/confighandler.h"
#include "utils/valuehandler.h"
#include "widgets/capture/capturetoolbutton.h"
#include "widgets/capture/capturewidget.h"
#include "widgets/editorwindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace {

QTemporaryDir* g_tmp = nullptr;

QString tmpRoot()
{
    return QDir(g_tmp->path()).canonicalPath();
}

bool inside(const QString& path)
{
    const QString root = tmpRoot();
    const QString clean = QDir::cleanPath(path);
    return clean.startsWith(root) ||
           clean.startsWith(QDir::cleanPath(g_tmp->path()));
}

QPixmap syntheticShot()
{
    QPixmap shot(400, 300);
    shot.fill(QColor(40, 120, 200));
    QPainter p(&shot);
    p.fillRect(QRect(50, 50, 120, 80), QColor(240, 200, 40));
    p.end();
    return shot;
}

// Closes any modal dialog a button opens so the loop never blocks
void closeModals()
{
    if (QWidget* modal = QApplication::activeModalWidget()) {
        qWarning("closing modal %s", modal->metaObject()->className());
        modal->close();
    }
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        closeModals();
        QTest::qWait(20);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

// Removes every window a click left behind (editors, pins, toasts, the
// widget itself) so the next row starts clean
void closeAllWindows()
{
    for (int round = 0; round < 3; ++round) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w->isVisible() || w->testAttribute(Qt::WA_DeleteOnClose)) {
                w->close();
            }
        }
        pump(50);
    }
}

void waitForAnimationJobs()
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 60000 &&
           !qApp->findChildren<AnimExportJob*>().isEmpty()) {
        pump(100);
    }
}

enum class Mode
{
    OverlayNoTask,   // Save, Copy, Pin, Open app, ...
    OverlaySaveTask, // Accept, Beautify, Animate
    OverlayWorkflow, // the hotkey workflow: applyEffects, save, openEditor
    Editor,          // windowed editor
};

const char* modeName(Mode m)
{
    switch (m) {
        case Mode::OverlayNoTask:
            return "overlay-notask";
        case Mode::OverlaySaveTask:
            return "overlay-save";
        case Mode::OverlayWorkflow:
            return "overlay-workflow";
        case Mode::Editor:
            return "editor";
    }
    return "?";
}

} // namespace

Q_DECLARE_METATYPE(Mode)

class TestToolbarRobot : public QObject
{
    Q_OBJECT

private:
    // The window under test and its CaptureWidget
    QPointer<QWidget> m_window;
    QPointer<CaptureWidget> m_capture;

    void open(Mode mode)
    {
        const QString out = tmpRoot() + QStringLiteral("/shots");
        QDir().mkpath(out);
        if (mode == Mode::Editor) {
            const QString path = out + QStringLiteral("/edit.png");
            QPixmap shot = syntheticShot();
            shot.save(path);
            auto* editor = new EditorWindow(path, shot);
            m_window = editor;
            m_capture = editor->findChild<CaptureWidget*>();
            editor->show();
        } else {
            CaptureRequest req(CaptureRequest::GRAPHICAL_MODE);
            if (mode == Mode::OverlaySaveTask) {
                req.addSaveTask(out + QStringLiteral("/overlay.png"));
            } else if (mode == Mode::OverlayWorkflow) {
                req.addSaveTask();
                req.setWorkflow(QStringLiteral("TAKE_SCREENSHOT"));
            }
            req.setInitialSelection(QRect(100, 100, 300, 200));
            auto* capture = new CaptureWidget(req);
            m_window = capture;
            m_capture = capture;
            capture->show();
        }
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QVERIFY(m_capture);
    }

    QList<CaptureToolButton*> clickableButtons() const
    {
        QList<CaptureToolButton*> out;
        for (CaptureToolButton* b :
             m_window->findChildren<CaptureToolButton*>()) {
            if (b->isVisible()) {
                out << b;
            }
        }
        return out;
    }

private slots:
    // Bug: BoundedInt::expected() passed the maximum as QString::arg's
    // field width, so settings errors read "number between    3 and %2".
    // Lives here because valuehandler.cpp needs the whole app to link.
    void boundedIntExpectedNamesBothLimits_bugLiteralPercent2()
    {
        BoundedInt handler(3, 10, 5);
        QCOMPARE(handler.expected(), QStringLiteral("number between 3 and 10"));
    }

    void initTestCase()
    {
        // Tripwires: refuse to run if anything resolves outside the temp dir
        const QString root = tmpRoot();
        for (auto loc : { QStandardPaths::AppDataLocation,
                          QStandardPaths::GenericConfigLocation,
                          QStandardPaths::CacheLocation,
                          QStandardPaths::PicturesLocation,
                          QStandardPaths::GenericDataLocation }) {
            const QString p = QStandardPaths::writableLocation(loc);
            QVERIFY2(inside(p),
                     qPrintable(QStringLiteral("REFUSING TO RUN: %1 is outside "
                                               "%2")
                                  .arg(p, root)));
        }
        ConfigHandler config;
        QVERIFY2(inside(config.configFilePath()),
                 qPrintable(QStringLiteral("REFUSING TO RUN: config %1")
                              .arg(config.configFilePath())));

        const QString shots = root + QStringLiteral("/shots");
        QDir().mkpath(shots);
        config.setSavePath(shots);
        config.setSavePathFixed(true);
        config.setCheckForUpdates(false);
        config.setDisabledTrayIcon(true);
        config.setShowDesktopNotification(false);
        config.setAutoCloseIdleDaemon(false);
        config.setShowHelp(false);
        config.setShowQuitPrompt(false);
        config.setHoverWindowDetection(false);
        config.setUploadWithoutConfirmation(false);
        config.setAnimDurationSec(3);
        config.setAnimFps(15);
        config.setAnimFormat(2);
        config.setActionsTakeScreenshot(
          QStringList{ QStringLiteral("applyEffects"),
                       QStringLiteral("save"),
                       QStringLiteral("openEditor") });
        // Every button type visible, so each one gets clicked
        config.setButtons(CaptureToolButton::getIterableButtonTypes());

        {
            // A setting this test writes must not trip the config check
            AbstractLogger log = AbstractLogger::error(AbstractLogger::Stderr);
            QVERIFY(ConfigHandler().checkForErrors(&log));
        }

        // Closes a dialog even inside its own nested event loop (a dialog
        // opened from a widget's destructor never returns to pump())
        auto* modalCloser = new QTimer(qApp);
        connect(modalCloser, &QTimer::timeout, qApp, &closeModals);
        modalCloser->start(100);

        Flameshot::instance();
        FlameshotDaemon::start();
        QVERIFY2(FlameshotDaemon::instance() != nullptr,
                 "the daemon must be in-process so nothing reaches a running "
                 "Pixieshot");
    }

    void cleanup()
    {
        closeAllWindows();
        waitForAnimationJobs();
        closeAllWindows();
    }

    void cleanupTestCase()
    {
        // Encoders run off the GUI thread; let them finish before exit
        waitForAnimationJobs();
        closeAllWindows();
    }

    void leftClickEveryToolbarButtonNoUseAfterFree_data()
    {
        QTest::addColumn<Mode>("mode");
        QTest::addColumn<int>("type");
        for (Mode mode : { Mode::Editor,
                           Mode::OverlaySaveTask,
                           Mode::OverlayWorkflow,
                           Mode::OverlayNoTask }) {
            // Discover which buttons this mode shows
            open(mode);
            QList<CaptureTool::Type> types;
            for (CaptureToolButton* b : clickableButtons()) {
                types << b->tool()->type();
            }
            closeAllWindows();
            QVERIFY2(!types.isEmpty(), modeName(mode));
            // Animate first: the reported crash
            std::stable_sort(types.begin(),
                             types.end(),
                             [](CaptureTool::Type a, CaptureTool::Type b) {
                                 return (a == CaptureTool::TYPE_ANIMATE) >
                                        (b == CaptureTool::TYPE_ANIMATE);
                             });
            for (CaptureTool::Type t : types) {
                const QString tag =
                  QStringLiteral("%1/%2").arg(
                    QString::fromLatin1(modeName(mode)),
                    QString::fromLatin1(QMetaEnum::fromType<CaptureTool::Type>()
                                          .valueToKey(t)));
                QTest::newRow(qPrintable(tag)) << mode << int(t);
            }
        }
    }

    void leftClickEveryToolbarButtonNoUseAfterFree()
    {
        QFETCH(Mode, mode);
        QFETCH(int, type);
        open(mode);
        CaptureToolButton* target = nullptr;
        for (CaptureToolButton* b : clickableButtons()) {
            if (b->tool()->type() == type) {
                target = b;
            }
        }
        QVERIFY2(target, "button not shown");
        QTest::mouseClick(target, Qt::LeftButton, {}, target->rect().center());
        pump(300);
        // A second click on a tool that kept the widget open (toggle off)
        if (m_capture && target && target->isVisible()) {
            QPointer<CaptureToolButton> again(target);
            QTest::mouseClick(again, Qt::LeftButton, {}, again->rect().center());
            pump(200);
        }
    }
};

int main(int argc, char** argv)
{
    // Everything the app writes goes under one temp dir. Set before Qt or
    // Foundation read any of these.
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "no temp dir\n");
        return 2;
    }
    g_tmp = &tmp;
    const QByteArray home = tmp.path().toUtf8();
    qputenv("HOME", home);
    qputenv("CFFIXED_USER_HOME", home);
    qputenv("XDG_CONFIG_HOME", home + "/.config");
    qputenv("XDG_CACHE_HOME", home + "/.cache");
    qputenv("XDG_DATA_HOME", home + "/.local/share");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       tmp.path() + QStringLiteral("/.config"));

    QApplication app(argc, argv);
    AppIdentity::apply();
    // As main.cpp: the button list is saved as QList<int>
    qRegisterMetaType<QList<int>>();
    app.setQuitOnLastWindowClosed(false);
    TestToolbarRobot test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_toolbarrobot.moc"
