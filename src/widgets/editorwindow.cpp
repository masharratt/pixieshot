// SPDX-License-Identifier: GPL-3.0-or-later

#include "editorwindow.h"
#include "core/capturerequest.h"
#include "core/flameshot.h"
#include "utils/captureproject.h"
#include "utils/globalvalues.h"
#include "utils/colorutils.h"
#include "utils/confighandler.h"
#include "utils/pathinfo.h"
#include "widgets/capture/capturebutton.h"
#include "widgets/capture/capturetoolbutton.h"
#include "widgets/capture/capturewidget.h"
#include "widgets/editcanvas.h"
#include "widgets/editorsettingsbar.h"
#include "platform/windowspace.h"

#include <QCursor>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QIcon>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>

namespace {
// Toolbar strip metrics in logical points
constexpr int kToolbarPad = 6;
constexpr int kToolbarSpacing = 4;
} // namespace

EditorWindow::EditorWindow(const QString& path,
                           const QPixmap& image,
                           const QJsonArray& objects,
                           bool beautifyOnSave)
  : QWidget(nullptr, Qt::Window | Qt::WindowMinMaxButtonsHint)
  , m_image(image)
  , m_path(path)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(tr("Edit - %1").arg(QFileInfo(path).fileName()));

    // Edits always overwrite the opened file; the image is never resampled
    CaptureRequest req(CaptureRequest::GRAPHICAL_MODE);
    req.setEditImage(image);
    req.addSaveTask(path);
    req.setOverwriteExisting(true);
    if (beautifyOnSave) {
        req.addTask(CaptureRequest::BEAUTIFY);
    }

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("editorToolbar"));
    m_toolbar->setAttribute(Qt::WA_StyledBackground, true);
    m_toolbar->setStyleSheet(
      QStringLiteral("#editorToolbar { background: #2b2b2b; }"));

    auto* scroll = new QScrollArea(this);
    m_scroll = scroll;
    scroll->setObjectName(QStringLiteral("editorScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
      QStringLiteral("#editorScroll, #editorScroll > QWidget { background: "
                     "#2b2b2b; }"));
    m_capture = new CaptureWidget(req, false);
    scroll->setWidget(m_capture);
    if (!objects.isEmpty()) {
        m_restoreWarnings = m_capture->restoreObjects(objects);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toolbar);
    layout->addWidget(scroll, 1);

    // Move the tool buttons into the fixed strip
    m_buttons = m_capture->toolbarButtons();
    for (CaptureToolButton* b : m_buttons) {
        b->setParent(m_toolbar);
        b->setFixedSize(GlobalValues::buttonBaseSize(),
                        GlobalValues::buttonBaseSize());
        b->show();
    }
    addSelectButton();
    addTrailingButtons();
    addSettingsBar();
    m_toolbar->setFixedHeight(toolbarHeightFor(width()));
    layoutToolbar();
    dockSidePanel();

    new QShortcut(QKeySequence::Close, this, SLOT(close()));
    // V selects and E expands the canvas, unless the user is typing
    auto* selectShortcut = new QShortcut(QKeySequence(Qt::Key_V), this);
    connect(selectShortcut, &QShortcut::activated, this, [this]() {
        if (m_capture && !typingInField()) {
            m_capture->deactivateTool();
        }
    });
    auto* expandShortcut = new QShortcut(QKeySequence(Qt::Key_E), this);
    connect(expandShortcut, &QShortcut::activated, this, [this]() {
        if (m_capture && !typingInField()) {
            m_capture->expandEditCanvas();
        }
    });
    // Cmd+Shift+S and Cmd+O on macOS
    auto* saveProjectShortcut = new QShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this);
    connect(saveProjectShortcut,
            &QShortcut::activated,
            this,
            &EditorWindow::saveProject);
    auto* openProjectShortcut =
      new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), this);
    connect(openProjectShortcut,
            &QShortcut::activated,
            this,
            &EditorWindow::openProject);

    // Esc, Save and the other finishing tools close the CaptureWidget
    connect(m_capture, &QObject::destroyed, this, [this]() { close(); });
    connect(m_capture,
            &CaptureWidget::editCanvasResized,
            this,
            &EditorWindow::growToCanvas);
    connect(m_capture,
            &CaptureWidget::toolActiveChanged,
            this,
            [this](bool active) { setSelectChecked(!active); });
}

