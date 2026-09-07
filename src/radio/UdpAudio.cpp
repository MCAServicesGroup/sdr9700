#include "UdpAudio.h"
#include "LogCategories.h"
#include <algorithm>
#include <cstring>

namespace
{
constexpr int kAudioThreadShutdownWaitMs = 500;
constexpr int kTxAudioFrameMs = 20;
constexpr int kMaxQueuedTxAudioFrames = 8;
constexpr quint8 kLpcmMono16Codec = 0x04;
} // namespace

UdpAudio::UdpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup,
                   audioSetup txSetup, QUdpSocket* boundSocket)
{
    qInfo(logUdp()).noquote() << "Starting UdpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;
    this->rxSetup = rxSetup;
    this->txSetup = txSetup;
    if (this->txSetup.sampleRate > 0 && this->txSetup.codec == kLpcmMono16Codec)
    {
        m_txSilencePacketBytes = int(this->txSetup.sampleRate / 50) * int(sizeof(qint16));
    }
    // Keep a reusable silence frame for TX under-runs. The byte content and
    // frame size match the previous per-tick QByteArray allocation exactly.
    m_txSilenceFrame = QByteArray(m_txSilencePacketBytes, '\0');

    if (txSetup.sampleRate == 0)
    {
        enableTx = false;
    }

    if (!init(lport, boundSocket))
    {
        return;
    }

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &UdpAudio::dataReceived);

    // Audio workers are not started here; enableAudio() is called once the
    // radio reaches the ready (synced) state so playback begins with known
    // frequency and mode. areYouThereTimer (below) keeps the audio stream
    // alive during the sync window.


    areYouThereTimer = new QTimer(this);
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, false, 0x03, 0));
    areYouThereTimer->start(AREYOUTHERE_PERIOD);

    m_dtmfTimer = new QTimer(this);
    m_dtmfTimer->setInterval(kTxAudioFrameMs);
    m_dtmfTimer->setSingleShot(false);
    connect(m_dtmfTimer, &QTimer::timeout, this, &UdpAudio::sendNextDtmfFrame);

    txAudioTimer = new QTimer(this);
    txAudioTimer->setInterval(kTxAudioFrameMs);
    txAudioTimer->setTimerType(Qt::PreciseTimer);
    txAudioTimer->setSingleShot(false);
    connect(txAudioTimer, &QTimer::timeout, this, &UdpAudio::sendNextTxAudioFrame);
}

UdpAudio::~UdpAudio()
{
    qDebug(logUdp()).noquote() << "[SHUTDOWN] ~UdpAudio enter";
    stopAudioWorker(rxaudio, rxAudioThread, "rxAudioThread");
    stopAudioWorker(txaudio, txAudioThread, "txAudioThread");
    qDebug(logUdp()).noquote() << "[SHUTDOWN] ~UdpAudio complete";
}

void UdpAudio::stopAudioWorker(AudioHandlerBase*& handler, QThread*& workerThread, const char* name)
{
    if (handler)
    {
        qDebug(logUdp()).noquote() << "[SHUTDOWN]" << name << "handler dispose";
        handler->dispose();
        if (!workerThread)
        {
            handler->deleteLater();
        }
        handler = nullptr;
    }

    if (workerThread == nullptr)
    {
        return;
    }

    qDebug(logUdp()).noquote() << "[SHUTDOWN]" << name << "quit";
    workerThread->quit();
    if (!workerThread->wait(kAudioThreadShutdownWaitMs))
    {
        qWarning(logUdp()).noquote() << "[SHUTDOWN]" << name << "did not stop within" << kAudioThreadShutdownWaitMs
                                     << "ms; requesting interruption";
        workerThread->requestInterruption();
        workerThread->quit();
        if (!workerThread->wait(kAudioThreadShutdownWaitMs))
        {
            qCritical(logUdp()).noquote()
                << "[SHUTDOWN]" << name << "did not stop after bounded shutdown; leaving thread detached";
            workerThread->setParent(nullptr);
            connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater, Qt::DirectConnection);
            workerThread = nullptr;
            return;
        }
    }
    delete workerThread;
    workerThread = nullptr;
    qDebug(logUdp()).noquote() << "[SHUTDOWN]" << name << "done";
}

void UdpAudio::sendAudioBuffer(const QByteArray& data)
{
    int len = 0;
    while (len < data.length())
    {
        const int chunkLen = qMin(1364, data.length() - len);
        const char* chunk = data.constData() + len;
        len += chunkLen;
        audio_packet p{};
        p.len = (quint32)sizeof(p) + chunkLen;
        p.sentid = myId;
        p.rcvdid = remoteId;
        // The IC-9700 expects normal TX audio fragments to use 0x0080.
        // Marking intermediate fragments as 0x0081 can produce short
        // decode artifacts at the start of transmit.
        p.ident = 0x0080;
        p.datalen = (quint16)qToBigEndian((quint16)chunkLen);
        p.sendseq = (quint16)qToBigEndian((quint16)sendAudioSeq);
        QByteArray tx = encodePacket(p);
        // Append directly from the source frame so TX packetization performs
        // one payload copy instead of building an intermediate QByteArray.
        tx.append(chunk, chunkLen);
        sendTrackedPacket(tx);
        sendAudioSeq++;
    }
}

