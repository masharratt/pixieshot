// SPDX-License-Identifier: GPL-3.0-or-later

#include "beautifyaction.h"

BeautifyAction beautifyActionFor(bool inEditor, bool lastStepIsBeautify)
{
    if (!inEditor) {
        return BeautifyAction::FinishCapture;
    }
    return lastStepIsBeautify ? BeautifyAction::RevertInEditor
                              : BeautifyAction::ApplyInEditor;
}
