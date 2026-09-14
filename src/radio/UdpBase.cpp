#include "UdpBase.h"
#include "LogCategories.h"

#include <QMutexLocker>
#include <QRandomGenerator>
#include <QTime>

#include <utility>

bool UdpBase::init(quint16 bindPort, QUdpSocket* boundSocket)
{
    departureSent = false;
    udp = boundSocket != nullptr ? boundSocket : new QUdpSocket(this);
    if (boundSocket != nullptr)
    {
        udp->setParent(this);
    }
    if (boundSocket == nullptr && !udp->bind(bindPort))
    {
        qCritical(logUdp()).noquote() << "Unable to bind UDP port" << bindPort << ":" << udp->errorString();
        delete udp;
        udp = nullptr;
        return false;
    }
    if (udp->state() != QAbstractSocket::BoundState || (bindPort != 0 && udp->localPort() != bindPort))
    {
        qCritical(logUdp()).noquote() << "Reserved UDP socket is not bound to requested port" << bindPort;
        delete udp;
        udp = nullptr;
        return false;
    }

    localPort = udp->localPort();
    qInfo(logUdp()).noquote().nospace() << "UDP stream bound localPort=" << localPort << " remotePort=" << port;
    // RS-BA1 session IDs are opaque per-stream tokens. Keep the bound port in
    // the low half and randomize the high half so a replacement process cannot
    // be mistaken for a retained predecessor transport from the same host.
    const quint32 randomHigh = QRandomGenerator::global()->generate() & 0xffff;
    myId = (randomHigh << 16) | localPort;

    retransmitTimer = new QTimer(this);
    connect(retransmitTimer, &QTimer::timeout, this, &UdpBase::sendRetransmitRequest);
    retransmitTimer->start(RETRANSMIT_PERIOD);
    mono.start();
    markPacketReceived();
    return true;
}

bool UdpBase::isSocketBound() const
{
    return udp != nullptr && udp->state() == QAbstractSocket::BoundState;
}

bool UdpBase::acceptDatagramFrom(const QNetworkDatagram& datagram) const
{
    const bool addressMatches = datagram.senderAddress().isEqual(radioIP, QHostAddress::ConvertV4MappedToIPv4);
    const bool portMatches = datagram.senderPort() == port;
    if (!addressMatches || !portMatches)
    {
        qWarning(logUdp()).noquote() << "Ignoring UDP datagram from unexpected endpoint"
                                     << datagram.senderAddress().toString() << datagram.senderPort() << "expected"
                                     << radioIP.toString() << port;
        return false;
    }
    return true;
}

UdpBase::~UdpBase()
{
    qInfo(logUdp()).noquote().nospace() << "Closing UDP stream: " << radioIP.toString() << ':' << port;
    if (udp != nullptr)
    {
        sendDeparture();
        udp->close();
        delete udp;
    }
}

void UdpBase::sendDeparture()
{
    if (udp == nullptr || departureSent || !departureAllowed)
    {
        return;
    }

    departureSent = true;
    qInfo(logUdp()).noquote().nospace() << "Sending UDP stream departure to " << radioIP.toString() << ':' << port;
    sendControl(false, 0x05, 0x00);
}

