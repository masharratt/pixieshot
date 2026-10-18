// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "texttool.h"
#include "tools/text/textconfig.h"
#include "tools/text/textwidget.h"
#include "tools/toolstate.h"
#include "utils/confighandler.h"

#include <QRegion>
#include <cmath>

#define BASE_POINT_SIZE 8
#define MAX_INFO_LENGTH 24

TextTool::TextTool(QObject* parent)
  : CaptureTool(parent)
  , m_size(1)
{
    QString fontFamily = ConfigHandler().fontFamily();
    if (!fontFamily.isEmpty()) {
        m_font.setFamily(ConfigHandler().fontFamily());
    }
    m_alignment = Qt::AlignLeft;
}

TextTool::~TextTool()
{
    closeEditor();
}

void TextTool::copyParams(const TextTool* from, TextTool* to)
{
    CaptureTool::copyParams(from, to);
    to->m_font = from->m_font;
    to->m_alignment = from->m_alignment;
    to->m_text = from->m_text;
    to->m_size = from->m_size;
    to->m_color = from->m_color;
    to->m_textArea = from->m_textArea;
    to->m_currentPos = from->m_currentPos;
}

bool TextTool::isValid() const
{
    return !m_text.isEmpty();
}

bool TextTool::closeOnButtonPressed() const
{
    return false;
}

bool TextTool::isSelectable() const
{
    return true;
}

bool TextTool::showMousePreview() const
{
    return false;
}

QRect TextTool::boundingRect() const
{
    return m_textArea;
}

QIcon TextTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "text.svg");
}

QString TextTool::name() const
{
    return tr("Text");
}

QString TextTool::info()
{
    if (m_text.length() > 0) {
        m_tempString = QString("%1 - %2").arg(name(), m_text.trimmed());
        m_tempString = m_tempString.split("\n").at(0);
        if (m_tempString.length() > MAX_INFO_LENGTH) {
            m_tempString.truncate(MAX_INFO_LENGTH);
            m_tempString += "…";
        }
        return m_tempString;
    }
    return name();
}

CaptureTool::Type TextTool::type() const
{
    return CaptureTool::TYPE_TEXT;
}

QString TextTool::description() const
{
    return tr("Add text to your capture");
}

QWidget* TextTool::widget()
{
    closeEditor();
    m_widget = new TextWidget();
    m_widget->setTextColor(m_color);
    m_font.setPointSize(m_size + BASE_POINT_SIZE);
    m_widget->setFont(m_font);
    m_widget->setAlignment(m_alignment);
    m_widget->setText(m_text);
    m_widget->selectAll();
    connect(m_widget, &TextWidget::textUpdated, this, &TextTool::updateText);
    connect(
      m_widget,
      &TextWidget::editingFinished,
      this,
      [this]() { emit requestAction(REQ_COMMIT_CURRENT_TOOL); },
      Qt::QueuedConnection);
    return m_widget;
}

void TextTool::closeEditor()
{
    if (!m_widget.isNull()) {
        m_widget->hide();
        delete m_widget;
        m_widget = nullptr;
    }
    if (!m_confW.isNull()) {
        m_confW->hide();
        delete m_confW;
        m_confW = nullptr;
    }
}

QWidget* TextTool::configurationWidget()
{
    m_confW = new TextConfig();
    connect(
      m_confW, &TextConfig::fontFamilyChanged, this, &TextTool::updateFamily);
    connect(m_confW,
            &TextConfig::fontItalicChanged,
            this,
            &TextTool::updateFontItalic);
    connect(m_confW,
            &TextConfig::fontStrikeOutChanged,
            this,
            &TextTool::updateFontStrikeOut);
    connect(m_confW,
            &TextConfig::fontUnderlineChanged,
            this,
            &TextTool::updateFontUnderline);
    connect(m_confW,
            &TextConfig::fontWeightChanged,
            this,
            &TextTool::updateFontWeight);

    connect(
      m_confW, &TextConfig::alignmentChanged, this, &TextTool::updateAlignment);

    m_confW->setFontFamily(m_font.family());
    m_confW->setItalic(m_font.italic());
    m_confW->setUnderline(m_font.underline());
    m_confW->setStrikeOut(m_font.strikeOut());
    m_confW->setWeight(m_font.weight());
    m_confW->setTextAlignment(m_alignment);
    return m_confW;
}

