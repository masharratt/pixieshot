// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "toolfactory.h"
#include "tools/drawabletools.h"
#include "tools/accept/accepttool.h"
#include "tools/copy/copytool.h"
#include "tools/exit/exittool.h"
#ifdef ENABLE_IMGUR
#include "tools/imgupload/imguploadertool.h"
#endif
#include "tools/launcher/applaunchertool.h"
#include "tools/move/movetool.h"
#include "tools/beautify/beautifytool.h"
#if defined(Q_OS_MACOS)
#include "tools/animate/animatetool.h"
#include "tools/ocr/ocrtool.h"
#endif
#include "tools/pin/pintool.h"
#include "tools/redo/redotool.h"
#include "tools/save/savetool.h"
#include "tools/sizedecrease/sizedecreasetool.h"
#include "tools/sizeincrease/sizeincreasetool.h"
#include "tools/undo/undotool.h"

ToolFactory::ToolFactory(QObject* parent)
  : QObject(parent)
{}

CaptureTool* ToolFactory::CreateTool(CaptureTool::Type t, QObject* parent)
{
#define if_TYPE_return_TOOL(TYPE, TOOL)                                        \
    case CaptureTool::TYPE:                                                    \
        return new TOOL(parent)

    if (CaptureTool* drawable = createDrawableTool(t, parent)) {
        return drawable;
    }
    switch (t) {
        if_TYPE_return_TOOL(TYPE_MOVESELECTION, MoveTool);
        if_TYPE_return_TOOL(TYPE_UNDO, UndoTool);
        if_TYPE_return_TOOL(TYPE_COPY, CopyTool);
        if_TYPE_return_TOOL(TYPE_SAVE, SaveTool);
        if_TYPE_return_TOOL(TYPE_EXIT, ExitTool);
#ifdef ENABLE_IMGUR
        if_TYPE_return_TOOL(TYPE_IMAGEUPLOADER, ImgUploaderTool);
#endif
#if !defined(Q_OS_MACOS)
        if_TYPE_return_TOOL(TYPE_OPEN_APP, AppLauncher);
#endif
        if_TYPE_return_TOOL(TYPE_REDO, RedoTool);
        if_TYPE_return_TOOL(TYPE_PIN, PinTool);
        if_TYPE_return_TOOL(TYPE_SIZEINCREASE, SizeIncreaseTool);
        if_TYPE_return_TOOL(TYPE_SIZEDECREASE, SizeDecreaseTool);
        if_TYPE_return_TOOL(TYPE_ACCEPT, AcceptTool);
        if_TYPE_return_TOOL(TYPE_BEAUTIFY, BeautifyTool);
#if defined(Q_OS_MACOS)
        if_TYPE_return_TOOL(TYPE_OCR, OcrTool);
        if_TYPE_return_TOOL(TYPE_ANIMATE, AnimateTool);
#endif
        default:
            return nullptr;
    }
}
