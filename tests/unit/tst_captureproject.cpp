// SPDX-License-Identifier: GPL-3.0-or-later

// Re-editable projects: every annotation type must survive
// serialize -> deserialize and render exactly as before, and bad files must
// fail cleanly.

#include "fakes/drawobject.h"
#include "tools/abstracttwopointtool.h"
#include "tools/capturecontext.h"
#include "tools/capturetool.h"
#include "tools/drawabletools.h"
#include "utils/captureproject.h"
#include "utils/confighandler.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

const QSize kCanvas(160, 120);

// A base with structure everywhere, so pixelate and invert have real input
QPixmap baseCanvas()
{
    QImage img(kCanvas, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            img.setPixel(x, y, qRgb((x * 7) % 256, (y * 5) % 256, (x * y) % 256));
        }
    }
    return QPixmap::fromImage(img);
}

// Same drawing as CaptureWidget::drawToolsData: each object on top of the
// previous ones, antialiased
QImage render(const QList<CaptureTool*>& tools)
{
    QPixmap pm = baseCanvas();
    for (CaptureTool* t : tools) {
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        t->process(painter, pm);
    }
    return pm.toImage();
}

using testtools::drawObject;

QString typeName(CaptureTool::Type t)
{
    return QString::fromLatin1(
      QMetaEnum::fromType<CaptureTool::Type>().valueToKey(t));
}

QJsonObject objectJson(const QJsonArray& objects, int i)
{
    return objects.at(i).toObject();
}

// Wraps a tool list so test objects are always freed
struct Tools
{
    QList<CaptureTool*> list;
    ~Tools() { qDeleteAll(list); }
    QList<QPointer<CaptureTool>> pointers() const
    {
        QList<QPointer<CaptureTool>> out;
        for (CaptureTool* t : list) {
            out.append(t);
        }
        return out;
    }
};

QByteArray container(quint32 magic,
                     quint32 version,
                     const QByteArray& json,
                     const QByteArray& png)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_0);
    s << magic << version << json << png;
    return bytes;
}

QByteArray pngOf(const QImage& image)
{
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG");
    return png;
}

} // namespace

