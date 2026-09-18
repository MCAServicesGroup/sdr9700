#include "RadioSessionRecoveryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <limits>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#endif

namespace
{
constexpr auto kRadioAddressKey = "radioAddress";
constexpr auto kOwnerNameKey = "ownerName";
constexpr auto kOwnerProcessIdKey = "ownerProcessID";
constexpr auto kTokenRequestKey = "tokenRequest";
constexpr auto kTokenKey = "token";
constexpr auto kControlKey = "control";
constexpr auto kCivKey = "civ";
constexpr auto kAudioKey = "audio";
constexpr auto kLocalPortKey = "localPort";
constexpr auto kRemotePortKey = "remotePort";
constexpr auto kLocalSessionIdKey = "localSessionID";
constexpr auto kRemoteSessionIdKey = "remoteSessionID";

QJsonObject transportToJson(const sdr9700::RadioSessionTransportIdentity& identity)
{
    QJsonObject object;
    object.insert(QString::fromLatin1(kLocalPortKey), static_cast<qint64>(identity.localPort));
    object.insert(QString::fromLatin1(kRemotePortKey), static_cast<qint64>(identity.remotePort));
    object.insert(QString::fromLatin1(kLocalSessionIdKey), static_cast<qint64>(identity.localSessionId));
    object.insert(QString::fromLatin1(kRemoteSessionIdKey), static_cast<qint64>(identity.remoteSessionId));
    return object;
}

std::optional<sdr9700::RadioSessionTransportIdentity> transportFromJson(const QJsonValue& value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const qint64 localPort = object.value(QString::fromLatin1(kLocalPortKey)).toInteger(-1);
    const qint64 remotePort = object.value(QString::fromLatin1(kRemotePortKey)).toInteger(-1);
    const qint64 localSessionId = object.value(QString::fromLatin1(kLocalSessionIdKey)).toInteger(-1);
    const qint64 remoteSessionId = object.value(QString::fromLatin1(kRemoteSessionIdKey)).toInteger(-1);
    if (localPort <= 0 || localPort > std::numeric_limits<quint16>::max() || remotePort <= 0 ||
        remotePort > std::numeric_limits<quint16>::max() || localSessionId <= 0 ||
        localSessionId > std::numeric_limits<quint32>::max() || remoteSessionId <= 0 ||
        remoteSessionId > std::numeric_limits<quint32>::max())
    {
        return std::nullopt;
    }
    return sdr9700::RadioSessionTransportIdentity{static_cast<quint16>(localPort), static_cast<quint16>(remotePort),
                                                  static_cast<quint32>(localSessionId),
                                                  static_cast<quint32>(remoteSessionId)};
}

bool isOwnerOnly(const QFileDevice::Permissions permissions)
{
    constexpr QFileDevice::Permissions kDisallowed = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                                     QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                                     QFileDevice::WriteOther | QFileDevice::ExeOther;
    return !(permissions & kDisallowed);
}
} // namespace