CaptureTool* TextTool::copy(QObject* parent)
{
    auto* textTool = new TextTool(parent);
    if (m_confW != nullptr) {
        connect(m_confW,
                &TextConfig::fontFamilyChanged,
                textTool,
                &TextTool::updateFamily);
        connect(m_confW,
                &TextConfig::fontItalicChanged,
                textTool,
                &TextTool::updateFontItalic);
        connect(m_confW,
                &TextConfig::fontStrikeOutChanged,
                textTool,
                &TextTool::updateFontStrikeOut);
        connect(m_confW,
                &TextConfig::fontUnderlineChanged,
                textTool,
                &TextTool::updateFontUnderline);
        connect(m_confW,
                &TextConfig::fontWeightChanged,
                textTool,
                &TextTool::updateFontWeight);

        connect(m_confW,
                &TextConfig::alignmentChanged,
                textTool,
                &TextTool::updateAlignment);
    }
    copyParams(this, textTool);
    return textTool;
}

void TextTool::process(QPainter& painter, const QPixmap& pixmap)
{
    Q_UNUSED(pixmap)
    paintText(painter, -1);
}

// Types in: the first characters appear, line by line, at the places they
// have in the finished text (the layout never shifts)
void TextTool::processReveal(QPainter& painter,
                             const QPixmap& pixmap,
                             double progress)
{
    if (progress <= 0.0 || progress >= 1.0) {
        CaptureTool::processReveal(painter, pixmap, progress);
        return;
    }
    const qsizetype letters = m_text.size() - m_text.count(QLatin1Char('\n'));
    paintText(painter, int(std::lround(progress * letters)));
}

void TextTool::paintText(QPainter& painter, int shownLetters)
{
    if (m_text.isEmpty()) {
        return;
    }
    const int val = 5;
    QFont orig_font = painter.font();
    QPen orig_pen = painter.pen();
    QFontMetrics fm(m_font);
    QSize fontsize(fm.boundingRect(QRect(), 0, m_text).size());
    fontsize.setWidth(fontsize.width() + val * 2);
    fontsize.setHeight(fontsize.height() + val * 2);
    m_textArea.setSize(fontsize);
    const QRect area = m_textArea + QMargins(-val, -val, val, val);
    // draw text
    painter.setFont(m_font);
    painter.setPen(m_color);
    if (!editMode() && shownLetters != 0) {
        painter.save();
        if (shownLetters > 0) {
            // Clip to the typed part of each line
            QRegion typed;
            const QStringList lines = m_text.split(QLatin1Char('\n'));
            const int lineHeight = fm.lineSpacing();
            int left = shownLetters;
            for (int i = 0; i < lines.size() && left > 0; ++i) {
                const QString& line = lines[i];
                const int full = fm.horizontalAdvance(line);
                int x = area.left();
                if (m_alignment & Qt::AlignRight) {
                    x = area.right() + 1 - full;
                } else if (m_alignment & Qt::AlignHCenter) {
                    x = area.left() + (area.width() - full) / 2;
                }
                const int n = std::min<int>(left, line.size());
                left -= n;
                const int width = fm.horizontalAdvance(line.left(n)) +
                                  (n == line.size() ? val : 0);
                const int top = area.top() + i * lineHeight;
                const int bottom =
                  i == lines.size() - 1 ? area.bottom() + val : top + lineHeight;
                typed += QRect(x - val, top - (i == 0 ? val : 0), width + val,
                               bottom - top + (i == 0 ? val : 0));
            }
            painter.setClipRegion(typed, Qt::IntersectClip);
        }
        painter.drawText(area, m_alignment, m_text);
        painter.restore();
    }
    painter.setFont(orig_font);
    painter.setPen(orig_pen);

    if (m_widget != nullptr) {
        m_widget->setAlignment(m_alignment);
    }
}

void TextTool::drawObjectSelection(QPainter& painter)
{
    if (m_text.isEmpty()) {
        return;
    }
    drawObjectSelectionRect(painter, boundingRect());
}

void TextTool::paintMousePreview(QPainter& painter,
                                 const CaptureContext& context)
{
    Q_UNUSED(painter)
    Q_UNUSED(context)
}