void UdpBase::dataReceived(const QByteArray& r)
{
    if (r.length() < 0x10)
    {
        return;
    }

    const qint64 receivedAtMs = elapsedMs();

    switch (r.length())
    {
    case (CONTROL_SIZE):
    {
        const control_packet in = *decodePacket<control_packet>(r);
        if (in.type == 0x01 && in.len == 0x10)
        {
            packetsLost++;
            {
                QByteArray retransmitData;
                QMutexLocker txLocker(&txBufferMutex);
                auto match = txSeqBuf.find(in.seq);
                if (match != txSeqBuf.end())
                {
                    // Copy under the buffer lock, then send after releasing it
                    // to avoid nesting txBufferMutex and udpMutex.
                    qDebug(logUdp()).noquote()
                        << this->metaObject()->className() << ": Sending (single packet) retransmit of "
                        << QString("0x%1").arg(match->seqNum, 0, 16);
                    match->retransmitCount++;
                    retransmitData = match->data;
                }
                else
                {
                    const QString availableRange = txSeqBuf.isEmpty() ? QStringLiteral("empty retransmit buffer")
                                                                      : QStringLiteral("have 0x%1 to 0x%2")
                                                                            .arg(txSeqBuf.firstKey(), 0, 16)
                                                                            .arg(txSeqBuf.lastKey(), 0, 16);
                    qDebug(logUdp()).noquote() << this->metaObject()->className() << ": Remote requested packet"
                                               << QString("0x%1").arg(in.seq, 0, 16) << "not found," << availableRange;
                }
                txLocker.unlock();
                if (!retransmitData.isEmpty())
                {
                    QMutexLocker udpLocker(&udpMutex);
                    udp->writeDatagram(retransmitData, radioIP, port);
                }
            }
        }
        if (in.type == 0x04)
        {
            qInfo(logUdp()).noquote().nospace() << this->metaObject()->className() << "::PresenceReply";
            areYouThereCounter = 0;
            // IC-9700 sends "I am here" during discovery in response to this
            // client's "Are you there" probe.
            remoteId = in.sentid;
            if (areYouThereTimer != nullptr && areYouThereTimer->isActive())
            {
                areYouThereTimer->stop();
            }
            sendControl(false, 0x06, 0x01);
        }
        else if (in.type == 0x06)
        {
            // Sequence bookkeeping happens in the shared receive path below.
        }
        break;
    }
    case (PING_SIZE):
    {
        const ping_packet in = *decodePacket<ping_packet>(r);
        if (in.type == 0x07)
        {
            if (in.reply == 0x00)
            {

                static constexpr int kDayMs = 24 * 60 * 60 * 1000;

                auto normDay = [](int ms)
                {
                    ms %= kDayMs;
                    if (ms < 0)
                    {
                        ms += kDayMs;
                    }
                    return ms;
                };
                auto signedDeltaDay = [&](int a, int b)
                {
                    int d = normDay(a) - normDay(b);
                    if (d > kDayMs / 2)
                    {
                        d -= kDayMs;
                    }
                    if (d < -kDayMs / 2)
                    {
                        d += kDayMs;
                    }
                    return d;
                };

                const qint64 localNow = mono.elapsed();     // monotonic ms
                const int radioNow = normDay(int(in.time)); // ms since startup, wrapped daily

                // Maintain a prediction of radioNow from monotonic time for this
                // UDP stream. These values must be per-instance because sdr9700
                // can have separate control, CIV, and audio UDP streams alive.
                if (!pingHaveSync)
                {
                    pingHaveSync = true;
                    pingRadioBase = radioNow;
                    pingLocalBase = localNow;
                }

                // Project the last synchronized radio timestamp forward by
                // monotonic local elapsed time. Comparing the received ping
                // against this projection measures lateness without mixing in
                // wall-clock adjustments or time-zone changes.
                const int predictedRadioNow = normDay(pingRadioBase + int(localNow - pingLocalBase));

                // Positive means the ping timestamp is behind the local prediction.
                // Negative means it appears ahead (usually reorder/clock wobble).
                pingLatenessMs = signedDeltaDay(predictedRadioNow, radioNow);

                constexpr int kMaxBaselineClampMs = 2000;

                // Update baseline slowly so spikes do not become the expected latency.
                if (!pingBaselineValid)
                {
                    pingBaselineMs = pingLatenessMs;
                    pingBaselineValid = true;
                }
                else
                {
                    int dev = pingLatenessMs - pingBaselineMs;
                    if (std::abs(dev) < 200)
                    {
                        pingBaselineMs = (pingBaselineMs * 31 + pingLatenessMs) / 32;
                        pingBaselineMs = qBound(0, pingBaselineMs, kMaxBaselineClampMs);
                    }
                }

                ping_packet p{};
                p.len = sizeof(p);
                p.type = 0x07;
                p.sentid = myId;
                p.rcvdid = remoteId;
                p.reply = 0x01;
                p.seq = in.seq;
                p.time = in.time;
                udpMutex.lock();
                udp->writeDatagram(encodePacket(p), radioIP, port);
                udpMutex.unlock();
            }
            else if (in.reply == 0x01)
            {
                if (in.seq == pingSendSeq)
                {
                    pingSendSeq++;
                }
            }
            else
            {
                qInfo(logUdp()).noquote()
                    << this->metaObject()->className() << "Unhandled ping response. byte 0x10=" << r[16];
            }
        }
        break;
    }
    default:
    {
        // Continue into shared sequence tracking below.
    }
    break;
    }

    const control_packet in = *decodePacket<control_packet>(r);

    if (in.type == 0x01 && in.len != 0x10)
    {
        {
            QList<QByteArray> retransmitData;
            QList<quint16> unavailableSequences;
            QMutexLocker txLocker(&txBufferMutex);
            for (qsizetype i = 0x10; i + 1 < r.length(); i = i + 2)
            {
                quint16 seq = (quint8)r[i] | (quint8)r[i + 1] << 8;
                auto match = txSeqBuf.find(seq);
                if (match == txSeqBuf.end())
                {
                    const QString availableRange = txSeqBuf.isEmpty() ? QStringLiteral("empty retransmit buffer")
                                                                      : QStringLiteral("have 0x%1 to 0x%2")
                                                                            .arg(txSeqBuf.firstKey(), 0, 16)
                                                                            .arg(txSeqBuf.lastKey(), 0, 16);
                    qDebug(logUdp()).noquote() << this->metaObject()->className() << ": Remote requested packet"
                                               << QString("0x%1").arg(seq, 0, 16) << "not found," << availableRange;
                    unavailableSequences.append(seq);
                }
                else
                {
                    qDebug(logUdp()).noquote()
                        << this->metaObject()->className() << ": Sending (multiple packet) retransmit of "
                        << QString("0x%1").arg(match->seqNum, 0, 16);
                    match->retransmitCount++;
                    retransmitData.append(match->data);
                    packetsLost++;
                }
            }
            txLocker.unlock();

            if (!retransmitData.isEmpty())
            {
                QMutexLocker udpLocker(&udpMutex);
                for (const QByteArray& packet : std::as_const(retransmitData))
                {
                    udp->writeDatagram(packet, radioIP, port);
                }
            }
            for (quint16 sequence : std::as_const(unavailableSequences))
            {
                sendControl(false, 0, sequence);
            }
        }
        if ((r.length() - 0x10) % 2 != 0)
        {
            qWarning(logUdp()).noquote() << this->metaObject()->className()
                                         << ": Ignoring malformed odd-length retransmit request:" << r.length();
        }
    }
    else if (in.len != PING_SIZE && in.type == 0x00 && in.seq != 0x00)
    {
        rxBufferMutex.lock();
        if (rxSeqBuf.isEmpty())
        {
            rxSeqBuf.insert(in.seq, receivedAtMs);
        }
        else
        {
            if (in.seq < rxSeqBuf.firstKey() ||
                static_cast<qint16>(in.seq - rxSeqBuf.lastKey()) > static_cast<qint16>(MAX_MISSING))
            {
                qDebug(logUdp()).noquote()
                    << this->metaObject()->className() << "Large seq number gap detected, previous highest: "
                    << QString("0x%1").arg(rxSeqBuf.lastKey(), 0, 16)
                    << " current: " << QString("0x%1").arg(in.seq, 0, 16);
                rxSeqBuf.clear();
                rxSeqBuf.insert(in.seq, receivedAtMs);
                rxBufferMutex.unlock();
                missingMutex.lock();
                rxMissing.clear();
                missingMutex.unlock();
                return;
            }

            if (!rxSeqBuf.contains(in.seq))
            {
                if (in.seq > rxSeqBuf.lastKey() + 1)
                {
                    qDebug(logUdp()).noquote().nospace()
                        << this->metaObject()->className()
                        << "::MissingPacketsDetected previous=" << QString("0x%1").arg(rxSeqBuf.lastKey(), 0, 16)
                        << " current=" << QString("0x%1").arg(in.seq, 0, 16);
                    missingMutex.lock();
                    // Iterate in a wider type. A quint16 loop variable wraps to
                    // zero after sequence 65535 and would otherwise never leave
                    // a loop whose received sequence is 65535.
                    const quint32 firstMissing = quint32(rxSeqBuf.lastKey()) + 1U;
                    const quint32 receivedSequence = in.seq;
                    for (quint32 f = firstMissing; f < receivedSequence; ++f)
                    {
                        if (rxSeqBuf.size() > BUFSIZE)
                        {
                            rxSeqBuf.erase(rxSeqBuf.begin());
                        }
                        const auto sequence = static_cast<quint16>(f);
                        rxSeqBuf.insert(sequence, receivedAtMs);
                        if (!rxMissing.contains(sequence))
                        {
                            rxMissing.insert(sequence, 0);
                        }
                    }
                    if (rxSeqBuf.size() > BUFSIZE)
                    {
                        rxSeqBuf.erase(rxSeqBuf.begin());
                    }
                    rxSeqBuf.insert(in.seq, receivedAtMs);
                    missingMutex.unlock();
                }
                else
                {
                    if (rxSeqBuf.size() > BUFSIZE)
                    {
                        rxSeqBuf.erase(rxSeqBuf.begin());
                    }
                    rxSeqBuf.insert(in.seq, receivedAtMs);
                }
            }
            else
            {
                missingMutex.lock();
                auto s = rxMissing.find(in.seq);
                if (s != rxMissing.end())
                {
                    qDebug(logUdp()).noquote().nospace()
                        << this->metaObject()->className()
                        << "::MissingSequenceRecovered sequence=" << QString("0x%1").arg(in.seq, 0, 16);

                    s = rxMissing.erase(s);
                }
                missingMutex.unlock();
            }
        }
        rxBufferMutex.unlock();
    }
}

