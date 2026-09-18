// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QTimer>
#include <QMutex>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUuid>
#include <QElapsedTimer>

#include <QtEndian>

#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "PacketTypes.h"
#include "Types.h"

inline constexpr quint16 kIcomLanControlPortMax = 65532;

struct UdpConnectionSettings
{
    connectionType_t connectionType{};
    QString ipAddress;
    quint16 controlLANPort{0};
    quint16 civLANPort{0};
    quint16 audioLANPort{0};
    quint16 scopeLANPort{0};
    QString username;
    // Encoded for the IC-9700 LAN login exchange. The caller-owned settings
    // object and UdpHandler both retain this byte array for the
    // connection lifetime; UdpHandler clears its copy during shutdown.
    QByteArray passwordEncoded;
    QString clientName;
    bool halfDuplex{false};
    bool adminLogin{false};
};

struct networkAudioLevels
{
    bool haveTxLevels = false;
    bool haveRxLevels = false;
    quint8 rxAudioRMS = 0;
    quint8 txAudioRMS = 0;
    quint8 rxAudioPeak = 0;
    quint8 txAudioPeak = 0;
};

struct networkStatus
{
    quint8 rxAudioBufferPercent{0};
    quint8 txAudioBufferPercent{0};
    quint8 rxAudioLevel{0};
    quint8 txAudioLevel{0};
    quint16 rxLatency{0};
    quint16 txLatency{0};
    bool rxUnderrun{false};
    bool txUnderrun{false};
    bool rxOverrun{false};
    bool txOverrun{false};
    qint16 rxCurrentLatency{0};
    qint16 txCurrentLatency{0};
    quint32 packetsSent = 0;
    quint32 packetsLost = 0;
    quint16 rtt = 0;
    quint32 networkLatency = 0;
    qint64 timeDifference = 0;
    QString message;
    QString rxLatencyClass;
    bool userVisibleMessage{false};
    ConnectionStage connectionStage{ConnectionStage::Unchanged};
    MessageSeverity messageSeverity{MessageSeverity::Info};
};

class UdpBase : public QObject
{

  public:
    ~UdpBase();

    bool init(quint16 bindPort, QUdpSocket* boundSocket = nullptr);
    bool isSocketBound() const;

    void dataReceived(const QByteArray& r);
    void sendPing();

    void sendControl(bool tracked, quint8 type, quint16 seq);
    void sendDeparture();

    void printHex(const QByteArray& pdata);
    void printHex(const QByteArray& pdata, bool printVert, bool printHoriz);

    qint64 getTimeDifference() const { return pingLatenessMs; }
    quint32 packetsSentCount() const { return packetsSent; }
    quint32 packetsLostCount() const { return packetsLost; }
    qint64 lastPacketAgeMs() const { return msSinceLastReceived(); }
    quint16 boundLocalPort() const { return localPort; }
    quint32 localSessionId() const { return myId; }
    quint32 remoteSessionId() const { return remoteId; }
    quint16 remotePort() const { return port; }

  protected:
    QUdpSocket* udp = nullptr;
    uint32_t myId = 0;
    uint32_t remoteId = 0;
    // Authentication request sequencing is scoped to each fresh control
    // association. Start at zero, matching the deterministic lifecycle probe.
    uint16_t authSeq = 0;
    uint16_t sendSeqB = 0;
    uint16_t sendSeq = 1;
    uint16_t lastReceivedSeq = 1;
    quint16 highestTrackedReceiveSequence{0};
    bool receiveSequenceTrackingInitialized{false};
    uint16_t pkt0SendSeq = 0;
    uint16_t periodicSeq = 0;
    quint64 latency = 0;

    QHostAddress radioIP;
    QHostAddress localIP;
    bool isAuthenticated = false;
    quint16 localPort = 0;
    quint16 port = 0;
    bool periodicRunning = false;
    bool sentPacketConnect2 = false;
    bool departureSent = false;
    bool departureAllowed = true;
    qint64 lastReceivedMs = 0; // Monotonic timestamp from mono; safe across wall-clock midnight.
    // udpMutex and txBufferMutex are never nested. Receive bookkeeping may
    // acquire rxBufferMutex followed by missingMutex, never the reverse.
    QMutex udpMutex;
    QMutex txBufferMutex;
    QMutex rxBufferMutex;
    QMutex missingMutex;

    struct SEQBUFENTRY
    {
        qint64 timeSentMs = 0;
        uint16_t seqNum = 0;
        QByteArray data;
        quint8 retransmitCount = 0;
    };

    QMap<quint16, qint64> rxSeqBuf;
    QMap<quint16, SEQBUFENTRY> txSeqBuf;
    QMap<quint16, int> rxMissing;

