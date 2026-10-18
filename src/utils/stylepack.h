// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Style packs: one small .pixiestyle file (UTF-8 JSON) holding a whole
// Beautify / glass / background / Animate look, applied in one click.
//
//   {"format":"pixiestyle","version":1,"name":"...","author":"...",
//    "settings":{"<configKey>": value, ...}}
//
// Settings keys are ConfigHandler option names, limited by isStyleKey(), and
// every value goes through that option's own ValueHandler. Packs are data
// only: nothing in them is executed, and nothing outside the allow-list is
// ever read from or written to the config.
namespace stylepack {

constexpr int kFormatVersion = 1;
constexpr qint64 kMaxFileBytes = 64 * 1024;

QString fileSuffix(); // "pixiestyle", without the dot

struct Pack
{
    QString name;
    QString author;
    // Option key -> plain value: bool, int or string
    QVariantMap settings;
};

struct ParseResult
{
    bool ok = false;
    Pack pack; // keys already filtered and validated when ok
    QString error;
    QStringList warnings; // one per ignored key
};

// The allow-list, defined here and nowhere else: a recognized option whose
// key starts with "effect" or "anim". Behaviour options (beautifyAlways, save
// paths, upload, startup...) never match.
bool isStyleKey(const QString& key);
// Every recognized option that isStyleKey accepts, sorted
QStringList styleKeys();

ParseResult parse(const QByteArray& bytes);
QByteArray serialize(const Pack& pack);

// Reads the file, refusing anything over kMaxFileBytes before parsing
ParseResult loadFile(const QString& path);
// Writes atomically (QSaveFile); false with *error on failure
bool writeFile(const QString& path, const Pack& pack, QString* error);

// The current values of every style key
Pack fromCurrentConfig(const QString& name, const QString& author = {});
// Validates every key first, then writes only the valid allowed ones.
// Returns one warning per ignored key; writes nothing when the config is in
// an error state.
QStringList applyToConfig(const Pack& pack);

// Folder for user packs: <config dir>/styles (next to flameshot.ini)
QString userStylesDir();
// File name from a pack name: path separators, dots at the start and other
// unsafe characters removed, never empty, always ending in .pixiestyle
QString safeFileName(const QString& name);
// QDir(dir).filePath(safeFileName(name)), guaranteed to be directly in dir
QString packFilePath(const QString& dir, const QString& name);

struct Entry
{
    QString name;
    QString path;
    bool builtIn = false;
};
// Built-in packs (Qt resource, read-only) first, then the user's
QList<Entry> listPacks(const QString& userDir);
QString builtInDir(); // ":/stylepacks"

} // namespace stylepack