// The first button of the strip. Not a capture tool: it drops the active
// drawing tool so clicks select and move existing objects. It shows checked
// whenever no drawing tool is active.
void EditorWindow::addSelectButton()
{
    m_selectButton = new CaptureButton(m_toolbar);
    m_selectButton->setStyleSheet(m_selectButton->styleSheet() +
                                  QStringLiteral(" CaptureButton { padding: "
                                                 "0; }"));
    const int size = GlobalValues::buttonBaseSize();
    m_selectButton->setFixedSize(size, size);
    m_selectButton->setMask(
      QRegion(QRect(-1, -1, size + 2, size + 2), QRegion::Ellipse));
    m_selectButton->setIconSize(QSize(size, size) * 0.6);
    m_selectButton->setToolTip(tr("Select and move (V)"));
    connect(m_selectButton, &QPushButton::clicked, this, [this]() {
        if (m_capture) {
            m_capture->deactivateTool();
        }
    });
    m_selectButton->show();
    // No tool is active when the editor opens
    setSelectChecked(true);
}

void EditorWindow::setSelectChecked(bool checked)
{
    if (!m_selectButton) {
        return;
    }
    ConfigHandler config;
    const QColor ui = checked ? config.contrastUiColor() : config.uiColor();
    m_selectButton->setColor(ui);
    m_selectButton->setStyleSheet(m_selectButton->styleSheet() +
                                  QStringLiteral(" CaptureButton { padding: "
                                                 "0; }"));
    const QString iconDir = ColorUtils::colorIsDark(ui)
                              ? PathInfo::whiteIconPath()
                              : PathInfo::blackIconPath();
    m_selectButton->setIcon(QIcon(iconDir + QStringLiteral("cursor-default.svg")));
}

// A button in the strip after the tool buttons. It is not a capture tool: it
// calls straight into the editor, so the overlay never sees it.
CaptureButton* EditorWindow::addTrailingButton(
  const QString& iconName,
  const QString& toolTip,
  const std::function<void()>& handler)
{
    const QColor ui = ConfigHandler().uiColor();
    auto* button = new CaptureButton(m_toolbar);
    button->setColor(ui);
    button->setStyleSheet(button->styleSheet() +
                          QStringLiteral(" CaptureButton { padding: 0; }"));
    const int size = GlobalValues::buttonBaseSize();
    button->setFixedSize(size, size);
    button->setMask(
      QRegion(QRect(-1, -1, size + 2, size + 2), QRegion::Ellipse));
    const QString iconDir = ColorUtils::colorIsDark(ui)
                              ? PathInfo::whiteIconPath()
                              : PathInfo::blackIconPath();
    button->setIcon(QIcon(iconDir + iconName));
    button->setIconSize(QSize(size, size) * 0.6);
    button->setToolTip(toolTip);
    connect(button, &QPushButton::clicked, this, handler);
    button->show();
    m_trailingButtons.append(button);
    return button;
}

void EditorWindow::addTrailingButtons()
{
    addTrailingButton(
      QStringLiteral("expand-canvas.svg"), tr("Expand canvas (E)"), [this]() {
          if (m_capture) {
              m_capture->expandEditCanvas();
          }
      });
    addTrailingButton(QStringLiteral("project-save.svg"),
                      tr("Save project, annotations stay editable (%1)")
                        .arg(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)
                               .toString(QKeySequence::NativeText)),
                      [this]() { saveProject(); });
    addTrailingButton(QStringLiteral("project-open.svg"),
                      tr("Open project (%1)")
                        .arg(QKeySequence(Qt::CTRL | Qt::Key_O)
                               .toString(QKeySequence::NativeText)),
                      [this]() { openProject(); });
}

