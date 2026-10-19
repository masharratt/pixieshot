// SPDX-License-Identifier: GPL-3.0-or-later

// The Beautify background picker in the windowed editor: changing it
// re-applies Beautify in place (one undo step), never writes the config, and
// Save and Animate use the same colours.
//
// Safety: before Qt starts, HOME, XDG paths and the QSettings user path are
// pointed at a temp dir, and every test refuses to run unless the config file
// resolves inside it. The daemon is hosted in this process, so nothing
// reaches a running Pixieshot.

#include "core/animexportjob.h"
#include "tools/animannotations.h"
#include "tools/capturetool.h"
#include "utils/appidentity.h"
#include "utils/backgroundchoice.h"
#include "utils/confighandler.h"
#include "widgets/capture/capturetoolbutton.h"
#include "widgets/capture/capturewidget.h"
#include "widgets/capture/modificationcommand.h"
#include "widgets/editorsettingsbar.h"
#include "widgets/editorwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUndoStack>

#include <cstdio>

namespace {

QTemporaryDir* g_tmp = nullptr;

bool insideTmp(const QString& path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString root = QDir::cleanPath(g_tmp->path());
    const QString canonicalRoot = QDir(g_tmp->path()).canonicalPath();
    return clean.startsWith(root + "/") || clean.startsWith(canonicalRoot + "/");
}

const QColor kConfigColor1(0x8B, 0x5C, 0xF6);
const QColor kConfigColor2(0x3B, 0x82, 0xF6);
constexpr int kPadding = 40;

QPixmap syntheticShot()
{
    QPixmap shot(200, 150);
    shot.fill(QColor(240, 240, 240));
    QPainter p(&shot);
    p.fillRect(QRect(20, 20, 60, 40), QColor(240, 200, 40));
    p.end();
    return shot;
}

int beautifySteps(const QUndoStack& stack)
{
    int n = 0;
    for (int i = 0; i < stack.count(); ++i) {
        if (stack.command(i)->id() == BeautifyEditCommand::kId) {
            ++n;
        }
    }
    return n;
}

// A background pixel of a beautified image: inside the padding, top-left
QColor backgroundPixel(const QImage& image)
{
    return image.pixelColor(4, 4);
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

bool near(const QColor& a, const QColor& b, int tolerance = 6)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

QString describe(const QColor& c)
{
    return c.name(QColor::HexArgb);
}

BackgroundChoice solid(const QColor& c)
{
    BackgroundChoice choice;
    choice.mode = BackgroundMode::Solid;
    choice.color1 = c;
    choice.color2 = c;
    return choice;
}

} // namespace

class TestEditorBackground : public QObject
{
    Q_OBJECT

private:
    QPointer<EditorWindow> m_editor;
    QPointer<CaptureWidget> m_capture;
    QString m_path;

    void open(bool beautifyOnSave = false)
    {
        const QString out = g_tmp->path() + QStringLiteral("/shots");
        QDir().mkpath(out);
        m_path = out + QStringLiteral("/edit.png");
        QPixmap shot = syntheticShot();
        QVERIFY(shot.save(m_path));
        m_editor = new EditorWindow(m_path, shot, {}, beautifyOnSave);
        m_capture = m_editor->findChild<CaptureWidget*>();
        m_editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_editor));
        QVERIFY(m_capture);
    }

    // Editor grab for the coordinator when EDITORBG_SAMPLES names a folder
    void saveSample(const QString& name)
    {
        const QByteArray dir = qgetenv("EDITORBG_SAMPLES");
        if (!dir.isEmpty()) {
            m_editor->resize(1100, 520);
            pump(100);
            m_editor->grab().save(QString::fromUtf8(dir) + QLatin1Char('/') +
                                  name);
        }
    }

    void configUnchanged()
    {
        ConfigHandler config;
        QCOMPARE(config.effectBackground(), int(BackgroundMode::Gradient));
        QCOMPARE(config.effectBackgroundColor1(), kConfigColor1);
        QCOMPARE(config.effectBackgroundColor2(), kConfigColor2);
    }

