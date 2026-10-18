// SPDX-License-Identifier: GPL-3.0-or-later

// Style packs: parse / serialize / apply against the real ConfigHandler and
// its ValueHandlers.
//
// Safety: before Qt starts, HOME, XDG paths and the QSettings user path are
// pointed at a temp dir, and every test refuses to run unless the config file
// and the styles folder resolve inside it. The real settings are never read
// or written.

#include "config/stylepackrow.h"
#include "config/workflowsconf.h"
#include "utils/appidentity.h"
#include "utils/confighandler.h"
#include "utils/stylepack.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

namespace {

QTemporaryDir* g_tmp = nullptr;

bool insideTmp(const QString& path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString root = QDir::cleanPath(g_tmp->path());
    const QString canonicalRoot = QDir(g_tmp->path()).canonicalPath();
    return clean.startsWith(root + "/") || clean.startsWith(canonicalRoot + "/");
}

QByteArray packJson(const QJsonObject& settings,
                    int version = 1,
                    const QString& format = QStringLiteral("pixiestyle"))
{
    QJsonObject root;
    root[QStringLiteral("format")] = format;
    root[QStringLiteral("version")] = version;
    root[QStringLiteral("name")] = QStringLiteral("Test pack");
    root[QStringLiteral("settings")] = settings;
    return QJsonDocument(root).toJson();
}

// Every non-style option the tests poke at, with its value before the test
QVariantMap snapshot(const QStringList& keys)
{
    ConfigHandler config;
    QVariantMap out;
    for (const QString& key : keys) {
        out[key] = config.value(key);
    }
    return out;
}

} // namespace

