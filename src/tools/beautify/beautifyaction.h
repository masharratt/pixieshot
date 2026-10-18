// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// What the Beautify toolbar button does
enum class BeautifyAction
{
    // Capture overlay: finish like Accept, the request's own tasks (and any
    // after-capture actions) run on the beautified image
    FinishCapture,
    // Windowed editor: replace the image with its beautified version in
    // place, as one undoable step; the window stays open
    ApplyInEditor,
    // Windowed editor, pressed again right after Beautify: undo it, so the
    // button toggles instead of stacking padding on padding
    RevertInEditor,
};

// inEditor: the button sits in the windowed editor (not the overlay).
// lastStepIsBeautify: the newest undo step is an in-editor Beautify.
BeautifyAction beautifyActionFor(bool inEditor, bool lastStepIsBeautify);
