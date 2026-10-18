// SPDX-License-Identifier: GPL-3.0-or-later

#include "workflowsconf.h"
#include "config/stylepackrow.h"
#include "utils/animatedframe.h"
#include "utils/beautifypalette.h"
#include "platform/screenrecorder.h"
#include "utils/confighandler.h"
#include "utils/imageeffects.h"

#include <QCheckBox>
#include <QEvent>
#include <QTimer>
#include <cmath>
#include <QComboBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

struct ActionLabel
{
    CaptureAction action;
    const char* text;
};

// Display order of the checkboxes; the run order is fixed by normalisedOrder
const ActionLabel kActionLabels[] = {
    { CaptureAction::ApplyEffects, QT_TR_NOOP("Apply effects") },
    { CaptureAction::Save, QT_TR_NOOP("Save") },
    { CaptureAction::CopyImage, QT_TR_NOOP("Copy image") },
    { CaptureAction::CopyPath, QT_TR_NOOP("Copy path") },
    { CaptureAction::Pin, QT_TR_NOOP("Pin") },
    { CaptureAction::OpenEditor, QT_TR_NOOP("Open editor") },
    { CaptureAction::ShowToast, QT_TR_NOOP("Show toast") },
};

// Small gradient stand-in for a capture, with a bar and a button so auto
// colors and From screenshot have something to pick up
QImage previewSample()
{
    QImage sample(96, 64, QImage::Format_ARGB32_Premultiplied);
    QLinearGradient gradient(0, 0, 96, 64);
    gradient.setColorAt(0, QColor(70, 130, 220));
    gradient.setColorAt(1, QColor(240, 170, 60));
    QPainter p(&sample);
    p.fillRect(sample.rect(), gradient);
    p.fillRect(QRect(0, 0, 96, 10), QColor(245, 246, 250));
    p.fillRect(QRect(60, 46, 28, 10), QColor(236, 72, 96));
    return sample;
}

#if defined(Q_OS_MACOS)
// Preview frame rate; the loop takes as long as the export would
constexpr int kAnimPreviewIntervalMs = 50;
// Logical widths of the Animate previews
constexpr int kAnimPreviewWidth = 300;
constexpr int kAnimThumbWidth = 200;

// A window-sized stand-in for a capture (title bar, sidebar, lines of
// "text", a chart card and a button) so the previews show the real balance
// of shot and background, and the thumbnail shows how small text reads
QImage animPreviewSample()
{
    QImage sample(1280, 800, QImage::Format_ARGB32_Premultiplied);
    sample.fill(QColor(245, 246, 250));
    QPainter p(&sample);
    p.fillRect(QRect(0, 0, 1280, 40), QColor(228, 230, 236));
    const QColor dots[] = { QColor(255, 95, 87), QColor(254, 188, 46), QColor(40, 200, 64) };
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < 3; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(dots[i]);
        p.drawEllipse(QPoint(24 + 22 * i, 20), 7, 7);
    }
    p.fillRect(QRect(0, 40, 240, 760), QColor(236, 238, 244));
    for (int i = 0; i < 9; ++i) {
        p.fillRect(QRect(28, 80 + 44 * i, 140 - (i * 23) % 60, 12), QColor(120, 126, 140));
    }
    p.fillRect(QRect(290, 80, 520, 30), QColor(36, 40, 52));
    for (int i = 0; i < 6; ++i) {
        p.fillRect(QRect(290, 140 + 30 * i, 900 - (i * 137) % 260, 12),
                   QColor(90, 96, 110));
    }
    QLinearGradient chart(290, 360, 1190, 720);
    chart.setColorAt(0, QColor(70, 130, 220));
    chart.setColorAt(1, QColor(240, 170, 60));
    p.setBrush(chart);
    p.drawRoundedRect(QRect(290, 340, 900, 360), 16, 16);
    p.setBrush(QColor(236, 72, 96));
    p.drawRoundedRect(QRect(1040, 730, 150, 44), 10, 10);
    return sample;
}
#endif

} // namespace