void UdpAudio::receiveAudioData(audioPacket audio)
{
    if (txaudio == nullptr)
    {
        qDebug(logUdp()).noquote() << "TX: receiveAudioData called but txaudio is null";
        return;
    }
    if (audio.data.length() > 0)
    {
        m_txSilencePacketBytes = audio.data.length();
        if (m_txSilenceFrame.size() != m_txSilencePacketBytes)
        {
            m_txSilenceFrame = QByteArray(m_txSilencePacketBytes, '\0');
        }

        // DTMF timer owns the audio path; mic frames are suppressed entirely.
        if (m_dtmfTimerActive)
        {
            return;
        }

        if (!m_txActive.load())
        {
            m_txAudioQueue.clear();
            return;
        }

        m_txAudioQueue.enqueue(audio.data);
        while (m_txAudioQueue.size() > kMaxQueuedTxAudioFrames)
        {
            m_txAudioQueue.dequeue();
        }
    }
}

void UdpAudio::queueDtmfPcm(const QByteArray& pcm)
{
    m_txAudioQueue.clear();
    m_dtmfPcm = pcm;
    m_dtmfPcmOffset = 0;
    if (!pcm.isEmpty())
    {
        m_dtmfTimerActive = true;
        m_dtmfTimer->start();
    }
}

void UdpAudio::sendNextTxAudioFrame()
{
    if (!m_txActive.load())
    {
        if (txAudioTimer)
        {
            txAudioTimer->stop();
        }
        m_txAudioQueue.clear();
        return;
    }

    if (m_dtmfTimerActive)
    {
        return;
    }

    QByteArray frame;
    if (!m_txAudioQueue.isEmpty())
    {
        frame = m_txAudioQueue.dequeue();
    }
    else
    {
        if (m_txSilenceFrame.size() != m_txSilencePacketBytes)
        {
            m_txSilenceFrame = QByteArray(m_txSilencePacketBytes, '\0');
        }
        frame = m_txSilenceFrame;
    }
    sendAudioBuffer(frame);
}

void UdpAudio::sendNextDtmfFrame()
{
    if (!m_txActive.load())
    {
        m_dtmfTimerActive = false;
        m_dtmfTimer->stop();
        return;
    }

    if (m_dtmfPcmOffset >= m_dtmfPcm.size())
    {
        m_dtmfTimerActive = false;
        m_dtmfTimer->stop();
        return;
    }

    const qsizetype take = qMin(qsizetype(m_txSilencePacketBytes), m_dtmfPcm.size() - m_dtmfPcmOffset);
    // DTMF is not the normal voice path, but it still runs on the same 20 ms
    // transmit cadence. Reuse this scratch frame so repeated tone chunks do not
    // create allocator noise while the radio is keyed.
    if (m_dtmfFrame.size() != m_txSilencePacketBytes)
    {
        m_dtmfFrame.resize(m_txSilencePacketBytes);
    }
    std::memset(m_dtmfFrame.data(), 0, size_t(m_dtmfFrame.size()));
    memcpy(m_dtmfFrame.data(), m_dtmfPcm.constData() + m_dtmfPcmOffset, size_t(take));
    m_dtmfPcmOffset += take;
    sendAudioBuffer(m_dtmfFrame);

    if (m_dtmfPcmOffset >= m_dtmfPcm.size())
    {
        m_dtmfTimerActive = false;
        m_dtmfTimer->stop();
    }
}

void UdpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void UdpAudio::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void UdpAudio::getRxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                           bool over)
{

    emit haveRxLevels(amplitude, amplitudeRMS, latency, current, under, over);
}

void UdpAudio::getTxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                           bool over)
{
    emit haveTxLevels(amplitude, amplitudeRMS, latency, current, under, over);
}

