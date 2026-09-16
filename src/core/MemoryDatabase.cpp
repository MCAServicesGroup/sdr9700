#include "MemoryDatabase.h"

#include "AppPaths.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>

namespace
{
constexpr quint32 kPayloadMagic = 0x5344524dU; // "SDRM"
constexpr quint16 kPayloadVersion = 1;
constexpr int kSchemaVersion = 2;

void setError(QString* destination, const QString& message)
{
    if (destination)
    {
        *destination = message;
    }
}

QByteArray fixedField(const char* data, qsizetype size)
{
    return QByteArray(data, size);
}

void restoreFixedField(const QByteArray& source, char* destination, qsizetype size)
{
    std::fill(destination, destination + size, '\0');
    const qsizetype count = qMin(size, source.size());
    std::copy_n(source.constData(), count, destination);
}

QByteArray serializeMemory(const MemoryType& memory)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_2);
    stream << kPayloadMagic << kPayloadVersion << memory.group << memory.channel << memory.split << memory.skip
           << memory.scan << memory.vfo << memory.vfoB << memory.frequency.Hz << memory.frequency.MHzDouble
           << static_cast<qint32>(memory.frequency.VFO) << memory.frequencyB.Hz << memory.frequencyB.MHzDouble
           << static_cast<qint32>(memory.frequencyB.VFO) << memory.clarifier << memory.clarRX << memory.clarTX
           << memory.mode << memory.modeB << memory.filter << memory.filterB << memory.datamode << memory.datamodeB
           << memory.duplex << memory.duplexB << memory.tonemode << memory.tonemodeB << memory.tone << memory.toneB
           << memory.tsql << memory.tsqlB << memory.dsql << memory.dsqlB << memory.dtcs << memory.dtcsB << memory.dtcsp
           << memory.dtcspB << memory.dvsql << memory.dvsqlB << memory.duplexOffset.Hz << memory.duplexOffset.MHzDouble
           << static_cast<qint32>(memory.duplexOffset.VFO) << memory.duplexOffsetB.Hz << memory.duplexOffsetB.MHzDouble
           << static_cast<qint32>(memory.duplexOffsetB.VFO) << fixedField(memory.UR, sizeof memory.UR)
           << fixedField(memory.URB, sizeof memory.URB) << fixedField(memory.R1, sizeof memory.R1)
           << fixedField(memory.R2, sizeof memory.R2) << fixedField(memory.R1B, sizeof memory.R1B)
           << fixedField(memory.R2B, sizeof memory.R2B) << memory.tuningStep << memory.tuningStepB << memory.progTs
           << memory.progTsB << memory.atten << memory.attenB << memory.preamp << memory.preampB << memory.antenna
           << memory.antennaB << memory.ipplus << memory.ipplusB << fixedField(memory.name, sizeof memory.name)
           << memory.sat << memory.del;
    return stream.status() == QDataStream::Ok ? payload : QByteArray();
}

