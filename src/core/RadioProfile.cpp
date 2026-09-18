#include "RadioProfile.h"
#include "AppPaths.h"
#include "AppSettings.h"
#include "LogCategories.h"
#include "MemoryDatabase.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <memory>

namespace
{
constexpr auto kEncryptedPasswordPrefix = "v2:";
constexpr int kPasswordSaltBytes = 16;
constexpr int kPasswordNonceBytes = 12;
constexpr int kPasswordTagBytes = 16;
constexpr int kPasswordKeyBytes = 32;
constexpr int kPasswordKdfIterations = 210000;
constexpr auto kPasswordKeyFileName = "profile-key.bin";

bool validRadioPort(int port)
{
    return port >= 1 && port <= 65535;
}

QByteArray randomBytes(int size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), size) != 1)
    {
        return {};
    }
    return bytes;
}

QByteArray derivePasswordKey(const QByteArray& material, const QByteArray& salt)
{
    QByteArray key(kPasswordKeyBytes, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(material.constData(), material.size(),
                          reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(), kPasswordKdfIterations,
                          EVP_sha256(), key.size(), reinterpret_cast<unsigned char*>(key.data())) != 1)
    {
        OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
        return {};
    }
    return key;
}

QString profileKeyPath()
{
    // Keep the profile key beside other app-owned settings. This avoids an
    // additional credential-service dependency and keeps headless tests
    // deterministic. If an attacker obtains both the settings and this
    // owner-readable key file, saved radio passwords can be decrypted offline.
    return QDir(sdr9700::configDirectory()).filePath(QString::fromLatin1(kPasswordKeyFileName));
}

void secureZero(QByteArray& data)
{
    if (!data.isEmpty())
    {
        OPENSSL_cleanse(data.data(), static_cast<size_t>(data.size()));
        data.clear();
    }
}

QString nextPreservedPath(const QString& path)
{
    QString preservedPath = path + QStringLiteral(".corrupt");
    int suffix = 1;
    while (QFileInfo::exists(preservedPath))
    {
        preservedPath = path + QStringLiteral(".corrupt.%1").arg(suffix++);
    }
    return preservedPath;
}

QByteArray readOrCreateProfileKey()
{
    const QString path = profileKeyPath();
    QFile keyFile(path);
    if (keyFile.open(QIODevice::ReadOnly))
    {
        QByteArray key = keyFile.readAll();
        if (key.size() >= kPasswordKeyBytes)
        {
            if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            {
                qWarning(logSystem()).noquote() << "Could not tighten profile key permissions for" << path;
            }
            const QByteArray result = key.left(kPasswordKeyBytes);
            secureZero(key);
            return result;
        }
        secureZero(key);

        const QString preservedPath = nextPreservedPath(path);
        keyFile.close();
        if (!QFile::rename(path, preservedPath))
        {
            qCritical(logSystem()).noquote()
                << "Could not preserve unreadable profile key; refusing to replace:" << path;
            return {};
        }
        qCritical(logSystem()).noquote() << "Preserved unreadable profile key as:" << preservedPath;
    }

    const QByteArray key = randomBytes(kPasswordKeyBytes);
    if (key.isEmpty())
    {
        return {};
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        return {};
    }
    if (!saveFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qWarning(logSystem()).noquote() << "Could not set owner-only permissions on temporary profile key";
    }
    if (saveFile.write(key) != key.size() || !saveFile.commit())
    {
        return {};
    }
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qWarning(logSystem()).noquote() << "Could not tighten profile key permissions for" << path;
    }
    return key;
}
} // namespace

RadioProfileStore& RadioProfileStore::instance()
{
    // Process-lifetime ownership avoids static-destruction ordering between
    // this QObject, AppSettings, and Qt's logging infrastructure at shutdown.
    static RadioProfileStore* store = new RadioProfileStore;
    return *store;
}

QByteArray RadioProfileStore::passwordKeyMaterial()
{
    // Current profile encryption key scheme: a per-user random secret stored in
    // the SDR9700 config directory, domain-separated for AES-GCM radio-profile
    // password encryption, then combined with each record's random salt. The
    // key file is protected with owner-only permissions but remains an app-local
    // secret, not a system-keyring secret; see profileKeyPath() for the accepted
    // risk and portability rationale.
    QByteArray material = readOrCreateProfileKey();
    if (material.isEmpty())
    {
        return {};
    }
    material += "|SDR9700-radio-profiles-aes-gcm";
    const QByteArray hash = QCryptographicHash::hash(material, QCryptographicHash::Sha256);
    secureZero(material);
    return hash;
}