WorkflowsConf::WorkflowsConf(QWidget* parent)
  : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignTop);

    addWorkflow(QStringLiteral("TAKE_SCREENSHOT"), tr("Capture screen"));
    addWorkflow(QStringLiteral("CAPTURE_AND_EDIT"), tr("Capture and edit"));
    addEffectsGroup();
#if defined(Q_OS_MACOS)
    addAnimateGroup();
#endif
    if (screenRecordingSupported()) {
        addRecordingGroup();
    }
    layout->addStretch();

    updateComponents();
}

void WorkflowsConf::addWorkflow(const QString& hotkey, const QString& title)
{
    auto* box = new QGroupBox(title, this);
    auto* boxLayout = new QVBoxLayout(box);

    Row row;
    row.shortcut = new QLabel(box);
    boxLayout->addWidget(row.shortcut);

    auto* hint = new QLabel(tr("Change the shortcut in the Shortcuts tab."), box);
    hint->setEnabled(false);
    boxLayout->addWidget(hint);

    auto* checks = new QHBoxLayout();
    for (const ActionLabel& entry : kActionLabels) {
        auto* check = new QCheckBox(tr(entry.text), box);
        row.boxes.insert(entry.action, check);
        checks->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, hotkey]() {
            onToggled(hotkey);
        });
    }
    boxLayout->addLayout(checks);

    static_cast<QVBoxLayout*>(layout())->addWidget(box);
    m_rows.insert(hotkey, row);
}

