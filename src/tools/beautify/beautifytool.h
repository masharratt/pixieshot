// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/abstractactiontool.h"

// Beautify effects (background, padding, shadow). In the capture overlay it
// finishes like Accept with the effects applied to the exported image; in
// the windowed editor it beautifies the image in place (undoable, pressing
// again reverts) and the window stays open.
class BeautifyTool : public AbstractActionTool
{
    Q_OBJECT
public:
    explicit BeautifyTool(QObject* parent = nullptr);

    bool closeOnButtonPressed() const override;

    QIcon icon(const QColor& background, bool inEditor) const override;
    QString name() const override;
    QString description() const override;

    CaptureTool* copy(QObject* parent = nullptr) override;

protected:
    CaptureTool::Type type() const override;

public slots:
    void pressed(CaptureContext& context) override;
};
