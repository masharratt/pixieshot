// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

// Based on Lightscreen areadialog.h, Copyright 2017  Christian Kaiser
// <info@ckaiser.com.ar> released under the GNU GPL2
// <https://www.gnu.org/licenses/gpl-2.0.txt>

// Based on KDE's KSnapshot regiongrabber.cpp, revision 796531, Copyright 2007
// Luca Gugelmann <lucag@student.ethz.ch> released under the GNU LGPL
// <http://www.gnu.org/licenses/old-licenses/library.txt>

#pragma once

#include "tools/capturecontext.h"
#include "tools/capturetool.h"
#include "utils/annotationlayer.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"
#include "platform/windowlist.h"
#include "widgets/capture/buttonhandler.h"
#include "widgets/capture/capturetoolbutton.h"
#include "widgets/capture/capturetoolobjects.h"
#include "widgets/capture/magnifierwidget.h"
#include "widgets/capture/modificationcommand.h"
#include "widgets/capture/selectionwidget.h"
#include "widgets/editcanvas.h"

#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QWidget>

class AbstractTwoPointTool;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QMouseEvent;
class QShortcut;
class QNetworkAccessManager;
class QNetworkReply;
class ColorPicker;
class NotifierBox;
class HoverEventFilter;
#if !defined(DISABLE_UPDATE_CHECKER)
class UpdateNotificationWidget;
#endif
class UtilityPanel;
class SidePanelWidget;

class CaptureWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CaptureWidget(const CaptureRequest& req,
                           bool fullScreen = true,
                           QWidget* parent = nullptr);
    ~CaptureWidget();

    QPixmap pixmap();
    void setCaptureToolObjects(const CaptureToolObjects& captureToolObjects);
#if !defined(DISABLE_UPDATE_CHECKER)
    void showAppUpdateNotification(const QString& appLatestVersion,
                                   const QString& appLatestUrl);
#endif

public slots:
    bool commitCurrentTool();
    void deleteToolWidgetOrClose();
    // Same effect as changing colour / size in the side panel
    void setDrawColor(const QColor& c);
    void applyToolSize(int size);
    // Deactivate the current drawing tool so clicks select and move objects.
    // No-op when no tool is active.
    void deactivateTool();

signals:
    // The active drawing tool changed; false when none is active (select mode)
    void toolActiveChanged(bool active);
    void colorChanged(const QColor& c);
    void toolSizeChanged(int size);
    // Windowed editor: the canvas grew or shrank (logical size)
    void editCanvasResized(const QSize& canvasLogical);

private slots:
    void undo();
    void redo();
    void cancel();
    void togglePanel();
    void childEnter();
    void childLeave();

    void deleteCurrentTool();

    void setState(CaptureToolButton* b);
    void handleToolSignal(CaptureTool::Request r);
    void handleButtonLeftClick(CaptureToolButton* b);
    void handleButtonRightClick(CaptureToolButton* b);
    void onToolSizeChanged(int size);
    void onToolSizeSettled(int size);
    void updateActiveLayer(int layer);
    void onMoveCaptureToolUp(int captureToolIndex);
    void onMoveCaptureToolDown(int captureToolIndex);
    void selectAll();
    void xywhTick();
    void onDisplayGridChanged(bool display);
    void onGridSizeChanged(int size);

    void startColorGrab();

public:
    void removeToolObject(int index = -1);
    void showxywh();

    // Windowed editor hooks: the tool buttons and the side panel pieces so the
    // host window can place them outside the scrolled canvas.
    QVector<CaptureToolButton*> toolbarButtons() const;
    QWidget* sidePanel() const;
    // Current draw colour and tool size (for the editor settings bar)
    QColor drawColor() const;
    int toolSize() const;
    QWidget* sidePanelToggle() const;

    // Re-editable project of what is on screen now: the base image (no
    // annotations, no effects) and the committed annotations relative to it
    CaptureProject currentProject();
    // Windowed editor: adds saved annotations over the image, still
    // movable and editable. Returns one message per skipped entry.
    QStringList restoreObjects(const QJsonArray& objects);

    // Windowed editor: add white space on every edge, undoable. Does nothing
    // outside edit mode.
    void expandEditCanvas(int stepLogical = kExpandStepLogical);
    // Applies (or with a negative step reverts) an expansion. Called by the
    // undo command, not meant for direct use.
    void applyCanvasExpansion(int stepLogical);
    // Windowed editor: puts back a canvas saved by the Beautify undo
    // command, not meant for direct use.
    void restoreEditCanvasState(const EditCanvasState& state);