void WorkflowsConf::addEffectsGroup()
{
    auto* box = new QGroupBox(tr("Effects"), this);
    auto* boxLayout = new QHBoxLayout(box);
    auto* form = new QFormLayout();

    // A pack rewrites the saved settings; reload every control and preview
    auto* stylePacks = new StylePackRow(box);
    connect(stylePacks,
            &StylePackRow::applied,
            this,
            &WorkflowsConf::updateComponents);
    form->addRow(tr("Style pack"), stylePacks);

    m_borderPx = new QSpinBox(box);
    m_borderPx->setRange(0, 100);
    m_borderPx->setSuffix(tr(" px"));
    form->addRow(tr("Border width"), m_borderPx);

    m_borderColor = new QPushButton(box);
    form->addRow(tr("Border color"), m_borderColor);

    m_cornerRadius = new QSpinBox(box);
    m_cornerRadius->setRange(0, 200);
    m_cornerRadius->setSuffix(tr(" px"));
    form->addRow(tr("Corner radius"), m_cornerRadius);

    m_shadow = new QCheckBox(tr("Drop shadow"), box);
    form->addRow(QString(), m_shadow);

    m_padding = new QSpinBox(box);
    m_padding->setRange(0, 512);
    m_padding->setSuffix(tr(" px"));
    form->addRow(tr("Padding"), m_padding);

    m_background = new QComboBox(box);
    m_background->addItem(tr("None"), int(BackgroundMode::None));
    m_background->addItem(tr("Solid"), int(BackgroundMode::Solid));
    m_background->addItem(tr("Gradient"), int(BackgroundMode::Gradient));
    m_background->addItem(tr("From screenshot"),
                          int(BackgroundMode::FromScreenshot));
    form->addRow(tr("Background"), m_background);

    m_palettePreset = new QComboBox(box);
    m_palettePreset->addItem(tr("Custom"), -1);
    const auto& presets = beautifypalette::presets();
    for (int i = 0; i < presets.size(); ++i) {
        m_palettePreset->addItem(tr(presets.at(i).name), i);
    }
    m_palettePreset->setToolTip(
      tr("Gradient colours for the background and the animated backgrounds."));
    form->addRow(tr("Palette"), m_palettePreset);

    m_backgroundColor1 = new QPushButton(box);
    form->addRow(tr("Background color"), m_backgroundColor1);

    m_backgroundColor2 = new QPushButton(box);
    form->addRow(tr("Gradient end color"), m_backgroundColor2);

    m_autoColors = new QCheckBox(tr("Auto colors"), box);
    m_autoColors->setToolTip(
      tr("Pick the gradient colors from the screenshot."));
    form->addRow(QString(), m_autoColors);

    m_glass = new QCheckBox(tr("Glass frame"), box);
    m_glassPx = new QSpinBox(box);
    m_glassPx->setRange(4, 64);
    m_glassPx->setSuffix(tr(" px"));
    auto* glassRow = new QHBoxLayout();
    glassRow->addWidget(m_glass);
    glassRow->addWidget(m_glassPx);
    glassRow->addStretch();
    form->addRow(QString(), glassRow);

    m_grain = new QSlider(Qt::Horizontal, box);
    m_grain->setRange(0, 100);
    m_grain->setToolTip(tr("Fine noise over the background, hides banding."));
    form->addRow(tr("Grain"), m_grain);

    m_aspect = new QComboBox(box);
    m_aspect->addItem(tr("Off"), 0);
    m_aspect->addItem(tr("Square 1:1"), 1);
    m_aspect->addItem(tr("Landscape 16:9"), 2);
    m_aspect->addItem(tr("Portrait 9:16"), 3);
    m_aspect->addItem(tr("Social 4:5"), 4);
    m_aspect->addItem(tr("Classic 4:3"), 5);
    form->addRow(tr("Size preset"), m_aspect);

    m_autoBalance = new QCheckBox(tr("Auto-balance margins"), box);
    form->addRow(QString(), m_autoBalance);

    m_beautifyAlways = new QCheckBox(tr("Beautify every capture"), box);
    form->addRow(QString(), m_beautifyAlways);
    boxLayout->addLayout(form);

    m_preview = new QLabel(box);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(180, 140);
    boxLayout->addWidget(m_preview, 1);

    connect(m_borderPx,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &WorkflowsConf::onEffectChanged);
    connect(m_cornerRadius,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &WorkflowsConf::onEffectChanged);
    connect(m_shadow, &QCheckBox::toggled, this, &WorkflowsConf::onEffectChanged);
    connect(m_padding,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &WorkflowsConf::onEffectChanged);
    connect(m_background,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &WorkflowsConf::onEffectChanged);
    connect(m_aspect,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &WorkflowsConf::onEffectChanged);
    connect(m_autoBalance,
            &QCheckBox::toggled,
            this,
            &WorkflowsConf::onEffectChanged);
    connect(
      m_autoColors, &QCheckBox::toggled, this, &WorkflowsConf::onEffectChanged);
    connect(m_glass, &QCheckBox::toggled, this, &WorkflowsConf::onEffectChanged);
    connect(m_glassPx,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &WorkflowsConf::onEffectChanged);
    connect(
      m_grain, &QSlider::valueChanged, this, &WorkflowsConf::onEffectChanged);
    connect(m_beautifyAlways,
            &QCheckBox::toggled,
            this,
            &WorkflowsConf::onEffectChanged);
    connect(m_palettePreset,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &WorkflowsConf::onPalettePresetChanged);
    // Picking a colour by hand leaves any preset palette (back to Custom)
    connect(m_backgroundColor1, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
          m_backgroundColor1Value, this, tr("Background color"));
        if (picked.isValid()) {
            m_backgroundColor1Value = picked;
            m_paletteValue.clear();
            onEffectChanged();
        }
    });
    connect(m_backgroundColor2, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
          m_backgroundColor2Value, this, tr("Gradient end color"));
        if (picked.isValid()) {
            m_backgroundColor2Value = picked;
            m_paletteValue.clear();
            onEffectChanged();
        }
    });
    connect(m_borderColor, &QPushButton::clicked, this, [this]() {
        const QColor picked =
          QColorDialog::getColor(m_borderColorValue, this, tr("Border color"));
        if (picked.isValid()) {
            m_borderColorValue = picked;
            onEffectChanged();
        }
    });

    static_cast<QVBoxLayout*>(layout())->addWidget(box);
}

