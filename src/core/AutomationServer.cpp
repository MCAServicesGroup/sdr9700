#include "AutomationServer.h"

#include "LogCategories.h"
#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QUuid>

namespace
{
constexpr qsizetype kMaximumRequestBytes = 64 * 1024;
constexpr int kMaximumClients = 4;

QJsonObject errorResponse(const QString& code, const QString& message)
{
    return QJsonObject{
        {QStringLiteral("ok"), false}, {QStringLiteral("error"), code}, {QStringLiteral("message"), message}};
}
} // namespace

AutomationServer::AutomationServer(QObject* parent) : QObject(parent), m_server(new QLocalServer(this))
{
#if defined(Q_OS_LINUX)
    // Linux implements QLocalServer access flags for filesystem and abstract
    // namespace sockets. macOS rejects the option during listen(), so its
    // filesystem endpoint is tightened explicitly after creation below.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
#endif
    connect(m_server, &QLocalServer::newConnection, this, &AutomationServer::acceptPendingConnections);
}

AutomationServer::~AutomationServer()
{
    stop();
}

bool AutomationServer::start(const RequestHandler& handler)
{
    if (m_server->isListening())
    {
        return true;
    }
    if (!handler)
    {
        return false;
    }

    m_handler = handler;
    const QString endpointToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name =
        QStringLiteral("sdr9700-automation-%1-%2").arg(QCoreApplication::applicationPid()).arg(endpointToken);
#if defined(Q_OS_MACOS)
    // The per-user macOS temporary directory is too long for the platform's
    // Unix-domain socket path limit. /private/tmp is the canonical short
    // filesystem location; owner-only endpoint permissions are applied below.
    name = QDir(QStringLiteral("/private/tmp")).filePath(name);
#endif
    if (!m_server->listen(name))
    {
        qCritical(logSystem()).noquote() << "Automation bridge could not listen:" << m_server->errorString();
        m_handler = {};
        return false;
    }
#if defined(Q_OS_UNIX) && !defined(Q_OS_LINUX)
    if (!QFile::setPermissions(m_server->fullServerName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qCritical(logSystem()).noquote() << "Automation socket permissions could not be restricted";
        m_server->close();
        QLocalServer::removeServer(name);
        m_handler = {};
        return false;
    }
#endif
    if (!writeDiscoveryFile())
    {
        m_server->close();
        m_handler = {};
        return false;
    }

    qInfo(logSystem()).noquote().nospace()
        << "Automation bridge listening socket=" << m_server->serverName() << " discovery=" << m_discoveryFilePath;
    return true;
}

void AutomationServer::stop()
{
    const bool hadClients = !m_clients.isEmpty();
    const auto sockets = m_clients.keys();
    for (QLocalSocket* socket : sockets)
    {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_clients.clear();
    if (m_server->isListening())
    {
        m_server->close();
    }
    removeDiscoveryFile();
    m_handler = {};
    if (hadClients)
    {
        emit clientCountChanged(0);
    }
}

QString AutomationServer::socketName() const
{
    return m_server->serverName();
}

void AutomationServer::acceptPendingConnections()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection())
    {
        if (m_clients.size() >= kMaximumClients)
        {
            const QJsonDocument response(
                errorResponse(QStringLiteral("too_many_clients"), QStringLiteral("Automation client limit reached")));
            socket->write(response.toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
            socket->disconnectFromServer();
            socket->deleteLater();
            continue;
        }

        m_clients.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { readClient(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() { removeClient(socket); });
        emit clientCountChanged(m_clients.size());
    }
}

void AutomationServer::readClient(QLocalSocket* socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
    {
        return;
    }
    it.value().append(socket->readAll());
    if (it.value().size() > kMaximumRequestBytes && !it.value().contains('\n'))
    {
        processLine(socket, QByteArray(kMaximumRequestBytes + 1, ' '));
        socket->disconnectFromServer();
        return;
    }

    while (true)
    {
        const qsizetype newline = it.value().indexOf('\n');
        if (newline < 0)
        {
            break;
        }
        const QByteArray line = it.value().left(newline).trimmed();
        it.value().remove(0, newline + 1);
        if (!line.isEmpty())
        {
            processLine(socket, line);
        }
    }
}

void AutomationServer::removeClient(QLocalSocket* socket)
{
    if (m_clients.remove(socket) == 0)
    {
        return;
    }
    socket->deleteLater();
    emit clientCountChanged(m_clients.size());
}

void AutomationServer::processLine(QLocalSocket* socket, const QByteArray& line)
{
    QJsonObject response;
    if (line.size() > kMaximumRequestBytes)
    {
        response = errorResponse(QStringLiteral("request_too_large"), QStringLiteral("Request exceeds 64 KiB"));
    }
    else
    {
        QJsonParseError parseError;
        const QJsonDocument requestDocument = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !requestDocument.isObject())
        {
            response = errorResponse(QStringLiteral("invalid_json"), QStringLiteral("Expected one JSON object"));
        }
        else
        {
            const QJsonObject request = requestDocument.object();
            response = m_handler ? m_handler(request)
                                 : errorResponse(QStringLiteral("bridge_unavailable"),
                                                 QStringLiteral("Automation bridge is stopping"));
            if (request.contains(QStringLiteral("id")))
            {
                response.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
            }
        }
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
}

bool AutomationServer::writeDiscoveryFile()
{
    const QString discoveryDirectory = QDir(sdr9700::configDirectory()).filePath(QStringLiteral("automation"));
    if (!QDir().mkpath(discoveryDirectory) ||
        !QFile::setPermissions(discoveryDirectory,
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
    {
        qCritical(logSystem()).noquote() << "Automation discovery directory could not be secured";
        return false;
    }
    m_discoveryFilePath =
        QDir(discoveryDirectory)
            .filePath(QStringLiteral("sdr9700-automation-%1.json").arg(QCoreApplication::applicationPid()));
    QSaveFile file(m_discoveryFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qCritical(logSystem()).noquote() << "Automation discovery file could not be opened:" << file.errorString();
        m_discoveryFilePath.clear();
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qCritical(logSystem()).noquote() << "Automation discovery file permissions could not be restricted";
        file.cancelWriting();
        m_discoveryFilePath.clear();
        return false;
    }
    const QJsonObject discovery{{QStringLiteral("application"), QStringLiteral("SDR9700")},
                                {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                                {QStringLiteral("protocol"), 1},
                                {QStringLiteral("socket"), m_server->fullServerName()},
                                {QStringLiteral("transmitAllowed"), false}};
    file.write(QJsonDocument(discovery).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        qCritical(logSystem()).noquote() << "Automation discovery file could not be committed:" << file.errorString();
        m_discoveryFilePath.clear();
        return false;
    }
    if (!QFile::setPermissions(m_discoveryFilePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        qCritical(logSystem()).noquote() << "Automation discovery file permissions could not be restricted";
        QFile::remove(m_discoveryFilePath);
        m_discoveryFilePath.clear();
        return false;
    }
    return true;
}

void AutomationServer::removeDiscoveryFile()
{
    if (!m_discoveryFilePath.isEmpty())
    {
        QFile::remove(m_discoveryFilePath);
        m_discoveryFilePath.clear();
    }
}
