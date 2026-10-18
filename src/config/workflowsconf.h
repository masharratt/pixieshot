// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/actionlist.h"
#include "utils/imageeffects.h"

#include <QMap>
#include <QWidget>

#include <QColor>
#include <QVector>

#include <memory>

class AnimatedFrames;
class AnnotationLayer;
class QTimer;

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

// Settings tab: which after-capture actions each workflow hotkey runs.
class WorkflowsConf : public QWidget
{
    Q_OBJECT
public:
    explicit WorkflowsConf(QWidget* parent = nullptr);
    ~WorkflowsConf() override;

public slots:
    void updateComponents();

private:
    void addWorkflow(const QString& hotkey, const QString& title);
    void onToggled(const QString& hotkey);
    void addEffectsGroup();
    void addRecordingGroup();
    void onRecordingChanged();
    void onEffectChanged();
    void onPalettePresetChanged();
    // Shows the preset the current palette matches, or Custom
    void syncPalettePreset();
    void updatePreview();
    // The Beautify settings as currently shown (not yet saved values)
    EffectSettings previewSettings() const;
#if defined(Q_OS_MACOS)
    void addAnimateGroup();
    void onAnimChanged();
    void rebuildAnimPreview();
    void tickAnimPreview();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
#endif

    struct Row
    {
        QLabel* shortcut = nullptr;
        QMap<CaptureAction, QCheckBox*> boxes;
    };
    QMap<QString, Row> m_rows;
    bool m_updating = false;

    QSpinBox* m_borderPx = nullptr;
    QPushButton* m_borderColor = nullptr;
    QSpinBox* m_cornerRadius = nullptr;
    QCheckBox* m_shadow = nullptr;
    QSpinBox* m_padding = nullptr;
    QComboBox* m_background = nullptr;
    QComboBox* m_palettePreset = nullptr;
    QPushButton* m_backgroundColor1 = nullptr;
    QPushButton* m_backgroundColor2 = nullptr;
    QComboBox* m_aspect = nullptr;
    QCheckBox* m_autoBalance = nullptr;
    QCheckBox* m_autoColors = nullptr;
    QCheckBox* m_glass = nullptr;
    QSpinBox* m_glassPx = nullptr;
    QSlider* m_grain = nullptr;
    QCheckBox* m_beautifyAlways = nullptr;
    QLabel* m_preview = nullptr;

    // Only created when screen recording is supported
    QMap<QString, QLabel*> m_recordShortcuts;
    QSpinBox* m_gifFps = nullptr;
    QSpinBox* m_gifMaxWidth = nullptr;
    QCheckBox* m_recordShowClicks = nullptr;
#if defined(Q_OS_MACOS)
    QComboBox* m_animStyle = nullptr;
    QComboBox* m_animMotion = nullptr;
    QComboBox* m_animAnnotations = nullptr;
    // The preview's sample annotations, built once
    std::shared_ptr<AnnotationLayer> m_animPreviewAnnotations;
    QSpinBox* m_animDuration = nullptr;
    QSpinBox* m_animFps = nullptr;
    QComboBox* m_animFormat = nullptr;
    QLabel* m_animPreview = nullptr;
    // The same animation about as wide as a feed thumbnail, to judge how it
    // reads small
    QLabel* m_animThumb = nullptr;
    QTimer* m_animTimer = nullptr;
    std::unique_ptr<AnimatedFrames> m_animFrames;
    std::unique_ptr<AnimatedFrames> m_animThumbFrames;
    double m_animPhase = 0.0;
#endif
    QColor m_borderColorValue;
    QColor m_backgroundColor1Value;
    QColor m_backgroundColor2Value;
    QVector<QColor> m_paletteValue; // empty: the two colours
};