class TestCaptureProject : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Defaults that make every tool draw deterministically
        ConfigHandler cfg;
        cfg.setArrowStyle(0);
        cfg.setReverseArrow(false);
        cfg.setInsecurePixelate(false);
        cfg.setDrawCircleCounterOutline(true);
    }

    void roundTripRendersIdentically_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("variant");
        using T = CaptureTool;
        QTest::newRow("pencil") << int(T::TYPE_PENCIL) << QString();
        QTest::newRow("line") << int(T::TYPE_DRAWER) << QString();
        QTest::newRow("line-bent") << int(T::TYPE_DRAWER) << "bent";
        QTest::newRow("arrow") << int(T::TYPE_ARROW) << QString();
        QTest::newRow("arrow-bent") << int(T::TYPE_ARROW) << "bent";
        QTest::newRow("arrow-curved-head") << int(T::TYPE_ARROW)
                                           << "curvedHead";
        QTest::newRow("selection-rect") << int(T::TYPE_SELECTION) << QString();
        QTest::newRow("rectangle") << int(T::TYPE_RECTANGLE) << QString();
        QTest::newRow("circle") << int(T::TYPE_CIRCLE) << QString();
        QTest::newRow("marker") << int(T::TYPE_MARKER) << QString();
        QTest::newRow("pixelate") << int(T::TYPE_PIXELATE) << QString();
        QTest::newRow("text") << int(T::TYPE_TEXT) << QString();
        QTest::newRow("circle-counter") << int(T::TYPE_CIRCLECOUNT)
                                        << QString();
        QTest::newRow("invert") << int(T::TYPE_INVERT) << QString();
        QTest::newRow("ruler") << int(T::TYPE_RULER) << QString();
    }

    void roundTripRendersIdentically()
    {
        QFETCH(int, type);
        QFETCH(QString, variant);
        Tools original;
        original.list.append(
          drawObject(static_cast<CaptureTool::Type>(type), variant));
        QVERIFY2(original.list.first(), "type is not drawable");
        QVERIFY(original.list.first()->isValid());

        const QImage before = render(original.list);
        // The object must actually draw, or the comparison proves nothing
        QVERIFY2(before != baseCanvas().toImage(), "object drew nothing");

        const QJsonArray json =
          captureproject::encodeObjects(original.pointers(), QPoint());
        QCOMPARE(json.size(), 1);
        QCOMPARE(objectJson(json, 0).value("type").toString(),
                 typeName(static_cast<CaptureTool::Type>(type)));

        QStringList warnings;
        Tools restored;
        restored.list =
          captureproject::decodeObjects(json, QPoint(), nullptr, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("; ")));
        QCOMPARE(restored.list.size(), 1);
        QCOMPARE(int(restored.list.first()->type()), type);

        QCOMPARE(render(restored.list), before);
        // Saving the restored object again gives the same data
        QCOMPARE(captureproject::encodeObjects(restored.pointers(), QPoint()),
                 json);
    }

    // Every type the factory can draw has a round-trip row above
    void everyDrawableTypeIsCovered()
    {
        const QSet<int> covered = {
            CaptureTool::TYPE_PENCIL,    CaptureTool::TYPE_DRAWER,
            CaptureTool::TYPE_ARROW,     CaptureTool::TYPE_SELECTION,
            CaptureTool::TYPE_RECTANGLE, CaptureTool::TYPE_CIRCLE,
            CaptureTool::TYPE_MARKER,    CaptureTool::TYPE_PIXELATE,
            CaptureTool::TYPE_TEXT,      CaptureTool::TYPE_CIRCLECOUNT,
            CaptureTool::TYPE_INVERT,    CaptureTool::TYPE_RULER,
        };
        const QMetaEnum e = QMetaEnum::fromType<CaptureTool::Type>();
        QStringList missing;
        QStringList unexpected;
        for (int i = 0; i < e.keyCount(); ++i) {
            const auto t = static_cast<CaptureTool::Type>(e.value(i));
            std::unique_ptr<CaptureTool> tool(createDrawableTool(t));
            const bool drawable = tool && tool->isSaveable();
            if (drawable && !covered.contains(t)) {
                missing << QString::fromLatin1(e.key(i));
            }
            if (!drawable && covered.contains(t)) {
                unexpected << QString::fromLatin1(e.key(i));
            }
        }
        QVERIFY2(missing.isEmpty() && unexpected.isEmpty(),
                 qPrintable(QStringLiteral("missing=[%1] unexpected=[%2]")
                              .arg(missing.join(','), unexpected.join(','))));
    }

    // Several objects, stacked: the order is kept and the counter numbers too
    void stackOfObjectsKeepsOrderAndCounters()
    {
        Tools original;
        original.list << drawObject(CaptureTool::TYPE_RECTANGLE, QString())
                      << drawObject(CaptureTool::TYPE_PIXELATE, QString())
                      << drawObject(CaptureTool::TYPE_CIRCLECOUNT, QString())
                      << drawObject(CaptureTool::TYPE_CIRCLECOUNT, QString())
                      << drawObject(CaptureTool::TYPE_TEXT, QString());
        original.list[3]->setCount(4);
        const QImage before = render(original.list);

        Tools restored;
        restored.list = captureproject::decodeObjects(
          captureproject::encodeObjects(original.pointers(), QPoint()),
          QPoint(),
          nullptr,
          nullptr);
        QCOMPARE(restored.list.size(), 5);
        QCOMPARE(restored.list[2]->count(), 3);
        QCOMPARE(restored.list[3]->count(), 4);
        QCOMPARE(render(restored.list), before);
    }

    // Restored objects are live tools: moving one moves it like the original
    void restoredObjectsStayEditable()
    {
        Tools original;
        original.list << drawObject(CaptureTool::TYPE_ARROW, "bent")
                      << drawObject(CaptureTool::TYPE_TEXT, QString())
                      << drawObject(CaptureTool::TYPE_PENCIL, QString());
        render(original.list); // lays out the text box, as the editor does
        Tools restored;
        restored.list = captureproject::decodeObjects(
          captureproject::encodeObjects(original.pointers(), QPoint()),
          QPoint(),
          nullptr,
          nullptr);
        QCOMPARE(restored.list.size(), 3);
        for (int i = 0; i < 3; ++i) {
            const QPoint to = *original.list[i]->pos() + QPoint(9, -4);
            original.list[i]->move(to);
            restored.list[i]->move(to);
        }
        const QImage moved = render(original.list);
        QCOMPARE(render(restored.list), moved);
        QVERIFY(moved != baseCanvas().toImage());
    }

    // Objects are stored relative to the base image and placed back at an
    // offset (the editor's margin)
    void shiftOnSaveAndLoadIsInverse()
    {
        Tools original;
        original.list << drawObject(CaptureTool::TYPE_ARROW, "bent")
                      << drawObject(CaptureTool::TYPE_TEXT, QString())
                      << drawObject(CaptureTool::TYPE_MARKER, QString());
        const QImage before = render(original.list);
        const QJsonArray json =
          captureproject::encodeObjects(original.pointers(), QPoint(-8, -8));
        // Stored coordinates really moved
        QCOMPARE(objectJson(json, 0).value("p1").toArray(),
                 QJsonArray({ 12, 22 }));
        Tools restored;
        restored.list =
          captureproject::decodeObjects(json, QPoint(8, 8), nullptr, nullptr);
        QCOMPARE(render(restored.list), before);
    }

    void fileRoundTripKeepsImageAndRatio()
    {
        Tools original;
        original.list << drawObject(CaptureTool::TYPE_ARROW, QString())
                      << drawObject(CaptureTool::TYPE_TEXT, QString());
        CaptureProject p;
        p.base = baseCanvas().toImage();
        p.base.setDevicePixelRatio(2.0);
        p.objects =
          captureproject::encodeObjects(original.pointers(), QPoint());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("a.fsproj");
        QString error;
        QVERIFY2(captureproject::save(path, p, &error), qPrintable(error));

        CaptureProject loaded;
        QVERIFY2(captureproject::load(path, loaded, &error), qPrintable(error));
        QCOMPARE(loaded.base.devicePixelRatio(), 2.0);
        QCOMPARE(loaded.base.size(), p.base.size());
        QCOMPARE(loaded.base.convertToFormat(QImage::Format_ARGB32),
                 p.base.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(loaded.objects, p.objects);
    }

    void newerFormatVersionIsRejected()
    {
        QJsonObject header;
        header.insert("dpr", 1.0);
        header.insert("objects", QJsonArray());
        const QByteArray bytes =
          container(captureproject::kMagic,
                    captureproject::kFormatVersion + 1,
                    QJsonDocument(header).toJson(QJsonDocument::Compact),
                    pngOf(baseCanvas().toImage()));
        CaptureProject p;
        QString error;
        QVERIFY(!captureproject::decode(bytes, p, &error));
        QVERIFY2(error.contains("newer"), qPrintable(error));
    }

    void currentFormatVersionIsAccepted()
    {
        QJsonObject header;
        header.insert("dpr", 1.0);
        header.insert("objects", QJsonArray());
        const QByteArray bytes =
          container(captureproject::kMagic,
                    captureproject::kFormatVersion,
                    QJsonDocument(header).toJson(QJsonDocument::Compact),
                    pngOf(baseCanvas().toImage()));
        CaptureProject p;
        QString error;
        QVERIFY2(captureproject::decode(bytes, p, &error), qPrintable(error));
        QCOMPARE(p.base.size(), kCanvas);
    }

    // Unknown (newer) types, action tools and damaged entries are skipped
    // with one warning each; the good objects still load
    void unknownAndBadObjectsAreSkippedWithWarning()
    {
        Tools good;
        good.list << drawObject(CaptureTool::TYPE_ARROW, QString())
                  << drawObject(CaptureTool::TYPE_RECTANGLE, QString());
        const QJsonArray valid =
          captureproject::encodeObjects(good.pointers(), QPoint());

        QJsonObject future = objectJson(valid, 0);
        future.insert("type", "TYPE_HOLOGRAM_FROM_THE_FUTURE");
        QJsonObject action;
        action.insert("type", "TYPE_SAVE");
        QJsonObject damaged = objectJson(valid, 1);
        damaged.remove("p1");
        QJsonObject noType;
        noType.insert("p1", QJsonArray({ 1, 2 }));

        const QJsonArray mixed = { future,  valid.at(0), action,
                                   damaged, QJsonValue(42), noType,
                                   valid.at(1) };
        QStringList warnings;
        Tools restored;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("TYPE_HOLOGRAM_FROM_THE_FUTURE"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("TYPE_SAVE"));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("damaged.*TYPE_RECTANGLE"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("not an object"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("no type"));
        restored.list =
          captureproject::decodeObjects(mixed, QPoint(), nullptr, &warnings);
        QCOMPARE(warnings.size(), 5);
        QCOMPARE(restored.list.size(), 2);
        QCOMPARE(restored.list[0]->type(), CaptureTool::TYPE_ARROW);
        QCOMPARE(restored.list[1]->type(), CaptureTool::TYPE_RECTANGLE);
    }

    void corruptFilesFailWithClearError_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("expect");

        QJsonObject header;
        header.insert("dpr", 1.0);
        header.insert("objects", QJsonArray());
        const QByteArray json =
          QJsonDocument(header).toJson(QJsonDocument::Compact);
        const QByteArray png = pngOf(baseCanvas().toImage());
        const QByteArray good = container(captureproject::kMagic,
                                          captureproject::kFormatVersion,
                                          json,
                                          png);

        QTest::newRow("empty") << QByteArray() << "not a Pixieshot project";
        QTest::newRow("random")
          << QByteArray("\x89PNG this is not a project at all", 33)
          << "not a Pixieshot project";
        QTest::newRow("truncated") << good.left(good.size() / 2) << "damaged";
        QTest::newRow("version-zero")
          << container(captureproject::kMagic, 0, json, png) << "damaged";
        QTest::newRow("bad-json")
          << container(captureproject::kMagic,
                       captureproject::kFormatVersion,
                       "{not json",
                       png)
          << "damaged";
        QJsonObject noObjects;
        noObjects.insert("dpr", 1.0);
        QTest::newRow("objects-missing")
          << container(captureproject::kMagic,
                       captureproject::kFormatVersion,
                       QJsonDocument(noObjects).toJson(),
                       png)
          << "damaged";
        QTest::newRow("bad-png")
          << container(captureproject::kMagic,
                       captureproject::kFormatVersion,
                       json,
                       "not a png")
          << "damaged";
    }

    void corruptFilesFailWithClearError()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, expect);
        CaptureProject p;
        QString error;
        QVERIFY(!captureproject::decode(bytes, p, &error));
        QVERIFY2(error.contains(expect, Qt::CaseInsensitive), qPrintable(error));
        QVERIFY(p.base.isNull());
        QVERIFY(p.objects.isEmpty());
    }

    // Bug: "Edit again" of a capture Beautified from the toolbar button saved
    // a plain file, because the project did not record that the export was
    // beautified.
    void beautifiedFlagSurvivesFileRoundTrip_bugEditAgainDropsBeautify()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const bool flag : { true, false }) {
            CaptureProject p;
            p.base = baseCanvas().toImage();
            p.beautified = flag;
            const QString path = dir.filePath(flag ? "b.fsproj" : "p.fsproj");
            QString error;
            QVERIFY2(captureproject::save(path, p, &error), qPrintable(error));
            CaptureProject loaded;
            QVERIFY2(captureproject::load(path, loaded, &error),
                     qPrintable(error));
            QCOMPARE(loaded.beautified, flag);
        }
    }

    // Projects written before the flag existed load as not beautified
    void missingBeautifiedKeyMeansFalse()
    {
        CaptureProject p;
        p.base = baseCanvas().toImage();
        CaptureProject out;
        QString error;
        QVERIFY2(captureproject::decode(captureproject::encode(p), out, &error),
                 qPrintable(error));
        QVERIFY(!out.beautified);
    }

    // Bug: a failed PNG encode was ignored, save() reported success and the
    // history linked a project that could never be opened again.
    void saveFailsWhenTheBaseCannotBeEncoded_bugEncodeFailureIgnored()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        CaptureProject p; // null base: PNG encode fails
        QVERIFY(captureproject::encode(p).isEmpty());
        const QString path = dir.filePath("bad.fsproj");
        QString error;
        QVERIFY(!captureproject::save(path, p, &error));
        QVERIFY2(!error.isEmpty(), "save must say why");
        QVERIFY2(!QFileInfo::exists(path), "no unreadable file left behind");
    }

    void missingFileFailsWithClearError()
    {
        QTemporaryDir dir;
        CaptureProject p;
        QString error;
        QVERIFY(!captureproject::load(dir.filePath("nope.fsproj"), p, &error));
        QVERIFY2(error.contains("nope.fsproj"), qPrintable(error));
    }
};

QTEST_MAIN(TestCaptureProject)
#include "tst_captureproject.moc"
