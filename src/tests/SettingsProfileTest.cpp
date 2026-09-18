// QtTest invokes private slots through the generated meta-object.
#include "AppPaths.h"
#include "AppSettings.h"
#include "MemoryDatabase.h"
#include "RadioProfile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtTest>

class SettingsProfileTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void storesSettingsInCurrentSchema();
    void storesAllSettingsGroupsAndValueTypes();
    void removesSettings();
    void rejectsUnknownSettings();
    void corruptedProfileKeyIsPreserved();
    void profilePasswordIsEncryptedAndRoundTrips();
    void corruptedProfilePasswordIsPreserved();
    void defersPasswordDecryptionUntilProfileUse();
    void managesProfileLifecycleAndLastSelection();
    void ignoresMalformedAndIncompleteProfiles();
    void rejectsInvalidProfilePorts();

  private:
    static QJsonObject settingsDocument();
};

void SettingsProfileTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(sdr9700::configDirectory()).removeRecursively();
    QDir(sdr9700::dataDirectory()).removeRecursively();
    RadioProfileStore::instance().load();
}

void SettingsProfileTest::cleanupTestCase()
{
    QDir(sdr9700::configDirectory()).removeRecursively();
    QDir(sdr9700::dataDirectory()).removeRecursively();
}

QJsonObject SettingsProfileTest::settingsDocument()
{
    QFile file(AppSettings::configPath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void SettingsProfileTest::storesSettingsInCurrentSchema()
{
    AppSettings& settings = AppSettings::instance();
    QVERIFY(settings.setValue(QStringLiteral("autoConnect"), true));
    QVERIFY(settings.setValue(QStringLiteral("audioOutputChannels"), 2));
    QVERIFY(settings.setValue(QStringLiteral("spectrumScopeSpanHZ"), 500000));
    QVERIFY(settings.setValue(QStringLiteral("spectrumScopeFramesPerSecond"), 25));
    QVERIFY(settings.setValue(QStringLiteral("tuningStepHZ"), 100));

    const QJsonObject root = settingsDocument();
    QCOMPARE(root.value(QStringLiteral("radioChooser")).toObject().value(QStringLiteral("autoConnect")).toString(),
             QStringLiteral("True"));
    QCOMPARE(root.value(QStringLiteral("audio")).toObject().value(QStringLiteral("outputChannels")).toString(),
             QStringLiteral("2"));
    QCOMPARE(root.value(QStringLiteral("spectrumScope")).toObject().value(QStringLiteral("spanHZ")).toString(),
             QStringLiteral("500000"));
    QCOMPARE(root.value(QStringLiteral("spectrumScope")).toObject().value(QStringLiteral("framesPerSecond")).toString(),
             QStringLiteral("25"));
    QCOMPARE(root.value(QStringLiteral("radio")).toObject().value(QStringLiteral("tuningStepHZ")).toString(),
             QStringLiteral("100"));
    QVERIFY(!root.contains(QStringLiteral("autoConnect")));
}

void SettingsProfileTest::storesAllSettingsGroupsAndValueTypes()
{
    AppSettings& settings = AppSettings::instance();
    QVERIFY(settings.setValue(QStringLiteral("audioInputDeviceID"), QStringLiteral("input-id")));
    QVERIFY(settings.setValue(QStringLiteral("spectrumScopeInvertMouseWheel"), false));
    QVERIFY(settings.setValue(QStringLiteral("mainWindowPositionX"), -42));
    QVERIFY(settings.setValue(QStringLiteral("memoryPollIntervalSeconds"), 900));
    QVERIFY(settings.setValue(QStringLiteral("memoryShowSpecialMemories"), true));
    QVERIFY(settings.setValue(QStringLiteral("memoryShowSatelliteMemories"), false));
    QVERIFY(settings.setValue(QStringLiteral("LANModLevel"), 127));
    QVERIFY(settings.setValue(QStringLiteral("ICOMRC28ButtonMapping"), QStringLiteral("{\"1\":\"mute\"}")));

    const QJsonObject root = settingsDocument();
    QCOMPARE(root.value(QStringLiteral("audio")).toObject().value(QStringLiteral("inputDeviceID")).toString(),
             QStringLiteral("input-id"));
    QCOMPARE(
        root.value(QStringLiteral("spectrumScope")).toObject().value(QStringLiteral("invertMouseWheel")).toString(),
        QStringLiteral("False"));
    QCOMPARE(root.value(QStringLiteral("mainWindow")).toObject().value(QStringLiteral("positionX")).toString(),
             QStringLiteral("-42"));
    QCOMPARE(
        root.value(QStringLiteral("memoryManager")).toObject().value(QStringLiteral("pollIntervalSeconds")).toString(),
        QStringLiteral("900"));
    QCOMPARE(
        root.value(QStringLiteral("memoryManager")).toObject().value(QStringLiteral("showSpecialMemories")).toString(),
        QStringLiteral("True"));
    QCOMPARE(root.value(QStringLiteral("memoryManager"))
                 .toObject()
                 .value(QStringLiteral("showSatelliteMemories"))
                 .toString(),
             QStringLiteral("False"));
    QCOMPARE(root.value(QStringLiteral("radio")).toObject().value(QStringLiteral("LANModLevel")).toString(),
             QStringLiteral("127"));
    QCOMPARE(root.value(QStringLiteral("accessories"))
                 .toObject()
                 .value(QStringLiteral("ICOMRC28ButtonMapping"))
                 .toObject()
                 .value(QStringLiteral("1"))
                 .toString(),
             QStringLiteral("mute"));
}

void SettingsProfileTest::removesSettings()
{
    AppSettings& settings = AppSettings::instance();
    QVERIFY(settings.setValue(QStringLiteral("statusClockUTC"), true));
    QVERIFY(settings.contains(QStringLiteral("statusClockUTC")));
    QVERIFY(settings.remove(QStringLiteral("statusClockUTC")));
    QVERIFY(!settings.contains(QStringLiteral("statusClockUTC")));
    QCOMPARE(settings.value(QStringLiteral("statusClockUTC"), QStringLiteral("fallback")).toString(),
             QStringLiteral("fallback"));
    QVERIFY(settings.remove(QStringLiteral("statusClockUTC")));
}

void SettingsProfileTest::rejectsUnknownSettings()
{
    AppSettings& settings = AppSettings::instance();
    QTest::ignoreMessage(QtWarningMsg, "Refusing to save unknown application setting: unknownSetting");
    QVERIFY(!settings.setValue(QStringLiteral("unknownSetting"), 42));
    QVERIFY(!settings.contains(QStringLiteral("unknownSetting")));
}

void SettingsProfileTest::corruptedProfileKeyIsPreserved()
{
    const QString keyPath = QDir(sdr9700::configDirectory()).filePath(QStringLiteral("profile-key.bin"));
    const QByteArray corruptKey("short-key");
    QFile keyFile(keyPath);
    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(keyFile.write(corruptKey), static_cast<qint64>(corruptKey.size()));
    keyFile.close();

    const RadioProfile profile{
        QUuid::createUuid(),        QStringLiteral("Key recovery"), QStringLiteral("192.0.2.20"), 50001,
        QStringLiteral("operator"), QStringLiteral("password"),
    };
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("Preserved unreadable profile key as:.*")));
    RadioProfileStore& store = RadioProfileStore::instance();
    QVERIFY(store.addProfile(profile));

    QFile preservedKey(keyPath + QStringLiteral(".corrupt"));
    QVERIFY(preservedKey.open(QIODevice::ReadOnly));
    QCOMPARE(preservedKey.readAll(), corruptKey);
    preservedKey.close();
    QVERIFY(QFileInfo(keyPath).size() >= 32);

    QVERIFY(store.removeProfile(profile.id));
    QVERIFY(QFile::remove(keyPath));
    QVERIFY(preservedKey.remove());
}