#if defined(Q_OS_MACOS)
void WorkflowsConf::addAnimateGroup()
{
    auto* box = new QGroupBox(tr("Animated background"), this);
    box->setToolTip(tr("Used by the Animate button: the Beautify look over a "
                       "looping animated background, saved as a video."));
    auto* boxLayout = new QHBoxLayout(box);
    auto* form = new QFormLayout();

    m_animStyle = new QComboBox(box);
    m_animStyle->addItem(tr("Aurora"), int(AnimStyle::Aurora));
    m_animStyle->addItem(tr("Gradient shift"), int(AnimStyle::GradientShift));
    m_animStyle->addItem(tr("Blobs"), int(AnimStyle::Blobs));
    m_animStyle->addItem(tr("Twinkle"), int(AnimStyle::Twinkle));
    m_animStyle->addItem(tr("Flow"), int(AnimStyle::Flow));
    m_animStyle->addItem(tr("Silk"), int(AnimStyle::Silk));
    form->addRow(tr("Style"), m_animStyle);

    m_animMotion = new QComboBox(box);
    m_animMotion->addItem(tr("None"), int(ShotMotion::None));
    m_animMotion->addItem(tr("Tilt and light sweep"), int(ShotMotion::Tilt));
    m_animMotion->addItem(tr("Float"), int(ShotMotion::Float));
    m_animMotion->setToolTip(tr("How the screenshot itself moves in the loop."));
    form->addRow(tr("Shot motion"), m_animMotion);

    m_animDuration = new QSpinBox(box);
    m_animDuration->setRange(3, 10);
    m_animDuration->setSuffix(tr(" s"));
    form->addRow(tr("Loop length"), m_animDuration);

    m_animFps = new QSpinBox(box);
    m_animFps->setRange(15, 60);
    m_animFps->setSuffix(tr(" fps"));
    m_animFps->setToolTip(tr("Video frame rate. GIFs use at most 15 fps."));
    form->addRow(tr("Frame rate"), m_animFps);

    m_animFormat = new QComboBox(box);
    m_animFormat->addItem(tr("MP4"), 0);
    m_animFormat->addItem(tr("GIF"), 1);
    m_animFormat->addItem(tr("MP4 and GIF"), 2);
    form->addRow(tr("Format"), m_animFormat);
    boxLayout->addLayout(form);

    m_animPreview = new QLabel(box);
    m_animPreview->setAlignment(Qt::AlignCenter);
    m_animPreview->setMinimumSize(kAnimPreviewWidth, kAnimPreviewWidth * 2 / 3);
    // The timer only runs while the preview can be seen
    m_animPreview->installEventFilter(this);
    boxLayout->addWidget(m_animPreview, 1);

    auto* thumbColumn = new QVBoxLayout();
    thumbColumn->addStretch();
    m_animThumb = new QLabel(box);
    m_animThumb->setAlignment(Qt::AlignCenter);
    m_animThumb->setFixedSize(kAnimThumbWidth, kAnimThumbWidth * 2 / 3);
    m_animThumb->setToolTip(
      tr("About the size it shows in a feed or chat: check the shot still reads."));
    thumbColumn->addWidget(m_animThumb);
    auto* thumbLabel = new QLabel(tr("Thumbnail"), box);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setEnabled(false);
    thumbColumn->addWidget(thumbLabel);
    thumbColumn->addStretch();
    boxLayout->addLayout(thumbColumn);

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(kAnimPreviewIntervalMs);
    connect(m_animTimer, &QTimer::timeout, this, &WorkflowsConf::tickAnimPreview);

    for (QComboBox* combo : { m_animStyle, m_animMotion, m_animFormat }) {
        connect(combo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &WorkflowsConf::onAnimChanged);
    }
    for (QSpinBox* spin : { m_animDuration, m_animFps }) {
        connect(spin,
                QOverload<int>::of(&QSpinBox::valueChanged),
                this,
                &WorkflowsConf::onAnimChanged);
    }
    static_cast<QVBoxLayout*>(layout())->addWidget(box);
}

void WorkflowsConf::onAnimChanged()
{
    if (m_updating) {
        return;
    }
    ConfigHandler config;
    config.setAnimStyle(m_animStyle->currentData().toInt());
    config.setAnimMotion(m_animMotion->currentData().toInt());
    config.setAnimDurationSec(m_animDuration->value());
    config.setAnimFps(m_animFps->value());
    config.setAnimFormat(m_animFormat->currentData().toInt());
    rebuildAnimPreview();
}

