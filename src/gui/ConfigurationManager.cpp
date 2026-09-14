#include "ConfigurationManager.h"

#include "ConfirmationDialog.h"
#include "AppSettings.h"
#include "MemorySyncPolicy.h"
#include "SpectrumFrameRate.h"
#include "UdpBase.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <limits>

namespace
{
constexpr auto kConfigFileFilter = "sdr9700 configuration (*.json);;JSON files (*.json);;All files (*)";

bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    if (file.write(data) != static_cast<qint64>(data.size()))
    {
        return false;
    }
    return file.commit();
}

bool boolStringValue(const QJsonValue& value, QJsonValue* normalized)
{
    if (value.isBool())
    {
        *normalized = value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
        return true;
    }

    const QString text = value.toVariant().toString();
    if (text.compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0 ||
        text.compare(QStringLiteral("False"), Qt::CaseInsensitive) == 0)
    {
        *normalized = text.compare(QStringLiteral("True"), Qt::CaseInsensitive) == 0 ? QStringLiteral("True")
                                                                                     : QStringLiteral("False");
        return true;
    }
    return false;
}

bool intStringValue(const QJsonValue& value, int min, int max, QJsonValue* normalized)
{
    bool ok = false;
    const int number = value.toVariant().toInt(&ok);
    if (!ok || number < min || number > max)
    {
        return false;
    }

    *normalized = QString::number(number);
    return true;
}

bool colorStringValue(const QJsonValue& value, QJsonValue* normalized)
{
    const QString color = value.toVariant().toString();
    if (!QColor::isValidColorName(color))
    {
        return false;
    }

    *normalized = QColor(color).name(QColor::HexRgb);
    return true;
}

bool stringValue(const QJsonValue& value, QJsonValue* normalized)
{
    if (value.isObject() || value.isArray() || value.isUndefined() || value.isNull())
    {
        return false;
    }

    *normalized = value.toVariant().toString();
    return true;
}

bool objectValue(const QJsonValue& value, QJsonValue* normalized)
{
    if (value.isObject())
    {
        *normalized = value.toObject();
        return true;
    }
    if (value.isString())
    {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject())
        {
            *normalized = doc.object();
            return true;
        }
    }
    return false;
}

void insertCleanSetting(QJsonObject* group, const QJsonObject& source, const QString& key,
                        bool (*validator)(const QJsonValue&, QJsonValue*))
{
    QJsonValue normalized;
    if (validator(source.value(key), &normalized))
    {
        group->insert(key, normalized);
    }
}

QJsonObject cleanRadioProfiles(const QJsonObject& source)
{
    const QJsonObject radioChooser = source.value(QStringLiteral("radioChooser")).toObject();
    const QJsonValue value = radioChooser.value(QStringLiteral("radioProfiles"));
    QJsonValue normalized;
    if (!objectValue(value, &normalized))
    {
        return {};
    }

    const QJsonObject profileRoot = normalized.toObject();
    QJsonObject cleaned;
    QJsonValue lastProfileId;
    if (stringValue(profileRoot.value(QStringLiteral("lastProfileID")), &lastProfileId))
    {
        cleaned.insert(QStringLiteral("lastProfileID"), lastProfileId);
    }

    const QJsonArray profiles = profileRoot.value(QStringLiteral("profiles")).toArray();
    QJsonArray cleanedProfiles;
    for (const QJsonValue& profileValue : profiles)
    {
        if (!profileValue.isObject())
        {
            continue;
        }

        const QJsonObject profile = profileValue.toObject();
        QJsonObject cleanedProfile;
        for (const QString& key : {QStringLiteral("ID"), QStringLiteral("name"), QStringLiteral("host"),
                                   QStringLiteral("username"), QStringLiteral("password")})
        {
            QJsonValue normalizedField;
            if (stringValue(profile.value(key), &normalizedField))
            {
                cleanedProfile.insert(key, normalizedField);
            }
        }

        QJsonValue port;
        if (intStringValue(profile.value(QStringLiteral("port")), 1, kIcomLanControlPortMax, &port))
        {
            cleanedProfile.insert(QStringLiteral("port"), port.toString().toInt());
        }

        if (cleanedProfile.contains(QStringLiteral("ID")) && cleanedProfile.contains(QStringLiteral("host")) &&
            cleanedProfile.contains(QStringLiteral("port")))
        {
            cleanedProfiles.append(cleanedProfile);
        }
    }
    cleaned.insert(QStringLiteral("profiles"), cleanedProfiles);
    return cleaned;
}

