#include "UdpHandler.h"
#include "RadioSessionRecoveryStore.h"
#include "UdpStatusMessages.h"
#include "AppInfo.h"
#include "LogCategories.h"

#include <QRandomGenerator>
#include <algorithm>
#include <iterator>
#include <utility>

namespace
{
constexpr quint32 kLoginErrorInvalidCredentials = 0xfeffffff;
constexpr int kAreYouThereMaxAttempts = 60;
constexpr int kClientSessionCodeWidth = 6;

template <size_t N> void copyPacketField(char (&destination)[N], const QByteArray& source)
{
    const int length = qMin<int>(static_cast<int>(N), source.size());
    if (length > 0)
    {
        memcpy(destination, source.constData(), length);
    }
}

template <size_t N> void copyPacketField(char (&destination)[N], const QString& source)
{
    copyPacketField(destination, source.toLatin1());
}

QString boundedLatin1(const char* s, int maxLen)
{
    return QString::fromLatin1(parseNullTerminatedString(QByteArray::fromRawData(s, maxLen), 0));
}

QString makeClientSessionName()
{
    // The IC-9700 exposes this 16-byte computer name in later busy-status
    // packets. A per-process suffix lets SDR9700 distinguish this process from
    // a stale previous process while still keeping a recognizable app prefix.
    const quint32 code = QRandomGenerator::global()->generate() & 0x00ffffff;
    return QStringLiteral("%1-%2").arg(
        QString::fromLatin1(APP_NAME),
        QStringLiteral("%1").arg(code, kClientSessionCodeWidth, 16, QLatin1Char('0')).toUpper());
}

QString clientSessionName()
{
    static const QString name = makeClientSessionName();
    return name;
}

bool isSdr9700SessionName(const QString& name)
{
    return name == QString::fromLatin1(APP_NAME) || name.startsWith(QStringLiteral("%1-").arg(APP_NAME));
}
} // namespace

UdpHandler::UdpHandler(UdpConnectionSettings settings, audioSetup rxAudio, audioSetup txAudio)
    : controlPort(settings.controlLANPort),
      civPort(0),
      audioPort(0),
      civLocalPort(0),
      audioLocalPort(0),
      rxSetup(rxAudio),
      txSetup(txAudio)
{
    // Discovery and authentication do not own the radio session. Enable the
    // inherited control-departure packet only after a stream grant arrives.
    setDepartureAllowed(false);
    this->port = this->controlPort;
    this->username = settings.username;
    passcode(settings.username, usernameEncoded);
    passwordEncoded = settings.passwordEncoded;
    this->compName = clientSessionName();
    std::fill(std::begin(audioLevelsTxPeak), std::end(audioLevelsTxPeak), 0);
    std::fill(std::begin(audioLevelsRxPeak), std::end(audioLevelsRxPeak), 0);
    std::fill(std::begin(audioLevelsTxRMS), std::end(audioLevelsTxRMS), 0);
    std::fill(std::begin(audioLevelsRxRMS), std::end(audioLevelsRxRMS), 0);

    qInfo(logUdp()).noquote().nospace() << "Starting control session client=" << compName
                                        << " rxLatency=" << rxSetup.latency << " txLatency=" << txSetup.latency
                                        << " rxSampleRate=" << rxSetup.sampleRate << " rxCodec=" << rxSetup.codec
                                        << " txSampleRate=" << txSetup.sampleRate << " txCodec=" << txSetup.codec;

    // Use numeric IPv4 addresses only here. Synchronous DNS lookup can stall
    // connection setup; if hostname support is needed later, resolve it before
    // constructing UdpHandler with QHostInfo::lookupHost().
    if (!radioIP.setAddress(settings.ipAddress))
    {
        qWarning(logUdp()).noquote() << "Invalid radio IPv4 address:" << settings.ipAddress;
        return;
    }

    // Ask the kernel routing table which source address it would use for this
    // radio. Enumerating interfaces and choosing the first active IPv4 address
    // is incorrect on VPN, VLAN, routed, and otherwise multi-homed hosts.
    QUdpSocket routeProbe;
    routeProbe.connectToHost(radioIP, controlPort, QIODevice::WriteOnly);
    if (routeProbe.state() == QAbstractSocket::ConnectedState || routeProbe.waitForConnected(100))
    {
        localIP = routeProbe.localAddress();
    }
}

void UdpHandler::init()
{
    if (radioIP.isNull() || localIP.isNull())
    {
        emit haveNetworkError(errorType(true, radioIP.toString(),
                                        "Unable to determine the local IPv4 route to the radio.",
                                        ErrorCode::ConnectionFailed));
        return;
    }
    if (!UdpBase::init(0))
    {
        emit haveNetworkError(errorType(true, radioIP.toString(), "Unable to bind the radio control UDP socket.",
                                        ErrorCode::PortReservationFailed));
        return;
    }

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &UdpHandler::dataReceived);

    // A crashed owner can leave the IC-9700 listening only to its retained
    // control and media identities. Retire those identities before discovery;
    // waiting for the replacement association's "I am ready" response can
    // deadlock because the radio may not answer it while the old association
    // remains live.
    const auto predecessor = sdr9700::RadioSessionRecoveryStore::loadForRadio(radioIP.toString());
    if (predecessor && predecessor->hasTransportIdentities())
    {
        qInfo(logUdp()).noquote() << "Retiring retained predecessor transports before radio discovery";
        reclaimPredecessorTransports(*predecessor);
        m_predecessorTransportsReclaimed = true;
    }

    tokenTimer = new QTimer(this);
    areYouThereTimer = new QTimer(this);
    pingTimer = new QTimer(this);
    idleTimer = new QTimer(this);
    watchdogTimer = new QTimer(this);
    civReadinessTimer = new QTimer(this);
    civReadinessTimer->setSingleShot(true);
    predecessorRemovalTimer = new QTimer(this);
    predecessorRemovalTimer->setSingleShot(true);

    connect(tokenTimer, &QTimer::timeout, this, std::bind(&UdpHandler::sendToken, this, 0x05));
    connect(areYouThereTimer, &QTimer::timeout, this, &UdpHandler::sendAreYouThere);
    connect(pingTimer, &QTimer::timeout, this, &UdpBase::sendPing);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, true, 0, 0));
    connect(watchdogTimer, &QTimer::timeout, this, &UdpHandler::monitorSessionHealth);
    connect(predecessorRemovalTimer, &QTimer::timeout, this, &UdpHandler::sendPredecessorTokenRemovalAttempt);
    connect(civReadinessTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_shuttingDown && m_civProbeSent && !m_civDataObserved)
                {
                    // Audio and unsolicited scope frames are insufficient here:
                    // both can arrive from a retained or sleeping media session.
                    // Report a typed bootstrap failure so the backend can first
                    // rule out a stale session and only then attempt power-on.
                    qWarning(logUdp()).noquote() << "CI-V identity probe timed out; command plane is unavailable";
                    emit haveNetworkError(errorType(false, radioIP.toString(),
                                                    "CI-V stream did not respond to radio commands.",
                                                    ErrorCode::CommandPlaneUnavailable));
                }
            });

    // Probe until the IC-9700 replies with "I am here".
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
    watchdogTimer->start(WATCHDOG_PERIOD);
}

