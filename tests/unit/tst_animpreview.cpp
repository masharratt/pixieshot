// SPDX-License-Identifier: GPL-3.0-or-later

// Animate preview and editable Animate output, against the whole app:
//  - the preview draws its frames with the export's own params and
//    renderer, so a preview frame at t is the export frame at t
//  - its style and motion pickers update it live, Play/Pause works
//  - Cancel returns to the editor with nothing changed and nothing written
//  - Export writes the files and a capture project (base, objects and the
//    Animate settings) into the history, and the toast offers Edit, which
//    reopens the editor with the annotations still movable
//
// Safety: as tst_toolbarrobot, HOME and every Qt / Foundation location are
// pointed at a temp dir before Qt starts, and the test refuses to run unless
// the settings, history and Pictures paths resolve inside it.

#include "core/animexportjob.h"
#include "core/capturerequest.h"
#include "core/flameshot.h"
#include "core/flameshotdaemon.h"
#include "fakes/drawobject.h"
#include "tools/animannotations.h"
#include "tools/capturetool.h"
#include "utils/abstractlogger.h"
#include "utils/animrender.h"
#include "utils/appidentity.h"
#include "utils/capturehistory.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"
#include "widgets/animpreviewdialog.h"
#include "widgets/capture/capturetoolbutton.h"
#include "widgets/capture/capturewidget.h"
#include "widgets/capturetoast.h"
#include "widgets/editorwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>

namespace {

QTemporaryDir* g_tmp = nullptr;

QString tmpRoot()
{
    return QDir(g_tmp->path()).canonicalPath();
}

bool inside(const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    return clean.startsWith(tmpRoot()) ||
           clean.startsWith(QDir::cleanPath(g_tmp->path()));
}

QString shotsDir()
{
    return tmpRoot() + QStringLiteral("/shots");
}

QPixmap syntheticShot()
{
    QPixmap shot(240, 150);
    shot.fill(QColor(245, 246, 250));
    QPainter p(&shot);
    p.fillRect(QRect(20, 30, 120, 40), QColor(236, 72, 96));
    p.end();
    return shot;
}

// Two real annotations as project JSON, relative to the image
QJsonArray twoObjects()
{
    std::unique_ptr<CaptureTool> rect(testtools::drawObject(
      CaptureTool::TYPE_RECTANGLE, QString(), QPoint(30, 30), QPoint(120, 90)));
    std::unique_ptr<CaptureTool> arrow(testtools::drawObject(
      CaptureTool::TYPE_ARROW, QString(), QPoint(150, 20), QPoint(200, 100)));
    return captureproject::encodeObjects({ rect.get(), arrow.get() }, QPoint());
}

AnimSource sampleSource()
{
    AnimSource source;
    source.base = syntheticShot().toImage();
    source.base.setDevicePixelRatio(1.0);
    source.dpr = 1.0;
    source.objects = twoObjects();
    return source;
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QTest::qWait(20);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

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

bool waitForAnimationJobs(int ms = 60000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        pump(50);
        if (qApp->findChildren<AnimExportJob*>().isEmpty()) {
            return true;
        }
    }
    return false;
}

template<typename T>
QList<T*> visibleTopLevels()
{
    QList<T*> out;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* t = qobject_cast<T*>(w); t && t->isVisible()) {
            out << t;
        }
    }
    return out;
}

AnimPreviewDialog* openPreview()
{
    const auto list = visibleTopLevels<AnimPreviewDialog>();
    return list.isEmpty() ? nullptr : list.last();
}

CaptureToolButton* animateButton(QWidget* window)
{
    for (CaptureToolButton* b : window->findChildren<CaptureToolButton*>()) {
        if (b->isVisible() && b->tool()->type() == CaptureTool::TYPE_ANIMATE) {
            return b;
        }
    }
    return nullptr;
}

QStringList outputsIn(const QString& dir)
{
    return QDir(dir).entryList({ QStringLiteral("*.gif"),
                                 QStringLiteral("*.mp4") },
                               QDir::Files);
}

} // namespace