QJsonObject cleanIcomRC28ButtonMapping(const QJsonObject& source)
{
    const QJsonObject accessories = source.value(QStringLiteral("accessories")).toObject();
    QJsonValue value;
    if (!objectValue(accessories.value(QStringLiteral("ICOMRC28ButtonMapping")), &value))
    {
        return {};
    }

    const QJsonObject mapping = value.toObject();
    QJsonObject cleaned;
    for (const QString& key : {QStringLiteral("F1Press"), QStringLiteral("F1Hold"), QStringLiteral("F2Press"),
                               QStringLiteral("F2Hold"), QStringLiteral("PTTMode"), QStringLiteral("autoSnap")})
    {
        QJsonValue normalized;
        if (stringValue(mapping.value(key), &normalized))
        {
            cleaned.insert(key, normalized);
        }
    }

    QJsonValue sensitivity;
    if (intStringValue(mapping.value(QStringLiteral("sensitivity")), 1, 10, &sensitivity))
    {
        cleaned.insert(QStringLiteral("sensitivity"), sensitivity);
    }
    return cleaned;
}

QJsonObject cleanConfigurationSettings(const QJsonDocument& doc)
{
    const QJsonObject source = doc.isObject() ? doc.object() : QJsonObject{};
    QJsonObject settings;

    const QJsonObject radioChooserSource = source.value(QStringLiteral("radioChooser")).toObject();
    QJsonObject radioChooser;
    QJsonValue autoConnect;
    if (boolStringValue(radioChooserSource.value(QStringLiteral("autoConnect")), &autoConnect))
    {
        radioChooser.insert(QStringLiteral("autoConnect"), autoConnect);
    }

    const QJsonObject radioProfiles = cleanRadioProfiles(source);
    if (!radioProfiles.isEmpty())
    {
        radioChooser.insert(QStringLiteral("radioProfiles"), radioProfiles);
    }
    if (!radioChooser.isEmpty())
    {
        settings.insert(QStringLiteral("radioChooser"), radioChooser);
    }

    QJsonObject accessories;
    const QJsonObject icomMapping = cleanIcomRC28ButtonMapping(source);
    if (!icomMapping.isEmpty())
    {
        accessories.insert(QStringLiteral("ICOMRC28ButtonMapping"), icomMapping);
    }
    if (!accessories.isEmpty())
    {
        settings.insert(QStringLiteral("accessories"), accessories);
    }

    const QJsonObject audioSource = source.value(QStringLiteral("audio")).toObject();
    QJsonObject audio;
    insertCleanSetting(&audio, audioSource, QStringLiteral("inputDeviceID"), stringValue);
    insertCleanSetting(&audio, audioSource, QStringLiteral("outputDeviceID"), stringValue);
    insertCleanSetting(&audio, audioSource, QStringLiteral("outputChannels"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, 2, normalized); });
    if (!audio.isEmpty())
    {
        settings.insert(QStringLiteral("audio"), audio);
    }

    const QJsonObject spectrumScopeSource = source.value(QStringLiteral("spectrumScope")).toObject();
    QJsonObject spectrumScope;
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("backgroundColor"), colorStringValue);
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("centerLineColor"), colorStringValue);
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("gridLineColor"), colorStringValue);
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("gridDensity"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 0, 2, normalized); });
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("invertMouseWheel"), boolStringValue);
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("framesPerSecond"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       {
                           QJsonValue candidate;
                           if (!intStringValue(value, 10, 30, &candidate) ||
                               !sdr9700::isSupportedSpectrumFramesPerSecond(candidate.toString().toInt()))
                           {
                               return false;
                           }
                           *normalized = candidate;
                           return true;
                       });
    insertCleanSetting(&spectrumScope, spectrumScopeSource, QStringLiteral("spanHZ"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, std::numeric_limits<int>::max(), normalized); });
    if (!spectrumScope.isEmpty())
    {
        settings.insert(QStringLiteral("spectrumScope"), spectrumScope);
    }

    const QJsonObject mainWindowSource = source.value(QStringLiteral("mainWindow")).toObject();
    QJsonObject mainWindow;
    insertCleanSetting(&mainWindow, mainWindowSource, QStringLiteral("positionX"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       {
                           return intStringValue(value, std::numeric_limits<int>::min(),
                                                 std::numeric_limits<int>::max(), normalized);
                       });
    insertCleanSetting(&mainWindow, mainWindowSource, QStringLiteral("positionY"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       {
                           return intStringValue(value, std::numeric_limits<int>::min(),
                                                 std::numeric_limits<int>::max(), normalized);
                       });
    insertCleanSetting(&mainWindow, mainWindowSource, QStringLiteral("statusClockUTC"), boolStringValue);
    if (!mainWindow.isEmpty())
    {
        settings.insert(QStringLiteral("mainWindow"), mainWindow);
    }

    const QJsonObject memoryManagerSource = source.value(QStringLiteral("memoryManager")).toObject();
    QJsonObject memoryManager;
    insertCleanSetting(&memoryManager, memoryManagerSource, QStringLiteral("pollIntervalSeconds"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       {
                           QJsonValue candidate;
                           if (!intStringValue(value, 0, 3600, &candidate))
                           {
                               return false;
                           }
                           const int seconds = candidate.toString().toInt();
                           if (sdr9700::normalizeMemoryPollIntervalSeconds(seconds) != seconds)
                           {
                               return false;
                           }
                           *normalized = candidate;
                           return true;
                       });
    insertCleanSetting(&memoryManager, memoryManagerSource, QStringLiteral("showSpecialMemories"), boolStringValue);
    insertCleanSetting(&memoryManager, memoryManagerSource, QStringLiteral("showSatelliteMemories"), boolStringValue);
    if (!memoryManager.isEmpty())
    {
        settings.insert(QStringLiteral("memoryManager"), memoryManager);
    }

    const QJsonObject radioSource = source.value(QStringLiteral("radio")).toObject();
    QJsonObject radio;
    insertCleanSetting(&radio, radioSource, QStringLiteral("LANModLevel"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 0, 255, normalized); });
    insertCleanSetting(&radio, radioSource, QStringLiteral("tuningStepHZ"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 1, std::numeric_limits<int>::max(), normalized); });
    insertCleanSetting(&radio, radioSource, QStringLiteral("volumeLevel"),
                       [](const QJsonValue& value, QJsonValue* normalized)
                       { return intStringValue(value, 0, 255, normalized); });
    if (!radio.isEmpty())
    {
        settings.insert(QStringLiteral("radio"), radio);
    }

    return settings;
}