void UdpHandler::shutdown()
{
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    const bool hadRadioSession = m_sessionOwnership.permitsRadioTeardown();
    qDebug(logUdp()).noquote() << "[SHUTDOWN] UdpHandler::shutdown() enter";

    // Stop all timers before deleting the UDP socket. Any timer that fires
    // after udp is deleted would call sendPing()/sendControl() on a null
    // pointer. pingTimer and idleTimer connect to UdpBase methods that
    // dereference udp unconditionally.
    if (pingTimer)
    {
        pingTimer->stop();
    }
    if (idleTimer)
    {
        idleTimer->stop();
    }
    if (watchdogTimer)
    {
        watchdogTimer->stop();
    }
    if (civReadinessTimer)
    {
        civReadinessTimer->stop();
    }
    if (predecessorRemovalTimer)
    {
        predecessorRemovalTimer->stop();
    }
    if (retransmitTimer)
    {
        retransmitTimer->stop();
    }
    if (tokenTimer)
    {
        tokenTimer->stop();
    }
    if (areYouThereTimer)
    {
        areYouThereTimer->stop();
    }

    m_shuttingDown = true;
    m_disconnectStatusReceived = false;
    m_tokenRemovalAcknowledged = false;

    bool radioConfirmed = false;
    if (hadRadioSession)
    {
        qInfo(logUdp()).noquote().nospace() << "[SHUTDOWN] stage=stream-close elapsedMs=" << shutdownTimer.elapsed();
        beginStreamShutdown();
        waitForStreamShutdownSettle(500);

        qInfo(logUdp()).noquote().nospace() << "[SHUTDOWN] stage=token-removal elapsedMs=" << shutdownTimer.elapsed();
        radioConfirmed = releaseAuthenticationToken(true);

        // UdpBase normally sends this from its destructor. The staged shutdown
        // closes the control socket earlier, so send the control-port departure
        // explicitly after the radio has acknowledged token removal.
        qInfo(logUdp()).noquote().nospace()
            << "[SHUTDOWN] stage=control-departure elapsedMs=" << shutdownTimer.elapsed();
        sendDeparture();
    }
    else
    {
        qInfo(logUdp()).noquote()
            << "[SHUTDOWN] no owned radio session; performing local cleanup without radio-side teardown";
        releaseAuthenticationToken(false);
    }
    m_sessionOwnership.release();
    setDepartureAllowed(false);

    qInfo(logUdp()).noquote().nospace() << "[SHUTDOWN] stage=local-stream-cleanup elapsedMs="
                                        << shutdownTimer.elapsed();
    closeStreams();

    if (civPortReservation)
    {
        civPortReservation->close();
        delete civPortReservation;
        civPortReservation = nullptr;
    }
    if (audioPortReservation)
    {
        audioPortReservation->close();
        delete audioPortReservation;
        audioPortReservation = nullptr;
    }

    // Close this handler's UDP socket so readyRead signals stop before shutdown returns.
    if (udp != nullptr)
    {
        qDebug(logUdp()).noquote() << "[SHUTDOWN] closing handler UDP socket";
        udp->close();
        delete udp;
        udp = nullptr;
    }
    usernameEncoded.fill('\0');
    usernameEncoded.clear();
    passwordEncoded.fill('\0');
    passwordEncoded.clear();
    m_shuttingDown = false;

    qInfo(logUdp()).noquote().nospace() << "[SHUTDOWN] complete elapsedMs=" << shutdownTimer.elapsed()
                                        << " hadRadioSession=" << hadRadioSession
                                        << " radioConfirmed=" << radioConfirmed
                                        << " tokenAcknowledged=" << m_tokenRemovalAcknowledged
                                        << " disconnectStatusReceived=" << m_disconnectStatusReceived;
}

UdpHandler::~UdpHandler()
{
    // Ensure the control socket is closed even if shutdown() was skipped.
    closeStreams();
    releaseAuthenticationToken(false);
    setDepartureAllowed(m_sessionOwnership.permitsRadioTeardown());
    if (civPortReservation)
    {
        delete civPortReservation;
        civPortReservation = nullptr;
    }
    if (audioPortReservation)
    {
        delete audioPortReservation;
        audioPortReservation = nullptr;
    }
    usernameEncoded.fill('\0');
    passwordEncoded.fill('\0');
}

void UdpHandler::closeStreams()
{
    // Close CI-V first so the radio stops producing command traffic while the
    // slower Qt audio worker teardown runs.
    if (civ != nullptr)
    {
        civ->closeStream();
    }
    if (audio != nullptr)
    {
        qDebug(logUdp()).noquote() << "[SHUTDOWN] deleting audio";
        delete audio;
        audio = nullptr;
        qDebug(logUdp()).noquote() << "[SHUTDOWN] audio deleted";
    }
    if (civ != nullptr)
    {
        qDebug(logUdp()).noquote() << "[SHUTDOWN] deleting civ";
        delete civ;
        civ = nullptr;
        qDebug(logUdp()).noquote() << "[SHUTDOWN] civ deleted";
    }
    streamOpened = false;
    m_civStreamReady = false;
    m_civProbeSent = false;
    m_civDataObserved = false;
    m_preReadinessPayloadLogged = false;
    if (civReadinessTimer)
    {
        civReadinessTimer->stop();
    }
    m_civTransportReady = false;
    m_audioTransportReady = false;
    m_healthFailureReported = false;
    m_audioSilenceReported = false;
    m_sessionWatchdog.reset();
}

void UdpHandler::startMediaStreamsWhenReady()
{
    if (m_shuttingDown || m_civStreamReady || !m_civTransportReady || !m_audioTransportReady || civ == nullptr ||
        audio == nullptr)
    {
        return;
    }

    // The IC-9700 allocates CI-V and audio as one media session. Match the
    // deterministic lifecycle probe: finish both UDP transport handshakes,
    // open the CI-V pipe, then prove that the command plane is live with the
    // model-neutral broadcast identity query before releasing the ordinary
    // startup command burst. A retained predecessor can otherwise produce a
    // successful stream grant and audio while silently keeping CI-V dormant.
    qInfo(logUdp()).noquote() << "CI-V and audio transports ready; opening CI-V stream";
    const sdr9700::RadioSessionRecoveryRecord recoveryRecord{
        radioIP.toString(),
        compName,
        0,
        tokRequest,
        token,
        {boundLocalPort(), remotePort(), localSessionId(), remoteSessionId()},
        {civ->boundLocalPort(), civ->remotePort(), civ->localSessionId(), civ->remoteSessionId()},
        {audio->boundLocalPort(), audio->remotePort(), audio->localSessionId(), audio->remoteSessionId()}};
    if (!sdr9700::RadioSessionRecoveryStore::save(recoveryRecord))
    {
        qWarning(logUdp()).noquote() << "Could not save complete radio session recovery record";
    }
    civ->requestDataRestart();
    m_civStreamReady = true;
    m_healthFailureReported = false;
    m_sessionWatchdog.reset();
    m_civProbeSent = true;
    civ->send(QByteArray::fromHex("fefe00e11900fd"));
    civReadinessTimer->start(2500);
}

void UdpHandler::beginStreamShutdown()
{
    if (civ != nullptr)
    {
        qInfo(logUdp()).noquote() << "[SHUTDOWN] sending CI-V stream close";
        civ->closeStream();
        civ->sendDeparture();
    }
    if (audio != nullptr)
    {
        qInfo(logUdp()).noquote() << "[SHUTDOWN] sending audio stream departure";
        audio->sendDeparture();
    }
}

void UdpHandler::waitForStreamShutdownSettle(int timeoutMs)
{
    if (udp == nullptr || timeoutMs <= 0)
    {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (udp->waitForReadyRead(qMin(50, remaining)))
        {
            dataReceived();
        }
    }
    qInfo(logUdp()).noquote().nospace() << "[SHUTDOWN] stream-close settle complete elapsedMs=" << timer.elapsed();
}

void UdpHandler::monitorSessionHealth()
{
    if (m_shuttingDown || !streamOpened || !m_civStreamReady || civ == nullptr || m_healthFailureReported)
    {
        return;
    }

    const qint64 controlSilenceMs = lastPacketAgeMs();
    const qint64 civSilenceMs = civ->lastPacketAgeMs();
    const qint64 audioSilenceMs = audio != nullptr ? audio->lastPacketAgeMs() : 0;

    if (RadioSessionWatchdog::isHealthy(controlSilenceMs, civSilenceMs))
    {
        emit sessionHeartbeat();
    }

    if (audio != nullptr && audioSilenceMs >= RadioSessionWatchdog::kAudioSilenceDiagnosticMs)
    {
        if (!m_audioSilenceReported)
        {
            qWarning(logUdp()).noquote() << "Session watchdog: no audio packets for" << audioSilenceMs
                                         << "ms; control and CI-V health will determine recovery";
            m_audioSilenceReported = true;
        }
    }
    else if (m_audioSilenceReported)
    {
        qInfo(logUdp()).noquote() << "Session watchdog: audio packet flow resumed";
        m_audioSilenceReported = false;
    }

    const RadioSessionWatchdog::Action action = m_sessionWatchdog.evaluate(controlSilenceMs, civSilenceMs);
    if (action == RadioSessionWatchdog::Action::RestartCiv)
    {
        qWarning(logUdp()).noquote() << "Session watchdog: no CI-V data for" << civSilenceMs << "ms; restart attempt"
                                     << m_sessionWatchdog.civRecoveryAttempts() << "of"
                                     << RadioSessionWatchdog::kMaxCivRecoveryAttempts;
        civ->requestDataRestart();
        return;
    }
    if (action == RadioSessionWatchdog::Action::Disconnect)
    {
        m_healthFailureReported = true;
        qCritical(logUdp()).noquote().nospace()
            << "Session watchdog: radio communication stalled controlSilenceMs=" << controlSilenceMs
            << " civSilenceMs=" << civSilenceMs << " audioSilenceMs=" << audioSilenceMs;
        emit haveNetworkError(errorType(false, radioIP.toString(), "Radio communication stalled; reconnecting.",
                                        ErrorCode::Disconnected));
    }
}

