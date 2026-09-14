#include "AppSettings.h"
#include "AppPaths.h"
#include "LogCategories.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QSaveFile>
#include <QStringList>
#include <algorithm>
#include <array>
#include <utility>

namespace
{
struct SettingDefinition
{
    constexpr SettingDefinition(const char* appKey, const char* group, const char* storedKey, bool storesJson = false)
        : appKey(appKey), group(group), storedKey(storedKey), storesJson(storesJson)
    {
    }

    const char* appKey;
    const char* group;
    const char* storedKey;
    bool storesJson;
};

constexpr std::array kSettingDefinitions{
    SettingDefinition{"audioInputDeviceID", "audio", "inputDeviceID"},
    SettingDefinition{"audioOutputChannels", "audio", "outputChannels"},
    SettingDefinition{"audioOutputDeviceID", "audio", "outputDeviceID"},
    SettingDefinition{"spectrumScopeBackgroundColor", "spectrumScope", "backgroundColor"},
    SettingDefinition{"spectrumScopeCenterLineColor", "spectrumScope", "centerLineColor"},
    SettingDefinition{"spectrumScopeGridDensity", "spectrumScope", "gridDensity"},
    SettingDefinition{"spectrumScopeGridLineColor", "spectrumScope", "gridLineColor"},
    SettingDefinition{"spectrumScopeInvertMouseWheel", "spectrumScope", "invertMouseWheel"},
    SettingDefinition{"spectrumScopeFramesPerSecond", "spectrumScope", "framesPerSecond"},
    SettingDefinition{"spectrumScopeSpanHZ", "spectrumScope", "spanHZ"},
    SettingDefinition{"mainWindowPositionX", "mainWindow", "positionX"},
    SettingDefinition{"mainWindowPositionY", "mainWindow", "positionY"},
    SettingDefinition{"statusClockUTC", "mainWindow", "statusClockUTC"},
    SettingDefinition{"memoryPollIntervalSeconds", "memoryManager", "pollIntervalSeconds"},
    SettingDefinition{"memoryShowSpecialMemories", "memoryManager", "showSpecialMemories"},
    SettingDefinition{"memoryShowSatelliteMemories", "memoryManager", "showSatelliteMemories"},
    SettingDefinition{"LANModLevel", "radio", "LANModLevel"},
    SettingDefinition{"tuningStepHZ", "radio", "tuningStepHZ"},
    SettingDefinition{"volumeLevel", "radio", "volumeLevel"},
    SettingDefinition{"autoConnect", "radioChooser", "autoConnect"},
    SettingDefinition{"radioProfiles", "radioChooser", "radioProfiles", true},
    SettingDefinition{"ICOMRC28ButtonMapping", "accessories", "ICOMRC28ButtonMapping", true},
};

const SettingDefinition* settingDefinition(const QString& appKey)
{
    const auto definition = std::find_if(kSettingDefinitions.cbegin(), kSettingDefinitions.cend(),
                                         [&appKey](const SettingDefinition& candidate)
                                         { return appKey == QLatin1String(candidate.appKey); });
    return definition == kSettingDefinitions.cend() ? nullptr : &*definition;
}

void insertStoredSetting(QJsonObject* target, const QString& key, const QString& storedValue)
{
    const SettingDefinition* definition = settingDefinition(key);
    if (definition && definition->storesJson)
    {
        QJsonParseError error;
        const QJsonDocument nested = QJsonDocument::fromJson(storedValue.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && nested.isObject())
        {
            target->insert(key, nested.object());
            return;
        }
        if (error.error == QJsonParseError::NoError && nested.isArray())
        {
            target->insert(key, nested.array());
            return;
        }
    }

    target->insert(key, storedValue);
}

void loadStoredSetting(QHash<QString, QString>* values, const QString& key, const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull())
    {
        return;
    }
    if (value.isObject())
    {
        values->insert(key, QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
    }
    else if (value.isArray())
    {
        values->insert(key, QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)));
    }
    else
    {
        values->insert(key, value.toVariant().toString());
    }
}
} // namespace

AppSettings& AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
{
    m_deferredSaveTimer.setSingleShot(true);
    m_deferredSaveTimer.setInterval(250);
    QObject::connect(&m_deferredSaveTimer, &QTimer::timeout,
                     [this]()
                     {
                         if (!save())
                         {
                             qWarning(logSystem()).noquote()
                                 << "Could not save deferred application settings; retrying";
                             m_deferredSaveTimer.start(1000);
                         }
                     });
    load();
}

AppSettings::~AppSettings()
{
    if (m_deferredSavePending && !save())
    {
        qWarning(logSystem()).noquote() << "Could not save deferred application settings during shutdown";
    }
}

QVariant AppSettings::value(const QString& key, const QVariant& defaultValue) const
{
    const auto it = m_values.constFind(key);
    if (it == m_values.constEnd())
    {
        return defaultValue;
    }
    return *it;
}

bool AppSettings::setValue(const QString& key, const QVariant& settingValue)
{
    if (!settingDefinition(key))
    {
        qWarning(logSystem()).noquote() << "Refusing to save unknown application setting:" << key;
        return false;
    }

    const QString encodedValue = encodeValue(settingValue);
    if (m_values.value(key) == encodedValue && m_values.contains(key))
    {
        return true;
    }

    const bool hadPreviousValue = m_values.contains(key);
    const QString previousValue = m_values.value(key);
    m_values.insert(key, encodedValue);
    if (!save())
    {
        if (hadPreviousValue)
        {
            m_values.insert(key, previousValue);
        }
        else
        {
            m_values.remove(key);
        }
        return false;
    }
    return true;
}

