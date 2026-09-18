#include "UdpCivData.h"
#include "LogCategories.h"

#include <functional>
#include <utility>

namespace
{
constexpr int kOpenStartRetryIntervalMs = 100;
constexpr int kOpenStartMaxAttempts = 50;

bool isScopeDataDatagram(const QByteArray& datagram)
{
    return datagram.size() > DATA_SIZE + 5 && static_cast<uchar>(datagram.at(DATA_SIZE + 4)) == 0x27 &&
           static_cast<uchar>(datagram.at(DATA_SIZE + 5)) == 0x00;
}

bool isScopeDataPayload(const QByteArray& payload)
{
    return payload.size() > 5 && static_cast<uchar>(payload.at(4)) == 0x27 && static_cast<uchar>(payload.at(5)) == 0x00;
}
} // namespace

UdpCivData::UdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, quint16 localPort, QUdpSocket* boundSocket)
{
    qInfo(logUdp()).noquote() << "Starting UdpCivData";
    localIP = local;
    port = civPort;
    radioIP = ip;

    if (!UdpBase::init(localPort, boundSocket))
    {
        return;
    }

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &UdpCivData::dataReceived);

    sendControl(false, 0x03, 0x00);

    pingTimer = new QTimer(this);
    idleTimer = new QTimer(this);
    areYouThereTimer = new QTimer(this);
    startCivDataTimer = new QTimer(this);

    connect(pingTimer, &QTimer::timeout, this, &UdpBase::sendPing);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, true, 0, 0));
    connect(startCivDataTimer, &QTimer::timeout, this, &UdpCivData::requestDataStart);
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&UdpBase::sendControl, this, false, 0x03, 0));
    // Ping and idle intervals use the LAN protocol constants from PacketTypes.h.
    // The are-you-there timer stops after the radio replies "I am here".
    pingTimer->start(PING_PERIOD);
    idleTimer->start(IDLE_PERIOD);
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

UdpCivData::~UdpCivData()
{
    closeStream();
}

void UdpCivData::closeStream()
{
    if (m_closeSent)
    {
        return;
    }
    m_closeSent = true;
    if (startCivDataTimer)
    {
        startCivDataTimer->stop();
    }
    sendOpenClose(true);
}

void UdpCivData::requestDataRestart()
{
    if (!m_closeSent)
    {
        m_openStartRequestCount = 0;
        requestDataStart();
        if (startCivDataTimer != nullptr && !startCivDataTimer->isActive())
        {
            startCivDataTimer->start(kOpenStartRetryIntervalMs);
        }
    }
}

void UdpCivData::requestDataStart()
{
    if (m_closeSent)
    {
        if (startCivDataTimer != nullptr)
        {
            startCivDataTimer->stop();
        }
        return;
    }

    if (m_openStartRequestCount >= kOpenStartMaxAttempts)
    {
        qWarning(logUdp()).noquote() << "CI-V data-start request did not produce data after" << m_openStartRequestCount
                                     << "attempts";
        if (startCivDataTimer != nullptr)
        {
            startCivDataTimer->stop();
        }
        return;
    }

    ++m_openStartRequestCount;
    sendOpenClose(false);
}

void UdpCivData::send(QByteArray d)
{
    qDebug(logRadioTraffic()).noquote().nospace()
        << "UdpCivData::TX port=" << port << " radioIP=" << radioIP.toString() << " len=" << d.length()
        << " data=" << QString::fromLatin1(d.toHex(' '));
    data_packet p{};
    p.len = (quint32)sizeof(p) + d.length();
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.reply = (char)0xc1;
    p.datalen = d.length();
    p.sendseq = qToBigEndian(sendSeqB);

    QByteArray t = encodePacket(p);
    t.append(d);
    sendTrackedPacket(std::move(t));
    sendSeqB++;
    return;
}