void UdpHandler::beginStandbyWakeHold()
{
    // Silence is expected after the power-on frame while the IC-9700 boots.
    // The backend owns this bounded interval and will replace the provisional
    // session when it expires. Leaving the ordinary CI-V watchdog active here
    // lets it declare a stall first, bypassing the wake policy and adding an
    // unrelated reconnect delay even though the wake command succeeded.
    if (watchdogTimer)
    {
        watchdogTimer->stop();
    }
    qInfo(logUdp()).noquote() << "Session watchdog paused for bounded standby-wake hold";
}

bool UdpHandler::releaseAuthenticationToken(bool waitForAcknowledgement)
{
    if (!m_sessionOwnership.permitsRadioTeardown())
    {
        qInfo(logUdp()).noquote() << "[SHUTDOWN] no owned radio session; clearing local authentication state only";
        isAuthenticated = false;
        gotAuthOK = false;
        token = 0;
        return false;
    }
    if (udp == nullptr || (!isAuthenticated && !gotAuthOK && token == 0))
    {
        qInfo(logUdp()).noquote() << "[SHUTDOWN] no active authentication token to remove";
        return false;
    }

    qInfo(logUdp()).noquote() << "Sending token removal packet";
    m_disconnectStatusReceived = false;
    if (!waitForAcknowledgement)
    {
        sendToken(0x01);
    }
    else
    {
        constexpr int kDisconnectRetryIntervalMs = 500;
        constexpr int kDisconnectMaxAttempts = 8;

        for (int attempt = 1;
             attempt <= kDisconnectMaxAttempts && !m_tokenRemovalAcknowledged && !m_disconnectStatusReceived; ++attempt)
        {
            qInfo(logUdp()).noquote() << "Token removal attempt" << attempt << "of" << kDisconnectMaxAttempts;
            sendToken(0x01);

            QElapsedTimer attemptTimer;
            attemptTimer.start();
            while (!m_tokenRemovalAcknowledged && !m_disconnectStatusReceived &&
                   attemptTimer.elapsed() < kDisconnectRetryIntervalMs)
            {
                const int remaining = kDisconnectRetryIntervalMs - static_cast<int>(attemptTimer.elapsed());
                if (udp->waitForReadyRead(qMin(50, remaining)))
                {
                    dataReceived();
                }
            }
        }

        if (!m_tokenRemovalAcknowledged && !m_disconnectStatusReceived)
        {
            qWarning(logUdp()).noquote() << "Radio did not acknowledge token removal after" << kDisconnectMaxAttempts
                                         << "attempts; completing local disconnect";
        }
    }
    isAuthenticated = false;
    gotAuthOK = false;
    token = 0;
    if (m_tokenRemovalAcknowledged || m_disconnectStatusReceived)
    {
        sdr9700::RadioSessionRecoveryStore::removeOwned(radioIP.toString(), compName);
    }
    return m_tokenRemovalAcknowledged || m_disconnectStatusReceived;
}

bool UdpHandler::reserveStreamPorts()
{
    if (civPortReservation)
    {
        civPortReservation->close();
        delete civPortReservation;
    }
    if (audioPortReservation)
    {
        audioPortReservation->close();
        delete audioPortReservation;
    }
    civPortReservation = new QUdpSocket(this);
    audioPortReservation = new QUdpSocket(this);
    if (!civPortReservation->bind() || !audioPortReservation->bind())
    {
        qWarning(logUdp()).noquote() << "Unable to reserve local CI-V/audio UDP ports";
        delete civPortReservation;
        civPortReservation = nullptr;
        delete audioPortReservation;
        audioPortReservation = nullptr;
        civLocalPort = 0;
        audioLocalPort = 0;
        emit haveNetworkError(errorType(false, radioIP.toString(), "Unable to reserve local UDP ports.",
                                        ErrorCode::PortReservationFailed));
        return false;
    }

    civLocalPort = civPortReservation->localPort();
    audioLocalPort = audioPortReservation->localPort();
    return true;
}

void UdpHandler::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void UdpHandler::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void UdpHandler::enableAudio()
{
    if (audio != nullptr)
    {
        audio->enableAudio();
    }
}

void UdpHandler::setRxAudioDevice(const QAudioDevice& device)
{
    rxSetup.port = device;
    if (audio != nullptr)
    {
        audio->setRxAudioDevice(device);
    }
}

void UdpHandler::setTxAudioDevice(const QAudioDevice& device)
{
    txSetup.port = device;
    if (audio != nullptr)
    {
        audio->setTxAudioDevice(device);
    }
}

void UdpHandler::stopLocalAudio()
{
    if (audio != nullptr)
    {
        audio->stopLocalAudio();
    }
}

void UdpHandler::setPttActive(bool active)
{
    // UdpHandler and UdpAudio live on udpHandlerThread, so the handoff to
    // UdpAudio is direct once this slot is invoked by Commander.
    if (audio != nullptr)
    {
        audio->setTxActive(active);
    }
}

void UdpHandler::queueDtmfPcm(const QByteArray& pcm)
{
    if (audio != nullptr)
    {
        audio->queueDtmfPcm(pcm);
    }
}

void UdpHandler::receiveFromCivStream(const QByteArray& data)
{
    bool identityReply = false;
    if (m_civProbeSent && !m_civDataObserved)
    {
        // A retained stream can deliver stale scope data before this process
        // has opened its own CI-V pipe. Require the directed response to our
        // broadcast Transceiver ID query: FE FE <controller> <radio> 19 00 ...
        for (qsizetype offset = 0; offset + 6 < data.size(); ++offset)
        {
            const auto byteAt = [&data](qsizetype index) { return static_cast<quint8>(data.at(index)); };
            if (byteAt(offset) == 0xfe && byteAt(offset + 1) == 0xfe && byteAt(offset + 2) == 0xe1 &&
                byteAt(offset + 3) != 0x00 && byteAt(offset + 3) != 0xe1 && byteAt(offset + 4) == 0x19 &&
                byteAt(offset + 5) == 0x00)
            {
                identityReply = true;
                break;
            }
        }
    }
    if (identityReply)
    {
        m_civDataObserved = true;
        civReadinessTimer->stop();
        qInfo(logUdp()).noquote()
            << "Directed CI-V identity reply proved command plane ready; releasing startup commands";
        emit streamReady();
    }
    if (!m_civDataObserved)
    {
        if (!m_preReadinessPayloadLogged)
        {
            m_preReadinessPayloadLogged = true;
            qDebug(logUdp()).noquote() << "Ignoring pre-readiness CI-V payloads until directed identity reply";
        }
        return;
    }
    emit haveDataFromPort(data);
}

void UdpHandler::receiveAudioData(const audioPacket& data)
{
    emit haveAudioData(data);
}

void UdpHandler::receiveDataFromUserToRadio(QByteArray data)
{
    if (civ != nullptr)
    {
        qDebug(logRadioTraffic()).noquote().nospace()
            << "UdpHandler::CivHandoff port=" << civPort << " len=" << data.size();
        civ->send(data);
    }
    else
    {
        qDebug(logRadioTraffic()).noquote().nospace()
            << "UdpHandler::CivHandoffDropped reason=stream-unavailable len=" << data.size()
            << " hex=" << QString::fromLatin1(data.toHex(' '));
    }
}

