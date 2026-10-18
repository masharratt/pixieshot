// SPDX-License-Identifier: GPL-3.0-or-later

#include "stylepack.h"
#include "utils/confighandler.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <cmath>

namespace stylepack {

namespace {

const auto kFormatKey = QStringLiteral("format");
const auto kFormatValue = QStringLiteral("pixiestyle");
const auto kVersionKey = QStringLiteral("version");
const auto kNameKey = QStringLiteral("name");
const auto kAuthorKey = QStringLiteral("author");
const auto kDescriptionKey = QStringLiteral("description");
const auto kSettingsKey = QStringLiteral("settings");

// Longest file name stem; leaves room for the suffix in common limits
constexpr int kMaxStemLength = 64;

QString tr(const char* text)
{
    return QCoreApplication::translate("StylePack", text);
}

QString notAPack()
{
    return tr("This is not a Pixieshot style pack.");
}

QString damaged()
{
    return tr("The style pack is damaged or incomplete.");
}

QString tooLarge()
{
    return tr("The style pack is too large (over %1 KB).")
      .arg(kMaxFileBytes / 1024);
}

ParseResult failure(const QString& message)
{
    ParseResult r;
    r.error = message;
    return r;
}

// A JSON value as the plain QVariant a pack holds, or invalid when its JSON
// type does not match what the option stores (bool, whole number or text).
// The option's ValueHandler then checks the range or format.
QVariant normalise(const QString& key, const QJsonValue& json)
{
    const QVariant def = ConfigHandler::optionDefault(key);
    switch (def.typeId()) {
        case QMetaType::Bool:
            return json.isBool() ? QVariant(json.toBool()) : QVariant();
        case QMetaType::Int: {
            if (!json.isDouble()) {
                return {};
            }
            const double d = json.toDouble();
            if (std::floor(d) != d || std::abs(d) > 1e9) {
                return {};
            }
            return QVariant(static_cast<int>(d));
        }
        case QMetaType::QColor:
        case QMetaType::QString:
            return json.isString() ? QVariant(json.toString()) : QVariant();
        default:
            // Any other option kind: hand the raw value to its check
            return json.toVariant();
    }
}

// The reason a key/value pair cannot be applied, or empty when it can
QString rejection(const QString& key, const QVariant& value)
{
    if (!ConfigHandler::recognizedGeneralOptions().contains(key)) {
        return tr("Ignored \"%1\": not a Pixieshot setting.").arg(key);
    }
    if (!isStyleKey(key)) {
        return tr("Ignored \"%1\": not a look setting, packs cannot change "
                  "it.")
          .arg(key);
    }
    if (!value.isValid() || !ConfigHandler::isValidOptionValue(key, value)) {
        return tr("Ignored \"%1\": value is out of range or the wrong type.")
          .arg(key);
    }
    return {};
}

void warn(QStringList* warnings, const QString& message)
{
    qWarning().noquote() << "Style pack:" << message;
    warnings->append(message);
}

// A config value as the plain value a pack stores. The type comes from the
// option's default: QSettings hands numbers back as text.
QVariant plainValue(const QString& key, const QVariant& value)
{
    switch (ConfigHandler::optionDefault(key).typeId()) {
        case QMetaType::Bool:
            return value.toBool();
        case QMetaType::Int:
            return value.toInt();
        case QMetaType::QString:
            return value.toString();
        case QMetaType::QColor: {
            const QColor c = value.value<QColor>();
            if (c.alpha() == 255) {
                return c.name(QColor::HexRgb);
            }
            // Config colours with alpha are written #RRGGBBAA
            return c.name(QColor::HexRgb) +
                   QStringLiteral("%1").arg(c.alpha(), 2, 16, QLatin1Char('0'));
        }
        default:
            return value.toString();
    }
}

} // namespace

QString fileSuffix()
{
    return QStringLiteral("pixiestyle");
}

bool isStyleKey(const QString& key)
{
    if (!ConfigHandler::recognizedGeneralOptions().contains(key)) {
        return false;
    }
    // Behaviour or delivery, not look: applying a pack must not switch the
    // user's MP4/GIF choice or frame rate. beautifyAlways is listed so a
    // future option with a look-like name cannot slip in by accident.
    static const QStringList kDenied = { QStringLiteral("beautifyAlways"),
                                         QStringLiteral("animFormat"),
                                         QStringLiteral("animFps") };
    if (kDenied.contains(key)) {
        return false;
    }
    return key.startsWith(QLatin1String("effect")) ||
           key.startsWith(QLatin1String("anim"));
}

QStringList styleKeys()
{
    QStringList keys;
    for (const QString& key : ConfigHandler::recognizedGeneralOptions()) {
        if (isStyleKey(key)) {
            keys << key;
        }
    }
    keys.sort();
    return keys;
}

ParseResult parse(const QByteArray& bytes)
{
    if (bytes.size() > kMaxFileBytes) {
        return failure(tooLarge());
    }
    if (bytes.trimmed().isEmpty()) {
        return failure(notAPack());
    }
    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        return failure(damaged());
    }
    if (!doc.isObject()) {
        return failure(notAPack());
    }
    const QJsonObject root = doc.object();
    if (root.value(kFormatKey).toString() != kFormatValue) {
        return failure(notAPack());
    }
    const QJsonValue versionValue = root.value(kVersionKey);
    if (!versionValue.isDouble() || versionValue.toInt() < 1) {
        return failure(damaged());
    }
    if (versionValue.toInt() > kFormatVersion) {
        return failure(
          tr("This style pack was made by a newer Pixieshot (format %1). "
             "Update Pixieshot to use it.")
            .arg(versionValue.toInt()));
    }
    if (!root.value(kSettingsKey).isObject()) {
        return failure(damaged());
    }