void SettingsProfileTest::profilePasswordIsEncryptedAndRoundTrips()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    const RadioProfile profile{
        QUuid::createUuid(),        QStringLiteral("Test Radio"),      QStringLiteral("192.0.2.10"), 50001,
        QStringLiteral("operator"), QStringLiteral("secret-password"),
    };

    QVERIFY(store.addProfile(profile));

    const QJsonObject root = settingsDocument();
    const QJsonObject profiles =
        root.value(QStringLiteral("radioChooser")).toObject().value(QStringLiteral("radioProfiles")).toObject();
    const QJsonObject storedProfile = profiles.value(QStringLiteral("profiles")).toArray().at(0).toObject();
    const QString storedPassword = storedProfile.value(QStringLiteral("password")).toString();
    QVERIFY(storedPassword.startsWith(QStringLiteral("v2:")));
    QVERIFY(!storedPassword.contains(profile.password));
    QVERIFY(!QJsonDocument(root).toJson().contains(profile.password.toUtf8()));

    store.load();
    const RadioProfile* loaded = store.profileForUse(profile.id);
    QVERIFY(loaded != nullptr);
    QCOMPARE(loaded->name, profile.name);
    QCOMPARE(loaded->host, profile.host);
    QCOMPARE(loaded->port, profile.port);
    QCOMPARE(loaded->username, profile.username);
    QCOMPARE(loaded->password, profile.password);

    // Saving an unrelated profile field must reuse the existing ciphertext;
    // deriving a fresh PBKDF2 key for every profile made routine saves scale
    // poorly with the number of remembered radios.
    QVERIFY(store.setLastProfileId(profile.id));
    const QJsonObject resavedProfile = settingsDocument()
                                           .value(QStringLiteral("radioChooser"))
                                           .toObject()
                                           .value(QStringLiteral("radioProfiles"))
                                           .toObject()
                                           .value(QStringLiteral("profiles"))
                                           .toArray()
                                           .at(0)
                                           .toObject();
    QCOMPARE(resavedProfile.value(QStringLiteral("password")).toString(), storedPassword);
}

