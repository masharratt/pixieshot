// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "tools/abstracttwopointtool.h"

#include <QPainter>
#include <QPainterPath>

class ArrowTool : public AbstractTwoPointTool
{
    Q_OBJECT
public:
    explicit ArrowTool(QObject* parent = nullptr);

    QIcon icon(const QColor& background, bool inEditor) const override;
    QString name() const override;
    QString description() const override;
    bool supportsBend() const override { return true; }
    QRect boundingRect() const override;
    QWidget* configurationWidget() override;
    void saveState(QJsonObject& state) const override;
    bool loadState(const QJsonObject& state) override;

    CaptureTool* copy(QObject* parent = nullptr) override;
    void process(QPainter& painter, const QPixmap& pixmap) override;
    void processReveal(QPainter& painter,
                       const QPixmap& pixmap,
                       double progress) override;

protected:
    void copyParams(const ArrowTool* from, ArrowTool* to);
    CaptureTool::Type type() const override;

public slots:
    void pressed(CaptureContext& context) override;

private slots:
    void setArrowStyle(int style);

private:
    // Everything the arrow draws: the shaft (a line or a curve), its pen,
    // and the head at `tip`
    struct Shape
    {
        QPainterPath shaft;
        QLineF line; // the straight shaft, when not bent
        bool bent = false;
        QPen pen;
        QPainterPath head;
        QPoint tip;
        bool straight() const { return !bent; }
    };
    Shape shape() const;
    Shape bentShape(const QPoint& start, const QPoint& tip) const;

    enum class ArrowStyle
    {
        Default = 0,
        Curved = 1,
    };

    QPainterPath m_arrowPath;
    ArrowStyle m_arrowStyle = ArrowStyle::Default;
};