bool deserializeMemory(const QByteArray& payload, MemoryType* memory)
{
    if (!memory)
    {
        return false;
    }
    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_2);
    quint32 magic = 0;
    quint16 version = 0;
    qint32 frequencyVfo = 0;
    qint32 frequencyBVfo = 0;
    qint32 offsetVfo = 0;
    qint32 offsetBVfo = 0;
    QByteArray ur;
    QByteArray urb;
    QByteArray r1;
    QByteArray r2;
    QByteArray r1b;
    QByteArray r2b;
    QByteArray name;
    MemoryType decoded;
    stream >> magic >> version >> decoded.group >> decoded.channel >> decoded.split >> decoded.skip >> decoded.scan >>
        decoded.vfo >> decoded.vfoB >> decoded.frequency.Hz >> decoded.frequency.MHzDouble >> frequencyVfo >>
        decoded.frequencyB.Hz >> decoded.frequencyB.MHzDouble >> frequencyBVfo >> decoded.clarifier >> decoded.clarRX >>
        decoded.clarTX >> decoded.mode >> decoded.modeB >> decoded.filter >> decoded.filterB >> decoded.datamode >>
        decoded.datamodeB >> decoded.duplex >> decoded.duplexB >> decoded.tonemode >> decoded.tonemodeB >>
        decoded.tone >> decoded.toneB >> decoded.tsql >> decoded.tsqlB >> decoded.dsql >> decoded.dsqlB >>
        decoded.dtcs >> decoded.dtcsB >> decoded.dtcsp >> decoded.dtcspB >> decoded.dvsql >> decoded.dvsqlB >>
        decoded.duplexOffset.Hz >> decoded.duplexOffset.MHzDouble >> offsetVfo >> decoded.duplexOffsetB.Hz >>
        decoded.duplexOffsetB.MHzDouble >> offsetBVfo >> ur >> urb >> r1 >> r2 >> r1b >> r2b >> decoded.tuningStep >>
        decoded.tuningStepB >> decoded.progTs >> decoded.progTsB >> decoded.atten >> decoded.attenB >> decoded.preamp >>
        decoded.preampB >> decoded.antenna >> decoded.antennaB >> decoded.ipplus >> decoded.ipplusB >> name >>
        decoded.sat >> decoded.del;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() || magic != kPayloadMagic || version != kPayloadVersion)
    {
        return false;
    }
    decoded.frequency.VFO = static_cast<selVFO_t>(frequencyVfo);
    decoded.frequencyB.VFO = static_cast<selVFO_t>(frequencyBVfo);
    decoded.duplexOffset.VFO = static_cast<selVFO_t>(offsetVfo);
    decoded.duplexOffsetB.VFO = static_cast<selVFO_t>(offsetBVfo);
    restoreFixedField(ur, decoded.UR, sizeof decoded.UR);
    restoreFixedField(urb, decoded.URB, sizeof decoded.URB);
    restoreFixedField(r1, decoded.R1, sizeof decoded.R1);
    restoreFixedField(r2, decoded.R2, sizeof decoded.R2);
    restoreFixedField(r1b, decoded.R1B, sizeof decoded.R1B);
    restoreFixedField(r2b, decoded.R2B, sizeof decoded.R2B);
    restoreFixedField(name, decoded.name, sizeof decoded.name);
    *memory = decoded;
    return true;
}
} // namespace

