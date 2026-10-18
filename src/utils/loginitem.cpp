// SPDX-License-Identifier: GPL-3.0-or-later

#include "loginitem.h"
#include "appidentity.h"

#include <QCoreApplication>
#include <QDebug>
#include <QProcess>

namespace loginitem {

namespace {

const char* kFallbackBundle = "/Applications/Pixieshot.app";

#if defined(Q_OS_MACOS)
// Runs one AppleScript line; output is empty and ok false on failure
bool runScript(const QString& script, QString* output = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("osascript"),
                  QStringList() << QStringLiteral("-e") << script);
    if (!process.waitForFinished(15000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        qWarning() << "Login item script failed:" << process.errorString()
                   << process.readAllStandardError();
        return false;
    }
    if (output) {
        *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    return true;
}

bool existsNamed(const QString& name)
{
    QString out;
    return runScript(existsScript(name), &out) && out == QLatin1String("true");
}
#endif

QString quoted(const QString& s)
{
    QString e = s;
    e.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    e.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return e;
}

} // namespace

QString bundlePath(const QString& executablePath)
{
    const QString marker = QStringLiteral(".app/");
    const int i = executablePath.indexOf(marker);
    if (i < 0) {
        return QString::fromLatin1(kFallbackBundle);
    }
    return executablePath.left(i + 4);
}

QString addScript(const QString& bundle)
{
    return QStringLiteral("tell application \"System Events\" to make login "
                          "item at end with properties {name: \"%1\",path:\"%2\""
                          ", hidden:false}")
      .arg(AppIdentity::displayName(), quoted(bundle));
}

QString removeScript(const QString& name)
{
    return QStringLiteral(
             "tell application \"System Events\" to delete login item \"%1\"")
      .arg(quoted(name));
}

QString existsScript(const QString& name)
{
    return QStringLiteral(
             "tell application \"System Events\" to (name of every login item) "
             "contains \"%1\"")
      .arg(quoted(name));
}

QStringList legacyNames()
{
    return { QStringLiteral("Flameshot"), QStringLiteral("flameshot") };
}

bool exists()
{
#if defined(Q_OS_MACOS)
    return existsNamed(AppIdentity::displayName());
#else
    return false;
#endif
}

bool enable()
{
#if defined(Q_OS_MACOS)
    disable();
    return runScript(
      addScript(bundlePath(QCoreApplication::applicationFilePath())));
#else
    return false;
#endif
}

void disable()
{
#if defined(Q_OS_MACOS)
    QStringList names = legacyNames();
    names << AppIdentity::displayName();
    for (const QString& n : names) {
        if (existsNamed(n)) {
            runScript(removeScript(n));
        }
    }
#endif
}

bool migrateLegacy(bool wanted)
{
#if defined(Q_OS_MACOS)
    bool hadLegacy = false;
    for (const QString& n : legacyNames()) {
        if (existsNamed(n)) {
            hadLegacy = true;
            runScript(removeScript(n));
        }
    }
    if (wanted && (hadLegacy || !exists())) {
        runScript(addScript(bundlePath(QCoreApplication::applicationFilePath())));
        return true;
    }
    return hadLegacy;
#else
    Q_UNUSED(wanted)
    return false;
#endif
}

} // namespace loginitem
