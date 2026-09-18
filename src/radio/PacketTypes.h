// The packed LAN packet unions below are byte overlays. Incoming packets are
// backed by received datagrams, and outgoing packet instances are zeroed before
// individual protocol fields are written.
#pragma once
#include <QByteArray>
#include <QByteArrayView>
#include <QObject>
#include <QtEndian>
#include <cstring>
#include <optional>
#include <type_traits>

// IC-9700 LAN protocol timing and capacity limits. Names ending in PERIOD or
// LOCK_PERIOD are milliseconds; PURGE_SECONDS and STALE_CONNECTION are
// seconds. BUFSIZE and MAX_MISSING bound retransmission bookkeeping rather
// than socket receive-buffer sizes.
inline constexpr int PURGE_SECONDS = 10;
inline constexpr int TOKEN_RENEWAL = 60000;
inline constexpr int PING_PERIOD = 500;
inline constexpr int IDLE_PERIOD = 100;
inline constexpr int AREYOUTHERE_PERIOD = 500;
inline constexpr int WATCHDOG_PERIOD = 500;
inline constexpr int RETRANSMIT_PERIOD = 100;
inline constexpr int LOCK_PERIOD = 10;
inline constexpr int STALE_CONNECTION = 15;
inline constexpr int BUFSIZE = 500;
inline constexpr int MAX_MISSING = 50;
inline constexpr int AUDIO_PERIOD = 20;
inline constexpr int GUIDLEN = 16;

// Exact datagram lengths for fixed-size IC-9700 LAN packet families. These
// values are also used by static layout tests, so changing one requires wire
// evidence and a corresponding packet-structure update.
inline constexpr int CONTROL_SIZE = 0x10;
inline constexpr int WATCHDOG_SIZE = 0x14;
inline constexpr int PING_SIZE = 0x15;
inline constexpr int OPENCLOSE_SIZE = 0x16;
inline constexpr quint8 CIV_STREAM_CLOSED = 0x00;
inline constexpr quint8 CIV_STREAM_OPEN = 0x05;
inline constexpr int RETRANSMIT_RANGE_SIZE = 0x18;
inline constexpr int TOKEN_SIZE = 0x40;
inline constexpr int STATUS_SIZE = 0x50;
inline constexpr int LOGIN_RESPONSE_SIZE = 0x60;
inline constexpr int LOGIN_SIZE = 0x80;
inline constexpr int CONNINFO_SIZE = 0x90;
inline constexpr int CAPABILITIES_SIZE = 0x42;
inline constexpr int RADIO_CAP_SIZE = 0x66;
inline constexpr int MAX_CAPABILITY_RADIOS = 255;

// Header lengths for packet families followed by a variable-size payload.
// CIV_SIZE and DATA_SIZE share the same physical header length but retain
// separate names to document their distinct call-site semantics.
inline constexpr int CIV_SIZE = 0x15;
inline constexpr int AUDIO_SIZE = 0x18;
inline constexpr int DATA_SIZE = 0x15;

#pragma pack(push)
#pragma pack(1)

// 0x10-byte control packet used for connect, disconnect, idle, and
// retransmission-control messages.
typedef union control_packet
{
    struct
    {
        quint32 len;
        quint16 type;
        quint16 seq;
        quint32 sentid;
        quint32 rcvdid;
    };
    char packet[CONTROL_SIZE];
}* control_packet_t;

// 0x14-byte watchdog packet. The trailing 16-bit fields are preserved at their
// observed wire offsets even though SDR9700 does not currently interpret them.
typedef union watchdog_packet
{
    struct
    {
        quint32 len;      // 0x00
        quint16 type;     // 0x04
        quint16 seq;      // 0x06
        quint32 sentid;   // 0x08
        quint32 rcvdid;   // 0x0c
        quint16 secondsa; // 0x10
        quint16 secondsb; // 0x12
    };
    char packet[WATCHDOG_SIZE];
}* watchdog_packet_t;

// 0x15-byte ping packet. The same overlay is reused for the CI-V data header:
// the union after `reply` represents either the radio uptime in a ping or the
// CI-V payload length and CI-V stream sequence number.
typedef union ping_packet
{
    struct
    {
        quint32 len;    // 0x00
        quint16 type;   // 0x04
        quint16 seq;    // 0x06
        quint32 sentid; // 0x08
        quint32 rcvdid; // 0x0c
        quint8 reply;   // 0x10
        union
        {
            struct
            {
                quint32 time; // 0x11 (uptime of device)
            };
            struct
            {
                quint16 datalen; // 0x11
                quint16 sendseq; // 0x13
            };
        };
    };
    char packet[PING_SIZE];
} *ping_packet_t, *data_packet_t, data_packet;