// Writes the base image and the annotations to a .fsproj file. A text box
// still being typed in is committed first so it is included.
void EditorWindow::saveProject()
{
    if (!m_capture) {
        return;
    }
    m_capture->commitCurrentTool();
    const QString suffix = captureproject::fileSuffix();
    QString suggested = QFileInfo(m_path).absolutePath() + QLatin1Char('/') +
                        QFileInfo(m_path).completeBaseName() +
                        QLatin1Char('.') + suffix;
    QString path = QFileDialog::getSaveFileName(
      this,
      tr("Save Project"),
      suggested,
      tr("Pixieshot project (*.%1)").arg(suffix));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QLatin1Char('.') + suffix;
    }
    QString error;
    if (!captureproject::save(path, m_capture->currentProject(), &error)) {
        QMessageBox::warning(this, tr("Save Project"), error);
    }
}

// Opens a .fsproj file in a new editor window. Saving there writes a PNG
// next to the project file.
void EditorWindow::openProject()
{
    const QString suffix = captureproject::fileSuffix();
    const QString path = QFileDialog::getOpenFileName(
      this,
      tr("Open Project"),
      QFileInfo(m_path).absolutePath(),
      tr("Pixieshot project (*.%1)").arg(suffix));
    if (path.isEmpty()) {
        return;
    }
    CaptureProject project;
    QString error;
    if (!captureproject::load(path, project, &error)) {
        QMessageBox::warning(this, tr("Open Project"), error);
        return;
    }
    const QFileInfo info(path);
    Flameshot::instance()->editProject(
      info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
        QStringLiteral(".png"),
      project);
}

// Colour and size row under the tool buttons. Edits go to the CaptureWidget
// slots the side panel uses; its signals keep the bar in sync with changes
// made anywhere else (wheel, radial picker, shortcuts, side panel).
void EditorWindow::addSettingsBar()
{
    m_settingsBar = new EditorSettingsBar(m_toolbar);
    m_settingsBar->setColor(m_capture->drawColor());
    m_settingsBar->setSize(m_capture->toolSize());
    connect(m_settingsBar,
            &EditorSettingsBar::colorPicked,
            m_capture,
            &CaptureWidget::setDrawColor);
    connect(m_settingsBar,
            &EditorSettingsBar::sizePicked,
            m_capture,
            &CaptureWidget::applyToolSize);
    connect(m_capture,
            &CaptureWidget::colorChanged,
            m_settingsBar,
            &EditorSettingsBar::setColor);
    connect(m_capture,
            &CaptureWidget::toolSizeChanged,
            m_settingsBar,
            &EditorSettingsBar::setSize);
    m_settingsBar->show();
}

void EditorWindow::growToCanvas(const QSize& canvasLogical)
{
    QScreen* screen = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen->availableGeometry();
    const QSize target(
      std::min(canvasLogical.width(), available.width() * 9 / 10),
      std::min(canvasLogical.height() + m_toolbar->height(),
               available.height() * 9 / 10));
    const QSize grown(std::max(width(), target.width()),
                      std::max(height(), target.height()));
    if (grown != size()) {
        resize(grown);
    }
}

EditorWindow::~EditorWindow()
{
    if (m_capture) {
        m_capture->disconnect(this);
    }
}

int EditorWindow::toolbarHeightFor(int width) const
{
    return buttonRowsHeightFor(width) + EditorSettingsBar::barHeight();
}

int EditorWindow::buttonRowsHeightFor(int width) const
{
    const int size = GlobalValues::buttonBaseSize();
    const int rows = toolbarRows(
      stripCount(), size, kToolbarSpacing, width - 2 * kToolbarPad);
    if (rows == 0) {
        return 0;
    }
    return rows * size + (rows - 1) * kToolbarSpacing + 2 * kToolbarPad;
}