protected:
    void paintEvent(QPaintEvent* paintEvent) override;
    void mousePressEvent(QMouseEvent* mouseEvent) override;
    void showEvent(QShowEvent* event) override;
    void mouseMoveEvent(QMouseEvent* mouseEvent) override;
    void mouseReleaseEvent(QMouseEvent* mouseEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* keyEvent) override;
    void keyReleaseEvent(QKeyEvent* keyEvent) override;
    void wheelEvent(QWheelEvent* wheelEvent) override;
    void resizeEvent(QResizeEvent* resizeEvent) override;
    void moveEvent(QMoveEvent* moveEvent) override;
    void changeEvent(QEvent* changeEvent) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void pushObjectsStateToUndoStack();
    void releaseActiveTool();
    void uncheckActiveTool();
    int selectToolItemAtPos(const QPoint& pos);
    void showColorPicker(const QPoint& pos);
    bool startDrawObjectTool(const QPoint& pos);
    QPointer<CaptureTool> activeToolObject();
    void initContext(bool fullscreen, const CaptureRequest& req);
    void initPanel();
    void initSelection();
    void initShortcuts();
    void initButtons();
    void initHelpMessage();
    void initQuitPrompt();
    void updateSizeIndicator();
    void updateCursor();
    void updateSelectionState();
    void updateTool(CaptureTool* tool);
    void updateLayersPanel();
    bool promptQuit();
    void pushToolToStack();
    void makeChild(QWidget* w);
    void restoreCircleCountState();

    QList<QShortcut*> newShortcut(const QKeySequence& key,
                                  QWidget* parent,
                                  const char* slot);

    void setToolSize(int size);

    QRect extendedSelection() const;
    QRect extendedRect(const QRect& r) const;
    QRect paddedUpdateRect(const QRect& r) const;
    void drawErrorMessage(const QString& msg, QPainter* painter);
    void drawInactiveRegion(QPainter* painter);
    void updateHoverWindow(const QPoint& localPos);
    bool hoverWindowActive() const;
    // Windowed edit mode: builds the canvas (image plus margin) from the
    // request's edit image
    void initEditCanvas(const QPixmap& image);
    // Windowed editor Beautify: flattens the annotations into the image and
    // replaces it with the beautified result, as one undoable step
    void beautifyEditImage();
    EditCanvasState currentEditCanvasState();
    // Beautify button, overlay or editor (see beautifyActionFor)
    void handleBeautifyRequest();
    // Animate button: finish and export (overlay) or export in place
    void handleAnimateRequest();
    // What Animate starts from in an editor state: the image without its
    // annotations, cut to `crop` (device pixels inside the image; null for
    // all of it), and the annotations as objects relative to that
    AnimSource animSourceOf(const EditCanvasState& state, const QRect& crop);
    void drawPickerSizeLabel(QPainter* painter);
    void selectHoveredWindow();
    void drawToolsData(bool drawSelection = true);
    void drawObjectSelection();
    AbstractTwoPointTool* bendableObjectWithHandleAt(const QPoint& pos);
    void dragBendHandle(const QPoint& pos);
    AbstractTwoPointTool* bendTarget();

    void processPixmapWithTool(QPixmap* pixmap, CaptureTool* tool);

    CaptureTool* activeButtonTool() const;
    CaptureTool::Type activeButtonToolType() const;

    QPoint snapToGrid(const QPoint& point) const;

    ////////////////////////////////////////
    // Class members

    // Context information
    CaptureContext m_context;

    // Main ui color
    QColor m_uiColor;
    // Secondary ui color
    QColor m_contrastUiColor;

    // Outside selection opacity
    int m_opacity;
    int m_toolSizeByKeyboard;

    // utility flags
    bool m_mouseIsClicked;
    bool m_newSelection;
    bool m_movingSelection;
    bool m_captureDone;
    bool m_previewEnabled;
    bool m_adjustmentButtonPressed;
    bool m_configError;
    bool m_configErrorResolved;