class TestAnimPreview : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString root = tmpRoot();
        for (auto loc : { QStandardPaths::AppDataLocation,
                          QStandardPaths::GenericConfigLocation,
                          QStandardPaths::CacheLocation,
                          QStandardPaths::PicturesLocation }) {
            const QString p = QStandardPaths::writableLocation(loc);
            QVERIFY2(inside(p),
                     qPrintable(
                       QStringLiteral("REFUSING TO RUN: %1 is outside %2")
                         .arg(p, root)));
        }
        ConfigHandler config;
        QVERIFY2(inside(config.configFilePath()),
                 qPrintable(QStringLiteral("REFUSING TO RUN: config %1")
                              .arg(config.configFilePath())));
        QVERIFY2(inside(CaptureHistory::defaultIndexPath()),
                 "REFUSING TO RUN: history outside the temp dir");

        QDir().mkpath(shotsDir());
        config.setSavePath(shotsDir());
        config.setSavePathFixed(true);
        config.setCheckForUpdates(false);
        config.setDisabledTrayIcon(true);
        config.setShowDesktopNotification(false);
        config.setAutoCloseIdleDaemon(false);
        config.setShowHelp(false);
        config.setShowQuitPrompt(false);
        // The shortest loop the settings allow, so exports finish quickly
        config.setAnimDurationSec(3);
        config.setAnimFps(15);
        config.setAnimFormat(0);
        config.setAnimStyle(int(AnimStyle::Aurora));
        config.setAnimMotion(int(ShotMotion::None));
        config.setCaptureToastSeconds(30);
        config.setButtons(CaptureToolButton::getIterableButtonTypes());
        {
            // A setting this test writes must not trip the config check
            // (an invalid config refuses later writes)
            AbstractLogger log = AbstractLogger::error(AbstractLogger::Stderr);
            QVERIFY(ConfigHandler().checkForErrors(&log));
        }

        Flameshot::instance();
        FlameshotDaemon::start();
        QVERIFY(FlameshotDaemon::instance() != nullptr);
    }

    void cleanup()
    {
        closeAllWindows();
        QVERIFY(waitForAnimationJobs());
        closeAllWindows();
    }

    // The preview is not a second renderer: its frame at t is the frame the
    // export draws at t, from the same params, at the preview's size
    void previewFrameAtTIsTheExportFrameAtT()
    {
        const AnimSource source = sampleSource();
        for (const int format : { 0, 1 }) {
            AnimChoice choice;
            choice.style = int(AnimStyle::Flow);
            choice.motion = int(ShotMotion::Tilt);
            choice.format = format;
            AnimPreviewDialog dialog(source, choice);
            const AnimTarget target =
              format == 1 ? AnimTarget::Gif : AnimTarget::Mp4;
            QCOMPARE(dialog.target(), target);
            const AnimRenderParams exportParams =
              AnimExportJob::paramsFor(animannotations::prepare(source), choice);
            const AnimatedFrames exportFrames =
              animFramesFor(exportParams, target, dialog.previewLongSide());
            for (const double t : { 0.0, 0.37, 0.81 }) {
                const QImage preview = dialog.frameAt(t);
                QVERIFY(!preview.isNull());
                QCOMPARE(preview, exportFrames.frame(t));
            }
            // What the dialog shows is that frame
            dialog.setPlaying(false);
            dialog.seek(0.5);
            QCOMPARE(dialog.shownFrame(), exportFrames.frame(0.5));
            // Same canvas shape as the real export, just smaller
            const QSize full = animFramesFor(exportParams, target).size();
            const QSize small = exportFrames.size();
            QVERIFY(small.width() <= full.width());
            QVERIFY(qAbs(double(small.width()) / small.height() -
                         double(full.width()) / full.height()) < 0.05);
        }
    }

    void stylePickerAndMotionPickerUpdateThePreviewLive()
    {
        AnimPreviewDialog dialog(sampleSource(), AnimChoice{});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.setPlaying(false);
        dialog.seek(0.5);
        const QImage before = dialog.shownFrame();

        auto* style =
          dialog.findChild<QComboBox*>(QStringLiteral("animPreviewStyle"));
        QVERIFY(style);
        style->setCurrentIndex(style->findData(int(AnimStyle::Silk)));
        QCOMPARE(dialog.choice().style, int(AnimStyle::Silk));
        const QImage silk = dialog.shownFrame();
        QVERIFY2(silk != before, "the style change did not reach the preview");

        auto* motion =
          dialog.findChild<QComboBox*>(QStringLiteral("animPreviewMotion"));
        QVERIFY(motion);
        motion->setCurrentIndex(motion->findData(int(ShotMotion::Float)));
        QCOMPARE(dialog.choice().motion, int(ShotMotion::Float));
        QVERIFY2(dialog.shownFrame() != silk,
                 "the motion change did not reach the preview");

        // Optional visual samples for review: ANIM_SAMPLES_DIR=<folder>
        if (const QString dir = qEnvironmentVariable("ANIM_SAMPLES_DIR");
            !dir.isEmpty()) {
            QDir().mkpath(dir);
            for (const double t : { 0.2, 0.6 }) {
                dialog.seek(t);
                dialog.grab().save(
                  QStringLiteral("%1/preview-silk-float-%2.png")
                    .arg(dir)
                    .arg(int(t * 100)));
            }
        }

        auto* format =
          dialog.findChild<QComboBox*>(QStringLiteral("animPreviewFormat"));
        QVERIFY(format);
        format->setCurrentIndex(format->findData(1));
        QCOMPARE(dialog.choice().format, 1);
        QCOMPARE(dialog.target(), AnimTarget::Gif);
    }

    void playAdvancesAndPauseHolds()
    {
        AnimPreviewDialog dialog(sampleSource(), AnimChoice{});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QVERIFY2(dialog.isPlaying(), "the loop plays as soon as it opens");
        const double start = dialog.phase();
        pump(400);
        QVERIFY2(dialog.phase() != start, "playing but the phase did not move");

        auto* play =
          dialog.findChild<QPushButton*>(QStringLiteral("animPreviewPlay"));
        QVERIFY(play);
        QTest::mouseClick(play, Qt::LeftButton);
        QVERIFY(!dialog.isPlaying());
        const double held = dialog.phase();
        pump(300);
        QCOMPARE(dialog.phase(), held);
    }

    void cancelReturnsToTheEditorWithNothingLost()
    {
        const QString path = shotsDir() + QStringLiteral("/cancel.png");
        QPixmap shot = syntheticShot();
        QVERIFY(shot.save(path));
        auto* editor = new EditorWindow(path, shot, twoObjects());
        editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(editor));
        QPointer<CaptureWidget> capture = editor->findChild<CaptureWidget*>();
        QVERIFY(capture);
        const CaptureProject before = capture->currentProject();
        QCOMPARE(before.objects.size(), 2);
        const QStringList filesBefore = outputsIn(shotsDir());

        CaptureToolButton* button = animateButton(editor);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton, {}, button->rect().center());
        AnimPreviewDialog* dialog = openPreview();
        QVERIFY2(dialog, "clicking Animate must open the preview");
        QVERIFY2(qApp->findChildren<AnimExportJob*>().isEmpty(),
                 "nothing is exported before Export is pressed");

        auto* cancel =
          dialog->findChild<QPushButton*>(QStringLiteral("animPreviewCancel"));
        QVERIFY(cancel);
        QTest::mouseClick(cancel, Qt::LeftButton);
        pump(200);

        QVERIFY(openPreview() == nullptr);
        QVERIFY(editor->isVisible());
        QVERIFY(capture);
        const CaptureProject after = capture->currentProject();
        QCOMPARE(after.objects, before.objects);
        QCOMPARE(after.base, before.base);
        QVERIFY(qApp->findChildren<AnimExportJob*>().isEmpty());
        QCOMPARE(outputsIn(shotsDir()), filesBefore);
    }

    // "add a toggle in the config for animation to show preview or not":
    // with the preview off, Animate exports straight away with the saved
    // choices and no dialog appears
    void previewOffExportsStraightAway()
    {
        ConfigHandler().setAnimShowPreview(false);
        const QString path = shotsDir() + QStringLiteral("/nopreview.png");
        QPixmap shot = syntheticShot();
        QVERIFY(shot.save(path));
        auto* editor = new EditorWindow(path, shot, twoObjects());
        editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(editor));
        const QStringList filesBefore = outputsIn(shotsDir());

        CaptureToolButton* button = animateButton(editor);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton, {}, button->rect().center());
        pump(100);
        const bool noDialog = openPreview() == nullptr;
        const bool started = !qApp->findChildren<AnimExportJob*>().isEmpty();
        QVERIFY(waitForAnimationJobs());
        ConfigHandler().setAnimShowPreview(true);
        QVERIFY2(noDialog, "the preview must not open when it is switched off");
        QVERIFY2(started || outputsIn(shotsDir()).size() > filesBefore.size(),
                 "Animate must export straight away when the preview is off");
    }

    void previewIsOnByDefault()
    {
        QCOMPARE(ConfigHandler::optionDefault(QStringLiteral("animShowPreview")),
                 QVariant(true));
    }

    // Export from the preview: GIF output that can be edited again
    void exportedAnimationIsEditableFromItsToast()
    {
        const QString path = shotsDir() + QStringLiteral("/editable.png");
        QPixmap shot = syntheticShot();
        QVERIFY(shot.save(path));
        auto* editor = new EditorWindow(path, shot, twoObjects());
        editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(editor));
        QPointer<CaptureWidget> capture = editor->findChild<CaptureWidget*>();
        const QJsonArray objects = capture->currentProject().objects;

        CaptureToolButton* button = animateButton(editor);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton, {}, button->rect().center());
        AnimPreviewDialog* dialog = openPreview();
        QVERIFY(dialog);
        auto* style = dialog->findChild<QComboBox*>("animPreviewStyle");
        auto* motion = dialog->findChild<QComboBox*>("animPreviewMotion");
        auto* format = dialog->findChild<QComboBox*>("animPreviewFormat");
        style->setCurrentIndex(style->findData(int(AnimStyle::Blobs)));
        motion->setCurrentIndex(motion->findData(int(ShotMotion::Float)));
        format->setCurrentIndex(format->findData(1)); // GIF
        auto* exportButton =
          dialog->findChild<QPushButton*>(QStringLiteral("animPreviewExport"));
        QVERIFY(exportButton);
        QTest::mouseClick(exportButton, Qt::LeftButton);
        pump(100);
        QVERIFY(openPreview() == nullptr);
        QVERIFY(waitForAnimationJobs());

        // The choices are kept for next time
        ConfigHandler config;
        QCOMPARE(config.animStyle(), int(AnimStyle::Blobs));
        QCOMPARE(config.animMotion(), int(ShotMotion::Float));
        QCOMPARE(config.animFormat(), 1);

        // History: the GIF with its project
        const QList<HistoryEntry> history =
          CaptureHistory(CaptureHistory::defaultIndexPath()).entries();
        QVERIFY(!history.isEmpty());
        const HistoryEntry entry = history.first(); // newest
        QCOMPARE(entry.kind, QStringLiteral("gif"));
        QVERIFY(QFileInfo::exists(entry.path));
        QVERIFY2(!entry.project.isEmpty(), "Animate output has no project");
        CaptureProject project;
        QString error;
        QVERIFY2(captureproject::load(entry.project, project, &error),
                 qPrintable(error));
        QVERIFY(project.isAnimation());
        QCOMPARE(project.objects, objects);
        QCOMPARE(project.base.size(), shot.size());
        QCOMPARE(project.anim.value("animStyle").toInt(), int(AnimStyle::Blobs));
        QCOMPARE(project.anim.value("animMotion").toInt(),
                 int(ShotMotion::Float));
        QCOMPARE(project.anim.value("animFormat").toInt(), 1);
        QVERIFY2(project.anim.contains("effectCornerRadius") ||
                   project.anim.keys().filter("effect").size() > 0,
                 "the effect settings used are kept too");

        // The toast offers Edit
        CaptureToast* toast = nullptr;
        for (CaptureToast* t : visibleTopLevels<CaptureToast>()) {
            toast = t;
        }
        QVERIFY2(toast, "no toast for the Animate output");
        QPushButton* edit = nullptr;
        for (QPushButton* b : toast->findChildren<QPushButton*>()) {
            if (b->text() == QObject::tr("Edit")) {
                edit = b;
            }
        }
        QVERIFY(edit);
        QVERIFY2(!edit->isHidden(), "Animate output toast hides Edit");

        // Edit reopens the editor with the annotations as objects; Save
        // writes a PNG, never over the GIF
        editor->close();
        pump(100);
        QTest::mouseClick(edit, Qt::LeftButton);
        pump(200);
        const auto editors = visibleTopLevels<EditorWindow>();
        QCOMPARE(editors.size(), 1);
        EditorWindow* again = editors.first();
        QVERIFY2(again->windowTitle().endsWith(QStringLiteral(".png")),
                 qPrintable(again->windowTitle()));
        auto* reopened = again->findChild<CaptureWidget*>();
        QVERIFY(reopened);
        QCOMPARE(reopened->currentProject().objects, objects);

        // Animate again starts from the settings the output was made with
        config.setAnimStyle(int(AnimStyle::Aurora));
        config.setAnimMotion(int(ShotMotion::None));
        CaptureToolButton* animateAgain = animateButton(again);
        QVERIFY(animateAgain);
        QTest::mouseClick(
          animateAgain, Qt::LeftButton, {}, animateAgain->rect().center());
        AnimPreviewDialog* second = openPreview();
        QVERIFY(second);
        QCOMPARE(second->choice().style, int(AnimStyle::Blobs));
        QCOMPARE(second->choice().motion, int(ShotMotion::Float));
        QCOMPARE(second->choice().format, 1);
        second->reject();
    }

    // Overlay: Export finishes the capture (closing the overlay and with it
    // the dialog) and the animation is still written, editable
    void overlayExportFinishesTheCaptureAndKeepsItEditable()
    {
        ConfigHandler().setAnimFormat(1);
        CaptureRequest req(CaptureRequest::GRAPHICAL_MODE);
        req.addSaveTask(shotsDir() + QStringLiteral("/overlay.png"));
        req.setInitialSelection(QRect(100, 100, 300, 200));
        QPointer<CaptureWidget> overlay = new CaptureWidget(req);
        overlay->show();
        QVERIFY(QTest::qWaitForWindowExposed(overlay));
        const int before =
          CaptureHistory(CaptureHistory::defaultIndexPath()).entries().size();

        CaptureToolButton* button = animateButton(overlay);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton, {}, button->rect().center());
        AnimPreviewDialog* dialog = openPreview();
        QVERIFY2(dialog, "Animate in the overlay must open the preview");
        QVERIFY(overlay->isVisible());
        QTest::mouseClick(
          dialog->findChild<QPushButton*>(QStringLiteral("animPreviewExport")),
          Qt::LeftButton);
        pump(300);
        QVERIFY2(!overlay || !overlay->isVisible(), "the overlay stays open");
        QVERIFY(waitForAnimationJobs());

        const QList<HistoryEntry> entries =
          CaptureHistory(CaptureHistory::defaultIndexPath()).entries();
        QVERIFY(entries.size() > before);
        bool gifWithProject = false;
        for (const HistoryEntry& e : entries.mid(0, entries.size() - before)) {
            gifWithProject = gifWithProject ||
                             (e.kind == QLatin1String("gif") &&
                              QFileInfo::exists(e.project));
        }
        QVERIFY2(gifWithProject, "the overlay's GIF has no project");
    }

    void animationToastShowsEditOnlyWithAProject()
    {
        const QString gif = shotsDir() + QStringLiteral("/missing.gif");
        for (const bool withProject : { true, false }) {
            closeAllWindows();
            QVERIFY(CaptureToast::showForAnimation(
              gif,
              QStringLiteral("gif"),
              withProject ? QStringLiteral("/nowhere/p.fsproj") : QString(),
              30));
            const auto toasts = visibleTopLevels<CaptureToast>();
            QCOMPARE(toasts.size(), 1);
            QPushButton* edit = nullptr;
            for (QPushButton* b : toasts.first()->findChildren<QPushButton*>()) {
                if (b->text() == QObject::tr("Edit")) {
                    edit = b;
                }
            }
            QVERIFY(edit);
            QCOMPARE(!edit->isHidden(), withProject);
        }
    }

    void cleanupTestCase()
    {
        QVERIFY(waitForAnimationJobs());
        closeAllWindows();
    }
};

int main(int argc, char** argv)
{
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
    QSettings::setPath(
      QSettings::IniFormat, QSettings::UserScope, tmp.path() + "/.config");

    QApplication app(argc, argv);
    AppIdentity::apply();
    qRegisterMetaType<QList<int>>();
    app.setQuitOnLastWindowClosed(false);
    TestAnimPreview test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_animpreview.moc"