void SettingsProfileTest::corruptedProfilePasswordIsPreserved()
{
    AppSettings& settings = AppSettings::instance();
    QJsonObject profiles = settingsDocument()
                               .value(QStringLiteral("radioChooser"))
                               .toObject()
                               .value(QStringLiteral("radioProfiles"))
                               .toObject();
    QJsonArray profileArray = profiles.value(QStringLiteral("profiles")).toArray();
    QJsonObject profile = profileArray.at(0).toObject();
    QString encrypted = profile.value(QStringLiteral("password")).toString();
    encrypted[encrypted.size() - 1] = encrypted.endsWith(QLatin1Char('A')) ? QLatin1Char('B') : QLatin1Char('A');
    profile.insert(QStringLiteral("password"), encrypted);
    profileArray[0] = profile;
    profiles.insert(QStringLiteral("profiles"), profileArray);
    QVERIFY(settings.setValue(QStringLiteral("radioProfiles"),
                              QString::fromUtf8(QJsonDocument(profiles).toJson(QJsonDocument::Compact))));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Loading radio profile.*")));
    RadioProfileStore& store = RadioProfileStore::instance();
    store.load();
    QVERIFY(store.profileForUse(QUuid(profile.value(QStringLiteral("ID")).toString())) != nullptr);
    QCOMPARE(store.profiles().size(), 1);
    QCOMPARE(store.profiles().constFirst().password, QString());
    QCOMPARE(store.unreadablePasswordProfileNames(), QStringList{profile.value(QStringLiteral("name")).toString()});

    QVERIFY(store.setLastProfileId(store.profiles().constFirst().id));
    const QJsonObject savedProfile = settingsDocument()
                                         .value(QStringLiteral("radioChooser"))
                                         .toObject()
                                         .value(QStringLiteral("radioProfiles"))
                                         .toObject()
                                         .value(QStringLiteral("profiles"))
                                         .toArray()
                                         .at(0)
                                         .toObject();
    QCOMPARE(savedProfile.value(QStringLiteral("password")).toString(), encrypted);

    RadioProfile recoveredProfile = store.profiles().constFirst();
    recoveredProfile.password = QStringLiteral("replacement-password");
    QVERIFY(store.updateProfile(recoveredProfile));
    QVERIFY(!store.hasUnreadablePassword(recoveredProfile.id));
    store.load();
    const RadioProfile* reloadedProfile = store.profileForUse(recoveredProfile.id);
    QVERIFY(reloadedProfile != nullptr);
    QCOMPARE(reloadedProfile->password, recoveredProfile.password);
    QVERIFY(store.removeProfile(recoveredProfile.id));
}

void SettingsProfileTest::defersPasswordDecryptionUntilProfileUse()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    const RadioProfile profile{
        QUuid::createUuid(),        QStringLiteral("Lazy password"),   QStringLiteral("192.0.2.30"), 50001,
        QStringLiteral("operator"), QStringLiteral("deferred-secret"),
    };
    QVERIFY(store.addProfile(profile));

    const QString keyPath = QDir(sdr9700::configDirectory()).filePath(QStringLiteral("profile-key.bin"));
    QFile keyFile(keyPath);
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    const QByteArray key = keyFile.readAll();
    keyFile.close();
    QVERIFY(QFile::remove(keyPath));

    store.load();
    QVERIFY(!QFileInfo::exists(keyPath));
    const RadioProfile* metadata = store.profileById(profile.id);
    QVERIFY(metadata != nullptr);
    QVERIFY(metadata->password.isEmpty());

    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(keyFile.write(key), static_cast<qint64>(key.size()));
    keyFile.close();
    QVERIFY(QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const RadioProfile* resolved = store.profileForUse(profile.id);
    QVERIFY(resolved != nullptr);
    QCOMPARE(resolved->password, profile.password);
    QVERIFY(store.removeProfile(profile.id));
}

