// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

#include "utils/backgroundchoice.h"

class QComboBox;
class QHBoxLayout;
class QSlider;
class QSpinBox;
class QToolButton;

// Second row of the editor window's top strip: colour swatches from the
// user's palette, a "+" button for any other colour, a size slider with a
// number box, and the Beautify background (type, its colours and quick
// presets from the built-in palettes). It only emits user intent; the owner
// applies it and calls setColor / setSize / setBackground to reflect changes
// made anywhere else.
class EditorSettingsBar : public QWidget
{
    Q_OBJECT
public:
    explicit EditorSettingsBar(QWidget* parent = nullptr);

    // Fixed height of the bar in logical points
    static int barHeight();

    // Reflect state; never emits the user-intent signals
    void setColor(const QColor& color);
    void setSize(int size);
    void setBackground(const BackgroundChoice& choice);
    BackgroundChoice background() const { return m_background; }

signals:
    void colorPicked(const QColor& color);
    void sizePicked(int size);
    void backgroundPicked(const BackgroundChoice& choice);

private:
    void addBackgroundControls(QHBoxLayout* row);
    void refreshBackground();
    void pickBackgroundColor(int index);
    void rebuildSwatches();
    void pickCustomColor();
    void refreshSelection();

    QHBoxLayout* m_swatchLayout{ nullptr };
    QList<QToolButton*> m_swatches;
    QList<QColor> m_colors;
    QToolButton* m_plus{ nullptr };
    QSlider* m_slider{ nullptr };
    QSpinBox* m_spin{ nullptr };
    QColor m_current;

    BackgroundChoice m_background;
    QComboBox* m_backgroundType{ nullptr };
    QToolButton* m_backgroundSwatches[2]{ nullptr, nullptr };
    QHBoxLayout* m_presetLayout{ nullptr };
    QList<QToolButton*> m_presets;
    BackgroundMode m_presetMode = BackgroundMode::None;
};