namespace sdr9700
{
QString RadioSessionRecoveryStore::filePath()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (root.isEmpty())
    {
        root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return QDir(root).filePath(QStringLiteral("SDR9700/radio-session-recovery.json"));
}

bool RadioSessionRecoveryStore::save(const RadioSessionRecoveryRecord& record)
{
    const QString path = filePath();
    QDir directory = QFileInfo(path).dir();
    if (!directory.mkpath(QStringLiteral(".")))
    {
        return false;
    }
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QJsonObject object;
    object.insert(QString::fromLatin1(kRadioAddressKey), record.radioAddress);
    object.insert(QString::fromLatin1(kOwnerNameKey), record.ownerName);
    object.insert(QString::fromLatin1(kOwnerProcessIdKey), QCoreApplication::applicationPid());
    object.insert(QString::fromLatin1(kTokenRequestKey), static_cast<qint64>(record.tokenRequest));
    object.insert(QString::fromLatin1(kTokenKey), static_cast<qint64>(record.token));
    // A token-only record is still useful during the short interval between
    // authentication and completion of both media handshakes. Replace it with
    // a complete record as soon as UdpHandler knows all transport identities.
    if (record.hasTransportIdentities())
    {
        object.insert(QString::fromLatin1(kControlKey), transportToJson(record.control));
        object.insert(QString::fromLatin1(kCivKey), transportToJson(record.civ));
        object.insert(QString::fromLatin1(kAudioKey), transportToJson(record.audio));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
    {
        return false;
    }
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

std::optional<RadioSessionRecoveryRecord> RadioSessionRecoveryStore::load(const QString& radioAddress,
                                                                          const QString& ownerName)
{
    QFile file(filePath());
    if (!file.exists() || !isOwnerOnly(file.permissions()) || !file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    RadioSessionRecoveryRecord record;
    record.radioAddress = object.value(QString::fromLatin1(kRadioAddressKey)).toString();
    record.ownerName = object.value(QString::fromLatin1(kOwnerNameKey)).toString();
    record.ownerProcessId = object.value(QString::fromLatin1(kOwnerProcessIdKey)).toInteger(-1);
    const qint64 tokenRequest = object.value(QString::fromLatin1(kTokenRequestKey)).toInteger(-1);
    const qint64 token = object.value(QString::fromLatin1(kTokenKey)).toInteger(-1);
    if (record.radioAddress != radioAddress || record.ownerName != ownerName || record.ownerProcessId <= 0 ||
        tokenRequest < 0 || tokenRequest > std::numeric_limits<quint16>::max() || token < 0 ||
        token > std::numeric_limits<quint32>::max())
    {
        return std::nullopt;
    }
    record.tokenRequest = static_cast<quint16>(tokenRequest);
    record.token = static_cast<quint32>(token);
    // Accept a token-only journal written before the media transports became
    // ready, but reject a partially written identity set. Reclaiming only one
    // of the three transports can leave the radio in the same split state this
    // recovery mechanism is intended to prevent.
    const bool hasAnyTransport = object.contains(QString::fromLatin1(kControlKey)) ||
                                 object.contains(QString::fromLatin1(kCivKey)) ||
                                 object.contains(QString::fromLatin1(kAudioKey));
    if (hasAnyTransport)
    {
        const auto control = transportFromJson(object.value(QString::fromLatin1(kControlKey)));
        const auto civ = transportFromJson(object.value(QString::fromLatin1(kCivKey)));
        const auto audio = transportFromJson(object.value(QString::fromLatin1(kAudioKey)));
        if (!control || !civ || !audio)
        {
            return std::nullopt;
        }
        record.control = *control;
        record.civ = *civ;
        record.audio = *audio;
    }
    return record;
}

std::optional<RadioSessionRecoveryRecord> RadioSessionRecoveryStore::loadRecoverable(const QString& radioAddress,
                                                                                     const QString& ownerName)
{
#if !defined(Q_OS_UNIX)
    // SDR9700 currently supports Linux and macOS. Refuse recovery on any
    // future platform until it has an equivalent side-effect-free process
    // liveness check; guessing here could tear down another live client.
    Q_UNUSED(radioAddress);
    Q_UNUSED(ownerName);
    return std::nullopt;
#else
    const auto record = load(radioAddress, ownerName);
    if (!record || ownerProcessIsRunning(*record))
    {
        return std::nullopt;
    }
    return record;
#endif
}

std::optional<RadioSessionRecoveryRecord> RadioSessionRecoveryStore::loadForRadio(const QString& radioAddress)
{
    QFile file(filePath());
    if (!file.exists() || !isOwnerOnly(file.permissions()) || !file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }
    const QString ownerName = document.object().value(QString::fromLatin1(kOwnerNameKey)).toString();
    if (ownerName.isEmpty())
    {
        return std::nullopt;
    }
    return loadRecoverable(radioAddress, ownerName);
}

bool RadioSessionRecoveryStore::ownerProcessIsRunning(const RadioSessionRecoveryRecord& record)
{
#if defined(Q_OS_UNIX)
    if (record.ownerProcessId <= 0)
    {
        return false;
    }
    errno = 0;
    return ::kill(static_cast<pid_t>(record.ownerProcessId), 0) == 0 || errno == EPERM;
#else
    // loadRecoverable() refuses recovery before reaching this helper on an
    // unsupported platform. Returning true preserves that fail-closed rule if
    // another caller performs a direct liveness query in the future.
    Q_UNUSED(record);
    return true;
#endif
}

void RadioSessionRecoveryStore::removeOwned(const QString& radioAddress, const QString& ownerName)
{
    if (load(radioAddress, ownerName))
    {
        QFile::remove(filePath());
    }
}
} // namespace sdr9700