void WorkflowsConf::rebuildAnimPreview()
{
    if (m_animStyle == nullptr) {
        return;
    }
    static const QImage sample = animPreviewSample();
    const EffectSettings settings = previewSettings();
    const AnimStyle style = animatedbg::styleFrom(m_animStyle->currentData().toInt());
    const auto motion = shotmotion::motionFrom(m_animMotion->currentData().toInt());
    const qreal dpr = devicePixelRatioF();
    // Rendered at the pixels shown, the way the export scales a capture
    // down: the effects shrink with the shot
    auto build = [&](int logicalWidth) {
        auto frames = std::make_unique<AnimatedFrames>(
          sample,
          settings,
          style,
          animatedbg::kDefaultSeed,
          int(std::lround(logicalWidth * dpr)));
        frames->setMotion(motion, m_animDuration->value());
        return frames;
    };
    m_animFrames = build(kAnimPreviewWidth);
    m_animThumbFrames = build(kAnimThumbWidth);
    tickAnimPreview();
}

void WorkflowsConf::tickAnimPreview()
{
    if (!m_animFrames) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    auto show = [&](QLabel* label, const AnimatedFrames& frames) {
        QPixmap pixmap = QPixmap::fromImage(frames.frame(m_animPhase));
        const QSize box = label->minimumSize() * dpr;
        if (pixmap.width() > box.width() || pixmap.height() > box.height()) {
            pixmap = pixmap.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        pixmap.setDevicePixelRatio(dpr);
        label->setPixmap(pixmap);
    };
    show(m_animPreview, *m_animFrames);
    show(m_animThumb, *m_animThumbFrames);
    const double step =
      kAnimPreviewIntervalMs / (1000.0 * std::max(1, m_animDuration->value()));
    m_animPhase = std::fmod(m_animPhase + step, 1.0);
}

bool WorkflowsConf::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_animPreview && m_animTimer != nullptr) {
        if (event->type() == QEvent::Show) {
            m_animTimer->start();
        } else if (event->type() == QEvent::Hide) {
            m_animTimer->stop();
        }
    }
    return QWidget::eventFilter(watched, event);
}
#endif

void WorkflowsConf::addRecordingGroup()
{
    auto* box = new QGroupBox(tr("Screen recording"), this);
    auto* boxLayout = new QVBoxLayout(box);

    for (const QString& name :
         { QStringLiteral("RECORD_MP4"), QStringLiteral("RECORD_GIF") }) {
        auto* label = new QLabel(box);
        boxLayout->addWidget(label);
        m_recordShortcuts.insert(name, label);
    }
    auto* hint = new QLabel(
      tr("Change the shortcuts in the Shortcuts tab. Press the shortcut again "
         "to stop."),
      box);
    hint->setEnabled(false);
    boxLayout->addWidget(hint);

    auto* form = new QFormLayout();
    m_gifFps = new QSpinBox(box);
    m_gifFps->setRange(1, 50);
    m_gifFps->setSuffix(tr(" fps"));
    form->addRow(tr("GIF frame rate"), m_gifFps);

    m_gifMaxWidth = new QSpinBox(box);
    m_gifMaxWidth->setRange(120, 3840);
    m_gifMaxWidth->setSuffix(tr(" px"));
    form->addRow(tr("GIF maximum width"), m_gifMaxWidth);
    boxLayout->addLayout(form);

    m_recordShowClicks = new QCheckBox(tr("Highlight mouse clicks"), box);
    m_recordShowClicks->setToolTip(
      tr("Draw a ring where the mouse is clicked. Needs macOS 15 or newer."));
    boxLayout->addWidget(m_recordShowClicks);
    connect(m_recordShowClicks,
            &QCheckBox::toggled,
            this,
            &WorkflowsConf::onRecordingChanged);

    connect(m_gifFps,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &WorkflowsConf::onRecordingChanged);
    connect(m_gifMaxWidth,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &WorkflowsConf::onRecordingChanged);

    static_cast<QVBoxLayout*>(layout())->addWidget(box);
}