void UdpBase::sendRetransmitRequest()
{
    // Request gaps that remain missing at the retransmit timer interval.
    // Lock order: rxBufferMutex → missingMutex (must match dataReceived to prevent AB/BA deadlock).
    QMutexLocker rxLocker(&rxBufferMutex);
    QMutexLocker missingLocker(&missingMutex);
    if (rxMissing.isEmpty())
    {
        return;
    }
    else if (rxMissing.size() > MAX_MISSING)
    {
        qInfo(logUdp()).noquote() << "Too many missing packets," << rxMissing.size() << "flushing all buffers";
        rxMissing.clear();
        rxSeqBuf.clear();
        return;
    }
    rxLocker.unlock(); // rxSeqBuf not needed below; release early so dataReceived is not blocked

    QByteArray missingSeqs;
    auto appendRetransmitSeqRange = [](QByteArray& buffer, quint16 first, quint16 last)
    {
        const quint16 firstLe = qToLittleEndian(first);
        const quint16 lastLe = qToLittleEndian(last);
        buffer.append(reinterpret_cast<const char*>(&firstLe), sizeof(firstLe));
        buffer.append(reinterpret_cast<const char*>(&lastLe), sizeof(lastLe));
    };

    auto it = rxMissing.begin();
    while (it != rxMissing.end())
    {
        if (it.key() != 0x0)
        {
            if (it.value() < 4)
            {
                appendRetransmitSeqRange(missingSeqs, it.key(), it.key());
                it.value()++;
                it++;
            }
            else
            {
                qInfo(logUdp()).noquote() << this->metaObject()->className() << ": No response for missing packet"
                                          << QString("0x%1").arg(it.key(), 0, 16) << "deleting";
                it = rxMissing.erase(it);
            }
        }
        else
        {
            qInfo(logUdp()).noquote() << this->metaObject()->className() << ": found empty key in missing buffer";
            it++;
        }
    }
    missingLocker.unlock();

    if (missingSeqs.length() != 0)
    {
        control_packet p{};
        p.len = sizeof(p);
        p.type = 0x01;
        p.seq = 0x0000;
        p.sentid = myId;
        p.rcvdid = remoteId;
        if (missingSeqs.length() == 4)
        {
            p.seq = quint16(quint8(missingSeqs[0])) | (quint16(quint8(missingSeqs[1])) << 8);
            qInfo(logUdp()).noquote().nospace()
                << this->metaObject()->className()
                << " requesting missing packet sequence=" << QStringLiteral("0x%1").arg(p.seq, 0, 16);
            udpMutex.lock();
            udp->writeDatagram(encodePacket(p), radioIP, port);
            udpMutex.unlock();
        }
        else
        {
            qInfo(logUdp()).noquote().nospace() << this->metaObject()->className()
                                                << " requesting missing packets sequences=" << missingSeqs.toHex(':');
            p.len = (quint32)sizeof(p) + missingSeqs.size();
            missingSeqs.insert(0, p.packet, sizeof(p));

            udpMutex.lock();
            udp->writeDatagram(missingSeqs, radioIP, port);
            udpMutex.unlock();
        }
    }
}

