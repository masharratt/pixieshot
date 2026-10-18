// SPDX-License-Identifier: GPL-3.0-or-later

#include "captureproject.h"
#include "tools/capturetool.h"
#include "tools/drawabletools.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QSaveFile>
#include <QUuid>

#include <cmath>
#include <memory>

namespace captureproject {

namespace {

const auto kDprKey = QStringLiteral("dpr");
const auto kObjectsKey = QStringLiteral("objects");
const auto kBeautifiedKey = QStringLiteral("beautified");
const auto kTypeKey = QStringLiteral("type");
const auto kAnimKey = QStringLiteral("anim");

QString tr(const char* text)
{
    return QCoreApplication::translate("CaptureProject", text);
}

QString notAProject()
{
    return tr("This is not a Pixieshot project file.");
}

QString damaged()
{
    return tr("The project file is damaged or incomplete.");
}

void warn(QStringList* warnings, const QString& message)
{
    qWarning().noquote() << "Capture project:" << message;
    if (warnings) {
        warnings->append(message);
    }
}

// Moves a tool by `shift` through its own move(), like the canvas expansion
void shiftTool(CaptureTool* tool, const QPoint& shift)
{
    if (!shift.isNull() && tool->pos()) {
        tool->move(*tool->pos() + shift);
    }
}

void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

QJsonArray encodeObjects(const QList<QPointer<CaptureTool>>& tools,
                         const QPoint& shift)
{
    const QMetaEnum types = QMetaEnum::fromType<CaptureTool::Type>();
    QJsonArray out;
    for (const QPointer<CaptureTool>& tool : tools) {
        if (!tool || !tool->isSaveable()) {
            continue;
        }
        // Work on a copy so the caller's objects stay where they are
        std::unique_ptr<CaptureTool> copy(tool->copy(nullptr));
        shiftTool(copy.get(), shift);
        QJsonObject state;
        state.insert(kTypeKey, QString::fromLatin1(types.valueToKey(tool->type())));
        copy->saveState(state);
        out.append(state);
    }
    return out;
}

QList<CaptureTool*> decodeObjects(const QJsonArray& objects,
                                  const QPoint& shift,
                                  QObject* parent,
                                  QStringList* warnings)
{
    const QMetaEnum types = QMetaEnum::fromType<CaptureTool::Type>();
    QList<CaptureTool*> out;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        if (!objects.at(i).isObject()) {
            warn(warnings,
                 tr("Skipped entry %1: not an object.").arg(i + 1));
            continue;
        }
        const QJsonObject state = objects.at(i).toObject();
        const QString name = state.value(kTypeKey).toString();
        if (name.isEmpty()) {
            warn(warnings, tr("Skipped entry %1: no type.").arg(i + 1));
            continue;
        }
        bool known = false;
        const int value = types.keyToValue(name.toLatin1().constData(), &known);
        std::unique_ptr<CaptureTool> tool(
          known ? createDrawableTool(static_cast<CaptureTool::Type>(value))
                : nullptr);
        if (!tool || !tool->isSaveable()) {
            warn(warnings,
                 tr("Skipped an annotation of unknown type %1 (saved by a "
                    "newer Pixieshot?).")
                   .arg(name));
            continue;
        }
        if (!tool->loadState(state) || !tool->isValid()) {
            warn(warnings, tr("Skipped a damaged %1 annotation.").arg(name));
            continue;
        }
        shiftTool(tool.get(), shift);
        tool->setParent(parent);
        out.append(tool.release());
    }
    return out;
}

QByteArray encode(const CaptureProject& project)
{
    QJsonObject header;
    header.insert(kDprKey, project.base.devicePixelRatio());
    header.insert(kObjectsKey, project.objects);
    header.insert(kBeautifiedKey, project.beautified);
    if (project.isAnimation()) {
        header.insert(kAnimKey, project.anim);
    }

    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (project.base.isNull() || !project.base.save(&buffer, "PNG")) {
            return {};
        }
    }

    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kMagic << kFormatVersion
           << QJsonDocument(header).toJson(QJsonDocument::Compact) << png;
    return bytes;
}

bool decode(const QByteArray& bytes, CaptureProject& out, QString* error)
{
    out = CaptureProject();
    QDataStream stream(bytes);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    stream >> magic;
    if (stream.status() != QDataStream::Ok || magic != kMagic) {
        setError(error, notAProject());
        return false;
    }
    stream >> version;
    if (stream.status() != QDataStream::Ok || version == 0) {
        setError(error, damaged());
        return false;
    }
    if (version > kFormatVersion) {
        setError(error,
                 tr("This project was saved by a newer version of Pixieshot "
                    "(project format %1; this version reads up to %2).")
                   .arg(version)
                   .arg(kFormatVersion));
        return false;
    }

    QByteArray json;
    QByteArray png;
    stream >> json >> png;
    if (stream.status() != QDataStream::Ok) {
        setError(error, damaged());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    const QJsonObject header = doc.object();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
        !header.value(kObjectsKey).isArray()) {
        setError(error, damaged());
        return false;
    }

    QImage base;
    if (!base.loadFromData(png, "PNG")) {
        setError(error, damaged());
        return false;
    }
    const double dpr = header.value(kDprKey).toDouble(1.0);
    base.setDevicePixelRatio(std::isfinite(dpr) && dpr >= 1.0 ? dpr : 1.0);

    out.base = base;
    out.objects = header.value(kObjectsKey).toArray();
    out.beautified = header.value(kBeautifiedKey).toBool(false);
    out.anim = header.value(kAnimKey).toObject();
    return true;
}

bool save(const QString& path, const CaptureProject& project, QString* error)
{
    QSaveFile file(path);
    const QByteArray bytes = encode(project);
    if (bytes.isEmpty()) {
        setError(error,
                 tr("The capture image could not be encoded, so the project "
                    "was not saved."));
        return false;
    }
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error,
                 tr("Could not save the project to %1: %2")
                   .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    return true;
}

bool load(const QString& path, CaptureProject& out, QString* error)
{
    out = CaptureProject();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error,
                 tr("Could not open the project %1: %2")
                   .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    QString why;
    if (!decode(file.readAll(), out, &why)) {
        setError(error,
                 QStringLiteral("%1\n%2").arg(QDir::toNativeSeparators(path),
                                              why));
        return false;
    }
    return true;
}

QString saveInFolder(const QString& folder,
                     const CaptureProject& project,
                     QString* error)
{
    QDir().mkpath(folder);
    const QString path =
      folder + QLatin1Char('/') +
      QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
      QLatin1Char('-') +
      QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) +
      QLatin1Char('.') + fileSuffix();
    if (!save(path, project, error)) {
        return {};
    }
    return path;
}

} // namespace captureproject