#if !defined(DISABLE_UPDATE_CHECKER)
    UpdateNotificationWidget* m_updateNotificationWidget;
#endif
    quint64 m_lastMouseWheel;
    QPointer<CaptureToolButton> m_sizeIndButton;
    // Last pressed button
    QPointer<CaptureToolButton> m_activeButton;
    QPointer<CaptureTool> m_activeTool;
    // A button's own tool while its pressed() runs (see setState). It is
    // m_activeTool then, so tool requests reach it, but the button owns it:
    // releaseActiveTool() must never delete it.
    CaptureTool* m_pressedButtonTool = nullptr;
    bool m_activeToolIsMoved;
    // True while the bend handle of the selected arrow/line is dragged
    bool m_bendDragging = false;
    // Index of the last arrow/line drawn, bendable while its tool is still
    // active. An index, not a pointer: undo snapshots replace every object
    // with a copy, which would leave a pointer stale.
    int m_lastBendableIndex = -1;
    // True while a bend handle is painted into m_context.screenshot; it must
    // be cleared before the screenshot is exported
    bool m_bendHandleShown = false;
    QPointer<QWidget> m_toolWidget;
    QPointer<QMessageBox> m_quitPrompt;

    ButtonHandler* m_buttonHandler;
    UtilityPanel* m_panel;
    QWidget* m_panelToggleButton{ nullptr };
    SidePanelWidget* m_sidePanel;
    ColorPicker* m_colorPicker;
    ConfigHandler m_config;
    NotifierBox* m_notifierBox;
    HoverEventFilter* m_eventFilter;
    SelectionWidget* m_selection;
    MagnifierWidget* m_magnifier;
    QString m_helpMessage;

    SelectionWidget::SideType m_mouseOverHandle;

    QMap<CaptureTool::Type, CaptureTool*> m_tools;
    CaptureToolObjects m_captureToolObjects;
    CaptureToolObjects m_captureToolObjectsBackup;

    QPoint m_mousePressedPos;
    QPoint m_activeToolOffsetToMouseOnStart;

    // XYWH display position and timer
    bool m_xywhDisplay;
    QTimer m_xywhTimer;

    QUndoStack m_undoStack;

    bool m_existingObjectIsChanged;

    // For start moving after more than X offset
    QPoint m_startMovePos;
    bool m_startMove;

    // Grid
    bool m_displayGrid{ false };
    int m_gridSize{ 10 };

    bool m_clipboardWorkaroundDone{ false };

    // Hover-to-window detection. Window bounds are global logical points;
    // m_hoverRect is overlay-local logical points (widget coordinates, the
    // same space as m_selection->geometry()), never device pixels.
    QList<WindowInfo> m_windowCandidates;
    bool m_hoverEnabled{ false };
    QRect m_hoverRect;
    bool m_windowPressPending{ false };

    // ShareX-style picker look (see pickerstyle.h)
    bool m_pickerStyle{ false };

    // Windowed edit mode. The image sits inside the canvas screenshot; the
    // selection is exactly the image rect and cannot change.
    // m_editImageRectLogical is logical points (widget coordinates),
    // m_editImageRectDevice is device pixels in m_context.screenshot.
    bool m_editMode{ false };
    QRect m_editImageRectLogical;
    QRect m_editImageRectDevice;
};