QByteArray RadioProfileStore::passwordFingerprint(const QString& plain)
{
    if (plain.isEmpty())
    {
        return {};
    }
    QByteArray material = passwordKeyMaterial();
    if (material.isEmpty())
    {
        return {};
    }
    material += "|password-fingerprint|";
    material += plain.toUtf8();
    const QByteArray fingerprint = QCryptographicHash::hash(material, QCryptographicHash::Sha256);
    secureZero(material);
    return fingerprint;
}

QString RadioProfileStore::encryptPassword(const QString& plain)
{
    if (plain.isEmpty())
    {
        return {};
    }

    const QByteArray salt = randomBytes(kPasswordSaltBytes);
    const QByteArray nonce = randomBytes(kPasswordNonceBytes);
    QByteArray keyMaterial = passwordKeyMaterial();
    QByteArray key = derivePasswordKey(keyMaterial, salt);
    secureZero(keyMaterial);
    if (salt.isEmpty() || nonce.isEmpty() || key.isEmpty())
    {
        secureZero(key);
        return {};
    }

    QByteArray plaintext = plain.toUtf8();
    QByteArray ciphertext(plaintext.size(), Qt::Uninitialized);
    std::array<unsigned char, kPasswordTagBytes> tag{};

    const std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                              &EVP_CIPHER_CTX_free);
    if (!ctx)
    {
        secureZero(key);
        secureZero(plaintext);
        return {};
    }

    int len = 0;
    int ciphertextLen = 0;
    bool ok = EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1;
    ok = ok && EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
                                  reinterpret_cast<const unsigned char*>(nonce.constData())) == 1;
    ok = ok && EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &len,
                                 reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) == 1;
    ciphertextLen = len;
    ok = ok &&
         EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()) + ciphertextLen, &len) == 1;
    ciphertextLen += len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) == 1;

    if (!ok)
    {
        secureZero(key);
        secureZero(plaintext);
        secureZero(ciphertext);
        return {};
    }

    ciphertext.resize(ciphertextLen);
    const QByteArray payload =
        salt + nonce + QByteArray(reinterpret_cast<const char*>(tag.data()), tag.size()) + ciphertext;
    secureZero(key);
    secureZero(plaintext);
    return QString::fromLatin1(kEncryptedPasswordPrefix) + QString::fromLatin1(payload.toBase64());
}

static bool tryDecryptAesGcm(const QByteArray& key, const QByteArray& nonce, const QByteArray& tag,
                             const QByteArray& ciphertext, QByteArray& plaintext)
{
    plaintext.resize(ciphertext.size());
    const std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                              &EVP_CIPHER_CTX_free);
    if (!ctx)
    {
        return false;
    }
    int len = 0;
    int plaintextLen = 0;
    bool ok = EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1;
    ok = ok && EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
                                  reinterpret_cast<const unsigned char*>(nonce.constData())) == 1;
    ok =
        ok && EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(plaintext.data()), &len,
                                reinterpret_cast<const unsigned char*>(ciphertext.constData()), ciphertext.size()) == 1;
    plaintextLen = len;
    ok =
        ok && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag.size(), const_cast<char*>(tag.constData())) == 1;
    ok = ok &&
         EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(plaintext.data()) + plaintextLen, &len) == 1;
    plaintextLen += len;
    if (ok)
    {
        plaintext.resize(plaintextLen);
    }
    return ok;
}

