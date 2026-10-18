// SPDX-License-Identifier: GPL-3.0-or-later
// "Animate" preview: plays the loop before anything is exported, with the
// style, shot motion and format pickers. Frames come from the export's own
// params and renderer (AnimExportJob::paramsFor, animFramesFor), drawn
// smaller, so what plays here is what Export writes. Export accepts the
// dialog (the caller exports with choice()); Cancel rejects it and nothing
// changes. macOS only, like the export.

#pragma once

#include "tools/animannotations.h"
#include "utils/animrender.h"
#include "utils/annotationlayer.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>

#include <memory>

class QComboBox;
class QLabel;
class QPushButton;

class AnimPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    // Long side of the preview canvas in points
    static constexpr int kPreviewLongSide = 440;
    static constexpr int kPreviewFps = 20;

    AnimPreviewDialog(const AnimSource& source,
                      const AnimChoice& initial,
                      QWidget* parent = nullptr);
    ~AnimPreviewDialog() override;

    const AnimSource& source() const { return m_source; }
    AnimChoice choice() const;
    // The file type the preview looks like (GIF only: the GIF)
    AnimTarget target() const;
    // Long side the frames are drawn at, in pixels
    int previewLongSide() const { return m_longSide; }
    const AnimRenderParams& renderParams() const { return m_params; }

    // The preview frame at phase 0..1
    QImage frameAt(double phase) const;
    // Shows the frame at `phase` (playing continues from there)
    void seek(double phase);
    double phase() const { return m_phase; }
    // The frame on screen now
    QImage shownFrame() const { return m_shown; }

    bool isPlaying() const { return m_timer.isActive(); }
    void setPlaying(bool playing);

private:
    // A picker changed: new params and frames, same phase
    void rebuild();
    void tick();
    void showPhase(double phase);
    void updatePlayButton();

    AnimSource m_source;
    PreparedAnimSource m_prepared;
    AnimRenderParams m_params;
    std::unique_ptr<AnimatedFrames> m_frames;
    int m_longSide = kPreviewLongSide;
    qreal m_dpr = 1.0;

    QLabel* m_view = nullptr;
    QPushButton* m_play = nullptr;
    QComboBox* m_style = nullptr;
    QComboBox* m_motion = nullptr;
    QComboBox* m_format = nullptr;

    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_phase = 0.0;
    double m_phaseAtStart = 0.0; // phase when the clock last started
    QImage m_shown;
};
