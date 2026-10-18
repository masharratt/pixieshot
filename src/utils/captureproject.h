// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>

class CaptureTool;
class QObject;

// A re-editable capture: the base image without annotations or effects, and
// every annotation as data, so it can be opened again with the objects still
// movable and editable.
struct CaptureProject
{
    // Device pixels; devicePixelRatio is kept through save and load
    QImage base;
    // One JSON object per annotation, bottom first, in logical points
    // relative to the base image's top-left
    QJsonArray objects;
    // The saved file was Beautified on export while this base is not (the
    // effect is not part of the base), so editing again must re-apply it
    bool beautified = false;

    bool isEmpty() const { return base.isNull(); }
};

// Single file: a small binary header, then the JSON (UTF-8) and the base
// image as PNG, each length-prefixed (QDataStream, Qt 6.0 format):
//   quint32 kMagic, quint32 format version, QByteArray json, QByteArray png
// The JSON is { "dpr": number, "objects": [ { "type": "TYPE_ARROW", ... } ] }.
namespace captureproject {

constexpr quint32 kMagic = 0x46535052; // "FSPR"
// Bumped only when old readers could not read the file. New tool types do
// not bump it: older readers skip types they do not know.
constexpr quint32 kFormatVersion = 1;
inline QString fileSuffix()
{
    return QStringLiteral("fsproj");
}

// Saves each saveable tool, moved by `shift`. The tools are not changed.
QJsonArray encodeObjects(const QList<QPointer<CaptureTool>>& tools,
                         const QPoint& shift);
// Recreates the tools, moved by `shift`, owned by `parent` (the caller
// deletes them when parent is null). Entries of unknown types, action tools
// and damaged entries are skipped with a logged warning, also appended to
// `warnings` when given.
QList<CaptureTool*> decodeObjects(const QJsonArray& objects,
                                  const QPoint& shift,
                                  QObject* parent,
                                  QStringList* warnings);

// Empty when the base image cannot be encoded as PNG
QByteArray encode(const CaptureProject& project);
// On failure `out` is left empty and `error` says why, in words for the user
bool decode(const QByteArray& bytes, CaptureProject& out, QString* error);

bool save(const QString& path, const CaptureProject& project, QString* error);
bool load(const QString& path, CaptureProject& out, QString* error);

} // namespace captureproject
