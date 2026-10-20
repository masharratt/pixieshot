// SPDX-License-Identifier: GPL-3.0-or-later
// The Animate pickers (style, shot motion, format), filled the same way in
// the settings and in the Animate preview. Item data is the config value.

#pragma once

class QComboBox;
class QString;

namespace animpickers {

// The name a style is shown by (pickers, style pack chips)
QString styleName(int value);
void fillStyles(QComboBox* combo);
void fillMotions(QComboBox* combo);
void fillFormats(QComboBox* combo);
// Selects the item whose data is `value` (the first item when none is)
void select(QComboBox* combo, int value);

} // namespace animpickers