int EditorWindow::stripCount() const
{
    return m_buttons.size() + (m_selectButton ? 1 : 0) +
           m_trailingButtons.size();
}

// One row of buttons that wraps to further rows when the window is narrow
void EditorWindow::layoutToolbar()
{
    const int size = GlobalValues::buttonBaseSize();
    const int usable = std::max(1, width() - 2 * kToolbarPad);
    const int perRow =
      std::max(1, (usable + kToolbarSpacing) / (size + kToolbarSpacing));
    int i = 0;
    auto place = [&](QWidget* w) {
        w->move(kToolbarPad + (i % perRow) * (size + kToolbarSpacing),
                kToolbarPad + (i / perRow) * (size + kToolbarSpacing));
        ++i;
    };
    if (m_selectButton) {
        place(m_selectButton);
    }
    for (CaptureToolButton* b : m_buttons) {
        place(b);
    }
    for (CaptureButton* b : m_trailingButtons) {
        place(b);
    }
    if (m_settingsBar) {
        const int y = buttonRowsHeightFor(width());
        m_settingsBar->setGeometry(0, y, width(), EditorSettingsBar::barHeight());
    }
}

// The panel and its toggle become children of the scroll viewport, so they
// stay at the left edge while the canvas scrolls underneath.
void EditorWindow::dockSidePanel()
{
    QWidget* viewport = m_scroll->viewport();
    viewport->installEventFilter(this);
    for (QWidget* w : { m_capture->sidePanel(), m_capture->sidePanelToggle() }) {
        if (w) {
            w->setParent(viewport);
        }
    }
    if (QWidget* toggle = m_capture->sidePanelToggle()) {
        toggle->show();
    }
}

bool EditorWindow::typingInField()
{
    QWidget* focus = QApplication::focusWidget();
    return qobject_cast<QTextEdit*>(focus) ||
           qobject_cast<QLineEdit*>(focus) ||
           qobject_cast<QPlainTextEdit*>(focus) ||
           qobject_cast<QAbstractSpinBox*>(focus);
}

bool EditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (m_capture && watched == m_scroll->viewport() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        QWidget* viewport = m_scroll->viewport();
        QWidget* panel = m_capture->sidePanel();
        QWidget* toggle = m_capture->sidePanelToggle();
        if (panel) {
            panel->setFixedHeight(viewport->height());
            panel->move(0, 0);
            panel->raise();
        }
        if (toggle) {
            toggle->move(0, (viewport->height() - toggle->height()) / 2);
            toggle->raise();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EditorWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_toolbar->setFixedHeight(toolbarHeightFor(width()));
    layoutToolbar();
}

void EditorWindow::present()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen->availableGeometry();
    const qreal dpr = m_image.devicePixelRatio();
    // The toolbar height depends on the window width, so plan twice: once for
    // the width, then again with the strip height added to the window
    const EditCanvas widthPlan =
      planEditCanvas(m_image.size(), dpr, kEditMargin, available);
    const EditCanvas plan =
      planEditCanvas(m_image.size(),
                     dpr,
                     kEditMargin,
                     available,
                     toolbarHeightFor(widthPlan.windowSize.width()));
    resize(plan.windowSize);
    QRect frame(QPoint(0, 0), plan.windowSize);
    frame.moveCenter(available.center());
    move(frame.topLeft());
    showOnActiveSpace(this);
    show();
    raise();
    activateWindow();
    if (m_capture) {
        m_capture->setFocus();
    }
    if (!m_restoreWarnings.isEmpty()) {
        const QStringList warnings = m_restoreWarnings;
        m_restoreWarnings.clear();
        QTimer::singleShot(0, this, [this, warnings]() {
            QMessageBox::warning(
              this,
              tr("Edit Again"),
              tr("Some annotations could not be restored:\n%1")
                .arg(warnings.join(QLatin1Char('\n'))));
        });
    }
}