void UdpAudio::dataReceived()
{

    while (udp->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        if (!acceptDatagramFrom(datagram))
        {
            continue;
        }
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16):
        {
            const auto decodedControl = decodePacket<control_packet>(r);
            const control_packet* control = &*decodedControl;
            if (control->type == 0x06 && !m_transportReadyEmitted)
            {
                m_transportReadyEmitted = true;
                emit ready();
            }
            break;
        }
        default:
        {
            if (r.length() < AUDIO_SIZE)
            {
                break;
            }

            const auto decodedControl = decodePacket<control_packet>(r);
            const control_packet* in = &*decodedControl;

            if (in->type != 0x01 && in->len >= 0x20)
            {
                const quint32 packetLength = in->len;
                if (packetLength != quint32(r.length()))
                {
                    qWarning(logUdp()).noquote() << "Dropping audio datagram with mismatched length: header"
                                                 << packetLength << "actual" << r.length();
                    break;
                }

                const auto decodedAudio = decodePacket<audio_packet>(r);
                const audio_packet* audioIn = &*decodedAudio;
                const int payloadLength = r.length() - AUDIO_SIZE;
                const quint16 declaredPayloadLength = qFromBigEndian(audioIn->datalen);
                if (declaredPayloadLength != payloadLength)
                {
                    qWarning(logUdp()).noquote() << "Dropping audio datagram with mismatched payload length: header"
                                                 << declaredPayloadLength << "actual" << payloadLength;
                    break;
                }

                if (in->seq == 0)
                {
                    seqPrefix++;
                }

                // Log the first few RX audio packet headers per stream instance
                // so reconnects get fresh diagnostics.
                if (++rxPacketDiagnosticsCount <= 3)
                {
                    qDebug(logUdp()).noquote().nospace()
                        << "RX audio header type=" << Qt::hex << in->type << " len=" << in->len << " seq=" << in->seq
                        << " dataLen=" << (r.length() - 0x18)
                        << " header=" << QString::fromLatin1(r.left(0x18).toHex(' '));
                }

                if (rxAudioThread == nullptr && m_audioReady)
                {
                    startAudio();
                }

                const int excess = pingLatenessMs - (pingBaselineMs + rxSetup.latency);

                if (excess > 0)
                {
                    qDebug(logUdp()).noquote()
                        << "Audio latency high:"
                        << "lateness" << pingLatenessMs << "baseline" << pingBaselineMs << "excess" << excess;

                    if (latencyCounter < 6 && ++latencyCounter == 6)
                    {
                        // Continue delivering audio during a latency episode.
                        // The former pseudo-flush dropped every sixth packet
                        // without resetting any buffer, producing periodic
                        // audible gaps while leaving the latency condition
                        // unchanged. Log once per episode and let the output
                        // handler's bounded buffer perform normal recovery.
                        qInfo(logUdp()).noquote() << "Audio latency remains elevated; continuing bounded delivery";
                    }
                }
                else
                {
                    latencyCounter = 0;
                }
                audioPacket tempAudio;
                tempAudio.seq = (quint32(seqPrefix) << 16) | quint32(in->seq);
                tempAudio.createdAtMs = audioMonotonicTimestampMs();
                tempAudio.sent = 0;
                tempAudio.data = r.mid(0x18);

                emit haveAudioData(tempAudio);
                markPacketReceived();
            }
            break;
        }
        }

        UdpBase::dataReceived(r);
        r.clear();
        datagram.clear();
    }
}

void UdpAudio::enableAudio()
{
    m_audioReady = true;
    if (rxAudioThread == nullptr)
    {
        startAudio();
    }
    if (m_txActive.load())
    {
        startTxAudio();
    }
}

void UdpAudio::setRxAudioDevice(const QAudioDevice& device)
{
    if (rxSetup.port == device)
    {
        return;
    }

    rxSetup.port = device;
    stopAudioWorker(rxaudio, rxAudioThread, "rxAudioThread");
    if (m_audioReady)
    {
        startAudio();
    }
}

void UdpAudio::setTxAudioDevice(const QAudioDevice& device)
{
    if (txSetup.port == device)
    {
        return;
    }

    txSetup.port = device;
    const bool restart = m_txActive.load();
    stopTxAudio();
    if (restart && m_audioReady)
    {
        startTxAudio();
    }
}

void UdpAudio::stopLocalAudio()
{
    m_audioReady = false;
    stopAudioWorker(rxaudio, rxAudioThread, "rxAudioThread");
    stopAudioWorker(txaudio, txAudioThread, "txAudioThread");
}