void UdpCivData::sendOpenClose(bool close)
{
    const quint8 magic = close ? CIV_STREAM_CLOSED : CIV_STREAM_OPEN;
    qDebug(logUdp()).noquote().nospace() << "UdpCivData::sendOpenClose close=" << close << " remoteId=0x" << Qt::hex
                                         << remoteId;

    openclose_packet p{};
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.data = 0x01c0;
    p.sendseq = qToBigEndian(sendSeqB);
    p.magic = magic;

    sendSeqB++;

    // Captured RS-BA1 behavior, and the deterministic lifecycle probe used to
    // validate retained-session recovery, send the identical pipe-open/close
    // datagram twice. The IC-9700 can acknowledge a replacement transport yet
    // leave its CI-V pipe dormant when only one open datagram follows cleanup
    // of a retained predecessor session.
    sendUntrackedPacket(encodePacket(p), 2);
    return;
}

void UdpCivData::dataReceived()
{
    while (udp->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        if (!acceptDatagramFrom(datagram))
        {
            continue;
        }
        QByteArray r = datagram.data();
        // Update loss/retransmit bookkeeping before any accepted payload can
        // reach Commander. The CI-V sequence gate suppresses duplicate
        // sequence numbers and records reordering without delaying delivery.
        UdpBase::dataReceived(r);

        const bool scopeDataDatagram = isScopeDataDatagram(r);
        switch (r.length())
        {
        case (CONTROL_SIZE):
        {
            const auto decoded = decodePacket<control_packet>(r);
            const control_packet* in = &*decoded;
            // Type 0 control packets are the steady-state LAN keepalive/ack
            // stream. Logging every one can overwhelm stderr and hide the
            // startup/memory-sync evidence we actually need in field logs.
            if (in->type != 0x00)
            {
                qDebug(logUdp()).noquote().nospace() << "UdpCivData::Control type=0x" << Qt::hex << int(in->type);
            }
            if (in->type == 0x04)
            {
                areYouThereTimer->stop();
            }
            else if (in->type == 0x06)
            {
                if (remoteId != in->sentid)
                {
                    m_sequenceGate.reset();
                }
                remoteId = in->sentid;
                if (!m_readyEmitted)
                {
                    m_readyEmitted = true;
                    emit ready();
                }
            }
            break;
        }
        default:
        {
            if (r.length() > 21)
            {
                const auto decoded = decodePacket<ping_packet>(r);
                const ping_packet* in = &*decoded;
                if (in->type != 0x01)
                {
                    if (in->len != quint32(r.length()))
                    {
                        qWarning(logUdp()).noquote() << "Dropping CI-V datagram with mismatched length: header"
                                                     << in->len << "actual" << r.length();
                        break;
                    }
                    if (quint32(in->datalen) + DATA_SIZE != in->len)
                    {
                        qWarning(logUdp()).noquote() << "Dropping CI-V datagram with mismatched payload length: header"
                                                     << in->datalen << "packet length" << in->len;
                        break;
                    }
                    markPacketReceived();
                    // Large 0x27 scope data datagrams arrive continuously once
                    // the Spectrum Scope is enabled. Do not log them here; the
                    // scope parser and model are the right places to diagnose
                    // display behavior, and raw UDP logging can hide connection
                    // and memory-sync state.
                    if (!scopeDataDatagram)
                    {
                        qDebug(logRadioTraffic()).noquote().nospace()
                            << "UdpCivData::RX len=" << r.length()
                            << " hex=" << QString::fromLatin1(r.left(16).toHex(' '));
                    }
                    const CivSequenceGateResult gateResult = m_sequenceGate.accept(in->seq, r.mid(DATA_SIZE));
                    deliverSequencedPayloads(gateResult);
                }
            }
            break;
        }
        }
        r.clear();
        datagram.clear();
    }
}

void UdpCivData::deliverSequencedPayloads(const CivSequenceGateResult& result)
{
    // A retained radio stream can send syntactically valid datagrams from the
    // predecessor sequence space. They are not proof that this replacement's
    // CI-V pipe opened. Stop open retries only when the sequence gate actually
    // accepts data for delivery to Commander.
    if (!result)
    {
        return;
    }
    if (!isScopeDataPayload(*result))
    {
        if (startCivDataTimer != nullptr)
        {
            startCivDataTimer->stop();
        }
        m_openStartRequestCount = 0;
    }
    emit receive(*result);
}