QString RadioProfileStore::decryptPassword(const QString& stored)
{
    if (stored.isEmpty())
    {
        return {};
    }
    if (!stored.startsWith(QString::fromLatin1(kEncryptedPasswordPrefix)))
    {
        return {};
    }

    const QByteArray payload =
        QByteArray::fromBase64(stored.mid(QString::fromLatin1(kEncryptedPasswordPrefix).size()).toLatin1());
    const int headerBytes = kPasswordSaltBytes + kPasswordNonceBytes + kPasswordTagBytes;
    if (payload.size() <= headerBytes)
    {
        return {};
    }

    const QByteArray salt = payload.mid(0, kPasswordSaltBytes);
    const QByteArray nonce = payload.mid(kPasswordSaltBytes, kPasswordNonceBytes);
    const QByteArray tag = payload.mid(kPasswordSaltBytes + kPasswordNonceBytes, kPasswordTagBytes);
    const QByteArray ciphertext = payload.mid(headerBytes);
    QByteArray keyMaterial = passwordKeyMaterial();
    QByteArray key = derivePasswordKey(keyMaterial, salt);
    secureZero(keyMaterial);
    if (key.isEmpty())
    {
        secureZero(key);
        return {};
    }

    QByteArray plaintext;
    if (!tryDecryptAesGcm(key, nonce, tag, ciphertext, plaintext))
    {
        secureZero(key);
        secureZero(plaintext);
        return {};
    }

    QString decrypted = QString::fromUtf8(plaintext);
    secureZero(key);
    secureZero(plaintext);
    return decrypted;
}

void RadioProfileStore::load()
{
    m_profiles.clear();
    m_unreadablePasswords.clear();
    m_encryptedPasswords.clear();
    m_passwordFingerprints.clear();
    AppSettings& settings = AppSettings::instance();
    const QString stored = settings.value("radioProfiles", "{}").toString();
    const QJsonDocument doc = QJsonDocument::fromJson(stored.toUtf8());
    const QJsonObject root = doc.object();

    m_lastProfileId = QUuid(root.value("lastProfileID").toString());

    const QJsonArray profileArray = root.value("profiles").toArray();
    for (const QJsonValue& value : profileArray)
    {
        const QJsonObject obj = value.toObject();
        RadioProfile p;
        p.id = QUuid(obj.value("ID").toString());
        p.name = obj.value("name").toString();
        p.host = obj.value("host").toString();
        const int port = obj.value("port").toInt(50001);
        if (!validRadioPort(port))
        {
            qWarning(logSystem()).noquote() << "Skipping radio profile with invalid LAN port:" << p.name << port;
            continue;
        }
        p.port = static_cast<quint16>(port);
        p.username = obj.value("username").toString();
        const QString storedPassword = obj.value("password").toString();
        p.password = decryptPassword(storedPassword);
        if (!storedPassword.isEmpty() && p.password.isEmpty())
        {
            qWarning(logSystem()).noquote() << "Loading radio profile with unreadable encrypted password:" << p.name;
            m_unreadablePasswords.insert(p.id, storedPassword);
        }
        else if (!p.password.isEmpty())
        {
            m_encryptedPasswords.insert(p.id, storedPassword);
            m_passwordFingerprints.insert(p.id, passwordFingerprint(p.password));
        }
        if (!p.id.isNull() && !p.host.isEmpty())
        {
            m_profiles.append(p);
        }
    }
}

bool RadioProfileStore::save() const
{
    QJsonObject root;
    root.insert("lastProfileID", m_lastProfileId.toString());

    QJsonArray profileArray;
    QHash<QUuid, QString> encryptedPasswords;
    QHash<QUuid, QByteArray> passwordFingerprints;
    for (const RadioProfile& p : m_profiles)
    {
        QJsonObject obj;
        obj.insert("ID", p.id.toString());
        obj.insert("name", p.name);
        obj.insert("host", p.host);
        obj.insert("port", static_cast<int>(p.port));
        obj.insert("username", p.username);
        QString encryptedPassword;
        QByteArray fingerprint;
        if (p.password.isEmpty())
        {
            encryptedPassword = m_unreadablePasswords.value(p.id);
        }
        else
        {
            fingerprint = passwordFingerprint(p.password);
            if (!fingerprint.isEmpty() && fingerprint == m_passwordFingerprints.value(p.id))
            {
                encryptedPassword = m_encryptedPasswords.value(p.id);
            }
            if (encryptedPassword.isEmpty())
            {
                encryptedPassword = encryptPassword(p.password);
            }
        }
        if (!p.password.isEmpty() && encryptedPassword.isEmpty())
        {
            qWarning(logSystem()).noquote()
                << "Radio profile password encryption failed; refusing to overwrite saved profiles";
            return false;
        }
        obj.insert("password", encryptedPassword);
        if (!encryptedPassword.isEmpty() && !fingerprint.isEmpty())
        {
            encryptedPasswords.insert(p.id, encryptedPassword);
            passwordFingerprints.insert(p.id, fingerprint);
        }
        profileArray.append(obj);
    }
    root.insert("profiles", profileArray);

    if (!AppSettings::instance().setValue("radioProfiles",
                                          QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact))))
    {
        return false;
    }
    m_encryptedPasswords = std::move(encryptedPasswords);
    m_passwordFingerprints = std::move(passwordFingerprints);
    return true;
}