void WorkflowsConf::onRecordingChanged()
{
    if (m_updating) {
        return;
    }
    ConfigHandler config;
    config.setGifFps(m_gifFps->value());
    config.setGifMaxWidth(m_gifMaxWidth->value());
    config.setRecordShowClicks(m_recordShowClicks->isChecked());
}

void WorkflowsConf::onEffectChanged()
{
    if (m_updating) {
        return;
    }
    ConfigHandler config;
    config.setEffectBorderPx(m_borderPx->value());
    config.setEffectBorderColor(m_borderColorValue);
    config.setEffectCornerRadius(m_cornerRadius->value());
    config.setEffectShadow(m_shadow->isChecked());
    config.setEffectPadding(m_padding->value());
    config.setEffectBackground(m_background->currentData().toInt());
    config.setEffectBackgroundColor1(m_backgroundColor1Value);
    config.setEffectBackgroundColor2(m_backgroundColor2Value);
    config.setEffectAspect(m_aspect->currentData().toInt());
    config.setEffectAutoBalance(m_autoBalance->isChecked());
    config.setEffectAutoColors(m_autoColors->isChecked());
    config.setEffectGlass(m_glass->isChecked());
    config.setEffectGlassPx(m_glassPx->value());
    config.setEffectGrain(m_grain->value());
    config.setEffectPalette(beautifypalette::format(m_paletteValue));
    config.setBeautifyAlways(m_beautifyAlways->isChecked());
    updatePreview();
}

void WorkflowsConf::onPalettePresetChanged()
{
    if (m_updating) {
        return;
    }
    const int index = m_palettePreset->currentData().toInt();
    const auto& presets = beautifypalette::presets();
    if (index >= 0 && index < presets.size()) {
        m_paletteValue = presets.at(index).stops;
        m_backgroundColor1Value = m_paletteValue.first();
        m_backgroundColor2Value = m_paletteValue.last();
    } else {
        m_paletteValue.clear();
    }
    onEffectChanged();
}

void WorkflowsConf::syncPalettePreset()
{
    int index = -1;
    const auto& presets = beautifypalette::presets();
    for (int i = 0; i < presets.size(); ++i) {
        if (!m_paletteValue.isEmpty() && presets.at(i).stops == m_paletteValue) {
            index = i;
        }
    }
    const QSignalBlocker block(m_palettePreset);
    m_palettePreset->setCurrentIndex(std::max(0, m_palettePreset->findData(index)));
}