void SettingsProfileTest::managesProfileLifecycleAndLastSelection()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    store.load();
    const RadioProfile first{
        QUuid::createUuid(), QStringLiteral("First"), QStringLiteral("192.0.2.1"), 50001, QString(), QString()};
    RadioProfile second{
        QUuid::createUuid(), QStringLiteral("Second"), QStringLiteral("192.0.2.2"), 50002, QString(), QString()};

    QVERIFY(store.addProfile(first));
    QVERIFY(store.addProfile(second));
    QCOMPARE(store.profiles().size(), 2);
    QVERIFY(store.setLastProfileId(second.id));
    QCOMPARE(store.lastProfileId(), second.id);

    MemoryDatabase memoryDatabase;
    QString databaseError;
    QVERIFY2(memoryDatabase.open(&databaseError), qPrintable(databaseError));
    QVERIFY2(memoryDatabase.applySyncSnapshot(second.id, {MemoryType{}}, 1, &databaseError), qPrintable(databaseError));
    QCOMPARE(memoryDatabase.memories(second.id, &databaseError).size(), 1);

    second.name = QStringLiteral("Updated");
    second.port = 50003;
    QVERIFY(store.updateProfile(second));
    QCOMPARE(store.profileById(second.id)->name, QStringLiteral("Updated"));
    QCOMPARE(store.profileById(second.id)->port, quint16(50003));
    RadioProfile missing;
    missing.id = QUuid::createUuid();
    QVERIFY(!store.updateProfile(missing));

    QVERIFY(store.removeProfile(second.id));
    QVERIFY(store.profileById(second.id) == nullptr);
    QVERIFY(store.lastProfileId().isNull());
    QVERIFY(memoryDatabase.memories(second.id, &databaseError).isEmpty());
    QVERIFY(!memoryDatabase.syncState(second.id, &databaseError).completedAt.isValid());
    QVERIFY(!store.removeProfile(QUuid::createUuid()));

    store.load();
    QCOMPARE(store.profiles().size(), 1);
    QVERIFY(store.profileById(first.id) != nullptr);
}

void SettingsProfileTest::ignoresMalformedAndIncompleteProfiles()
{
    QJsonObject malformed;
    malformed.insert(QStringLiteral("lastProfileID"), QStringLiteral("not-a-uuid"));
    QJsonArray profiles;
    profiles.append(QJsonObject{{QStringLiteral("ID"), QUuid::createUuid().toString()},
                                {QStringLiteral("name"), QStringLiteral("Missing host")}});
    profiles.append(QJsonObject{{QStringLiteral("ID"), QStringLiteral("not-a-uuid")},
                                {QStringLiteral("host"), QStringLiteral("192.0.2.3")}});
    malformed.insert(QStringLiteral("profiles"), profiles);
    QVERIFY(AppSettings::instance().setValue(
        QStringLiteral("radioProfiles"), QString::fromUtf8(QJsonDocument(malformed).toJson(QJsonDocument::Compact))));

    RadioProfileStore::instance().load();
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());
    QVERIFY(RadioProfileStore::instance().lastProfileId().isNull());
}

void SettingsProfileTest::rejectsInvalidProfilePorts()
{
    QJsonArray profiles;
    for (int port : {-1, 0, 65536, 70000})
    {
        profiles.append(QJsonObject{{QStringLiteral("ID"), QUuid::createUuid().toString()},
                                    {QStringLiteral("name"), QStringLiteral("Bad port")},
                                    {QStringLiteral("host"), QStringLiteral("192.0.2.4")},
                                    {QStringLiteral("port"), port}});
    }
    const QJsonObject root{{QStringLiteral("profiles"), profiles}};
    QVERIFY(AppSettings::instance().setValue(QStringLiteral("radioProfiles"),
                                             QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact))));

    for (int i = 0; i < profiles.size(); ++i)
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Skipping radio profile.*")));
    }
    RadioProfileStore::instance().load();
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());
}

QTEST_GUILESS_MAIN(SettingsProfileTest)
#include "SettingsProfileTest.moc"
