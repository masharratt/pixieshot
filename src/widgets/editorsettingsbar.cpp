// SPDX-License-Identifier: GPL-3.0-or-later

#include "editorsettingsbar.h"
#include "editorsettings.h"
#include "utils/confighandler.h"
#include "widgets/panel/sidepanelwidget.h"

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

namespace {
constexpr int kBarHeight = 40;
constexpr int kSwatchSize = 22;
constexpr int kPresetSize = 18;

QString rgba(const QColor& c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(c.alpha());
}

// A swatch fill: one colour, or the stops left to right
QString fillOf(const QVector<QColor>& stops)
{
    if (stops.size() < 2) {
        return stops.isEmpty() ? QStringLiteral("transparent")
                               : rgba(stops.first());
    }
    QStringList parts;
    for (int i = 0; i < stops.size(); ++i) {
        parts << QStringLiteral("stop:%1 %2")
                   .arg(double(i) / (stops.size() - 1))
                   .arg(rgba(stops.at(i)));
    }
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:1, %1)")
      .arg(parts.join(QStringLiteral(", ")));
}

QString swatchStyle(const QString& fill, int size, bool selected = false)
{
    return QStringLiteral("QToolButton { background: %1; border: %2px solid "
                          "%3; border-radius: %4px; }")
      .arg(fill)
      .arg(selected ? 2 : 1)
      .arg(selected ? QStringLiteral("#ffffff") : QStringLiteral("#666666"))
      .arg(size / 2);
}
} // namespace

EditorSettingsBar::EditorSettingsBar(QWidget* parent)
  : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(kBarHeight);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 0, 6, 0);
    row->setSpacing(6);

    auto* colorLabel = new QLabel(tr("Color"), this);
    colorLabel->setStyleSheet(QStringLiteral("color: #dddddd;"));
    row->addWidget(colorLabel);

    m_swatchLayout = new QHBoxLayout();
    m_swatchLayout->setContentsMargins(0, 0, 0, 0);
    m_swatchLayout->setSpacing(4);
    row->addLayout(m_swatchLayout);

    m_plus = new QToolButton(this);
    m_plus->setText(QStringLiteral("+"));
    m_plus->setToolTip(tr("Other color"));
    m_plus->setFixedSize(kSwatchSize, kSwatchSize);
    m_plus->setStyleSheet(QStringLiteral(
      "QToolButton { color: #dddddd; border: 1px solid #888888; "
      "border-radius: %1px; background: transparent; }")
                            .arg(kSwatchSize / 2));
    connect(m_plus, &QToolButton::clicked, this, [this]() {
        pickCustomColor();
    });
    row->addWidget(m_plus);

    row->addSpacing(12);
    auto* sizeLabel = new QLabel(tr("Size"), this);
    sizeLabel->setStyleSheet(QStringLiteral("color: #dddddd;"));
    row->addWidget(sizeLabel);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(1, maxToolSize);
    m_slider->setMinimumWidth(minSliderWidth);
    m_slider->setMaximumWidth(220);
    row->addWidget(m_slider);

    m_spin = new QSpinBox(this);
    m_spin->setRange(1, maxToolSize);
    m_spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addWidget(m_spin);

    row->addSpacing(12);
    addBackgroundControls(row);
    row->addStretch(1);

    // Keep slider and box in sync, and report only user edits
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        const int size = clampThickness(v, 1, maxToolSize);
        {
            QSignalBlocker block(m_spin);
            m_spin->setValue(size);
        }
        emit sizePicked(size);
    });
    connect(m_spin,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [this](int v) {
                const int size = clampThickness(v, 1, maxToolSize);
                {
                    QSignalBlocker block(m_slider);
                    m_slider->setValue(size);
                }
                emit sizePicked(size);
            });

    rebuildSwatches();
}

int EditorSettingsBar::barHeight()
{
    return kBarHeight;
}

void EditorSettingsBar::rebuildSwatches()
{
    qDeleteAll(m_swatches);
    m_swatches.clear();
    m_colors = swatchColors(ConfigHandler().userColors().toList(), kMaxSwatches);
    for (const QColor& c : m_colors) {
        auto* b = new QToolButton(this);
        b->setFixedSize(kSwatchSize, kSwatchSize);
        b->setToolTip(c.name(c.alpha() < 255 ? QColor::HexArgb
                                              : QColor::HexRgb));
        b->setProperty("swatchColor", c);
        connect(b, &QToolButton::clicked, this, [this, c]() {
            emit colorPicked(c);
        });
        m_swatchLayout->addWidget(b);
        m_swatches.append(b);
    }
    refreshSelection();
}

// Draw each swatch with a ring on the current colour
void EditorSettingsBar::refreshSelection()
{
    for (QToolButton* b : m_swatches) {
        const QColor c = b->property("swatchColor").value<QColor>();
        const bool selected = m_current.isValid() && c.rgba() == m_current.rgba();
        b->setStyleSheet(
          QStringLiteral("QToolButton { background: rgba(%1,%2,%3,%4); "
                         "border: %5px solid %6; border-radius: %7px; }")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(c.alpha())
            .arg(selected ? 3 : 1)
            .arg(selected ? QStringLiteral("#ffffff") : QStringLiteral("#666666"))
            .arg(kSwatchSize / 2));
    }
}

void EditorSettingsBar::pickCustomColor()
{
    const QColor chosen = QColorDialog::getColor(
      m_current.isValid() ? m_current : QColor(Qt::red),
      this,
      tr("Choose color"),
      QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
        emit colorPicked(chosen);
    }
}