void TextTool::drawEnd(const QPoint& point)
{
    m_textArea.moveTo(point);
}

void TextTool::drawMove(const QPoint& point)
{
    m_widget->move(point);
}

void TextTool::drawStart(const CaptureContext& context)
{
    m_color = context.color;
    m_size = context.toolSize;
    emit requestAction(REQ_ADD_CHILD_WIDGET);
}

void TextTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}

void TextTool::onColorChanged(const QColor& color)
{
    m_color = color;
    if (m_widget != nullptr) {
        m_widget->setTextColor(color);
    }
}

void TextTool::onSizeChanged(int size)
{
    m_size = size;
    m_font.setPointSize(m_size + BASE_POINT_SIZE);
    if (m_widget != nullptr) {
        m_widget->setFont(m_font);
    }
}

void TextTool::updateText(const QString& newText)
{
    m_text = newText;
}

void TextTool::updateFamily(const QString& text)
{
    m_font.setFamily(text);
    if (m_textOld.isEmpty()) {
        ConfigHandler().setFontFamily(m_font.family());
    }
    if (m_widget != nullptr) {
        m_widget->setFont(m_font);
    }
}

void TextTool::updateFontUnderline(const bool underlined)
{
    m_font.setUnderline(underlined);
    if (m_widget != nullptr) {
        m_widget->setFont(m_font);
    }
}

void TextTool::updateFontStrikeOut(const bool strikeout)
{
    m_font.setStrikeOut(strikeout);
    if (m_widget != nullptr) {
        m_widget->setFont(m_font);
    }
}

void TextTool::updateFontWeight(const QFont::Weight weight)
{
    m_font.setWeight(weight);
    if (m_widget != nullptr) {
        m_widget->setFont(m_font);
    }
}

void TextTool::updateFontItalic(const bool italic)
{
    m_font.setItalic(italic);
    if (m_widget != nullptr) {
        m_widget->setFont(m_font);
    }
}

void TextTool::move(const QPoint& pos)
{
    m_textArea.moveTo(pos);
}

void TextTool::updateAlignment(Qt::AlignmentFlag alignment)
{
    m_alignment = alignment;
    if (m_widget != nullptr) {
        m_widget->setAlignment(m_alignment);
    }
}

const QPoint* TextTool::pos()
{
    m_currentPos = m_textArea.topLeft();
    return &m_currentPos;
}

void TextTool::setEditMode(bool editMode)
{
    if (editMode) {
        m_textOld = m_text;
    }
    CaptureTool::setEditMode(editMode);
}

bool TextTool::isChanged()
{
    return QString::compare(m_text, m_textOld, Qt::CaseInsensitive) != 0;
}

void TextTool::saveState(QJsonObject& state) const
{
    CaptureTool::saveState(state);
    state.insert(QStringLiteral("text"), m_text);
    state.insert(QStringLiteral("font"), m_font.toString());
    state.insert(QStringLiteral("alignment"), static_cast<int>(m_alignment));
    state.insert(QStringLiteral("size"), m_size);
    state.insert(QStringLiteral("color"), toolstate::fromColor(m_color));
    state.insert(QStringLiteral("rect"), toolstate::fromRect(m_textArea));
}

bool TextTool::loadState(const QJsonObject& state)
{
    const QJsonValue text = state.value(QStringLiteral("text"));
    const QJsonValue font = state.value(QStringLiteral("font"));
    int alignment = 0;
    QFont loadedFont;
    if (!CaptureTool::loadState(state) || !text.isString() ||
        text.toString().isEmpty() || !font.isString() ||
        !loadedFont.fromString(font.toString()) ||
        !toolstate::toInt(state.value(QStringLiteral("alignment")),
                          0,
                          Qt::AlignHorizontal_Mask,
                          alignment) ||
        !toolstate::toInt(
          state.value(QStringLiteral("size")), 0, 10000, m_size) ||
        !toolstate::toColor(state.value(QStringLiteral("color")), m_color) ||
        !toolstate::toRect(state.value(QStringLiteral("rect")), m_textArea)) {
        return false;
    }
    m_text = text.toString();
    m_font = loadedFont;
    m_alignment = static_cast<Qt::AlignmentFlag>(alignment);
    return true;
}