// 0x16-byte CI-V stream open/close packet.
typedef union openclose_packet
{
    struct
    {
        quint32 len;     // 0x00
        quint16 type;    // 0x04
        quint16 seq;     // 0x06
        quint32 sentid;  // 0x08
        quint32 rcvdid;  // 0x0c
        quint16 data;    // 0x10
        char unused;     // 0x11
        quint16 sendseq; // 0x13
        char magic;      // 0x15
    };
    char packet[OPENCLOSE_SIZE];
}* startstop_packet_t;

// 0x18-byte audio header followed by encoded audio payload bytes.
typedef union audio_packet
{
    struct
    {
        quint32 len;     // 0x00
        quint16 type;    // 0x04
        quint16 seq;     // 0x06
        quint32 sentid;  // 0x08
        quint32 rcvdid;  // 0x0c

        quint16 ident;   // 0x10
        quint16 sendseq; // 0x12
        quint16 unused;  // 0x14
        quint16 datalen; // 0x16
    };
    char packet[AUDIO_SIZE];
}* audio_packet_t;

// 0x18-byte retransmission request containing up to four missing sequence
// numbers.
typedef union retransmit_range_packet
{
    struct
    {
        quint32 len;    // 0x00
        quint16 type;   // 0x04
        quint16 seq;    // 0x06
        quint32 sentid; // 0x08
        quint32 rcvdid; // 0x0c
        quint16 first;  // 0x10
        quint16 second; // 0x12
        quint16 third;  // 0x14
        quint16 fourth; // 0x16
    };
    char packet[RETRANSMIT_RANGE_SIZE];
}* retransmit_range_packet_t;

// 0x40-byte authentication-token request or response. The overlapping payload
// layouts represent the capability-identification and GUID forms observed at
// the same offsets during different authentication phases.
typedef union token_packet
{
    struct
    {
        quint32 len;         // 0x00
        quint16 type;        // 0x04
        quint16 seq;         // 0x06
        quint32 sentid;      // 0x08
        quint32 rcvdid;      // 0x0c
        quint32 payloadsize; // 0x10
        quint8 requestreply; // 0x14
        quint8 requesttype;  // 0x15
        quint16 innerseq;    // 0x16
        char unusedb[2];     // 0x18
        quint16 tokrequest;  // 0x1a
        quint32 token;       // 0x1c
        union
        {
            struct
            {
                quint16 authstartid;  // 0x20
                char unusedg2[2];     // 0x22
                quint16 resetcap;     // 0x24
                char unusedg1;        // 0x26
                quint16 commoncap;    // 0x27
                char unusedh;         // 0x29
                quint8 macaddress[6]; // 0x2a
            };
            quint8 guid[GUIDLEN];     // 0x20
        };
        quint32 response;             // 0x30
        char unusede[12];             // 0x34
    };
    char packet[TOKEN_SIZE];
}* token_packet_t;

// 0x50-byte stream-allocation status response. On success the trailing fields
// identify the negotiated CI-V and audio ports; on failure `error` and `disc`
// describe why the requested radio session was not established.
typedef union status_packet
{
    struct
    {
        quint32 len;         // 0x00         0
        quint16 type;        // 0x04         4
        quint16 seq;         // 0x06         6
        quint32 sentid;      // 0x08         8
        quint32 rcvdid;      // 0x0c         12
        quint32 payloadsize; // 0x10           18
        quint8 requestreply; // 0x14           19
        quint8 requesttype;  // 0x15           20
        quint16 innerseq;    // 0x16           22
        char unusedb[2];     // 0x18
        quint16 tokrequest;  // 0x1a
        quint32 token;       // 0x1c
        union
        {
            struct
            {
                quint16 authstartid;  // 0x20
                char unusedd[5];      // 0x22
                quint16 commoncap;    // 0x27
                char unusede;         // 0x29
                quint8 macaddress[6]; // 0x2a
            };
            quint8 guid[GUIDLEN];     // 0x20
        };
        quint32 error;                // 0x30
        char unusedg[12];             // 0x34
        char disc;                    // 0x40
        char unusedh;                 // 0x41
        quint16 civport;              // 0x42 // Sent bigendian
        quint16 unusedi;              // 0x44 // Sent bigendian
        quint16 audioport;            // 0x46 // Sent bigendian
        char unusedj[7];              // 0x48; struct data ends at 0x4e and the packet union supplies byte 0x4f
    };
    char packet[STATUS_SIZE];
}* status_packet_t;