void UdpHandler::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                             bool over)
{
    status.rxAudioLevel = amplitudePeak;
    status.rxLatency = latency;
    status.rxCurrentLatency = qint32(current);
    status.rxUnderrun = under;
    status.rxOverrun = over;
    audioLevelsRxPeak[(audioLevelsRxPosition) % audioLevelBufferSize] = amplitudePeak;
    audioLevelsRxRMS[(audioLevelsRxPosition) % audioLevelBufferSize] = amplitudeRMS;

    if ((audioLevelsRxPosition) % 4 == 0)
    {
        // Emit a short rolling summary instead of every audio level sample.
        quint8 meanPeak = findMax(audioLevelsRxPeak);
        quint8 meanRMS = findMean(audioLevelsRxRMS);
        networkAudioLevels l;
        l.haveRxLevels = true;
        l.rxAudioPeak = meanPeak;
        l.rxAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsRxPosition++;
}

void UdpHandler::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                             bool over)
{
    status.txAudioLevel = amplitudePeak;
    status.txLatency = latency;
    status.txCurrentLatency = qint32(current);
    status.txUnderrun = under;
    status.txOverrun = over;
    audioLevelsTxPeak[(audioLevelsTxPosition) % audioLevelBufferSize] = amplitudePeak;
    audioLevelsTxRMS[(audioLevelsTxPosition) % audioLevelBufferSize] = amplitudeRMS;

    if ((audioLevelsTxPosition) % 4 == 0)
    {
        // Emit a short rolling summary instead of every audio level sample.
        quint8 meanPeak = findMax(audioLevelsTxPeak);
        quint8 meanRMS = findMean(audioLevelsTxRMS);
        networkAudioLevels l;
        l.haveTxLevels = true;
        l.txAudioPeak = meanPeak;
        l.txAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsTxPosition++;
}

quint8 UdpHandler::findMean(const quint8* data)
{
    unsigned int sum = 0;
    for (int p = 0; p < audioLevelBufferSize; p++)
    {
        sum += data[p];
    }
    return sum / audioLevelBufferSize;
}

quint8 UdpHandler::findMax(const quint8* data)
{
    unsigned int max = 0;
    for (int p = 0; p < audioLevelBufferSize; p++)
    {
        if (data[p] > max)
        {
            max = data[p];
        }
    }
    return max;
}

void UdpHandler::dataReceived()
{
    while (udp->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        if (!acceptDatagramFrom(datagram))
        {
            continue;
        }
        markPacketReceived();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (CONTROL_SIZE):
        {
            const auto decoded = decodePacket<control_packet>(r);
            const control_packet* in = &*decoded;
            if (in->type == 0x04)
            {
                qInfo(logUdp()).noquote().nospace()
                    << "Control discovery acknowledged remote=" << datagram.senderAddress().toString() << ':'
                    << datagram.senderPort();

                if (areYouThereTimer->isActive())
                {
                    areYouThereTimer->stop();
                    pingTimer->start(PING_PERIOD);
                    idleTimer->start(IDLE_PERIOD);
                }
            }
            else if (in->type == 0x06)
            {
                qInfo(logUdp()).noquote() << "Control transport ready";
                if (!m_shuttingDown && !sentPacketLogin)
                {
                    sentPacketLogin = true;
                    // A crash leaves our last owned authentication token in
                    // the recovery store. Retire it on the new control
                    // association before the first login, matching the
                    // deterministic RS-BA1 recovery sequence. Logging in first
                    // creates an extra reissued authentication generation that
                    // can receive a stream grant without useful CI-V traffic.
                    const auto predecessor = sdr9700::RadioSessionRecoveryStore::loadForRadio(radioIP.toString());
                    if (predecessor)
                    {
                        m_staleSessionReclaimInProgress = true;
                        m_staleSessionReclaimAttempts = 1;
                        qInfo(logUdp()).noquote()
                            << "Removing retained predecessor authentication before initial login owner="
                            << predecessor->ownerName;
                        // Retained-session recovery normally completes in less
                        // than a second and requires no operator action. Keep
                        // its detailed progress in the log while preserving
                        // the stable "Connecting to radio" status message.
                        beginPredecessorTokenRemoval(*predecessor);
                    }
                    else
                    {
                        sendLogin();
                    }
                }
                else if (!m_shuttingDown)
                {
                    qDebug(logUdp()).noquote()
                        << this->metaObject()->className() << ": Ignoring duplicate ready response";
                }
            }
            break;
        }
        case (PING_SIZE):
        {
            const auto decoded = decodePacket<ping_packet>(r);
            const ping_packet* in = &*decoded;
            if (in->type == 0x07 && in->reply == 0x01 && streamOpened)
            {
                status.networkLatency += elapsedMs() - lastPingSentMs;
                status.networkLatency /= 2;
                status.packetsSent = packetsSent;
                status.packetsLost = packetsLost;
                if (audio != nullptr)
                {
                    status.packetsSent = status.packetsSent + audio->packetsSentCount();
                    status.packetsLost = status.packetsLost + audio->packetsLostCount();
                }
                if (civ != nullptr)
                {
                    status.packetsSent = status.packetsSent + civ->packetsSentCount();
                    status.packetsLost = status.packetsLost + civ->packetsLostCount();
                }

                if (status.rxCurrentLatency <= qint32(status.rxLatency) && !status.rxUnderrun && !status.rxOverrun)
                {
                    status.rxLatencyClass = QStringLiteral("normal");
                }
                else if (status.rxUnderrun)
                {
                    status.rxLatencyClass = QStringLiteral("underrun");
                }
                else if (status.rxOverrun)
                {
                    status.rxLatencyClass = QStringLiteral("overrun");
                }
                else
                {
                    status.rxLatencyClass = QStringLiteral("buffering");
                }
                QString txString;
                if (txSetup.codec == 0)
                {
                    txString = "(no tx)";
                }
                status.message = QString("%1 rx latency: %2 ms / rtt: %3 ms / loss: %4/%5")
                                     .arg(txString)
                                     .arg(status.rxCurrentLatency, 3)
                                     .arg(status.networkLatency, 3)
                                     .arg(status.packetsLost, 3)
                                     .arg(status.packetsSent, 3);
                status.userVisibleMessage = false;
                status.timeDifference = audio != nullptr ? audio->getTimeDifference() : 0;
                emit haveNetworkStatus(status);
            }
            break;
        }
        case (TOKEN_SIZE):
        {
            const auto decoded = decodePacket<token_packet>(r);
            const token_packet* in = &*decoded;
            if (in->requesttype == 0x05 && in->requestreply == 0x02 && in->type != 0x01)
            {
                // During ordered teardown only token-removal acknowledgement
                // traffic remains actionable. A delayed renewal response must
                // not restart the renewal timer, reopen streams, or begin a
                // fresh login while shutdown is trying to retire this token.
                if (m_shuttingDown)
                {
                    break;
                }
                const quint16 responseSequence = qFromBigEndian(in->innerseq);
                if (!sdr9700::matchesRadioSessionEnvelope(in->sentid, in->rcvdid, remoteId, myId) ||
                    !m_tokenRenewalRequest.matchesAuthenticationResponse(responseSequence))
                {
                    qInfo(logUdp()).noquote() << "Ignoring stale or unrelated token response";
                    break;
                }

                // A nonzero response to the first current-generation token
                // request is the radio reissuing retained authentication. The
                // returned six-byte identifier is authoritative and remains
                // usable; later renewals must still return zero.
                const bool initialAuthentication = !gotAuthOK;
                tokRequest = in->tokrequest;
                token = in->token;
                m_tokenRenewalRequest.clear();
                if (in->response == 0x0000 || initialAuthentication)
                {
                    qDebug(logUdp()).noquote().nospace()
                        << this->metaObject()->className()
                        << "::TokenRenewed result=" << (in->response == 0x0000 ? "accepted" : "reissued");
                    tokenTimer->start(TOKEN_RENEWAL);
                    gotAuthOK = true;
                    // Token renewal changes authentication fields but not the
                    // live UDP identities. Preserve the complete identity set;
                    // replacing it with a token-only aggregate would silently
                    // disable transport reclaim after a later crash.
                    auto recoveryRecord = sdr9700::RadioSessionRecoveryStore::load(radioIP.toString(), compName)
                                              .value_or(sdr9700::RadioSessionRecoveryRecord{
                                                  radioIP.toString(), compName, 0, 0, 0, {}, {}, {}});
                    recoveryRecord.tokenRequest = tokRequest;
                    recoveryRecord.token = token;
                    if (m_sessionOwnership.permitsRadioTeardown() &&
                        !sdr9700::RadioSessionRecoveryStore::save(recoveryRecord))
                    {
                        qWarning(logUdp()).noquote() << "Could not update the radio session recovery record";
                    }
                    // Authentication can complete before the connection-info
                    // advertisement tells us whether the radio is available.
                    // Never submit a zero-port stream request while another
                    // client owns the radio; setCurrentRadio() will request
                    // streams after it selects an available radio and reserves
                    // both local ports.
                    if (!streamOpened && !m_streamRequest.pending && civPortReservation && audioPortReservation &&
                        civLocalPort != 0 && audioLocalPort != 0)
                    {
                        sendRequestStream();
                    }
                }
                else if (in->response == 0xffffffff)
                {
                    qWarning(logUdp()).noquote()
                        << this->metaObject()->className() << ": Radio rejected token renewal, performing login";
                    closeStreams();
                    gotAuthOK = false;
                    isAuthenticated = false;
                    sendLogin();
                }
                else
                {
                    qWarning(logUdp()).noquote().nospace()
                        << "Unknown token-renewal response=0x" << Qt::hex << in->response;
                }
            }
            else if (in->requesttype == 0x01 && in->requestreply == 0x02 && in->type != 0x01)
            {
                const quint16 responseSequence = qFromBigEndian(in->innerseq);
                if (!sdr9700::matchesRadioSessionEnvelope(in->sentid, in->rcvdid, remoteId, myId) ||
                    !m_tokenRemovalRequest.matchesAuthenticationResponse(responseSequence))
                {
                    qInfo(logUdp()).noquote() << "Ignoring stale or unrelated token-removal response";
                    break;
                }
                m_tokenRemovalRequest.clear();
                m_tokenRemovalAcknowledged = true;
                qInfo(logUdp()).noquote().nospace()
                    << this->metaObject()->className() << ": token removal acknowledged response=0x" << Qt::hex
                    << in->response;
                if (m_predecessorRemovalPolicy.pending())
                {
                    completePredecessorTokenRemoval();
                }
            }
            break;
        }
        case (STATUS_SIZE):
        {
            const auto decoded = decodePacket<status_packet>(r);
            const status_packet* in = &*decoded;
            if (in->type != 0x01)
            {
                // A disconnect acknowledgement is part of the documented
                // shutdown handshake. All other status replies are stale
                // stream-negotiation traffic once shutdown begins and must not
                // create CI-V/audio handlers again.
                if (m_shuttingDown && !(in->error == 0x00000000 && in->disc == 0x01))
                {
                    break;
                }
                const quint16 responseSequence = qFromBigEndian(in->innerseq);
                const bool currentEnvelope =
                    sdr9700::matchesRadioSessionEnvelope(in->sentid, in->rcvdid, remoteId, myId);
                const bool currentStreamResponse =
                    currentEnvelope && m_streamRequest.matches(responseSequence, in->tokrequest, in->token);
                const bool currentDisconnect = currentEnvelope && in->tokrequest == tokRequest && in->token == token;
                if ((in->disc == 0x01 && !currentDisconnect) || (in->disc != 0x01 && !currentStreamResponse))
                {
                    qInfo(logUdp()).noquote() << "Ignoring stale or unrelated stream status response";
                    break;
                }
                if (in->disc != 0x01)
                {
                    m_streamRequest.clear();
                }
                if (in->error == 0x00000000 && in->disc == 0x01 && currentDisconnect &&
                    m_predecessorRemovalPolicy.pending())
                {
                    m_disconnectStatusReceived = true;
                    completePredecessorTokenRemoval();
                    break;
                }
                if (in->error != 0x00000000 && !streamOpened)
                {
                    if (sdr9700::shouldResetReissuedTokenAfterStreamRejection(m_staleSessionReclaimInProgress,
                                                                              m_retainedTokenResetAttempted, in->error))
                    {
                        // The replacement now possesses the authentication
                        // identifier reissued by the radio. Retire that
                        // retained token on this tracked control association,
                        // then begin a genuinely fresh login. This is the same
                        // ordering proven by the standalone recovery probe,
                        // without persisting predecessor credentials.
                        m_retainedTokenResetAttempted = true;
                        if (tokenTimer)
                        {
                            tokenTimer->stop();
                        }
                        qWarning(logUdp()).noquote()
                            << "Recovered authentication was denied stream ownership; removing reissued token and "
                               "retrying login";
                        sendToken(0x01);
                        gotAuthOK = false;
                        isAuthenticated = false;
                        closeStreams();
                        m_loginRequest.clear();
                        m_tokenRenewalRequest.clear();
                        m_streamRequest.clear();
                        m_currentStreamGrantObserved = false;
                        QTimer::singleShot(50, this,
                                           [this]()
                                           {
                                               if (m_shuttingDown || udp == nullptr)
                                               {
                                                   return;
                                               }
                                               if (radios.size() == 1)
                                               {
                                                   setCurrentRadio(0);
                                               }
                                               sendLogin();
                                           });
                        break;
                    }
                    emit haveNetworkError(errorType(
                        true, radioIP.toString(),
                        "The radio rejected the stream request. Restart the radio if the session remains busy.",
                        ErrorCode::ConnectionFailed));
                    qInfo(logUdp()).noquote() << this->metaObject()->className()
                                              << ": Connection failed, wait a few minutes or reboot the radio.";
                }
                else if (in->error == 0x00000000 && in->disc == 0x01)
                {
                    m_disconnectStatusReceived = true;
                    if (!m_shuttingDown)
                    {
                        m_sessionOwnership.release();
                        setDepartureAllowed(false);
                    }
                    if (!m_shuttingDown && !m_staleSessionReclaimInProgress)
                    {
                        emit haveNetworkError(errorType(false, radioIP.toString(),
                                                        "Radio ended the connection; reconnecting.",
                                                        ErrorCode::Disconnected));
                    }
                    qInfo(logUdp()).noquote() << "Radio acknowledged stream disconnection";
                    closeStreams();
                }
                else
                {
                    civPort = qFromBigEndian(in->civport);
                    audioPort = qFromBigEndian(in->audioport);
                    qInfo(logUdp()).noquote().nospace()
                        << "Stream request result error="
                        << QString("0x%1").arg(qFromBigEndian(in->error), 8, 16, QChar('0'))
                        << " disconnected=" << quint8(in->disc) << " civPort=" << civPort << " audioPort=" << audioPort
                        << " rxCodec=" << rxSetup.codec << " txCodec=" << txSetup.codec;
                    if (civPort == 0 || audioPort == 0)
                    {
                        qWarning(logUdp()).noquote().nospace()
                            << "Radio returned invalid stream ports; refusing to open CI-V/audio streams civPort="
                            << civPort << " audioPort=" << audioPort;
                        emit haveNetworkError(errorType(true, radioIP.toString(),
                                                        "Connection failed: radio returned invalid UDP stream ports.",
                                                        ErrorCode::ConnectionFailed));
                        if (civPortReservation)
                        {
                            civPortReservation->close();
                            delete civPortReservation;
                            civPortReservation = nullptr;
                        }
                        if (audioPortReservation)
                        {
                            audioPortReservation->close();
                            delete audioPortReservation;
                            audioPortReservation = nullptr;
                        }
                        streamOpened = false;
                        break;
                    }
                    // A successful status response with usable stream ports is
                    // the ownership boundary. A login token received before
                    // this point belongs only to negotiation and must never
                    // authorize teardown of another client's active stream.
                    m_sessionOwnership.acquire();
                    setDepartureAllowed(true);
                    const sdr9700::RadioSessionRecoveryRecord recoveryRecord{
                        radioIP.toString(), compName, 0, tokRequest, token, {}, {}, {}};
                    if (!sdr9700::RadioSessionRecoveryStore::save(recoveryRecord))
                    {
                        qWarning(logUdp()).noquote() << "Could not save the radio session recovery record";
                    }
                    if (!streamOpened)
                    {
                        m_staleSessionReclaimAttempts = 0;
                        m_staleSessionReclaimInProgress = false;
                        m_retainedTokenResetAttempted = false;

                        QUdpSocket* boundCivSocket = civPortReservation;
                        civPortReservation = nullptr;
                        civ = new UdpCivData(localIP, radioIP, civPort, civLocalPort, boundCivSocket);
                        if (!civ->isSocketBound())
                        {
                            delete civ;
                            civ = nullptr;
                            emit haveNetworkError(errorType(true, radioIP.toString(),
                                                            "Unable to bind the CI-V UDP socket.",
                                                            ErrorCode::PortReservationFailed));
                            break;
                        }
                        QObject::connect(civ, &UdpCivData::receive, this, &UdpHandler::receiveFromCivStream);
                        QObject::connect(civ, &UdpCivData::ready, this,
                                         [this]()
                                         {
                                             qInfo(logUdp()).noquote() << "CI-V transport ready";
                                             m_civTransportReady = true;
                                             startMediaStreamsWhenReady();
                                         });
                        streamOpened = true;

                        if (txSampleRates < 2)
                        {
                            txSetup.sampleRate = 0;
                            txSetup.codec = 0;
                        }
                    }
                    if (audio == nullptr)
                    {
                        QUdpSocket* boundAudioSocket = audioPortReservation;
                        audioPortReservation = nullptr;
                        audio = new UdpAudio(localIP, radioIP, audioPort, audioLocalPort, rxSetup, txSetup,
                                             boundAudioSocket);
                        if (!audio->isSocketBound())
                        {
                            delete audio;
                            audio = nullptr;
                            closeStreams();
                            emit haveNetworkError(errorType(true, radioIP.toString(),
                                                            "Unable to bind the audio UDP socket.",
                                                            ErrorCode::PortReservationFailed));
                            break;
                        }

                        QObject::connect(audio, &UdpAudio::haveAudioData, this, &UdpHandler::receiveAudioData);
                        QObject::connect(audio, &UdpAudio::ready, this,
                                         [this]()
                                         {
                                             qInfo(logUdp()).noquote() << "Audio transport ready";
                                             m_audioTransportReady = true;
                                             startMediaStreamsWhenReady();
                                         });
                        QObject::connect(this, &UdpHandler::haveChangeLatency, audio, &UdpAudio::changeLatency);
                        QObject::connect(this, &UdpHandler::haveSetVolume, audio, &UdpAudio::setVolume);
                        QObject::connect(audio, &UdpAudio::haveRxLevels, this, &UdpHandler::getRxLevels);
                        QObject::connect(audio, &UdpAudio::haveTxLevels, this, &UdpHandler::getTxLevels);
                    }

                    qInfo(logUdp()).noquote().nospace()
                        << this->metaObject()->className() << "::StreamRequestAccepted device=" << devName;
                }
            }
            break;
        }
        case (LOGIN_RESPONSE_SIZE):
        {
            const auto decoded = decodePacket<login_response_packet>(r);
            const login_response_packet* in = &*decoded;
            if (in->type != 0x01)
            {
                if (m_shuttingDown)
                {
                    // A login completion can race with teardown. Preserve a
                    // matching token so releaseAuthenticationToken() can
                    // retire it, but do not continue authentication, restart
                    // renewal, or request streams.
                    const quint16 responseSequence = qFromBigEndian(in->innerseq);
                    if (in->error != kLoginErrorInvalidCredentials &&
                        sdr9700::matchesRadioSessionEnvelope(in->sentid, in->rcvdid, remoteId, myId) &&
                        m_loginRequest.matchesLogin(responseSequence, in->tokrequest))
                    {
                        token = in->token;
                        m_loginRequest.clear();
                    }
                    break;
                }

                const quint16 responseSequence = qFromBigEndian(in->innerseq);
                if (!sdr9700::matchesRadioSessionEnvelope(in->sentid, in->rcvdid, remoteId, myId) ||
                    !m_loginRequest.matchesLogin(responseSequence, in->tokrequest))
                {
                    qInfo(logUdp()).noquote() << "Ignoring stale or unrelated login response";
                    break;
                }
                m_loginRequest.clear();

                m_connectionType = boundedLatin1(in->connection, sizeof(in->connection));
                qInfo(logUdp()).noquote().nospace() << "Connection type=" << m_connectionType;
                // The IC-9700 supports LPCM16 LAN receive audio in either
                // mono (0x04) or stereo (0x10). Stereo carries the separate
                // MAIN/SUB receiver paths when the radio provides them, so do
                // not reduce an explicitly requested two-channel stream to
                // mono during login. LAN transmit audio remains mono LPCM16.
                static constexpr quint8 kLpcmMono16 = 0x04;
                static constexpr quint8 kLpcmStereo16 = 0x10;
                if (rxSetup.codec != kLpcmMono16 && rxSetup.codec != kLpcmStereo16)
                {
                    qWarning(logUdp()).noquote() << "Unsupported LAN receive audio codec requested; using mono LPCM16";
                    rxSetup.codec = kLpcmMono16;
                }
                if (txSetup.codec != 0 && txSetup.codec != kLpcmMono16)
                {
                    qWarning(logUdp()).noquote() << "Unsupported LAN transmit audio codec requested; using mono LPCM16";
                    txSetup.codec = kLpcmMono16;
                }

                if (in->error == kLoginErrorInvalidCredentials)
                {
                    emit haveNetworkError(errorType(true, radioIP.toString(),
                                                    "The radio rejected the username or password.",
                                                    ErrorCode::AuthFailure));
                    qInfo(logUdp()).noquote().nospace()
                        << this->metaObject()->className() << "::AuthenticationRejected reason=invalid-credentials";
                }
                else if (!isAuthenticated)
                {

                    if (in->tokrequest == tokRequest)
                    {
                        qInfo(logUdp()).noquote() << "Login response matched current request; requesting streams";
                        token = in->token;
                        sendToken(0x02);
                        sendToken(0x05);
                        isAuthenticated = true;
                    }
                    else
                    {
                        qInfo(logUdp()).noquote()
                            << this->metaObject()->className() << ": Token response did not match, sent:" << tokRequest
                            << " got " << in->tokrequest;
                    }
                }

                qDebug(logUdp()).noquote().nospace() << "Negotiated connection type=" << m_connectionType;
            }
            break;
        }
        case (CONNINFO_SIZE):
        {
            // Connection status for one radio advertised by the IC-9700 LAN server.

            // Connection-info broadcasts are discovery/session-establishment
            // traffic, never teardown acknowledgements. A reordered idle
            // advertisement after closeStreams() must not reserve ports and
            // request a fresh stream while shutdown is removing the token.
            if (m_shuttingDown)
            {
                break;
            }

            const auto decoded = decodePacket<conninfo_packet>(r);
            const conninfo_packet* in = &*decoded;

            // The radio also uses this packet shape for the definitive stream
            // grant. Classify it before the general busy advertisement path;
            // otherwise our own current grant looks like a stale local owner
            // and can incorrectly start another login generation.
            if (in->requestreply == 0x02 && in->requesttype == 0x03)
            {
                const quint16 responseSequence = qFromBigEndian(in->innerseq);
                if (!sdr9700::matchesRadioSessionEnvelope(in->sentid, in->rcvdid, remoteId, myId) ||
                    !m_streamRequest.matchesIdentity(responseSequence, in->tokrequest, in->token))
                {
                    qInfo(logUdp()).noquote() << "Ignoring stale or unrelated stream grant";
                    break;
                }

                m_currentStreamGrantObserved = true;
                tokRequest = in->tokrequest;
                token = in->token;
                qInfo(logUdp()).noquote() << "Received current-generation stream grant";
                break;
            }

            QHostAddress ip = QHostAddress(qToBigEndian(in->ipaddress));

            qInfo(logUdp()).noquote().nospace()
                << "Connection status name=" << boundedLatin1(in->name, sizeof(in->name)) << " busy=" << in->busy
                << " computer=" << boundedLatin1(in->computer, sizeof(in->computer)) << " ipAddress=" << ip.toString();

            // Match the status packet to a previously advertised radio.
            for (qsizetype f = 0; f < radios.size(); ++f)
            {

                if ((radios[f].commoncap == 0x8010 && radios[f].macaddress[0] == in->macaddress[0] &&
                     radios[f].macaddress[1] == in->macaddress[1] && radios[f].macaddress[2] == in->macaddress[2] &&
                     radios[f].macaddress[3] == in->macaddress[3] && radios[f].macaddress[4] == in->macaddress[4] &&
                     radios[f].macaddress[5] == in->macaddress[5]) ||
                    !memcmp(radios[f].guid, in->guid, GUIDLEN))
                {

                    bool admin = false;
                    if (in->busy && in->computer[0] != '\x0')
                    {
                        admin = true;
                    }

                    qDebug(logUdp()).noquote().nospace() << "Radio user admin=" << admin;
                    emit setRadioUsage(static_cast<quint8>(f), admin, in->busy,
                                       boundedLatin1(in->computer, sizeof(in->computer)), ip.toString());
                    qDebug(logUdp()).noquote().nospace()
                        << "Radio usage index=" << f << " name=" << boundedLatin1(in->name, sizeof(in->name))
                        << " busy=" << in->busy << " computer=" << boundedLatin1(in->computer, sizeof(in->computer))
                        << " ipAddress=" << ip.toString();
                }
            }

            if (!streamOpened && radios.size() == 1)
            {

                qDebug(logUdp()).noquote() << "Single radio advertised; selecting radio index 0";

                if (in->busy)
                {
                    const QString inComputer = boundedLatin1(in->computer, sizeof(in->computer));
                    const bool sameClientSession = inComputer == compName;
                    const bool staleLocalSdr9700Session =
                        !sameClientSession && isSdr9700SessionName(inComputer) && ip == localIP;
                    if (sameClientSession && gotAuthOK && m_streamRequest.pending)
                    {
                        // A busy advertisement naming this process immediately
                        // after its current authenticated request is the
                        // 0x90-byte stream grant observed on the IC-9700. It is
                        // not evidence of a retained predecessor session.
                        m_currentStreamGrantObserved = true;
                        qInfo(logUdp()).noquote() << "Received current-generation stream grant advertisement";
                        break;
                    }
                    if (staleLocalSdr9700Session)
                    {
                        // The IC-9700 can keep reporting busy by an SDR9700
                        // process if that process died before stream/token
                        // close completed. Recover only prior SDR9700 sessions
                        // from this same IP; another station running SDR9700
                        // still blocks and this process's grant is handled above.
                        if (requestRetainedSessionRecovery(inComputer))
                        {
                            // Recovery is automatic and non-actionable. Its
                            // bounded attempts remain visible in diagnostics;
                            // avoid flashing a warning status message between normal
                            // connection lifecycle stages.
                        }
                        else
                        {
                            sendControl(false, 0x00, in->seq); // Respond with an idle.
                            if (!m_foreignSessionReported)
                            {
                                m_foreignSessionReported = true;
                                const QString message = QStringLiteral("Radio in use by %1").arg(ip.toString());
                                qInfo(logUdp()).noquote().nospace() << message << " computer=" << inComputer;
                                emit haveNetworkError(
                                    errorType(false, radioIP.toString(), message, ErrorCode::RadioBusy));
                            }
                        }
                    }
                    else if (in->ipaddress != 0x00)
                    {
                        sendControl(false, 0x00, in->seq); // Respond with an idle
                        if (!m_foreignSessionReported)
                        {
                            m_foreignSessionReported = true;
                            // Icom clients commonly advertise the generic
                            // computer name "icom-pc". The peer address is the
                            // useful identity for an operator deciding which
                            // station currently owns the radio.
                            const QString message = QStringLiteral("Radio in use by %1").arg(ip.toString());
                            qInfo(logUdp()).noquote().nospace() << message << " computer=" << inComputer;
                            emit haveNetworkError(errorType(false, radioIP.toString(), message, ErrorCode::RadioBusy));
                        }
                    }
                    else if (inComputer != compName)
                    {
                        sendControl(false, 0x00, in->seq); // Respond with an idle
                        if (!m_foreignSessionReported)
                        {
                            m_foreignSessionReported = true;
                            const QString message = QStringLiteral("Radio in use by another client");
                            qInfo(logUdp()).noquote() << message;
                            emit haveNetworkError(errorType(false, radioIP.toString(), message, ErrorCode::RadioBusy));
                        }
                    }
                }
                else
                {
                    qInfo(logUdp()).noquote().nospace() << "Radio available; requesting stream ownership model="
                                                        << boundedLatin1(in->name, sizeof(in->name));

                    setCurrentRadio(0);
                }
            }
            else if (streamOpened)
            {
                // Status refresh while the stream is already open.
            }
            break;
        }

        default:
        {
            if (r.length() < CAPABILITIES_SIZE)
            {
                qWarning(logUdp()).noquote()
                    << "Ignoring short UDP datagram that is not a known fixed packet:" << r.length();
                break;
            }

            const auto decoded = decodePacket<capabilities_packet>(r);
            const capabilities_packet* in = &*decoded;
            const int capabilityBytes = r.length() - CAPABILITIES_SIZE;
            if (capabilityBytes % RADIO_CAP_SIZE != 0)
            {
                qWarning(logUdp()).noquote() << "Ignoring malformed capabilities packet length:" << r.length();
                break;
            }

            const int availableRadios = capabilityBytes / RADIO_CAP_SIZE;
            const int advertisedRadios = qFromBigEndian(in->numradios);
            const int radioCount = boundedCapabilityRadioCount(advertisedRadios, availableRadios);
            if (advertisedRadios != availableRadios)
            {
                qWarning(logUdp()).noquote() << "Capabilities radio count mismatch, advertised" << advertisedRadios
                                             << "contains" << availableRadios;
            }
            if (qMin(advertisedRadios, availableRadios) > MAX_CAPABILITY_RADIOS)
            {
                qWarning(logUdp()).noquote() << "Capabilities radio list exceeds supported limit; using first"
                                             << MAX_CAPABILITY_RADIOS << "entries";
            }

            radios.clear();
            numRadios = static_cast<quint8>(radioCount);

            for (int i = 0; i < radioCount; ++i)
            {
                radio_cap_packet rad{};
                const char* tmpRad = r.constData();
                memcpy(&rad, tmpRad + CAPABILITIES_SIZE + i * RADIO_CAP_SIZE, RADIO_CAP_SIZE);
                radios.append(rad);
                qInfo(logUdp()).noquote().nospace()
                    << this->metaObject()->className()
                    << QString("::RadioCapabilities name=\"%1\" audio=\"%2\" civ=%3 mac=%4:%5:%6:%7:%8:%9 capf=%10")
                           .arg(boundedLatin1(rad.name, sizeof(rad.name)))
                           .arg(boundedLatin1(rad.audio, sizeof(rad.audio)))
                           .arg((quint8)rad.civ, 2, 16, QChar('0'))
                           .arg(rad.macaddress[0], 2, 16, QChar('0'))
                           .arg(rad.macaddress[1], 2, 16, QChar('0'))
                           .arg(rad.macaddress[2], 2, 16, QChar('0'))
                           .arg(rad.macaddress[3], 2, 16, QChar('0'))
                           .arg(rad.macaddress[4], 2, 16, QChar('0'))
                           .arg(rad.macaddress[5], 2, 16, QChar('0'))
                           .arg(rad.capf, 4, 16, QChar('0'));
            }

            emit requestRadioSelection(radios);

            break;
        }
        }
        UdpBase::dataReceived(r);
        r.clear();
        datagram.clear();
    }
    return;
}

