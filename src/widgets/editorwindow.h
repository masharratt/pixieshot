// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class CaptureButton;
class CaptureToolButton;
class CaptureWidget;
class EditorSettingsBar;
class QScrollArea;
struct CaptureProject;

// A normal top-level window that edits a saved capture. It hosts a
// CaptureWidget in windowed edit mode inside a scroll area and overwrites the
// file when the edit is saved. Closing the window discards unsaved edits.
// The tool buttons live in a fixed strip above the scroll area and the side
// panel is docked to the left edge of the viewport, so neither scrolls away
// with a large canvas. Save Project and Open Project keep the annotations
// editable in a .fsproj file.
class EditorWindow : public QWidget
{
    Q_OBJECT
public:
    // image is in device pixels; its device pixel ratio is the canvas ratio.
    // objects are saved annotations (see CaptureProject) placed back over the
    // image, still editable.
    EditorWindow(const QString& path,
                 const QPixmap& image,
                 const QJsonArray& objects = {},
                 // The file was Beautified on export: save it that way again
                 bool beautifyOnSave = false);
    ~EditorWindow() override;

    // Size and center the window on the screen under the cursor, then show it
    void present();
    // Opened from an "Animate" output: its settings (see CaptureProject::anim)
    // preset the Animate preview
    void setAnimSettings(const QJsonObject& settings);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // True while the user types into a text object or an input field, so
    // single-key shortcuts must not fire
    static bool typingInField();

private:
    // Strip height for the current buttons laid out in `width` points
    int toolbarHeightFor(int width) const;
    int stripCount() const;
    void layoutToolbar();
    void dockSidePanel();
    void addSelectButton();
    // A plain button at the end of the strip that calls `handler`
    CaptureButton* addTrailingButton(const QString& iconName,
                                     const QString& toolTip,
                                     const std::function<void()>& handler);
    void addTrailingButtons();
    void saveProject();
    void openProject();
    void setSelectChecked(bool checked);
    void addSettingsBar();
    // Height of the button rows alone, without the settings row
    int buttonRowsHeightFor(int width) const;
    // Grow the window (never shrink it) so a larger canvas shows, up to 90%
    // of the screen; beyond that the scroll area takes over.
    void growToCanvas(const QSize& canvasLogical);

    QPointer<CaptureWidget> m_capture;
    QScrollArea* m_scroll{ nullptr };
    QWidget* m_toolbar{ nullptr };
    QVector<CaptureToolButton*> m_buttons;
    CaptureButton* m_selectButton{ nullptr };
    // Expand canvas, Save Project, Open Project
    QVector<CaptureButton*> m_trailingButtons;
    // Annotations of the opened project that could not be restored
    QStringList m_restoreWarnings;
    EditorSettingsBar* m_settingsBar{ nullptr };
    QPixmap m_image;
    // The image file a Save overwrites
    QString m_path;
};