    ParseResult r;
    r.ok = true;
    r.pack.name = root.value(kNameKey).toString().trimmed();
    if (r.pack.name.isEmpty()) {
        r.pack.name = tr("Untitled style");
    }
    r.pack.author = root.value(kAuthorKey).toString().trimmed();
    r.pack.description =
      root.value(kDescriptionKey).toString().simplified().left(
        kMaxDescriptionLength);
    const QJsonObject settings = root.value(kSettingsKey).toObject();
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const QVariant value = normalise(it.key(), it.value());
        const QString why = rejection(it.key(), value);
        if (!why.isEmpty()) {
            warn(&r.warnings, why);
            continue;
        }
        r.pack.settings.insert(it.key(), value);
    }
    return r;
}

QByteArray serialize(const Pack& pack)
{
    QJsonObject settings;
    for (auto it = pack.settings.cbegin(); it != pack.settings.cend(); ++it) {
        settings.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    QJsonObject root;
    root.insert(kFormatKey, kFormatValue);
    root.insert(kVersionKey, kFormatVersion);
    root.insert(kNameKey, pack.name);
    if (!pack.author.isEmpty()) {
        root.insert(kAuthorKey, pack.author);
    }
    if (!pack.description.isEmpty()) {
        root.insert(kDescriptionKey, pack.description);
    }
    root.insert(kSettingsKey, settings);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ParseResult loadFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(tr("Could not open %1.").arg(path));
    }
    // Refuse before reading, so a huge file is never loaded into memory
    if (file.size() > kMaxFileBytes) {
        return failure(tooLarge());
    }
    return parse(file.read(kMaxFileBytes + 1));
}

bool writeFile(const QString& path, const Pack& pack, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(serialize(pack)) < 0 || !file.commit()) {
        if (error) {
            *error = tr("Could not write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

Pack fromCurrentConfig(const QString& name, const QString& author)
{
    Pack pack;
    pack.name = name;
    pack.author = author;
    ConfigHandler config;
    for (const QString& key : styleKeys()) {
        pack.settings.insert(key, plainValue(key, config.value(key)));
    }
    return pack;
}

QStringList applyToConfig(const Pack& pack)
{
    QStringList warnings;
    if (pack.settings.isEmpty()) {
        return warnings;
    }
    ConfigHandler config;
    if (config.hasError()) {
        warn(&warnings,
             tr("Your settings file has an error, so the style pack was not "
                "applied. Fix it in the configuration first."));
        return warnings;
    }
    // Validate everything first, then write only what passed
    QVariantMap accepted;
    for (auto it = pack.settings.cbegin(); it != pack.settings.cend(); ++it) {
        const QString why = rejection(it.key(), it.value());
        if (why.isEmpty()) {
            accepted.insert(it.key(), it.value());
        } else {
            warn(&warnings, why);
        }
    }
    for (auto it = accepted.cbegin(); it != accepted.cend(); ++it) {
        config.setValue(it.key(), it.value());
    }
    return warnings;
}

QString userStylesDir()
{
    const QString configDir =
      QFileInfo(ConfigHandler().configFilePath()).absolutePath();
    return QDir(configDir).filePath(QStringLiteral("styles"));
}

QString safeFileName(const QString& name)
{
    QString stem;
    for (const QChar c : name) {
        static const QString kUnsafe = QStringLiteral("/\\:*?\"<>|");
        if (!c.isPrint() || kUnsafe.contains(c)) {
            stem += QLatin1Char('-');
        } else {
            stem += c;
        }
    }
    stem = stem.simplified();
    // No hidden files and no "." / ".." stems
    while (stem.startsWith(QLatin1Char('.')) ||
           stem.startsWith(QLatin1Char('-')) ||
           stem.startsWith(QLatin1Char(' '))) {
        stem.remove(0, 1);
    }
    stem = stem.left(kMaxStemLength).trimmed();
    while (stem.endsWith(QLatin1Char('.'))) {
        stem.chop(1);
    }
    if (stem.isEmpty()) {
        stem = QStringLiteral("Style");
    }
    return stem + QLatin1Char('.') + fileSuffix();
}

QString packFilePath(const QString& dir, const QString& name)
{
    return QDir(dir).filePath(safeFileName(name));
}

QString builtInDir()
{
    return QStringLiteral(":/stylepacks");
}

QList<Entry> listPacks(const QString& userDir)
{
    QList<Entry> entries;
    const QStringList filter = { QStringLiteral("*.") + fileSuffix() };
    for (const auto& [dir, builtIn] :
         { std::pair{ builtInDir(), true }, std::pair{ userDir, false } }) {
        if (dir.isEmpty()) {
            continue;
        }
        const QFileInfoList files =
          QDir(dir).entryInfoList(filter, QDir::Files, QDir::Name);
        for (const QFileInfo& info : files) {
            if (info.size() > kMaxFileBytes) {
                continue;
            }
            const ParseResult r = loadFile(info.filePath());
            Entry e;
            e.path = info.filePath();
            e.builtIn = builtIn;
            // A broken file still shows, by file name, so Apply can say why
            e.name = r.ok ? r.pack.name : info.completeBaseName();
            entries << e;
        }
    }
    return entries;
}

} // namespace stylepack
