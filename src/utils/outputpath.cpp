// SPDX-License-Identifier: GPL-3.0-or-later

#include "outputpath.h"

#include <QFile>

namespace outputpath {

QString reserve(const std::function<QString()>& propose)
{
    QString path;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        path = propose();
        // NewOnly fails when the file already exists, which makes the claim
        // atomic across threads and processes
        QFile claim(path);
        if (claim.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            return path;
        }
    }
    return path;
}

} // namespace outputpath