bool UdpHandler::requestRetainedSessionRecovery(const QString& ownerName)
{
    constexpr int kMaxStaleSessionReclaimAttempts = 2;

    if (m_staleSessionReclaimInProgress)
    {
        qInfo(logUdp()).noquote() << "Stale SDR9700 session reclaim already in progress for" << ownerName;
        return true;
    }

    if (m_staleSessionReclaimAttempts >= kMaxStaleSessionReclaimAttempts)
    {
        qWarning(logUdp()).noquote() << "Stale SDR9700 session reclaim limit reached for" << ownerName;
        return false;
    }

    // Recovery is authorized only by a journal whose recorded process is no
    // longer alive. A same-host SDR9700 name alone is not proof of abandonment:
    // it may belong to another running instance of this application.
    const auto predecessor = sdr9700::RadioSessionRecoveryStore::loadRecoverable(radioIP.toString(), ownerName);
    if (!predecessor)
    {
        return false;
    }

    ++m_staleSessionReclaimAttempts;
    m_staleSessionReclaimInProgress = true;
    qInfo(logUdp()).noquote().nospace() << "Recovering retained SDR9700 LAN session owner=" << ownerName
                                        << " attempt=" << m_staleSessionReclaimAttempts << '/'
                                        << kMaxStaleSessionReclaimAttempts;
    if (tokenTimer)
    {
        tokenTimer->stop();
    }
    qInfo(logUdp()).noquote() << "Removing retained predecessor authentication before fresh login";
    beginPredecessorTokenRemoval(*predecessor);
    return true;
}