void UdpBase::sendControl(bool tracked, quint8 type, quint16 seq)
{
    if (udp == nullptr)
    {
        return;
    }
    control_packet p{};
    p.len = sizeof(p);
    p.type = type;
    p.sentid = myId;
    p.rcvdid = remoteId;

    if (!tracked)
    {
        p.seq = seq;
        QMutexLocker locker(&udpMutex);
        udp->writeDatagram(encodePacket(p), radioIP, port);
    }
    else
    {
        sendTrackedPacket(encodePacket(p));
    }
    return;
}

void UdpBase::sendPing()
{
    if (udp == nullptr)
    {
        return;
    }
    ping_packet p{};
    p.len = sizeof(p);
    p.type = 0x07;
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.seq = pingSendSeq;
    QTime now = QTime::currentTime();
    p.time = (quint32)now.msecsSinceStartOfDay();
    lastPingSentMs = elapsedMs();
    udpMutex.lock();
    udp->writeDatagram(encodePacket(p), radioIP, port);
    udpMutex.unlock();
    return;
}

void UdpBase::sendTrackedPacket(QByteArray d, int copies)
{
    if (udp == nullptr)
    {
        return;
    }
    if (d.size() < CONTROL_SIZE)
    {
        qWarning(logUdp()).noquote() << this->metaObject()->className()
                                     << "Refusing to send short tracked UDP packet:" << d.size();
        return;
    }

    {
        // IC-9700 LAN can request retransmission, so a tracked packet must be
        // buffered before it is transmitted or its sequence is advanced.
        QMutexLocker locker(&txBufferMutex);
        d[6] = sendSeq & 0xff;
        d[7] = (sendSeq >> 8) & 0xff;
        SEQBUFENTRY s;
        s.seqNum = sendSeq;
        s.timeSentMs = elapsedMs();
        s.retransmitCount = 0;
        s.data = d;

        if (sendSeq == 0)
        {
            // Sequence rollover starts a new retransmit window.
            txSeqBuf.clear();
        }
        if (txSeqBuf.size() > BUFSIZE)
        {
            txSeqBuf.erase(txSeqBuf.begin());
        }
        txSeqBuf.insert(sendSeq, s);
        sendSeq++;
    }

    purgeOldEntries();

    QMutexLocker udpLocker(&udpMutex);
    for (int copy = 0; copy < copies; ++copy)
    {
        const qint64 ret = udp->writeDatagram(d, radioIP, port);
        if (ret < 0)
        {
            qWarning(logUdp()).noquote() << this->metaObject()->className() << "writeDatagram FAILED to"
                                         << radioIP.toString() << ":" << port << udp->errorString();
        }
    }

    if (idleTimer != nullptr && idleTimer->isActive())
    {
        idleTimer->start(IDLE_PERIOD);
    }
    udpLocker.unlock();
    packetsSent += copies;
    return;
}