const RadioProfile* RadioProfileStore::profileById(const QUuid& id) const
{
    const auto it = std::find_if(m_profiles.cbegin(), m_profiles.cend(),
                                 [&id](const RadioProfile& profile) { return profile.id == id; });
    return it != m_profiles.cend() ? &(*it) : nullptr;
}

QStringList RadioProfileStore::unreadablePasswordProfileNames() const
{
    QStringList names;
    for (const RadioProfile& profile : m_profiles)
    {
        if (m_unreadablePasswords.contains(profile.id))
        {
            names.append(profile.name);
        }
    }
    return names;
}

bool RadioProfileStore::addProfile(const RadioProfile& p)
{
    if (p.id.isNull() || p.host.trimmed().isEmpty() || !validRadioPort(p.port))
    {
        return false;
    }
    m_profiles.append(p);
    if (!save())
    {
        m_profiles.removeLast();
        return false;
    }
    return true;
}

bool RadioProfileStore::updateProfile(const RadioProfile& p)
{
    if (p.id.isNull() || p.host.trimmed().isEmpty() || !validRadioPort(p.port))
    {
        return false;
    }
    for (RadioProfile& existing : m_profiles)
    {
        if (existing.id == p.id)
        {
            const RadioProfile previous = existing;
            const QString previousUnreadablePassword = m_unreadablePasswords.value(p.id);
            existing = p;
            if (!p.password.isEmpty())
            {
                m_unreadablePasswords.remove(p.id);
            }
            if (!save())
            {
                existing = previous;
                if (!previousUnreadablePassword.isEmpty())
                {
                    m_unreadablePasswords.insert(p.id, previousUnreadablePassword);
                }
                return false;
            }
            return true;
        }
    }
    return false;
}

bool RadioProfileStore::removeProfile(const QUuid& id)
{
    if (id.isNull() || profileById(id) == nullptr)
    {
        return false;
    }
    const QList<RadioProfile> previousProfiles = m_profiles;
    const QHash<QUuid, QString> previousUnreadablePasswords = m_unreadablePasswords;
    const QUuid previousLastProfileId = m_lastProfileId;
    m_profiles.removeIf([&](const RadioProfile& p) { return p.id == id; });
    m_unreadablePasswords.remove(id);
    if (m_lastProfileId == id)
    {
        m_lastProfileId = QUuid();
    }
    if (!save())
    {
        m_profiles = previousProfiles;
        m_unreadablePasswords = previousUnreadablePasswords;
        m_lastProfileId = previousLastProfileId;
        return false;
    }

    // The profile settings are the primary record. Once their durable removal
    // succeeds, remove the associated radio-memory mirror and sync metadata as
    // one SQLite transaction. Cache cleanup is deliberately best-effort: an
    // unavailable database must not resurrect or prevent deletion of a radio
    // profile, but the failure remains visible for diagnosis.
    MemoryDatabase memoryDatabase;
    QString databaseError;
    if (!memoryDatabase.open(&databaseError) || !memoryDatabase.removeProfile(id, &databaseError))
    {
        qWarning(logSystem()).noquote() << "Could not remove cached memories for deleted radio profile"
                                        << id.toString(QUuid::WithoutBraces) << ':' << databaseError;
    }
    return true;
}

bool RadioProfileStore::setLastProfileId(const QUuid& id)
{
    const QUuid previousLastProfileId = m_lastProfileId;
    m_lastProfileId = id;
    if (!save())
    {
        m_lastProfileId = previousLastProfileId;
        return false;
    }
    return true;
}