void UdpAudio::startAudio()
{
    if (rxSetup.port.isNull())
    {
        return;
    }

    if (rxSetup.type == qtAudio)
    {
        rxaudio = new AudioHandlerQtOutput();
    }
    else
    {
        qCritical(logAudio()).noquote() << "Unsupported Receive Audio Handler selected! Only qtAudio is supported.";
        return;
    }

    rxAudioThread = new QThread(this);
    rxAudioThread->setObjectName("rxAudio()");

    rxaudio->moveToThread(rxAudioThread);

    rxAudioThread->start(QThread::HighPriority);

    connect(this, &UdpAudio::setupRxAudio, rxaudio, &AudioHandlerBase::init);

    connect(this, &UdpAudio::haveAudioData, rxaudio, &AudioHandlerBase::incomingAudio);
    connect(this, &UdpAudio::haveChangeLatency, rxaudio, &AudioHandlerBase::changeLatency);
    connect(this, &UdpAudio::haveSetVolume, rxaudio, &AudioHandlerBase::setVolume);
    connect(rxaudio, &AudioHandlerBase::haveLevels, this, &UdpAudio::getRxLevels);
    connect(rxAudioThread, &QThread::finished, rxaudio, &QObject::deleteLater);
    connect(rxaudio, &AudioHandlerBase::initFailed, this, &UdpAudio::onRxAudioInitFailed);

    sendControl(false, 0x03, 0x00);

    if (pingTimer != nullptr)
    {
        pingTimer->stop();
        pingTimer->deleteLater();
    }
    pingTimer = new QTimer(this);
    connect(pingTimer, &QTimer::timeout, this, &UdpBase::sendPing);
    pingTimer->start(PING_PERIOD);

    emit setupRxAudio(rxSetup);
}

void UdpAudio::startTxAudio()
{
    if (!enableTx || txAudioThread != nullptr || txSetup.port.isNull())
    {
        return;
    }

    if (txSetup.type != qtAudio)
    {
        qCritical(logAudio()).noquote() << "Unsupported Transmit Audio Handler selected! Only qtAudio is supported.";
        return;
    }

    // Opening an idle microphone at connection time needlessly holds the
    // capture device and makes rapid application shutdown race CoreAudio
    // initialization on macOS. Create it only when transmission first needs
    // local audio, then retain it for the connection.
    txaudio = new AudioHandlerQtInput();
    txAudioThread = new QThread(this);
    txAudioThread->setObjectName("txAudio()");
    txaudio->moveToThread(txAudioThread);
    txAudioThread->start(QThread::HighPriority);

    connect(this, &UdpAudio::setupTxAudio, txaudio, &AudioHandlerBase::init);
    connect(txaudio, &AudioHandlerBase::haveAudioData, this, &UdpAudio::receiveAudioData);
    connect(txaudio, &AudioHandlerBase::haveLevels, this, &UdpAudio::getTxLevels);
    connect(txAudioThread, &QThread::finished, txaudio, &QObject::deleteLater);
    connect(txaudio, &AudioHandlerBase::initFailed, this, &UdpAudio::onTxAudioInitFailed);

    emit setupTxAudio(txSetup);
}

void UdpAudio::stopTxAudio()
{
    stopAudioWorker(txaudio, txAudioThread, "txAudioThread");
}

void UdpAudio::setTxActive(bool active)
{
    if (active && m_audioReady)
    {
        startTxAudio();
    }
    m_txActive.store(active);
    m_txAudioQueue.clear();
    if (!active)
    {
        if (txAudioTimer)
        {
            txAudioTimer->stop();
        }
        m_dtmfTimer->stop();
        m_dtmfTimerActive = false;
        m_dtmfPcm.clear();
        m_dtmfFrame.clear();
        m_dtmfPcmOffset = 0;
        // QAudioSource teardown has crashed inside CoreAudio when deferred
        // until the rest of the application is shutting down on macOS. The
        // input is only needed while keyed, so release it promptly after the
        // radio leaves transmit and recreate it for the next transmission.
        stopTxAudio();
    }
    else if (txAudioTimer && !txAudioTimer->isActive())
    {
        txAudioTimer->start();
        sendNextTxAudioFrame();
    }
    qDebug(logUdp()).noquote() << "UdpAudio: TX audio" << (active ? "ENABLED (PTT on)" : "DISABLED (PTT off)");
}

void UdpAudio::onRxAudioInitFailed()
{
    qWarning(logAudio()).noquote() << "RX Audio Initialization failed. Cleaning up.";
    if (rxAudioThread)
    {
        rxAudioThread->quit();
        // Detach from parent and wire self-delete so the thread cleans up
        // asynchronously. Null the member so dataReceived() can call startAudio()
        // again if a new stream arrives.
        rxAudioThread->setParent(nullptr);
        connect(rxAudioThread, &QThread::finished, rxAudioThread, &QObject::deleteLater, Qt::DirectConnection);
        rxAudioThread = nullptr;
    }
    rxaudio = nullptr;
}

void UdpAudio::onTxAudioInitFailed()
{
    qWarning(logAudio()).noquote() << "TX Audio Initialization failed. Cleaning up.";
    if (txAudioThread)
    {
        txAudioThread->quit();
        txAudioThread->setParent(nullptr);
        connect(txAudioThread, &QThread::finished, txAudioThread, &QObject::deleteLater, Qt::DirectConnection);
        txAudioThread = nullptr;
    }
    txaudio = nullptr;
}