void WorkflowsConf::updatePreview()
{
    m_borderColor->setText(m_borderColorValue.name());
    m_borderColor->setStyleSheet(
      QStringLiteral("background-color: %1;").arg(m_borderColorValue.name()));

    for (auto [button, color] :
         { std::pair{ m_backgroundColor1, m_backgroundColor1Value },
           std::pair{ m_backgroundColor2, m_backgroundColor2Value } }) {
        button->setText(color.name());
        button->setStyleSheet(
          QStringLiteral("background-color: %1;").arg(color.name()));
    }
    // The colors only matter where they are used: the second one and auto
    // colors for a gradient, none of them when the shot is the background
    const int mode = m_background->currentData().toInt();
    const bool gradientMode = mode == int(BackgroundMode::Gradient);
    const bool manualColors = !(gradientMode && m_autoColors->isChecked());
    m_backgroundColor2->setEnabled(gradientMode && manualColors);
    m_backgroundColor1->setEnabled(
      manualColors && (mode == int(BackgroundMode::Solid) || gradientMode));
    m_autoColors->setEnabled(gradientMode);
    m_palettePreset->setEnabled(gradientMode && manualColors);
    syncPalettePreset();
    m_glassPx->setEnabled(m_glass->isChecked());
    m_grain->setEnabled(mode != int(BackgroundMode::None));

    // Large paddings would overflow the box, so fit the result into it
    m_preview->setPixmap(
      QPixmap::fromImage(
        imageeffects::applyEffects(previewSample(), previewSettings()))
        .scaled(m_preview->minimumSize(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
#if defined(Q_OS_MACOS)
    rebuildAnimPreview();
#endif
}

EffectSettings WorkflowsConf::previewSettings() const
{
    EffectSettings settings;
    settings.borderPx = m_borderPx->value();
    settings.borderColor = m_borderColorValue;
    settings.cornerRadius = m_cornerRadius->value();
    settings.shadow = m_shadow->isChecked();
    settings.paddingPx = m_padding->value();
    settings.backgroundMode =
      static_cast<BackgroundMode>(m_background->currentData().toInt());
    settings.backgroundColor1 = m_backgroundColor1Value;
    settings.backgroundColor2 = m_backgroundColor2Value;
    settings.aspect = m_aspect->currentData().toInt();
    settings.autoBalance = m_autoBalance->isChecked();
    settings.autoColors = m_autoColors->isChecked();
    settings.glass = m_glass->isChecked();
    settings.glassPx = m_glassPx->value();
    settings.grain = m_grain->value();
    settings.palette = m_paletteValue;
    return settings;
}

void WorkflowsConf::onToggled(const QString& hotkey)
{
    if (m_updating) {
        return;
    }
    QList<CaptureAction> actions;
    const Row row = m_rows.value(hotkey);
    for (const ActionLabel& entry : kActionLabels) {
        if (row.boxes.value(entry.action)->isChecked()) {
            actions << entry.action;
        }
    }
    ConfigHandler().setWorkflowActions(hotkey, actions);
}

void WorkflowsConf::updateComponents()
{
    ConfigHandler config;
    m_updating = true;
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        const QList<CaptureAction> actions = config.workflowActions(it.key());
        for (auto b = it->boxes.begin(); b != it->boxes.end(); ++b) {
            b.value()->setChecked(actions.contains(b.key()));
        }
        const QString sequence = config.shortcut(it.key());
        it->shortcut->setText(
          tr("Shortcut: %1")
            .arg(sequence.isEmpty() ? tr("not set") : sequence));
    }
    m_borderPx->setValue(config.effectBorderPx());
    m_borderColorValue = config.effectBorderColor();
    m_cornerRadius->setValue(config.effectCornerRadius());
    m_shadow->setChecked(config.effectShadow());
    m_padding->setValue(config.effectPadding());
    m_background->setCurrentIndex(
      std::max(0, m_background->findData(config.effectBackground())));
    m_backgroundColor1Value = config.effectBackgroundColor1();
    m_backgroundColor2Value = config.effectBackgroundColor2();
    m_aspect->setCurrentIndex(
      std::max(0, m_aspect->findData(config.effectAspect())));
    m_autoBalance->setChecked(config.effectAutoBalance());
    m_autoColors->setChecked(config.effectAutoColors());
    m_glass->setChecked(config.effectGlass());
    m_glassPx->setValue(config.effectGlassPx());
    m_grain->setValue(config.effectGrain());
    m_paletteValue = beautifypalette::parse(config.effectPalette());
    m_beautifyAlways->setChecked(config.beautifyAlways());
    for (auto it = m_recordShortcuts.begin(); it != m_recordShortcuts.end();
         ++it) {
        const QString sequence = config.shortcut(it.key());
        const QString name = it.key() == QLatin1String("RECORD_MP4")
                               ? tr("Record MP4")
                               : tr("Record GIF");
        it.value()->setText(
          tr("%1 shortcut: %2")
            .arg(name, sequence.isEmpty() ? tr("not set") : sequence));
    }
#if defined(Q_OS_MACOS)
    m_animStyle->setCurrentIndex(
      std::max(0, m_animStyle->findData(config.animStyle())));
    m_animMotion->setCurrentIndex(
      std::max(0, m_animMotion->findData(config.animMotion())));
    m_animDuration->setValue(config.animDurationSec());
    m_animFps->setValue(config.animFps());
    m_animFormat->setCurrentIndex(
      std::max(0, m_animFormat->findData(config.animFormat())));
#endif
    if (m_gifFps != nullptr) {
        m_gifFps->setValue(config.gifFps());
        m_gifMaxWidth->setValue(config.gifMaxWidth());
        m_recordShowClicks->setChecked(config.recordShowClicks());
    }
    m_updating = false;
    updatePreview();
}

// Out of line: AnimatedFrames is only forward-declared in the header
WorkflowsConf::~WorkflowsConf() = default;
