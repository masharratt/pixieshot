// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QString>
#include <QWidget>

class QPixmap;

bool saveToFilesystem(const QPixmap& capture,
                      const QString& path,
                      const QString& messagePrefix = "",
                      QString* savedPath = nullptr,
                      bool overwrite = false,
                      const QString& forceExtension = QString());
QString ShowSaveFileDialog(const QString& title,
                           const QString& directory,
                           const QString& forceExtension = QString());
void saveToClipboardMime(const QPixmap& capture, const QString& imageType);
void saveToClipboard(const QPixmap& capture);
// GNOME Wayland: keeps the widget alive until clipboard data is fetched
bool saveToClipboardGnomeWorkaround(const QPixmap& pixmap, QWidget* keepAlive);
// forceExtension: default file type instead of the configured one (e.g. "png"
// for captures with transparency)
bool saveToFilesystemGUI(const QPixmap& capture,
                         const QString& forceExtension = QString());
