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
    // Windowed editor, project saved as beautified: Beautify is applied when
    // the edit is saved, so pressing the button must not stack a second one
    AlreadyPendingOnSave,
};

// inEditor: the button sits in the windowed editor (not the overlay).
// lastStepIsBeautify: the newest undo step is an in-editor Beautify.
// beautifyPendingOnSave: the editor's request already carries BEAUTIFY.
BeautifyAction beautifyActionFor(bool inEditor,
                                 bool lastStepIsBeautify,
                                 bool beautifyPendingOnSave = false);

// Whether an export applies Beautify. An explicit request always does.
// "Beautify every capture" only applies to fresh captures: a saved image
// opened in the editor was already beautified when it was captured, so
// beautifying it again on every save nests frame inside frame.
bool shouldBeautify(bool requestedByTask,
                    bool beautifyAlways,
                    bool editingExistingImage);
