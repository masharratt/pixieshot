// SPDX-License-Identifier: GPL-3.0-or-later

#include "beautifyaction.h"

BeautifyAction beautifyActionFor(bool inEditor,
                                 bool lastStepIsBeautify,
                                 bool beautifyPendingOnSave)
{
    if (!inEditor) {
        return BeautifyAction::FinishCapture;
    }
    if (beautifyPendingOnSave) {
        return BeautifyAction::AlreadyPendingOnSave;
    }
    return lastStepIsBeautify ? BeautifyAction::RevertInEditor
                              : BeautifyAction::ApplyInEditor;
}

bool shouldBeautify(bool requestedByTask,
                    bool beautifyAlways,
                    bool editingExistingImage)
{
    return requestedByTask || (beautifyAlways && !editingExistingImage);
}