// 0x60-byte login response carrying the authentication result and negotiated
// connection-type label.
typedef union login_response_packet
{
    struct
    {
        quint32 len;         // 0x00
        quint16 type;        // 0x04
        quint16 seq;         // 0x06
        quint32 sentid;      // 0x08
        quint32 rcvdid;      // 0x0c
        quint32 payloadsize; // 0x10
        quint8 requestreply; // 0x14
        quint8 requesttype;  // 0x15
        quint16 innerseq;    // 0x16
        char unusedb[2];     // 0x18
        quint16 tokrequest;  // 0x1a
        quint32 token;       // 0x1c
        quint16 authstartid; // 0x20
        char unusedd[14];    // 0x22
        quint32 error;       // 0x30
        char unusede[12];    // 0x34
        char connection[16]; // 0x40
        char unusedf[16];    // 0x50
    };
    char packet[LOGIN_RESPONSE_SIZE];
}* login_response_packet_t;

// 0x80-byte login request. Username and password contain the IC-9700-specific
// substituted byte representation produced by encodeLanText().
typedef union login_packet
{
    struct
    {
        quint32 len;         // 0x00
        quint16 type;        // 0x04
        quint16 seq;         // 0x06
        quint32 sentid;      // 0x08
        quint32 rcvdid;      // 0x0c
        quint32 payloadsize; // 0x10
        quint8 requestreply; // 0x14
        quint8 requesttype;  // 0x15
        quint16 innerseq;    // 0x16
        char unusedb[2];     // 0x18
        quint16 tokrequest;  // 0x1a
        quint32 token;       // 0x1c
        char unusedc[32];    // 0x20
        char username[16];   // 0x40
        char password[16];   // 0x50
        char name[16];       // 0x60
        char unusedf[16];    // 0x70
    };
    char packet[LOGIN_SIZE];
}* login_packet_t;

// 0x90-byte packet used in two protocol phases: radio-usage information from
// the server and a client stream request containing audio codecs, sample
// rates, local ports, and buffering preferences.
typedef union conninfo_packet
{
    struct
    {
        quint32 len;         // 0x00
        quint16 type;        // 0x04
        quint16 seq;         // 0x06
        quint32 sentid;      // 0x08
        quint32 rcvdid;      // 0x0c
        quint32 payloadsize; // 0x10
        quint8 requestreply; // 0x14
        quint8 requesttype;  // 0x15
        quint16 innerseq;    // 0x16
        char unusedb[2];     // 0x18
        quint16 tokrequest;  // 0x1a
        quint32 token;       // 0x1c
        union
        {
            struct
            {
                quint16 authstartid;  // 0x20
                char unusedg[5];      // 0x22
                quint16 commoncap;    // 0x27
                char unusedh;         // 0x29
                quint8 macaddress[6]; // 0x2a
            };
            quint8 guid[GUIDLEN];     // 0x20
        };
        char unusedab[16];            // 0x30
        char name[32];                // 0x40
        union
        {
            struct
            {
                quint32 busy;      // 0x60
                char computer[16]; // 0x64
                char unusedi[16];  // 0x74
                quint32 ipaddress; // 0x84
                char unusedj[8];   // 0x88
            };
            struct
            {
                char username[16]; // 0x60
                char rxenable;     // 0x70
                char txenable;     // 0x71
                char rxcodec;      // 0x72
                char txcodec;      // 0x73
                quint32 rxsample;  // 0x74
                quint32 txsample;  // 0x78
                quint32 civport;   // 0x7c
                quint32 audioport; // 0x80
                quint32 txbuffer;  // 0x84
                quint8 convert;    // 0x88
                char unusedl[7];   // 0x89
            };
        };
    };
    char packet[CONNINFO_SIZE];
}* conninfo_packet_t;

// Fixed 0x66-byte radio-capability record embedded after the capability-list
// header. Keep the explicit size and field offsets aligned with
// PacketLayoutTest; reserved bytes remain intentionally unnamed.

