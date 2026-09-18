// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QString>
#include <QStringList>
#include <QUuid>
#include <QHash>
#include <QList>
#include <QObject>

struct RadioProfile
{
    QUuid id;
    QString name;
    QString host;
    quint16 port{50001};
    QString username;
    // The password is necessarily plaintext while establishing a connection.
    // RadioProfileStore encrypts it before persistence; callers must not log
    // this field or retain additional plaintext copies beyond connection setup.
    QString password;
};

// Passwords are encrypted at rest with AES-256-GCM. RadioProfileStore derives
// each record's encryption key from a per-user random key file and a per-record
// salt. The key file and encrypted profiles share the app configuration
// directory, so this protects against accidental plaintext disclosure rather
// than compromise of the entire configuration directory. Plaintext remains in
// memory only while a profile is loaded, edited, or used for connection setup.
class RadioProfileStore : public QObject
{
    Q_OBJECT

  public:
    static RadioProfileStore& instance();

    void load();
    bool save() const;

    const QList<RadioProfile>& profiles() const { return m_profiles; }
    const RadioProfile* profileById(const QUuid& id) const;
    const RadioProfile* profileForUse(const QUuid& id);
    bool hasUnreadablePassword(const QUuid& id) const { return m_unreadablePasswords.contains(id); }
    QStringList unreadablePasswordProfileNames() const;

    bool addProfile(const RadioProfile& p);
    bool updateProfile(const RadioProfile& p);
    bool removeProfile(const QUuid& id);

    QUuid lastProfileId() const { return m_lastProfileId; }
    bool setLastProfileId(const QUuid& id);

  private:
    RadioProfileStore() = default;

    static QByteArray passwordKeyMaterial();
    static QByteArray passwordFingerprint(const QString& plain);
    static QString encryptPassword(const QString& plain);
    static QString decryptPassword(const QString& stored);

    QList<RadioProfile> m_profiles;
    QHash<QUuid, QString> m_unreadablePasswords;
    mutable QHash<QUuid, QString> m_encryptedPasswords;
    mutable QHash<QUuid, QByteArray> m_passwordFingerprints;
    QUuid m_lastProfileId;
};
