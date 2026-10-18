// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "capturetool.h"

#include <QPointF>
#include <optional>

class AbstractTwoPointTool : public CaptureTool
{
    Q_OBJECT
public:
    explicit AbstractTwoPointTool(QObject* parent = nullptr);

    bool isValid() const override;
    bool closeOnButtonPressed() const override;
    bool isSelectable() const override;
    bool showMousePreview() const override;
    QRect mousePreviewRect(const CaptureContext& context) const override;
    QRect boundingRect() const override;
    void move(const QPoint& pos) override;
    const QPoint* pos() override;
    int size() const override { return m_thickness; };
    const QColor& color() const { return m_color; };
    const QPair<QPoint, QPoint> points() const { return m_points; };
    void paintMousePreview(QPainter& painter,
                           const CaptureContext& context) override;
    void drawObjectSelection(QPainter& painter) override;
    bool isSaveable() const override { return true; }
    void saveState(QJsonObject& state) const override;
    bool loadState(const QJsonObject& state) override;

    // Bending: tools that opt in (arrow, line) show a handle at the curve
    // midpoint while selected. The bend is stored as the quadratic control
    // point; without it the tool is a straight line.
    virtual bool supportsBend() const { return false; }
    bool isBent() const { return m_control.has_value(); }
    // Where the handle sits: the curve midpoint (chord midpoint if straight).
    QPointF bendHandlePos() const;
    // Bend through handle. Within BendSnapPx of the straight midpoint the
    // bend is cleared instead.
    void setBendHandle(const QPointF& handle);
    // Paints only the round bend handle (no selection frame)
    void drawBendHandle(QPainter& painter) const;
    static constexpr double BendSnapPx = 3.0;
    static constexpr int BendHandleRadiusPx = 5;

public slots:
    void drawEnd(const QPoint& p) override;
    void drawMove(const QPoint& p) override;
    void drawMoveWithAdjustment(const QPoint& p) override;
    void onColorChanged(const QColor& c) override;
    void onSizeChanged(int size) override;
    virtual void drawStart(const CaptureContext& context) override;

private:
    QPoint adjustedVector(QPoint v) const;

protected:
    void copyParams(const AbstractTwoPointTool* from, AbstractTwoPointTool* to);
    const std::optional<QPointF>& control() const { return m_control; }
    void setPadding(int padding) { m_padding = padding; };

private:
    // class members
    int m_thickness;
    int m_padding;
    QColor m_color;
    QPair<QPoint, QPoint> m_points;
    std::optional<QPointF> m_control;

protected:
    // use m_padding to extend the area of the backup
    bool m_supportsOrthogonalAdj = false;
    bool m_supportsDiagonalAdj = false;
};
