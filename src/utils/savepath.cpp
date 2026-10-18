// SPDX-License-Identifier: GPL-3.0-or-later

#include "savepath.h"

#include <QDir>
#include <QFileInfo>

namespace savepath {

QString overwriteTarget(const QString& path, const QString& forceExtension)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    if (info.isDir()) {
        return {};
    }
    const QString suffix = info.suffix().toLower();
    const bool lossy = suffix == QLatin1String("jpg") ||
                       suffix == QLatin1String("jpeg");
    const QString forced = forceExtension.toLower();
    if (lossy && !forced.isEmpty() && forced != QLatin1String("jpg") &&
        forced != QLatin1String("jpeg")) {
        const QString sibling = info.dir().absoluteFilePath(
          info.completeBaseName() + QLatin1Char('.') + forced);
        return QFileInfo::exists(sibling) ? QString() : sibling;
    }
    if (info.isFile()) {
        return path;
    }
    if (!suffix.isEmpty() && info.dir().exists()) {
        return path;
    }
    return {};
}

} // namespace savepath