void UdpHandler::beginPredecessorTokenRemoval(const sdr9700::RadioSessionRecoveryRecord& predecessor)
{
    if (!m_predecessorTransportsReclaimed)
    {
        reclaimPredecessorTransports(predecessor);
        m_predecessorTransportsReclaimed = predecessor.hasTransportIdentities();
    }

    tokRequest = predecessor.tokenRequest;
    token = predecessor.token;
    m_predecessorOwnerName = predecessor.ownerName;
    m_tokenRemovalAcknowledged = false;
    m_disconnectStatusReceived = false;
    m_predecessorRemovalPolicy.begin();
    // Build the inner request once and resend that same correlated operation.
    // Generating a new inner sequence for each retry can make a delayed valid
    // acknowledgement look stale forever when it crosses a retry boundary.
    m_predecessorRemovalPacket = createTokenPacket(0x01);
    sendPredecessorTokenRemovalAttempt();
}

void UdpHandler::sendPredecessorTokenRemovalAttempt()
{
    if (m_shuttingDown || udp == nullptr || !m_predecessorRemovalPolicy.pending())
    {
        return;
    }
    if (!m_predecessorRemovalPolicy.takeAttempt())
    {
        qWarning(logUdp()).noquote() << "Radio did not acknowledge retained predecessor token removal after"
                                     << sdr9700::RetainedSessionRemovalPolicy::kMaxAttempts << "attempts";
        m_predecessorRemovalPolicy.reset();
        emit haveNetworkError(errorType(true, radioIP.toString(),
                                        "The radio did not acknowledge recovery of the retained session.",
                                        ErrorCode::ConnectionFailed));
        return;
    }

    qInfo(logUdp()).noquote() << "Predecessor token removal attempt" << m_predecessorRemovalPolicy.attempts() << "of"
                              << sdr9700::RetainedSessionRemovalPolicy::kMaxAttempts;
    sendTrackedPacket(m_predecessorRemovalPacket);
    predecessorRemovalTimer->start(500);
}