QJsonObject configurationSettingsFromDocument(const QJsonDocument& doc)
{
    return cleanConfigurationSettings(doc);
}

bool looksLikeConfiguration(const QJsonDocument& doc)
{
    return !configurationSettingsFromDocument(doc).isEmpty();
}

QByteArray configurationDataFromDocument(const QJsonDocument& doc)
{
    return QJsonDocument(configurationSettingsFromDocument(doc)).toJson(QJsonDocument::Indented);
}

QJsonDocument readJsonFile(QWidget* parent, const QString& path, const QString& title)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(parent, QStringLiteral("%1 Failed").arg(title),
                             QStringLiteral("%1 failed. Could not open the selected file.").arg(title));
        return {};
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::warning(parent, QStringLiteral("%1 Failed").arg(title),
                             QStringLiteral("%1 failed. The selected file is not valid JSON.").arg(title));
        return {};
    }

    return doc;
}

bool restartApplication(QWidget* parent)
{
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty())
    {
        arguments.removeFirst();
    }

    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments,
                                 QCoreApplication::applicationDirPath()))
    {
        QMessageBox::warning(parent, QStringLiteral("Restart sdr9700"),
                             QStringLiteral("Could not restart sdr9700. Close and reopen the application manually."));
        return false;
    }

    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
    return true;
}
} // namespace

