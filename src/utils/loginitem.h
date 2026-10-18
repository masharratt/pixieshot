// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

// macOS login item ("launch at startup") handling through System Events.
// The script builders and path logic are plain functions so they can be
// tested; the run functions do nothing off macOS.
namespace loginitem {

// The .app that contains the running executable, or /Applications/Pixieshot.app
// when the executable is not inside a bundle
QString bundlePath(const QString& executablePath);

QString addScript(const QString& bundle);
QString removeScript(const QString& name);
QString existsScript(const QString& name);

// Names an earlier build used for its login item
QStringList legacyNames();

// Login item of the running bundle
bool exists();
// Replaces any old item (current or legacy name) with one for this bundle
bool enable();
// Removes the item and any legacy one
void disable();
// Drops a legacy item and, when launch at startup is wanted, makes sure the
// current one exists. Returns true when anything was changed.
bool migrateLegacy(bool wanted);

} // namespace loginitem
