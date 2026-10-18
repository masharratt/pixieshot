// SPDX-License-Identifier: GPL-3.0-or-later

#include "animpreviewdialog.h"
#include "core/animexportjob.h"
#include "widgets/animpickers.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <cmath>

AnimPreviewDialog::AnimPreviewDialog(const AnimSource& source,
                                     const AnimChoice& initial,
                                     QWidget* parent)
  : QDialog(parent)
  , m_source(source)
  // Tools decode and pixelate once, here on the GUI thread
  , m_prepared(animannotations::prepare(source))
{
    setWindowTitle(tr("Animate"));
    setWindowModality(Qt::WindowModal);
    const QScreen* screen =
      parent != nullptr ? parent->screen() : QGuiApplication::primaryScreen();
    m_dpr = screen != nullptr ? screen->devicePixelRatio() : 1.0;
    m_longSide = qRound(kPreviewLongSide * m_dpr);

    m_view = new QLabel(this);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setObjectName(QStringLiteral("animPreviewView"));

    m_play = new QPushButton(this);
    m_play->setObjectName(QStringLiteral("animPreviewPlay"));
    m_play->setAutoDefault(false);
    connect(m_play, &QPushButton::clicked, this, [this]() {
        setPlaying(!isPlaying());
    });

    m_style = new QComboBox(this);
    m_style->setObjectName(QStringLiteral("animPreviewStyle"));
    animpickers::fillStyles(m_style);
    animpickers::select(m_style, initial.style);
    m_motion = new QComboBox(this);
    m_motion->setObjectName(QStringLiteral("animPreviewMotion"));
    animpickers::fillMotions(m_motion);
    animpickers::select(m_motion, initial.motion);
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("animPreviewFormat"));
    animpickers::fillFormats(m_format);
    animpickers::select(m_format, initial.format);
    m_format->setToolTip(tr("The preview shows how the GIF looks when GIF is "
                            "the only format."));

    auto* pickers = new QFormLayout();
    pickers->addRow(tr("Style"), m_style);
    pickers->addRow(tr("Shot motion"), m_motion);
    pickers->addRow(tr("Format"), m_format);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName(QStringLiteral("animPreviewCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* exportButton = new QPushButton(tr("Export"), this);
    exportButton->setObjectName(QStringLiteral("animPreviewExport"));
    exportButton->setDefault(true);
    connect(exportButton, &QPushButton::clicked, this, &QDialog::accept);

    auto* controls = new QHBoxLayout();
    controls->addWidget(m_play, 0, Qt::AlignTop);
    controls->addSpacing(12);
    controls->addLayout(pickers, 1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(exportButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addLayout(controls);
    layout->addLayout(buttons);

    for (QComboBox* combo : { m_style, m_motion, m_format }) {
        connect(combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this,
                &AnimPreviewDialog::rebuild);
    }

    m_timer.setInterval(1000 / kPreviewFps);
    connect(&m_timer, &QTimer::timeout, this, &AnimPreviewDialog::tick);

    rebuild();
    setPlaying(true);
}

AnimPreviewDialog::~AnimPreviewDialog() = default;

AnimChoice AnimPreviewDialog::choice() const
{
    AnimChoice choice;
    choice.style = m_style->currentData().toInt();
    choice.motion = m_motion->currentData().toInt();
    choice.format = m_format->currentData().toInt();
    return choice;
}

AnimTarget AnimPreviewDialog::target() const
{
    return animPreviewTarget(choice().format);
}

QImage AnimPreviewDialog::frameAt(double phase) const
{
    return m_frames ? m_frames->frame(phase) : QImage();
}

void AnimPreviewDialog::seek(double phase)
{
    m_phaseAtStart = phase;
    m_clock.restart();
    showPhase(phase);
}

void AnimPreviewDialog::setPlaying(bool playing)
{
    if (playing && !m_timer.isActive()) {
        m_phaseAtStart = m_phase;
        m_clock.restart();
        m_timer.start();
    } else if (!playing) {
        m_timer.stop();
    }
    updatePlayButton();
}

void AnimPreviewDialog::rebuild()
{
    m_params = AnimExportJob::paramsFor(m_prepared, choice());
    m_frames = std::make_unique<AnimatedFrames>(
      animFramesFor(m_params, target(), m_longSide));
    const QSize logical =
      (QSizeF(m_frames->size()) / m_dpr).toSize().expandedTo(QSize(1, 1));
    m_view->setFixedSize(logical);
    showPhase(m_phase);
}

void AnimPreviewDialog::tick()
{
    // Real time, whatever the frame rate the machine manages.
    // cfn: frames render on the UI thread per tick, move to a worker or
    // cache a loop if large shots make the dialog stutter

    const double loopSec = std::max(1, m_params.durationSec);
    const double elapsed = m_clock.elapsed() / 1000.0;
    showPhase(std::fmod(m_phaseAtStart + elapsed / loopSec, 1.0));
}

void AnimPreviewDialog::showPhase(double phase)
{
    m_phase = phase;
    m_shown = frameAt(phase);
    QPixmap pixmap = QPixmap::fromImage(m_shown);
    pixmap.setDevicePixelRatio(m_dpr);
    m_view->setPixmap(pixmap);
}

void AnimPreviewDialog::updatePlayButton()
{
    m_play->setText(isPlaying() ? tr("Pause") : tr("Play"));
}