void UdpHandler::completePredecessorTokenRemoval()
{
    if (!m_predecessorRemovalPolicy.acknowledge())
    {
        return;
    }
    predecessorRemovalTimer->stop();
    sdr9700::RadioSessionRecoveryStore::removeOwned(radioIP.toString(), m_predecessorOwnerName);
    qInfo(logUdp()).noquote() << "Retained predecessor token removal acknowledged; starting fresh login";

    token = 0;
    gotAuthOK = false;
    isAuthenticated = false;
    closeStreams();
    m_loginRequest.clear();
    m_tokenRenewalRequest.clear();
    m_tokenRemovalRequest.clear();
    m_streamRequest.clear();
    m_currentStreamGrantObserved = false;
    m_predecessorOwnerName.clear();
    m_predecessorRemovalPacket.clear();
    m_predecessorTransportsReclaimed = false;
    if (radios.size() == 1)
    {
        setCurrentRadio(0);
    }
    sendLogin();
}

void UdpHandler::reclaimPredecessorTransports(const sdr9700::RadioSessionRecoveryRecord& predecessor)
{
    if (!predecessor.hasTransportIdentities())
    {
        qInfo(logUdp()).noquote() << "Predecessor recovery record has no transport identities; removing token only";
        return;
    }

    // Bind the predecessor's exact local endpoint and place its saved session
    // ID pair in an ordinary RS-BA1 departure packet. The radio therefore sees
    // the packet as belonging to the abandoned transport, even though a new
    // SDR9700 process is sending it. Two copies match the protocol's defensive
    // departure behavior without entering the replacement stream's tracked
    // retransmission window.
    const auto sendDeparture = [this](const sdr9700::RadioSessionTransportIdentity& identity, const char* role)
    {
        QUdpSocket socket;
        if (!socket.bind(localIP, identity.localPort))
        {
            qWarning(logUdp()).noquote() << "Could not bind retained" << role << "port" << identity.localPort
                                         << "for predecessor departure:" << socket.errorString();
            return;
        }
        control_packet departure{};
        departure.len = sizeof(departure);
        departure.type = 0x05;
        departure.sentid = identity.localSessionId;
        departure.rcvdid = identity.remoteSessionId;
        const QByteArray packet = encodePacket(departure);
        socket.writeDatagram(packet, radioIP, identity.remotePort);
        socket.writeDatagram(packet, radioIP, identity.remotePort);
        socket.flush();
        qInfo(logUdp()).noquote() << "Sent retained predecessor departure for" << role << "transport";
    };

    sendDeparture(predecessor.control, "control");
    sendDeparture(predecessor.civ, "CI-V");
    sendDeparture(predecessor.audio, "audio");
}

