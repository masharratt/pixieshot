// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace savepath {

// Where an overwriting save (the windowed editor) writes. Returns the exact
// file, or an empty string when the caller should fall back to the usual
// numbered name.
//  - an existing file is replaced in place;
//  - a path that does not exist yet (an opened project's "a.png") is written
//    as named instead of picking another extension and then numbering;
//  - an existing JPEG that now needs transparency (forceExtension "png")
//    gets a PNG sibling, because JPEG would turn transparent pixels black.
//    An existing sibling is never replaced (empty result: numbered).
QString overwriteTarget(const QString& path, const QString& forceExtension);

} // namespace savepath