class TestStylePack : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Refuse to touch anything outside the temp dir
        QVERIFY2(insideTmp(ConfigHandler().configFilePath()),
                 qPrintable(ConfigHandler().configFilePath()));
        QVERIFY2(insideTmp(stylepack::userStylesDir()),
                 qPrintable(stylepack::userStylesDir()));
        // Known starting point for the style keys
        ConfigHandler config;
        config.setEffectPadding(64);
        config.setEffectGlass(false);
        config.setEffectCornerRadius(0);
        config.setEffectGrain(0);
        config.setEffectBackground(2);
        config.setBeautifyAlways(false);
    }

    void allowListIsStyleKeysOnly()
    {
        QVERIFY(stylepack::isStyleKey(QStringLiteral("effectPadding")));
        QVERIFY(stylepack::isStyleKey(QStringLiteral("effectGlass")));
        QVERIFY(stylepack::isStyleKey(QStringLiteral("animStyle")));
        QVERIFY(!stylepack::isStyleKey(QStringLiteral("beautifyAlways")));
        QVERIFY(!stylepack::isStyleKey(QStringLiteral("savePath")));
        QVERIFY(!stylepack::isStyleKey(QStringLiteral("startupLaunch")));
        QVERIFY(
          !stylepack::isStyleKey(QStringLiteral("uploadWithoutConfirmation")));
        // Prefix but not a real option
        QVERIFY(!stylepack::isStyleKey(QStringLiteral("effectNotAnOption")));
        const QStringList keys = stylepack::styleKeys();
        QVERIFY(keys.contains(QStringLiteral("effectBackgroundColor1")));
        QVERIFY(keys.contains(QStringLiteral("animDurationSec")));
        QVERIFY(!keys.contains(QStringLiteral("beautifyAlways")));
    }

    void roundTrip()
    {
        stylepack::Pack pack;
        pack.name = QStringLiteral("Round trip");
        pack.author = QStringLiteral("Tester");
        pack.settings[QStringLiteral("effectPadding")] = 72;
        pack.settings[QStringLiteral("effectGlass")] = true;
        pack.settings[QStringLiteral("effectBackgroundColor1")] =
          QStringLiteral("#0f172a");
        pack.settings[QStringLiteral("animStyle")] = 2;

        const stylepack::ParseResult r =
          stylepack::parse(stylepack::serialize(pack));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        QCOMPARE(r.pack.name, pack.name);
        QCOMPARE(r.pack.author, pack.author);
        QCOMPARE(r.pack.settings, pack.settings);
    }

    void fromCurrentConfigRoundTripsThroughApply()
    {
        ConfigHandler config;
        config.setEffectPadding(100);
        config.setEffectBackgroundColor1(QColor(QStringLiteral("#123456")));
        const stylepack::Pack saved =
          stylepack::fromCurrentConfig(QStringLiteral("Mine"));
        QCOMPARE(saved.settings.keys(), stylepack::styleKeys());
        QCOMPARE(saved.settings.value(QStringLiteral("effectPadding")).toInt(),
                 100);

        config.setEffectPadding(5);
        config.setEffectBackgroundColor1(QColor(Qt::red));
        const stylepack::ParseResult r =
          stylepack::parse(stylepack::serialize(saved));
        QVERIFY2(r.ok, qPrintable(r.error));
        const QStringList warnings = stylepack::applyToConfig(r.pack);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("; ")));
        QCOMPARE(ConfigHandler().effectPadding(), 100);
        QCOMPARE(ConfigHandler().effectBackgroundColor1(),
                 QColor(QStringLiteral("#123456")));
    }

    void allowListEnforcedOnParseAndApply()
    {
        const QStringList guarded = {
            QStringLiteral("savePath"),
            QStringLiteral("startupLaunch"),
            QStringLiteral("beautifyAlways"),
            QStringLiteral("uploadWithoutConfirmation"),
            QStringLiteral("copyURLAfterUpload"),
        };
        const QVariantMap before = snapshot(guarded);

        QJsonObject settings;
        settings[QStringLiteral("savePath")] = QStringLiteral("/tmp/evil");
        settings[QStringLiteral("startupLaunch")] = true;
        settings[QStringLiteral("beautifyAlways")] = true;
        settings[QStringLiteral("uploadWithoutConfirmation")] = true;
        settings[QStringLiteral("copyURLAfterUpload")] = false;
        settings[QStringLiteral("effectPadding")] = 90;
        const stylepack::ParseResult r = stylepack::parse(packJson(settings));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.pack.settings.keys(),
                 QStringList{ QStringLiteral("effectPadding") });
        for (const QString& key : guarded) {
            bool named = false;
            for (const QString& w : r.warnings) {
                named = named || w.contains(key);
            }
            QVERIFY2(named, qPrintable(QStringLiteral("no warning for ") + key));
        }

        // A pack built in code with the same keys: apply must skip them too
        stylepack::Pack direct;
        direct.name = QStringLiteral("Direct");
        direct.settings[QStringLiteral("savePath")] = QStringLiteral("/tmp");
        direct.settings[QStringLiteral("startupLaunch")] = true;
        direct.settings[QStringLiteral("beautifyAlways")] = true;
        direct.settings[QStringLiteral("uploadWithoutConfirmation")] = true;
        direct.settings[QStringLiteral("copyURLAfterUpload")] = false;
        direct.settings[QStringLiteral("effectPadding")] = 90;
        const QStringList warnings = stylepack::applyToConfig(direct);
        QCOMPARE(warnings.size(), guarded.size());
        QCOMPARE(ConfigHandler().effectPadding(), 90);
        const QVariantMap after = snapshot(guarded);
        for (const QString& key : guarded) {
            QVERIFY2(after.value(key) == before.value(key),
                     qPrintable(QStringLiteral("changed: ") + key));
        }
    }

    void unknownKeysIgnored()
    {
        QJsonObject settings;
        settings[QStringLiteral("effectNotAnOption")] = 3;
        settings[QStringLiteral("totallyUnknown")] = 3;
        settings[QStringLiteral("effectGrain")] = 20;
        const stylepack::ParseResult r = stylepack::parse(packJson(settings));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.pack.settings.keys(),
                 QStringList{ QStringLiteral("effectGrain") });
        QCOMPARE(r.warnings.size(), 2);
    }

    void invalidValuesIgnoredPerKey()
    {
        QJsonObject settings;
        settings[QStringLiteral("effectPadding")] = 9999;  // over 512
        settings[QStringLiteral("effectGlass")] = 7;       // not a bool
        settings[QStringLiteral("effectCornerRadius")] =
          QStringLiteral("14");                            // string, not int
        settings[QStringLiteral("effectGrain")] = 12.5;    // not integral
        settings[QStringLiteral("effectBackgroundColor1")] =
          QStringLiteral("not a colour");
        settings[QStringLiteral("effectGlassPx")] = 30;    // valid
        settings[QStringLiteral("effectBackgroundColor2")] =
          QStringLiteral("#abcdef");                       // valid
        const stylepack::ParseResult r = stylepack::parse(packJson(settings));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.warnings.size(), 5);
        QStringList kept = r.pack.settings.keys();
        kept.sort();
        QCOMPARE(kept,
                 (QStringList{ QStringLiteral("effectBackgroundColor2"),
                               QStringLiteral("effectGlassPx") }));

        ConfigHandler config;
        const QColor colour1Before = config.effectBackgroundColor1();
        // Apply the raw values too: apply validates again on its own
        stylepack::Pack direct;
        direct.name = QStringLiteral("Direct");
        direct.settings[QStringLiteral("effectPadding")] = 9999;
        direct.settings[QStringLiteral("effectGlass")] = 7;
        direct.settings[QStringLiteral("effectBackgroundColor1")] =
          QStringLiteral("not a colour");
        direct.settings[QStringLiteral("effectGlassPx")] = 30;
        direct.settings[QStringLiteral("effectBackgroundColor2")] =
          QStringLiteral("#abcdef");
        const QStringList warnings = stylepack::applyToConfig(direct);
        QCOMPARE(warnings.size(), 3);
        QCOMPARE(ConfigHandler().effectPadding(), 64);
        QCOMPARE(ConfigHandler().effectGlass(), false);
        QCOMPARE(ConfigHandler().effectBackgroundColor1(), colour1Before);
        QCOMPARE(ConfigHandler().effectGlassPx(), 30);
        QCOMPARE(ConfigHandler().effectBackgroundColor2(),
                 QColor(QStringLiteral("#abcdef")));
    }

    void errorsApplyNothing_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("errorWord");
        QJsonObject settings;
        settings[QStringLiteral("effectPadding")] = 200;
        QTest::newRow("newer version")
          << packJson(settings, 2) << QStringLiteral("newer");
        QTest::newRow("not a pack")
          << packJson(settings, 1, QStringLiteral("fsproj"))
          << QStringLiteral("not a");
        QTest::newRow("corrupt json")
          << QByteArray("{\"format\":\"pixiestyle\",\"version\":1,")
          << QStringLiteral("damaged");
        QTest::newRow("not an object")
          << QByteArray("[1,2,3]") << QStringLiteral("not a");
        QTest::newRow("empty") << QByteArray() << QStringLiteral("not a");
        QJsonObject noSettings;
        noSettings[QStringLiteral("format")] = QStringLiteral("pixiestyle");
        noSettings[QStringLiteral("version")] = 1;
        noSettings[QStringLiteral("name")] = QStringLiteral("x");
        QTest::newRow("no settings")
          << QJsonDocument(noSettings).toJson() << QStringLiteral("damaged");
        QByteArray big = packJson(settings);
        big.insert(1, QByteArray(" ").repeated(stylepack::kMaxFileBytes));
        QTest::newRow("oversize") << big << QStringLiteral("too large");
    }

    void errorsApplyNothing()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, errorWord);
        const stylepack::ParseResult r = stylepack::parse(bytes);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(errorWord, Qt::CaseInsensitive),
                 qPrintable(r.error));
        QVERIFY(r.pack.settings.isEmpty());
        // Nothing to apply, and applying the empty result changes nothing
        QVERIFY(stylepack::applyToConfig(r.pack).isEmpty());
        QCOMPARE(ConfigHandler().effectPadding(), 64);
    }

    void oversizeFileRefusedBeforeReading()
    {
        const QString path = g_tmp->filePath(QStringLiteral("big.pixiestyle"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(stylepack::kMaxFileBytes + 1, ' '));
        f.close();
        const stylepack::ParseResult r = stylepack::loadFile(path);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("too large"),
                                  Qt::CaseInsensitive),
                 qPrintable(r.error));
        QVERIFY(!stylepack::loadFile(g_tmp->filePath("missing.pixiestyle")).ok);
    }

    void builtInsParseAndApplyCleanly()
    {
        const QList<stylepack::Entry> entries =
          stylepack::listPacks(stylepack::userStylesDir());
        QStringList builtInNames;
        for (const stylepack::Entry& e : entries) {
            if (!e.builtIn) {
                continue;
            }
            builtInNames << e.name;
            const stylepack::ParseResult r = stylepack::loadFile(e.path);
            QVERIFY2(r.ok, qPrintable(e.path + ": " + r.error));
            QVERIFY2(r.warnings.isEmpty(),
                     qPrintable(e.path + ": " + r.warnings.join("; ")));
            QVERIFY2(!r.pack.settings.isEmpty(), qPrintable(e.path));
            const QStringList warnings = stylepack::applyToConfig(r.pack);
            QVERIFY2(warnings.isEmpty(),
                     qPrintable(e.path + ": " + warnings.join("; ")));
            // Every value in the pack is now the config value
            const stylepack::Pack now = stylepack::fromCurrentConfig(e.name);
            for (auto it = r.pack.settings.cbegin();
                 it != r.pack.settings.cend();
                 ++it) {
                QVERIFY2(now.settings.value(it.key()) == it.value(),
                         qPrintable(e.name + ": " + it.key()));
            }
        }
        builtInNames.sort();
        QCOMPARE(builtInNames,
                 (QStringList{ QStringLiteral("Album Art"),
                               QStringLiteral("Clean Paper"),
                               QStringLiteral("Midnight Glass"),
                               QStringLiteral("Ocean"),
                               QStringLiteral("Sunset Pop") }));
        // Built-ins never touch behaviour options
        QCOMPARE(ConfigHandler().beautifyAlways(), false);
    }

    void midnightGlassValues()
    {
        const stylepack::ParseResult r = stylepack::loadFile(
          stylepack::builtInDir() + QStringLiteral("/midnight-glass.pixiestyle"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(stylepack::applyToConfig(r.pack).isEmpty());
        ConfigHandler config;
        QCOMPARE(config.effectGlass(), true);
        QCOMPARE(config.effectPadding(), 72);
        QCOMPARE(config.effectCornerRadius(), 14);
        QCOMPARE(config.effectBackground(), 2);
    }

    void safeFileNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("traversal") << QStringLiteral("../../x");
        QTest::newRow("absolute") << QStringLiteral("/etc/passwd");
        QTest::newRow("backslash") << QStringLiteral("..\\..\\x");
        QTest::newRow("dots") << QStringLiteral("..");
        QTest::newRow("empty") << QString();
        QTest::newRow("spaces") << QStringLiteral("   ");
        QTest::newRow("hidden") << QStringLiteral(".hidden");
        QTest::newRow("control") << QStringLiteral("a\nb\tc");
        QTest::newRow("long") << QString(500, QLatin1Char('a'));
        QTest::newRow("normal") << QStringLiteral("Midnight Glass");
    }

    void safeFileNames()
    {
        QFETCH(QString, name);
        const QString dir = stylepack::userStylesDir();
        const QString path = stylepack::packFilePath(dir, name);
        QCOMPARE(QFileInfo(path).absolutePath(), QDir(dir).absolutePath());
        const QString file = QFileInfo(path).fileName();
        QVERIFY2(file.endsWith(QStringLiteral(".pixiestyle")), qPrintable(file));
        QVERIFY2(!file.startsWith(QLatin1Char('.')), qPrintable(file));
        QVERIFY(!file.contains(QLatin1Char('/')));
        QVERIFY(!file.contains(QLatin1Char('\\')));
        QVERIFY(file.size() <= 80);
        for (const QChar c : file) {
            QVERIFY(c.isPrint());
        }
    }

    void writeAndListUserPack()
    {
        const QString dir = stylepack::userStylesDir();
        QVERIFY(QDir().mkpath(dir));
        const stylepack::Pack pack =
          stylepack::fromCurrentConfig(QStringLiteral("My Look"));
        const QString path = stylepack::packFilePath(dir, pack.name);
        QString error;
        QVERIFY2(stylepack::writeFile(path, pack, &error), qPrintable(error));
        bool found = false;
        for (const stylepack::Entry& e : stylepack::listPacks(dir)) {
            if (!e.builtIn && e.name == QStringLiteral("My Look")) {
                found = e.path == path;
            }
        }
        QVERIFY(found);
        QVERIFY(stylepack::loadFile(path).ok);
    }

    // The Effects group: picking Midnight Glass and pressing Apply writes the
    // config and refreshes the visible controls
    void applyButtonRefreshesEffectsControls()
    {
        WorkflowsConf conf;
        conf.updateComponents();
        auto* row = conf.findChild<StylePackRow*>();
        QVERIFY(row);
        auto* combo = row->findChild<QComboBox*>();
        QVERIFY(combo);
        QVERIFY(combo->count() >= 5);
        const int index = combo->findData(
          stylepack::builtInDir() + QStringLiteral("/midnight-glass.pixiestyle"));
        QVERIFY(index >= 0);
        combo->setCurrentIndex(index);
        QPushButton* apply = nullptr;
        for (auto* b : row->findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("Apply")) {
                apply = b;
            }
        }
        QVERIFY(apply);
        QSignalSpy spy(row, &StylePackRow::applied);
        apply->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ConfigHandler().effectPadding(), 72);
        // Padding is the only 0-512 spin box; glass is "Glass frame"
        QSpinBox* padding = nullptr;
        for (auto* spin : conf.findChildren<QSpinBox*>()) {
            if (spin->maximum() == 512) {
                padding = spin;
            }
        }
        QVERIFY(padding);
        QCOMPARE(padding->value(), 72);
        QCheckBox* glass = nullptr;
        for (auto* box : conf.findChildren<QCheckBox*>()) {
            if (box->text() == QStringLiteral("Glass frame")) {
                glass = box;
            }
        }
        QVERIFY(glass);
        QVERIFY(glass->isChecked());
    }

    void refusesToApplyWhenConfigHasError()
    {
        // In error state ConfigHandler::setValue is a silent no-op, so apply
        // has to say so instead of reporting success
        ConfigHandler config;
        config.setErrorState(true);
        stylepack::Pack pack;
        pack.settings[QStringLiteral("effectPadding")] = 100;
        const QStringList warnings = stylepack::applyToConfig(pack);
        config.setErrorState(false);
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(ConfigHandler().effectPadding(), 64);
    }
};

int main(int argc, char** argv)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "no temp dir\n");
        return 2;
    }
    g_tmp = &tmp;
    const QByteArray home = tmp.path().toUtf8();
    qputenv("HOME", home);
    qputenv("CFFIXED_USER_HOME", home);
    qputenv("XDG_CONFIG_HOME", home + "/.config");
    qputenv("XDG_CACHE_HOME", home + "/.cache");
    qputenv("XDG_DATA_HOME", home + "/.local/share");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       tmp.path() + QStringLiteral("/.config"));

    QApplication app(argc, argv);
    AppIdentity::apply();
    TestStylePack test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_stylepack.moc"