private slots:
    void init()
    {
        QVERIFY2(insideTmp(ConfigHandler().configFilePath()),
                 qPrintable(ConfigHandler().configFilePath()));
        ConfigHandler config;
        config.setEffectPadding(kPadding);
        config.setEffectCornerRadius(0);
        config.setEffectShadow(false);
        config.setEffectGlass(false);
        config.setEffectGrain(0);
        config.setEffectAspect(0);
        config.setEffectAutoBalance(false);
        config.setEffectAutoColors(false);
        config.setEffectBorderPx(0);
        config.setEffectBackground(int(BackgroundMode::Gradient));
        config.setEffectBackgroundColor1(kConfigColor1);
        config.setEffectBackgroundColor2(kConfigColor2);
        config.setEffectPalette(QString());
        config.setBeautifyAlways(false);
    }

    void cleanup()
    {
        if (m_editor) {
            m_editor->close();
        }
        pump(100);
    }

    void defaultEqualsConfig()
    {
        open();
        const BackgroundChoice c = m_capture->editorBackground();
        QCOMPARE(int(c.mode), int(BackgroundMode::Gradient));
        QCOMPARE(c.color1, kConfigColor1);
        QCOMPARE(c.color2, kConfigColor2);
        // The bar starts on the same choice
        auto* bar = m_editor->findChild<EditorSettingsBar*>();
        QVERIFY(bar);
        QVERIFY(bar->background() == c);
    }

    void colourChangeReappliesBeautifyAsOneStep()
    {
        open();
        m_capture->handleBeautifyRequest();
        QCOMPARE(beautifySteps(m_capture->undoStack()), 1);
        const int steps = m_capture->undoStack().count();
        const QColor before = backgroundPixel(m_capture->currentEditImage());
        QVERIFY2(near(before, kConfigColor1, 30), qPrintable(describe(before)));
        saveSample(QStringLiteral("editor-before.png"));

        m_capture->setEditorBackground(solid(QColor(Qt::red)));
        const QColor after = backgroundPixel(m_capture->currentEditImage());
        QVERIFY2(near(after, QColor(Qt::red)), qPrintable(describe(after)));
        saveSample(QStringLiteral("editor-after-red.png"));
        // Replaced, not stacked
        QCOMPARE(m_capture->undoStack().count(), steps);
        QCOMPARE(beautifySteps(m_capture->undoStack()), 1);

        m_capture->setEditorBackground(solid(QColor(Qt::green)));
        QVERIFY(near(backgroundPixel(m_capture->currentEditImage()),
                     QColor(Qt::green)));
        QCOMPARE(m_capture->undoStack().count(), steps);

        // Undo takes Beautify off; redo brings back the latest colour
        m_capture->undoStack().undo();
        QCOMPARE(m_capture->currentEditImage().size(),
                 syntheticShot().size());
        m_capture->undoStack().redo();
        QVERIFY(near(backgroundPixel(m_capture->currentEditImage()),
                     QColor(Qt::green)));
        configUnchanged();
    }

    void choiceBeforeBeautifyIsUsedByBeautify()
    {
        open();
        m_capture->setEditorBackground(solid(QColor(Qt::blue)));
        QCOMPARE(beautifySteps(m_capture->undoStack()), 0);
        m_capture->handleBeautifyRequest();
        QVERIFY(near(backgroundPixel(m_capture->currentEditImage()),
                     QColor(Qt::blue)));
        configUnchanged();
    }

    void colourChangeAfterLaterStepsKeepsThem()
    {
        open();
        m_capture->handleBeautifyRequest();
        // A later step on top (an annotation-list change)
        m_capture->undoStack().push(new QUndoCommand());
        const int steps = m_capture->undoStack().count();
        m_capture->setEditorBackground(solid(QColor(Qt::red)));
        QVERIFY(near(backgroundPixel(m_capture->currentEditImage()),
                     QColor(Qt::red)));
        QCOMPARE(m_capture->undoStack().count(), steps);
        QCOMPARE(m_capture->undoStack().index(), steps);
        configUnchanged();
    }

    void animateSourceCarriesTheOverride()
    {
        open();
        QVERIFY(m_capture->editAnimSource().effectOverlay.isEmpty());
        m_capture->handleBeautifyRequest();
        m_capture->setEditorBackground(solid(QColor(Qt::red)));
        const AnimSource source = m_capture->editAnimSource();
        QCOMPARE(source.effectOverlay,
                 backgroundchoice::overlayOf(solid(QColor(Qt::red))));
        // Animate applies the effects itself, so it starts un-beautified
        QCOMPARE(source.base.size(), syntheticShot().size());
        // Preview and export both build their frames with paramsFor
        const AnimRenderParams params = AnimExportJob::paramsFor(
          animannotations::prepare(source), AnimChoice());
        QCOMPARE(int(params.effects.backgroundMode), int(BackgroundMode::Solid));
        QCOMPARE(params.effects.backgroundColor1, QColor(Qt::red));
        QCOMPARE(params.effects.paddingPx, kPadding);
        configUnchanged();
    }

    void saveUsesTheOverride()
    {
        open(true);
        m_capture->setEditorBackground(solid(QColor(Qt::red)));
        CaptureToolButton* save = nullptr;
        for (CaptureToolButton* b : m_editor->findChildren<CaptureToolButton*>()) {
            if (b->tool()->type() == CaptureTool::TYPE_ACCEPT) {
                save = b;
            }
        }
        QVERIFY(save);
        QTest::mouseClick(save, Qt::LeftButton, {}, save->rect().center());
        pump(500);
        QImage saved(m_path);
        QVERIFY(!saved.isNull());
        QVERIFY2(saved.width() > syntheticShot().width(),
                 "saved without Beautify");
        QVERIFY2(near(backgroundPixel(saved), QColor(Qt::red)),
                 qPrintable(describe(backgroundPixel(saved))));
        configUnchanged();
    }

    void barPresetPicksTheBackground()
    {
        open();
        m_capture->handleBeautifyRequest();
        auto* bar = m_editor->findChild<EditorSettingsBar*>();
        QVERIFY(bar);
        auto* preset = bar->findChild<QToolButton*>(
          QStringLiteral("backgroundPreset0"));
        QVERIFY(preset);
        const QColor before = backgroundPixel(m_capture->currentEditImage());
        QTest::mouseClick(preset, Qt::LeftButton, {}, preset->rect().center());
        const BackgroundChoice picked = m_capture->editorBackground();
        QVERIFY(picked == backgroundchoice::quickPresets(
                            BackgroundMode::Gradient)
                            .first()
                            .choice);
        QVERIFY(bar->background() == picked);
        QVERIFY(backgroundPixel(m_capture->currentEditImage()) != before);
        QCOMPARE(beautifySteps(m_capture->undoStack()), 1);
        saveSample(QStringLiteral("editor-preset-sunset.png"));

        // Switching the type to solid shows one swatch
        auto* type = bar->findChild<QComboBox*>(QStringLiteral("backgroundType"));
        QVERIFY(type);
        type->setCurrentIndex(type->findData(int(BackgroundMode::Solid)));
        QCOMPARE(int(m_capture->editorBackground().mode),
                 int(BackgroundMode::Solid));
        auto* end = bar->findChild<QToolButton*>(QStringLiteral("backgroundColor2"));
        QVERIFY(end);
        QVERIFY(end->isHidden());
        configUnchanged();
        saveSample(QStringLiteral("editor-solid-type.png"));
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
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       tmp.path() + QStringLiteral("/.config"));

    QApplication app(argc, argv);
    AppIdentity::apply();
    qRegisterMetaType<QList<int>>();
    app.setQuitOnLastWindowClosed(false);
    TestEditorBackground test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_editorbackground.moc"