typedef union radio_cap_packet
{
    struct
    {
        union
        {
            struct
            {
                quint8 unusede[7];    // 0x00
                quint16 commoncap;    // 0x07
                quint8 unused;        // 0x09
                quint8 macaddress[6]; // 0x0a
            };
            quint8 guid[GUIDLEN];     // 0x0
        };
        char name[32];                // 0x10
        char audio[32];               // 0x30
        quint16 conntype;             // 0x50
        char civ;                     // 0x52
        quint16 rxsample;             // 0x53
        quint16 txsample;             // 0x55
        quint8 enablea;               // 0x57
        quint8 enableb;               // 0x58
        quint8 enablec;               // 0x59
        quint32 baudrate;             // 0x5a
        quint16 capf;                 // 0x5e
        char unusedi;                 // 0x60
        quint16 capg;                 // 0x61
        char unusedj[3];              // 0x63
    };
    char packet[RADIO_CAP_SIZE];
}* radio_cap_packet_t;

// The capabilities header is 0x42 bytes. A normal one-radio response is
// 0xA8 bytes total: this header followed by one 0x66-byte radio_cap_packet.
typedef union capabilities_packet
{
    struct
    {
        quint32 len;         // 0x00
        quint16 type;        // 0x04
        quint16 seq;         // 0x06
        quint32 sentid;      // 0x08
        quint32 rcvdid;      // 0x0c
        quint32 payloadsize; // 0x10
        quint8 requestreply; // 0x14
        quint8 requesttype;  // 0x15
        quint16 innerseq;    // 0x16
        char unusedb[2];     // 0x18
        quint16 tokrequest;  // 0x1a
        quint32 token;       // 0x1c
        char unusedd[32];    // 0x20
        quint16 numradios;   // 0x40
    };
    char packet[CAPABILITIES_SIZE];
}* capabilities_packet_t;

typedef union rtp_header
{
    struct
    {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        quint8 csrc : 4;
        quint8 extension : 1;
        quint8 padding : 1;
        quint8 version : 2;
        quint8 payloadType : 7;
        quint8 marker : 1;
#elif Q_BYTE_ORDER == Q_BIG_ENDIAN
        // Linux and Apple Silicon macOS are little-endian today; retain the
        // explicit big-endian layout so the wire representation remains defined.
        quint8 version : 2;
        quint8 padding : 1;
        quint8 extension : 1;
        quint8 csrc : 4;
        quint8 marker : 1;
        quint8 payloadType : 7;
#else
#error "Q_BYTE_ORDER is not defined"
#endif
        quint16 seq;
        quint32 timestamp;
        quint32 ssrc;
    };
    quint8 packet[12];
}* rtp_header_t;

#pragma pack(pop)

static_assert(sizeof(control_packet) == CONTROL_SIZE);
static_assert(sizeof(watchdog_packet) == WATCHDOG_SIZE);
static_assert(sizeof(ping_packet) == PING_SIZE);
static_assert(sizeof(openclose_packet) == OPENCLOSE_SIZE);
static_assert(sizeof(audio_packet) == AUDIO_SIZE);
static_assert(sizeof(retransmit_range_packet) == RETRANSMIT_RANGE_SIZE);
static_assert(sizeof(token_packet) == TOKEN_SIZE);
static_assert(sizeof(status_packet) == STATUS_SIZE);
static_assert(sizeof(login_response_packet) == LOGIN_RESPONSE_SIZE);
static_assert(sizeof(login_packet) == LOGIN_SIZE);
static_assert(sizeof(conninfo_packet) == CONNINFO_SIZE);
static_assert(sizeof(radio_cap_packet) == RADIO_CAP_SIZE);
static_assert(sizeof(capabilities_packet) == CAPABILITIES_SIZE);
static_assert(sizeof(rtp_header) == 12);

inline int boundedCapabilityRadioCount(int advertisedRadios, int availableRadios) noexcept
{
    return qMin(qMin(qMax(advertisedRadios, 0), qMax(availableRadios, 0)), MAX_CAPABILITY_RADIOS);
}

template <typename Packet> std::optional<Packet> decodePacket(QByteArrayView bytes)
{
    static_assert(std::is_trivially_copyable_v<Packet>);
    if (bytes.size() < static_cast<qsizetype>(sizeof(Packet)))
    {
        return std::nullopt;
    }
    Packet packet{};
    std::memcpy(&packet, bytes.data(), sizeof(Packet));
    return packet;
}

template <typename Packet> QByteArray encodePacket(const Packet& packet)
{
    static_assert(std::is_trivially_copyable_v<Packet>);
    QByteArray bytes(sizeof(Packet), Qt::Uninitialized);
    std::memcpy(bytes.data(), &packet, sizeof(Packet));
    return bytes;
}