void EditorSettingsBar::setColor(const QColor& color)
{
    m_current = color;
    refreshSelection();
}

// Beautify background: the type, one swatch per colour it uses, and quick
// presets for that type. Shown before Beautify too, as what it will use.
void EditorSettingsBar::addBackgroundControls(QHBoxLayout* row)
{
    auto* label = new QLabel(tr("Background"), this);
    label->setStyleSheet(QStringLiteral("color: #dddddd;"));
    label->setToolTip(tr("Beautify background for this edit. Your saved "
                         "settings stay as they are."));
    row->addWidget(label);

    m_backgroundType = new QComboBox(this);
    m_backgroundType->setObjectName(QStringLiteral("backgroundType"));
    m_backgroundType->addItem(tr("None"), int(BackgroundMode::None));
    m_backgroundType->addItem(tr("Solid"), int(BackgroundMode::Solid));
    m_backgroundType->addItem(tr("Gradient"), int(BackgroundMode::Gradient));
    m_backgroundType->addItem(tr("From screenshot"),
                              int(BackgroundMode::FromScreenshot));
    m_backgroundType->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_backgroundType,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                const auto mode = static_cast<BackgroundMode>(
                  m_backgroundType->itemData(index).toInt());
                setBackground(backgroundchoice::withMode(m_background, mode));
                emit backgroundPicked(m_background);
            });
    row->addWidget(m_backgroundType);

    for (int i = 0; i < 2; ++i) {
        auto* swatch = new QToolButton(this);
        swatch->setObjectName(QStringLiteral("backgroundColor%1").arg(i + 1));
        swatch->setFixedSize(kSwatchSize, kSwatchSize);
        connect(swatch, &QToolButton::clicked, this, [this, i]() {
            pickBackgroundColor(i);
        });
        row->addWidget(swatch);
        m_backgroundSwatches[i] = swatch;
    }

    m_presetLayout = new QHBoxLayout();
    m_presetLayout->setContentsMargins(4, 0, 0, 0);
    m_presetLayout->setSpacing(3);
    row->addLayout(m_presetLayout);

    m_background =
      backgroundchoice::fromSettings(ConfigHandler().effectSettings());
    refreshBackground();
}

void EditorSettingsBar::refreshBackground()
{
    {
        QSignalBlocker block(m_backgroundType);
        m_backgroundType->setCurrentIndex(
          m_backgroundType->findData(int(m_background.mode)));
    }
    const QVector<QColor> colors = backgroundchoice::swatchColors(m_background);
    const bool gradient = m_background.mode == BackgroundMode::Gradient;
    for (int i = 0; i < 2; ++i) {
        QToolButton* swatch = m_backgroundSwatches[i];
        const bool shown = i < colors.size();
        swatch->setVisible(shown);
        if (!shown) {
            continue;
        }
        swatch->setStyleSheet(swatchStyle(rgba(colors.at(i)), kSwatchSize));
        swatch->setToolTip(gradient ? (i == 0 ? tr("Gradient start color")
                                              : tr("Gradient end color"))
                                    : tr("Background color"));
    }

    // Presets depend on the type only
    if (m_presetMode != m_background.mode || m_presets.isEmpty()) {
        qDeleteAll(m_presets);
        m_presets.clear();
        m_presetMode = m_background.mode;
        const auto presets = backgroundchoice::quickPresets(m_background.mode);
        for (int i = 0; i < presets.size(); ++i) {
            const auto& preset = presets.at(i);
            auto* b = new QToolButton(this);
            b->setObjectName(QStringLiteral("backgroundPreset%1").arg(i));
            b->setFixedSize(kPresetSize, kPresetSize);
            b->setToolTip(preset.name);
            const BackgroundChoice choice = preset.choice;
            connect(b, &QToolButton::clicked, this, [this, choice]() {
                setBackground(choice);
                emit backgroundPicked(m_background);
            });
            m_presetLayout->addWidget(b);
            m_presets.append(b);
        }
    }
    const auto presets = backgroundchoice::quickPresets(m_background.mode);
    for (int i = 0; i < m_presets.size() && i < presets.size(); ++i) {
        const BackgroundChoice& c = presets.at(i).choice;
        const QVector<QColor> stops =
          c.palette.size() >= 2 ? c.palette : QVector<QColor>{ c.color1 };
        m_presets.at(i)->setStyleSheet(
          swatchStyle(fillOf(stops), kPresetSize, c == m_background));
    }
}

void EditorSettingsBar::pickBackgroundColor(int index)
{
    const QVector<QColor> colors = backgroundchoice::swatchColors(m_background);
    if (index >= colors.size()) {
        return;
    }
    const QColor chosen = QColorDialog::getColor(
      colors.at(index), this, tr("Choose background color"));
    if (!chosen.isValid()) {
        return;
    }
    setBackground(backgroundchoice::withColor(m_background, index, chosen));
    emit backgroundPicked(m_background);
}

void EditorSettingsBar::setBackground(const BackgroundChoice& choice)
{
    m_background = choice;
    refreshBackground();
}

void EditorSettingsBar::setSize(int size)
{
    const int clamped = clampThickness(size, 1, maxToolSize);
    QSignalBlocker blockSlider(m_slider);
    QSignalBlocker blockSpin(m_spin);
    m_slider->setValue(clamped);
    m_spin->setValue(clamped);
}
