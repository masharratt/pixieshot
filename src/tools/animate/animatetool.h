// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/abstractactiontool.h"

// Export with an animated background (macOS). In the capture overlay it
// finishes like Beautify (the request's tasks run on the beautified image)
// and also writes the looping MP4/GIF next to it; in the windowed editor it
// exports the current image and the window stays open.
class AnimateTool : public AbstractActionTool
{
    Q_OBJECT
public:
    explicit AnimateTool(QObject* parent = nullptr);

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