    void sendTrackedPacket(QByteArray d, int copies = 1);
    void sendUntrackedPacket(const QByteArray& d, int copies = 1);
    void purgeOldEntries();

    QTimer* areYouThereTimer = nullptr; // Sends discovery handshakes until the radio replies "I am here."
    QTimer* pingTimer = nullptr;        // Measures LAN path delay and keeps the IC-9700 UDP session active.
    QTimer* idleTimer = nullptr;        // Sends idle control packets when no tracked command traffic is flowing.

    QTimer* watchdogTimer = nullptr;
    QTimer* retransmitTimer = nullptr;

    qint64 lastPingSentMs = 0;
    uint16_t pingSendSeq = 0;

    quint16 areYouThereCounter = 0;

    quint32 packetsSent = 0;
    quint32 packetsLost = 0;

    quint16 seqPrefix = 0;
    QString m_connectionType;
    qint64 elapsedMs() const { return mono.isValid() ? mono.elapsed() : 0; }
    // Watchdogs call this against the same monotonic clock used by
    // markPacketReceived(); do not mix this value with wall-clock time.
    qint64 msSinceLastReceived() const { return elapsedMs() - lastReceivedMs; }
    // Stream packet handlers call this only after accepting a packet that proves
    // the radio-side UDP session is still alive.
    void markPacketReceived() { lastReceivedMs = elapsedMs(); }
    bool acceptDatagramFrom(const QNetworkDatagram& datagram) const;
    void setDepartureAllowed(bool allowed) { departureAllowed = allowed; }

    QElapsedTimer mono;
    int pingDriftMs = 0; // Signed drift estimate: positive when the radio clock trails prediction.
    int badSyncCount = 0;

    // Per-UDP-stream sync state used to compare radio ping timestamps against
    // sdr9700 monotonic time. Keep these per-instance because control, CI-V, and
    // audio sockets may be active at the same time.
    bool pingHaveSync = false;
    int pingRadioBase = 0;
    qint64 pingLocalBase = 0;
    bool pingBaselineValid = false;
    int pingLatenessMs = 0; // Positive means measured packet delay versus prediction.
    int pingBaselineMs = 0; // Learned steady-state LAN/session delay.

  private:
    void sendRetransmitRequest();
};

// Encode IC-9700 LAN login text with the radio's fixed substitution table.
// The radio protocol treats username/password characters as single-byte ASCII;
// Latin-1 keeps those byte values deterministic instead of depending on the
// process locale.
inline void passcode(const QString& in, QByteArray& out)
{
    const quint8 sequence[] = {
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x47, 0x5d, 0x4c, 0x42,
        0x66, 0x20, 0x23, 0x46, 0x4e, 0x57, 0x45, 0x3d, 0x67, 0x76, 0x60, 0x41, 0x62, 0x39, 0x59, 0x2d, 0x68, 0x7e,
        0x7c, 0x65, 0x7d, 0x49, 0x29, 0x72, 0x73, 0x78, 0x21, 0x6e, 0x5a, 0x5e, 0x4a, 0x3e, 0x71, 0x2c, 0x2a, 0x54,
        0x3c, 0x3a, 0x63, 0x4f, 0x43, 0x75, 0x27, 0x79, 0x5b, 0x35, 0x70, 0x48, 0x6b, 0x56, 0x6f, 0x34, 0x32, 0x6c,
        0x30, 0x61, 0x6d, 0x7b, 0x2f, 0x4b, 0x64, 0x38, 0x2b, 0x2e, 0x50, 0x40, 0x3f, 0x55, 0x33, 0x37, 0x25, 0x77,
        0x24, 0x26, 0x74, 0x6a, 0x28, 0x53, 0x4d, 0x69, 0x22, 0x5c, 0x44, 0x31, 0x36, 0x58, 0x3b, 0x7a, 0x51, 0x5f,
        0x52, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0

    };

    QByteArray ba = in.toLatin1();
    const uchar* ascii = reinterpret_cast<const uchar*>(ba.constData());
    for (int i = 0; i < in.length() && i < 16; i++)
    {
        int p = ascii[i] + i;
        if (p > 126)
        {
            p = 32 + p % 127;
        }
        out.append(sequence[p]);
    }
    return;
}

// Extract an ASCII field from an IC-9700 packet starting at offset s. The radio
// pads many text fields with NUL bytes, so parsing stops at the first terminator
// or at the end of the packet.
inline QByteArray parseNullTerminatedString(QByteArray c, int s)
{
    QByteArray res;
    for (int i = s; i < c.length(); i++)
    {
        if (c[i] != '\0')
        {
            res.append(c[i]);
        }
        else
        {
            break;
        }
    }
    return res;
}