void UdpHandler::setCurrentRadio(quint8 radio)
{
    if (radio >= radios.size())
    {
        qWarning(logUdp()).noquote() << "Ignoring invalid radio selection" << radio << "available radios"
                                     << radios.size();
        emit haveNetworkError(
            errorType(false, radioIP.toString(), "Invalid radio selection.", ErrorCode::InvalidRadio));
        return;
    }

    closeStreams();

    qInfo(logUdp()).noquote().nospace() << "Selected radio index=" << radio;
    qInfo(logUdp()).noquote() << "Reserving local CI-V/audio UDP ports";

    // Reserve fresh local CI-V/audio ports for every stream request. Reusing a
    // remembered port after its reservation socket was released races another
    // process and can turn a successful login into a silent stream-bind failure.
    if (!reserveStreamPorts())
    {
        return;
    }
    // The radio advertises a nominal CI-V baud rate in its discovery record,
    // but LAN CI-V transport is not paced from that serial-port value. The
    // command path uses LAN-specific pacing and derives correlation recovery
    // deadlines from measured RTT and jitter, so no serial-rate state is
    // propagated into Commander.
    if (radios[radio].commoncap == 0x8010)
    {
        memcpy(&macaddress, radios[radio].macaddress, sizeof(macaddress));
        useGuid = false;
    }
    else
    {
        useGuid = true;
        memcpy(&guid, radios[radio].guid, GUIDLEN);
    }

    devName = boundedLatin1(radios[radio].name, sizeof(radios[radio].name));
    audioType = boundedLatin1(radios[radio].audio, sizeof(radios[radio].audio));
    civId = radios[radio].civ;
    rxSampleRates = radios[radio].rxsample;
    txSampleRates = radios[radio].txsample;
    if (txSampleRates < 2)
    {
        txSetup.sampleRate = 0;
        txSetup.codec = 0;
    }

    // Stream negotiation requires both a current authentication response and
    // freshly reserved local CI-V/audio ports. Normal discovery can select the
    // radio before authentication completes, while retained-session recovery
    // authenticates again after the radio has already been selected.
    if (gotAuthOK)
    {
        sendRequestStream();
    }
}

void UdpHandler::sendRequestStream()
{
    conninfo_packet p{};
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.payloadsize = qToBigEndian((quint32)(sizeof(p) - 0x10));
    p.requesttype = 0x03;
    p.requestreply = 0x01;

    if (!useGuid)
    {
        p.commoncap = 0x8010;
        memcpy(p.macaddress, macaddress, sizeof(p.macaddress));
    }
    else
    {
        memcpy(p.guid, guid, sizeof(p.guid));
    }
    const quint16 requestSequence = authSeq++;
    p.innerseq = qToBigEndian(requestSequence);
    p.tokrequest = tokRequest;
    p.token = token;
    copyPacketField(p.name, devName);
    p.rxenable = 1;
    if (this->txSampleRates > 1 && txSetup.sampleRate > 0 && txSetup.codec != 0)
    {
        p.txenable = 1;
        p.txcodec = txSetup.codec;
        p.txsample = qToBigEndian((quint32)txSetup.sampleRate);
        p.txbuffer = qToBigEndian((quint32)txSetup.latency);
    }
    p.rxcodec = rxSetup.codec;
    copyPacketField(p.username, usernameEncoded);
    p.rxsample = qToBigEndian((quint32)rxSetup.sampleRate);
    p.civport = qToBigEndian((quint32)civLocalPort);
    p.audioport = qToBigEndian((quint32)audioLocalPort);
    p.convert = 1;
    m_streamRequest.begin(requestSequence, tokRequest, token);
    m_currentStreamGrantObserved = false;
    QByteArray request = encodePacket(p);
    qInfo(logUdp()).noquote().nospace() << "Requesting stream: rxCodec=" << quint8(p.rxcodec)
                                        << " rxSampleRate=" << qFromBigEndian(p.rxsample)
                                        << " txEnabled=" << quint8(p.txenable) << " txCodec=" << quint8(p.txcodec)
                                        << " txSampleRate=" << qFromBigEndian(p.txsample)
                                        << " civLocalPort=" << civLocalPort << " audioLocalPort=" << audioLocalPort;
    sendTrackedPacket(std::move(request));
    return;
}

void UdpHandler::sendAreYouThere()
{
    // The IC-9700 LAN server can take longer than 10 seconds to answer after a
    // stale stream or abrupt client shutdown. Keep probing with the same
    // process-stable client name instead of forcing a rapid reconnect loop that
    // makes the radio look "unreachable" while it is recovering.
    if (areYouThereCounter == kAreYouThereMaxAttempts)
    {
        qInfo(logUdp()).noquote() << "Control discovery exhausted without a radio response";
        status.message = "Radio not responding!";
        emit haveNetworkError(
            errorType(true, radioIP.toString(), "Radio not responding; reconnecting.", ErrorCode::ConnectionFailed));
        areYouThereTimer->stop();
        return;
    }
    qInfo(logUdp()).noquote() << "Sending control discovery probe attempt" << (areYouThereCounter + 1) << "of"
                              << kAreYouThereMaxAttempts;

    areYouThereCounter++;
    UdpBase::sendControl(false, 0x03, 0x00);
}

void UdpHandler::sendLogin()
{

    qInfo(logUdp()).noquote() << "Sending login request";

    tokRequest = static_cast<quint16>(QRandomGenerator::global()->generate());

    login_packet p{};
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.payloadsize = qToBigEndian((quint32)(sizeof(p) - 0x10));
    p.requesttype = 0x00;
    p.requestreply = 0x01;

    const quint16 requestSequence = authSeq++;
    p.innerseq = qToBigEndian(requestSequence);
    p.tokrequest = tokRequest;
    copyPacketField(p.username, usernameEncoded);
    copyPacketField(p.password, passwordEncoded);
    copyPacketField(p.name, compName);

    m_loginRequest.begin(requestSequence, tokRequest, 0);

    sendTrackedPacket(encodePacket(p));
    return;
}

QByteArray UdpHandler::createTokenPacket(uint8_t magic)
{
    token_packet p{};
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.payloadsize = qToBigEndian((quint32)(sizeof(p) - 0x10));
    p.requesttype = magic;
    p.requestreply = 0x01;
    const quint16 requestSequence = authSeq++;
    p.innerseq = qToBigEndian(requestSequence);
    p.tokrequest = tokRequest;
    if (magic != 0x01)
    {
        p.resetcap = qToBigEndian((quint16)0x0798);
    }
    p.token = token;

    if (magic == 0x05)
    {
        m_tokenRenewalRequest.begin(requestSequence, tokRequest, token);
    }
    else if (magic == 0x01)
    {
        m_tokenRemovalRequest.begin(requestSequence, tokRequest, token);
    }

    return encodePacket(p);
}

void UdpHandler::sendToken(uint8_t magic)
{
    qDebug(logUdp()).noquote().nospace() << "UdpHandler::AuthenticationRequest type=0x" << Qt::hex << magic;
    sendTrackedPacket(createTokenPacket(magic));
}