MemoryDatabase::MemoryDatabase(QString path)
    : m_path(path.isEmpty() ? QDir(sdr9700::dataDirectory()).filePath(QStringLiteral("memories.sqlite3"))
                            : std::move(path)),
      m_connectionName(QStringLiteral("sdr9700-memory-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

MemoryDatabase::~MemoryDatabase()
{
    if (m_database.isValid())
    {
        m_database.close();
        m_database = {};
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool MemoryDatabase::open(QString* error)
{
    if (isOpen())
    {
        return true;
    }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
    {
        setError(error, QStringLiteral("Could not create the memory database directory."));
        return false;
    }
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_path);
    if (!m_database.open())
    {
        setError(error, m_database.lastError().text());
        return false;
    }
    return ensureSchema(error);
}

bool MemoryDatabase::isOpen() const
{
    return m_database.isValid() && m_database.isOpen();
}

bool MemoryDatabase::ensureSchema(QString* error)
{
    QSqlQuery query(m_database);
    // A full radio sweep can update hundreds of rows in quick succession. WAL
    // with NORMAL synchronization keeps those small independent commits from
    // repeatedly blocking the GUI on a complete filesystem flush while still
    // preserving a valid database after an application or host interruption.
    if (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) ||
        !query.exec(QStringLiteral("PRAGMA synchronous = NORMAL")))
    {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
    {
        setError(error, query.lastError().text());
        return false;
    }
    const int schemaVersion = query.value(0).toInt();
    if (schemaVersion > kSchemaVersion)
    {
        setError(error, QStringLiteral("The memory database was created by a newer SDR9700 version."));
        return false;
    }
    // Migrations are additive and idempotent: create every object required by
    // the current schema before advancing user_version. Future destructive or
    // data-transforming migrations must be explicit versioned steps here.
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS radio_memories ("
                                   "profile_id TEXT NOT NULL, memory_group INTEGER NOT NULL, channel INTEGER NOT NULL, "
                                   "payload BLOB NOT NULL, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                                   "PRIMARY KEY(profile_id, memory_group, channel))")))
    {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS memory_sync_state ("
                                   "profile_id TEXT NOT NULL PRIMARY KEY, completed_at TEXT NOT NULL, "
                                   "expected_slot_count INTEGER NOT NULL, received_slot_count INTEGER NOT NULL, "
                                   "complete INTEGER NOT NULL)")))
    {
        setError(error, query.lastError().text());
        return false;
    }
    if (schemaVersion < kSchemaVersion && !query.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion)))
    {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

QVector<MemoryType> MemoryDatabase::memories(const QUuid& profileId, QString* error) const
{
    QVector<MemoryType> result;
    if (!isOpen() || profileId.isNull())
    {
        return result;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral("SELECT payload FROM radio_memories WHERE profile_id = ? "
                                      "ORDER BY memory_group, channel")))
    {
        setError(error, query.lastError().text());
        return result;
    }
    query.addBindValue(profileId.toString(QUuid::WithoutBraces));
    if (!query.exec())
    {
        setError(error, query.lastError().text());
        return result;
    }
    while (query.next())
    {
        MemoryType memory;
        if (!deserializeMemory(query.value(0).toByteArray(), &memory))
        {
            setError(error, QStringLiteral("The memory database contains an unsupported or damaged record."));
            result.clear();
            return result;
        }
        result.append(memory);
    }
    return result;
}

bool MemoryDatabase::store(const QUuid& profileId, const MemoryType& memory, QString* error)
{
    if (!isOpen() || profileId.isNull())
    {
        setError(error, QStringLiteral("The memory database or radio profile is not available."));
        return false;
    }
    const QByteArray payload = serializeMemory(memory);
    if (payload.isEmpty())
    {
        setError(error, QStringLiteral("The radio memory could not be serialized."));
        return false;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral("INSERT INTO radio_memories(profile_id, memory_group, channel, payload) "
                                      "VALUES(?, ?, ?, ?) ON CONFLICT(profile_id, memory_group, channel) DO UPDATE SET "
                                      "payload=excluded.payload, updated_at=CURRENT_TIMESTAMP")))
    {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(profileId.toString(QUuid::WithoutBraces));
    query.addBindValue(memory.group);
    query.addBindValue(memory.channel);
    query.addBindValue(payload);
    if (!query.exec())
    {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MemoryDatabase::remove(const QUuid& profileId, quint16 group, quint16 channel, QString* error)
{
    if (!isOpen() || profileId.isNull())
    {
        setError(error, QStringLiteral("The memory database or radio profile is not available."));
        return false;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(
            QStringLiteral("DELETE FROM radio_memories WHERE profile_id = ? AND memory_group = ? AND channel = ?")))
    {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(profileId.toString(QUuid::WithoutBraces));
    query.addBindValue(group);
    query.addBindValue(channel);
    if (!query.exec())
    {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MemoryDatabase::applySyncSnapshot(const QUuid& profileId, const QVector<MemoryType>& replies,
                                       int expectedSlotCount, QString* error)
{
    if (!isOpen() || profileId.isNull() || expectedSlotCount < 0 || replies.size() > expectedSlotCount)
    {
        setError(error, QStringLiteral("The memory sync snapshot is invalid or the database is unavailable."));
        return false;
    }

    struct SerializedReply
    {
        MemoryType memory;
        QByteArray payload;
    };
    QVector<SerializedReply> serialized;
    serialized.reserve(replies.size());
    QSet<quint32> replyKeys;
    for (const MemoryType& memory : replies)
    {
        const quint32 key = (static_cast<quint32>(memory.group) << 16U) | memory.channel;
        if (replyKeys.contains(key))
        {
            setError(error, QStringLiteral("The memory sync snapshot contains a duplicate slot reply."));
            return false;
        }
        replyKeys.insert(key);
        QByteArray payload;
        if (!memory.del)
        {
            payload = serializeMemory(memory);
            if (payload.isEmpty())
            {
                setError(error, QStringLiteral("A radio memory reply could not be serialized."));
                return false;
            }
        }
        serialized.append({memory, std::move(payload)});
    }

    if (!m_database.transaction())
    {
        setError(error, m_database.lastError().text());
        return false;
    }
    auto rollbackWithError = [&](const QSqlError& sqlError)
    {
        const QString operationError = sqlError.text();
        if (!m_database.rollback())
        {
            setError(error, QStringLiteral("%1; transaction rollback also failed: %2")
                                .arg(operationError, m_database.lastError().text()));
            return false;
        }
        setError(error, operationError);
        return false;
    };

    QSqlQuery upsert(m_database);
    if (!upsert.prepare(QStringLiteral("INSERT INTO radio_memories(profile_id, memory_group, channel, payload) "
                                       "VALUES(?, ?, ?, ?) ON CONFLICT(profile_id, memory_group, channel) DO UPDATE "
                                       "SET payload=excluded.payload, updated_at=CURRENT_TIMESTAMP")))
    {
        return rollbackWithError(upsert.lastError());
    }
    QSqlQuery removeReply(m_database);
    if (!removeReply.prepare(
            QStringLiteral("DELETE FROM radio_memories WHERE profile_id = ? AND memory_group = ? AND channel = ?")))
    {
        return rollbackWithError(removeReply.lastError());
    }

    const QString profileKey = profileId.toString(QUuid::WithoutBraces);
    for (const SerializedReply& reply : serialized)
    {
        QSqlQuery& query = reply.memory.del ? removeReply : upsert;
        query.bindValue(0, profileKey);
        query.bindValue(1, reply.memory.group);
        query.bindValue(2, reply.memory.channel);
        if (!reply.memory.del)
        {
            query.bindValue(3, reply.payload);
        }
        if (!query.exec())
        {
            return rollbackWithError(query.lastError());
        }
    }

    QSqlQuery syncStateQuery(m_database);
    if (!syncStateQuery.prepare(QStringLiteral(
            "INSERT INTO memory_sync_state(profile_id, completed_at, expected_slot_count, received_slot_count, "
            "complete) VALUES(?, ?, ?, ?, ?) ON CONFLICT(profile_id) DO UPDATE SET "
            "completed_at=excluded.completed_at, expected_slot_count=excluded.expected_slot_count, "
            "received_slot_count=excluded.received_slot_count, complete=excluded.complete")))
    {
        return rollbackWithError(syncStateQuery.lastError());
    }
    syncStateQuery.addBindValue(profileKey);
    syncStateQuery.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    syncStateQuery.addBindValue(expectedSlotCount);
    syncStateQuery.addBindValue(replies.size());
    syncStateQuery.addBindValue(replies.size() == expectedSlotCount ? 1 : 0);
    if (!syncStateQuery.exec())
    {
        return rollbackWithError(syncStateQuery.lastError());
    }
    if (!m_database.commit())
    {
        const QString commitError = m_database.lastError().text();
        if (!m_database.rollback())
        {
            setError(error, QStringLiteral("%1; transaction rollback also failed: %2")
                                .arg(commitError, m_database.lastError().text()));
            return false;
        }
        setError(error, commitError);
        return false;
    }
    return true;
}

MemoryDatabaseSyncState MemoryDatabase::syncState(const QUuid& profileId, QString* error) const
{
    MemoryDatabaseSyncState state;
    if (!isOpen() || profileId.isNull())
    {
        return state;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral("SELECT completed_at, expected_slot_count, received_slot_count, complete "
                                      "FROM memory_sync_state WHERE profile_id = ?")))
    {
        setError(error, query.lastError().text());
        return state;
    }
    query.addBindValue(profileId.toString(QUuid::WithoutBraces));
    if (!query.exec())
    {
        setError(error, query.lastError().text());
        return state;
    }
    if (query.next())
    {
        state.completedAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
        state.expectedSlotCount = query.value(1).toInt();
        state.receivedSlotCount = query.value(2).toInt();
        state.complete = query.value(3).toBool();
    }
    return state;
}

bool MemoryDatabase::removeProfile(const QUuid& profileId, QString* error)
{
    if (!isOpen() || profileId.isNull())
    {
        setError(error, QStringLiteral("The memory database or radio profile is not available."));
        return false;
    }
    if (!m_database.transaction())
    {
        setError(error, m_database.lastError().text());
        return false;
    }
    auto rollbackWithError = [&](const QSqlError& sqlError)
    {
        const QString operationError = sqlError.text();
        if (!m_database.rollback())
        {
            setError(error, QStringLiteral("%1; profile-cache rollback also failed: %2")
                                .arg(operationError, m_database.lastError().text()));
            return false;
        }
        setError(error, operationError);
        return false;
    };

    const QString profileKey = profileId.toString(QUuid::WithoutBraces);
    QSqlQuery removeMemories(m_database);
    if (!removeMemories.prepare(QStringLiteral("DELETE FROM radio_memories WHERE profile_id = ?")))
    {
        return rollbackWithError(removeMemories.lastError());
    }
    removeMemories.addBindValue(profileKey);
    if (!removeMemories.exec())
    {
        return rollbackWithError(removeMemories.lastError());
    }

    QSqlQuery removeSyncState(m_database);
    if (!removeSyncState.prepare(QStringLiteral("DELETE FROM memory_sync_state WHERE profile_id = ?")))
    {
        return rollbackWithError(removeSyncState.lastError());
    }
    removeSyncState.addBindValue(profileKey);
    if (!removeSyncState.exec())
    {
        return rollbackWithError(removeSyncState.lastError());
    }
    if (!m_database.commit())
    {
        const QString commitError = m_database.lastError().text();
        if (!m_database.rollback())
        {
            setError(error, QStringLiteral("%1; profile-cache rollback also failed: %2")
                                .arg(commitError, m_database.lastError().text()));
            return false;
        }
        setError(error, commitError);
        return false;
    }
    return true;
}