void UdpBase::sendUntrackedPacket(const QByteArray& d, int copies)
{
    if (udp == nullptr || d.size() < CONTROL_SIZE)
    {
        return;
    }

    QMutexLocker udpLocker(&udpMutex);
    for (int copy = 0; copy < copies; ++copy)
    {
        const qint64 ret = udp->writeDatagram(d, radioIP, port);
        if (ret < 0)
        {
            qWarning(logUdp()).noquote() << this->metaObject()->className() << "writeDatagram FAILED to"
                                         << radioIP.toString() << ":" << port << udp->errorString();
        }
    }
    packetsSent += copies;
}

void UdpBase::purgeOldEntries()
{
    // Drop packet-buffer entries that are older than the retransmit window.
    if (txBufferMutex.tryLock(5))
    {
        if (!txSeqBuf.isEmpty())
        {
            for (auto it = txSeqBuf.begin(); it != txSeqBuf.end();)
            {
                if (elapsedMs() - it.value().timeSentMs > PURGE_SECONDS * 1000)
                {
                    txSeqBuf.erase(it++);
                }
                else
                {
                    break;
                }
            }
        }
        txBufferMutex.unlock();
    }
    else
    {
        qInfo(logUdp()).noquote() << this->metaObject()->className() << ": txBuffer mutex is locked";
    }

    if (rxBufferMutex.tryLock(5))
    {
        if (!rxSeqBuf.isEmpty())
        {
            for (auto it = rxSeqBuf.begin(); it != rxSeqBuf.end();)
            {
                if (elapsedMs() - it.value() > PURGE_SECONDS * 1000)
                {
                    rxSeqBuf.erase(it++);
                }
                else
                {
                    break;
                }
            }
        }
        rxBufferMutex.unlock();
    }
    else
    {
        qInfo(logUdp()).noquote() << this->metaObject()->className() << ": rxBuffer mutex is locked";
    }

    if (missingMutex.tryLock(5))
    {
        // Keep the missing-packet buffer bounded when retransmits fall behind.
        if (!rxMissing.isEmpty() && rxMissing.size() > 50)
        {
            for (size_t i = 0; i < 25; ++i)
            {
                rxMissing.erase(rxMissing.begin());
            }
        }
        missingMutex.unlock();
    }
    else
    {
        qInfo(logUdp()).noquote() << this->metaObject()->className() << ": missingBuffer mutex is locked";
    }
}

void UdpBase::printHex(const QByteArray& pdata)
{
    printHex(pdata, false, true);
}

void UdpBase::printHex(const QByteArray& pdata, bool printVert, bool printHoriz)
{
    // Manual packet dump helper; call only from gated diagnostics.
    qDebug(logUdp()).noquote() << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for (int i = 0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i, 8, 10, QChar('0')).arg((quint8)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((quint8)pdata[i], 2, 16, QChar('0')));
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if (printVert)
    {
        for (int i = 0; i < strings.length(); i++)
        {
            qDebug(logUdp()).noquote() << strings.at(i);
        }
    }

    if (printHoriz)
    {
        qDebug(logUdp()).noquote() << index;
        qDebug(logUdp()).noquote() << sdata;
    }
    qDebug(logUdp()).noquote() << "----- End hex dump -----";
}
