// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>
#include <QString>

// The product is called Pixieshot, but Qt derives the config file
// (~/.config/flameshot/flameshot.ini), history and project folders from these
// two names. They stay "flameshot" so existing user data keeps working.
namespace AppIdentity {
inline QString settingsName()
{
    return QStringLiteral("flameshot");
}
inline QString displayName()
{
    return QStringLiteral("Pixieshot");
}
inline void apply()
{
    QCoreApplication::setApplicationName(settingsName());
    QCoreApplication::setOrganizationName(settingsName());
}
} // namespace AppIdentity