void AppSettings::setValueDeferred(const QString& key, const QVariant& settingValue)
{
    if (!settingDefinition(key))
    {
        qWarning(logSystem()).noquote() << "Refusing to defer unknown application setting:" << key;
        return;
    }

    const QString encodedValue = encodeValue(settingValue);
    if (m_values.value(key) == encodedValue && m_values.contains(key))
    {
        return;
    }

    // Slider signals can arrive on every pixel of a drag. Coalesce those
    // updates into one atomic QSaveFile replacement after interaction settles;
    // the destructor performs a final synchronous save during normal shutdown.
    m_values.insert(key, encodedValue);
    m_deferredSavePending = true;
    m_deferredSaveTimer.start();
}

bool AppSettings::contains(const QString& key) const
{
    return m_values.contains(key);
}

bool AppSettings::remove(const QString& key)
{
    const bool hadPreviousValue = m_values.contains(key);
    const QString previousValue = m_values.value(key);
    m_values.remove(key);
    if (!save())
    {
        if (hadPreviousValue)
        {
            m_values.insert(key, previousValue);
        }
        return false;
    }
    return true;
}

bool AppSettings::save()
{
    if (!writeFile())
    {
        return false;
    }

    m_deferredSavePending = false;
    m_deferredSaveTimer.stop();
    return true;
}

bool AppSettings::writeFile() const
{
    const QString path = configPath();
    if (m_writesBlocked)
    {
        qCritical(logSystem()).noquote()
            << "Refusing to overwrite unreadable application settings; preserve or remove the file first:" << path;
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qCritical(logSystem()).noquote() << "Could not open application settings for writing:" << path
                                         << file.errorString();
        return false;
    }

    QJsonObject settings;
    QHash<QString, QJsonObject> groups;
    QList<QString> keys = m_values.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys)
    {
        const SettingDefinition* definition = settingDefinition(key);
        if (!definition)
        {
            qWarning(logSystem()).noquote() << "Ignoring unknown in-memory application setting:" << key;
            continue;
        }

        const QString storedValue = m_values.value(key);
        const QString groupName = QString::fromLatin1(definition->group);
        QJsonObject group = groups.value(groupName);
        insertStoredSetting(&group, key, storedValue);
        if (QString::fromLatin1(definition->storedKey) != key)
        {
            const QJsonValue storedJsonValue = group.take(key);
            group.insert(QString::fromLatin1(definition->storedKey), storedJsonValue);
        }
        groups.insert(groupName, group);
    }

    QList<QString> groupNames = groups.keys();
    std::sort(groupNames.begin(), groupNames.end());
    for (const QString& groupName : std::as_const(groupNames))
    {
        settings.insert(groupName, groups.value(groupName));
    }

    const QByteArray data = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (file.write(data) != static_cast<qint64>(data.size()))
    {
        qCritical(logSystem()).noquote() << "Could not write application settings:" << path << file.errorString();
        return false;
    }
    if (!file.commit())
    {
        qCritical(logSystem()).noquote() << "Could not commit application settings:" << path << file.errorString();
        return false;
    }

    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qWarning(logSystem()).noquote() << "Could not set owner-only permissions on settings file:" << path;
    }
    return true;
}

void AppSettings::load()
{
    const QString path = configPath();
    if (!QFileInfo::exists(path) || loadJson(path))
    {
        return;
    }

    QString preservedPath = path + QStringLiteral(".corrupt");
    int suffix = 1;
    while (QFileInfo::exists(preservedPath))
    {
        preservedPath = path + QStringLiteral(".corrupt.%1").arg(suffix++);
    }
    if (QFile::rename(path, preservedPath))
    {
        qCritical(logSystem()).noquote() << "Preserved unreadable application settings as:" << preservedPath;
        return;
    }

    m_writesBlocked = true;
    qCritical(logSystem()).noquote() << "Could not preserve unreadable application settings; writes are disabled:"
                                     << path;
}

bool AppSettings::loadJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qCritical(logSystem()).noquote() << "Could not read application settings:" << path << file.errorString();
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error.error != QJsonParseError::NoError)
        {
            qCritical(logSystem()).noquote() << "Could not parse application settings:" << path << error.errorString()
                                             << "at offset" << error.offset;
        }
        else
        {
            qCritical(logSystem()).noquote() << "Application settings root is not a JSON object:" << path;
        }
        return false;
    }

    const QJsonObject settings = doc.object();
    for (const SettingDefinition& definition : kSettingDefinitions)
    {
        const QJsonObject group = settings.value(QString::fromLatin1(definition.group)).toObject();
        loadStoredSetting(&m_values, QString::fromLatin1(definition.appKey),
                          group.value(QString::fromLatin1(definition.storedKey)));
    }

    return true;
}

QString AppSettings::configPath()
{
    return QDir(sdr9700::configDirectory()).filePath(QStringLiteral("sdr9700.json"));
}

QString AppSettings::encodeValue(const QVariant& value)
{
    if (value.typeId() == QMetaType::Bool)
    {
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    }
    if (value.typeId() == QMetaType::QByteArray)
    {
        return QString::fromUtf8(value.toByteArray());
    }
    return value.toString();
}
