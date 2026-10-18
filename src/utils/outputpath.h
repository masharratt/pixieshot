// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <functional>

namespace outputpath {

// Claims a file name by creating it empty, so a second caller that asks
// before the real file is written gets a different name. `propose` returns the
// first name that is free on disk; it is called again when the claim loses a
// race.
QString reserve(const std::function<QString()>& propose);

} // namespace outputpath