QString ConfigurationManager::configPath()
{
    return AppSettings::configPath();
}

bool ConfigurationManager::backupConfiguration(QWidget* parent)
{
    if (!AppSettings::instance().save())
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Configuration Failed"),
                             QStringLiteral("Configuration backup failed. Could not save the current settings before "
                                            "creating a backup."));
        return false;
    }

    const QString sourcePath = configPath();
    const QJsonDocument doc = readJsonFile(parent, sourcePath, QStringLiteral("Backup Configuration"));
    if (doc.isNull() || !looksLikeConfiguration(doc))
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Configuration Failed"),
                             QStringLiteral("Configuration backup failed. The current configuration is not valid."));
        return false;
    }

    const QString path =
        QFileDialog::getSaveFileName(parent, QStringLiteral("Backup Configuration"), QStringLiteral("sdr9700.json"),
                                     QString::fromLatin1(kConfigFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    if (!writeFile(path, configurationDataFromDocument(doc)))
    {
        QMessageBox::warning(parent, QStringLiteral("Backup Configuration Failed"),
                             QStringLiteral("Configuration backup failed. Could not save the selected file."));
        return false;
    }

    QMessageBox::information(parent, QStringLiteral("Backup Configuration Successful"),
                             QStringLiteral("Configuration backup successful.\n\nSaved to:\n%1").arg(path));
    return true;
}

bool ConfigurationManager::restoreConfigurationAndRestart(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Restore Configuration"), QString(),
                                                      QString::fromLatin1(kConfigFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    const QJsonDocument doc = readJsonFile(parent, path, QStringLiteral("Restore Configuration"));
    if (doc.isNull())
    {
        return false;
    }

    if (!looksLikeConfiguration(doc))
    {
        QMessageBox::warning(parent, QStringLiteral("Restore Configuration Failed"),
                             QStringLiteral("Configuration restore failed. The selected file does not look like an "
                                            "sdr9700 configuration file."));
        return false;
    }

    if (!sdr9700::ui::confirmAction(
            parent, QStringLiteral("Restore Configuration"),
            QStringLiteral("Restore this configuration backup and restart sdr9700? Current configuration will be "
                           "replaced."),
            QStringLiteral("Restore"), true))
    {
        return false;
    }

    const QString targetPath = configPath();
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (!writeFile(targetPath, configurationDataFromDocument(doc)))
    {
        QMessageBox::warning(parent, QStringLiteral("Restore Configuration Failed"),
                             QStringLiteral("Configuration restore failed. Could not write the restored "
                                            "configuration."));
        return false;
    }

    QMessageBox::information(parent, QStringLiteral("Restore Configuration Successful"),
                             QStringLiteral("Configuration restore successful.\n\nsdr9700 will restart now."));
    return restartApplication(parent);
}

bool ConfigurationManager::resetConfigurationAndRestart(QWidget* parent)
{
    if (!sdr9700::ui::confirmAction(
            parent, QStringLiteral("Reset Configuration"),
            QStringLiteral("Reset sdr9700 configuration and restart the application? This removes local settings "
                           "and radio profiles. Memories are stored separately."),
            QStringLiteral("Reset"), true))
    {
        return false;
    }

    const QString path = configPath();
    if (QFileInfo::exists(path) && !QFile::remove(path))
    {
        QMessageBox::warning(parent, QStringLiteral("Reset Configuration"),
                             QStringLiteral("Could not remove the sdr9700 configuration file."));
        return false;
    }

    return restartApplication(parent);
}
