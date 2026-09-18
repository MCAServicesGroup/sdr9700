// QtTest invokes private slots through the generated meta-object.
#include "UdpBase.h"

#include <QtTest>
#include <utility>

class TestUdpBase : public UdpBase
{
  public:
    quint16 boundPort() const { return udp ? udp->localPort() : 0; }
    bool hasPendingDatagrams() const { return udp && udp->hasPendingDatagrams(); }
    QNetworkDatagram receiveDatagram() { return udp ? udp->receiveDatagram() : QNetworkDatagram(); }

    void setExpectedPeer(const QHostAddress& address, quint16 peerPort)
    {
        radioIP = address;
        port = peerPort;
    }

    bool accepts(const QNetworkDatagram& datagram) const { return acceptDatagramFrom(datagram); }
    void setSendSequence(quint16 sequence) { sendSeq = sequence; }
    void allowDeparture(bool allowed) { setDepartureAllowed(allowed); }
    void sendTrackedControl()
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        sendTrackedPacket(QByteArray(packet.packet, CONTROL_SIZE));
    }
    const char* sendMovedTrackedControl()
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        QByteArray encoded(packet.packet, CONTROL_SIZE);
        const char* storage = encoded.constData();
        sendTrackedPacket(std::move(encoded));
        return storage;
    }
    QList<quint16> transmittedSequences() const { return txSeqBuf.keys(); }
    const char* transmittedStorage(quint16 sequence) const { return txSeqBuf.value(sequence).data.constData(); }
    QList<quint16> receivedSequences() const { return rxSeqBuf.keys(); }
    QList<quint16> missingSequences() const { return rxMissing.keys(); }
};

class UdpBaseTest : public QObject
{
    Q_OBJECT

  private slots:
    void bindsEphemeralLoopbackSocket();
    void receivesLoopbackDatagram();
    void validatesDatagramEndpoint();
    void encodesLoginTextDeterministically();
    void parsesNullTerminatedPacketText();
    void tracksMissingAndDuplicatePackets();
    void boundsLargeSequenceGaps();
    void tracksMissingPacketsAcrossSequenceRollover();
    void boundsReceiveHistoryInInsertionOrder();
    void clearsTransmitWindowAtSequenceRollover();
    void preservesMovedPacketStorage() const;
    void sendsDepartureOnlyOnce();
    void suppressesDepartureWithoutSessionOwnership();
    void rejectsTruncatedPackets();
};

void UdpBaseTest::bindsEphemeralLoopbackSocket()
{
    TestUdpBase stream;

    QVERIFY(stream.init(0));
    QVERIFY(stream.isSocketBound());
    QVERIFY(stream.boundPort() > 0);
}

void UdpBaseTest::receivesLoopbackDatagram()
{
    TestUdpBase stream;
    QUdpSocket peer;
    QVERIFY(stream.init(0));
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    stream.setExpectedPeer(QHostAddress::LocalHost, peer.localPort());

    QCOMPARE(peer.writeDatagram(QByteArrayLiteral("loopback"), QHostAddress::LocalHost, stream.boundPort()), qint64(8));
    QTRY_VERIFY(stream.hasPendingDatagrams());
    const QNetworkDatagram received = stream.receiveDatagram();
    QCOMPARE(received.data(), QByteArrayLiteral("loopback"));
    QVERIFY(stream.accepts(received));
}

void UdpBaseTest::validatesDatagramEndpoint()
{
    TestUdpBase stream;
    stream.setExpectedPeer(QHostAddress::LocalHost, 50001);

    QNetworkDatagram expected(QByteArrayLiteral("data"));
    expected.setSender(QHostAddress::LocalHost, 50001);
    QVERIFY(stream.accepts(expected));

    QNetworkDatagram wrongPort(QByteArrayLiteral("data"));
    wrongPort.setSender(QHostAddress::LocalHost, 50002);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Ignoring UDP datagram.*")));
    QVERIFY(!stream.accepts(wrongPort));
}

void UdpBaseTest::encodesLoginTextDeterministically()
{
    QByteArray first;
    QByteArray second;
    passcode(QStringLiteral("operator-password-longer-than-16"), first);
    passcode(QStringLiteral("operator-password-longer-than-16"), second);

    QCOMPARE(first, second);
    QCOMPARE(first.size(), 16);
    QVERIFY(!first.contains(QByteArrayLiteral("operator")));
}

void UdpBaseTest::parsesNullTerminatedPacketText()
{
    QCOMPARE(parseNullTerminatedString(QByteArray("prefix\0suffix", 13), 0), QByteArrayLiteral("prefix"));
    QCOMPARE(parseNullTerminatedString(QByteArrayLiteral("prefix"), 3), QByteArrayLiteral("fix"));
    const QByteArray fullWidthField(32, 'x');
    QCOMPARE(parseNullTerminatedString(fullWidthField, 0), fullWidthField);
    QCOMPARE(parseNullTerminatedString(QByteArrayLiteral("short"), 20), QByteArray());
}

void UdpBaseTest::tracksMissingAndDuplicatePackets()
{
    TestUdpBase stream;
    auto packetForSequence = [](quint16 sequence)
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        packet.type = 0;
        packet.seq = sequence;
        return QByteArray(packet.packet, CONTROL_SIZE);
    };

    stream.dataReceived(packetForSequence(1));
    stream.dataReceived(packetForSequence(3));
    QCOMPARE(stream.receivedSequences(), QList<quint16>({1, 2, 3}));
    QCOMPARE(stream.missingSequences(), QList<quint16>({2}));
    stream.dataReceived(packetForSequence(2));
    QVERIFY(stream.missingSequences().isEmpty());
    stream.dataReceived(packetForSequence(2));
    QVERIFY(stream.missingSequences().isEmpty());
}

void UdpBaseTest::boundsLargeSequenceGaps()
{
    TestUdpBase stream;
    auto packetForSequence = [](quint16 sequence)
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        packet.type = 0;
        packet.seq = sequence;
        return QByteArray(packet.packet, CONTROL_SIZE);
    };

    stream.dataReceived(packetForSequence(1));
    stream.dataReceived(packetForSequence(0x9000));
    QCOMPARE(stream.receivedSequences(), QList<quint16>({0x9000}));
    QVERIFY(stream.missingSequences().isEmpty());
}

void UdpBaseTest::tracksMissingPacketsAcrossSequenceRollover()
{
    TestUdpBase stream;
    auto packetForSequence = [](quint16 sequence)
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        packet.type = 0;
        packet.seq = sequence;
        return QByteArray(packet.packet, CONTROL_SIZE);
    };

    stream.dataReceived(packetForSequence(0xfffe));
    stream.dataReceived(packetForSequence(1));
    QCOMPARE(stream.missingSequences(), QList<quint16>({0, 0xffff}));
    stream.dataReceived(packetForSequence(0xffff));
    stream.dataReceived(packetForSequence(0));
    QVERIFY(stream.missingSequences().isEmpty());
}

void UdpBaseTest::boundsReceiveHistoryInInsertionOrder()
{
    TestUdpBase stream;
    for (int sequence = 1; sequence <= BUFSIZE + 100; ++sequence)
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        packet.type = 0;
        packet.seq = static_cast<quint16>(sequence);
        stream.dataReceived(QByteArray(packet.packet, CONTROL_SIZE));
    }

    QCOMPARE(stream.receivedSequences().size(), BUFSIZE);
    QCOMPARE(stream.receivedSequences().constFirst(), quint16(101));
    QCOMPARE(stream.receivedSequences().constLast(), quint16(BUFSIZE + 100));
}

void UdpBaseTest::clearsTransmitWindowAtSequenceRollover()
{
    TestUdpBase stream;
    QVERIFY(stream.init(0));
    stream.setExpectedPeer(QHostAddress::LocalHost, 9);
    stream.setSendSequence(0xffff);
    stream.sendTrackedControl();
    QCOMPARE(stream.transmittedSequences(), QList<quint16>({0xffff}));
    stream.sendTrackedControl();
    QCOMPARE(stream.transmittedSequences(), QList<quint16>({0}));
    stream.sendTrackedControl();
    QCOMPARE(stream.transmittedSequences(), QList<quint16>({0, 1}));
}

void UdpBaseTest::preservesMovedPacketStorage() const
{
    TestUdpBase stream;
    QVERIFY(stream.init(0));
    stream.setExpectedPeer(QHostAddress::LocalHost, 9);

    const char* originalStorage = stream.sendMovedTrackedControl();
    QCOMPARE(stream.transmittedStorage(1), originalStorage);
}

void UdpBaseTest::sendsDepartureOnlyOnce()
{
    TestUdpBase stream;
    QUdpSocket peer;
    QVERIFY(stream.init(0));
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    stream.setExpectedPeer(QHostAddress::LocalHost, peer.localPort());

    stream.sendDeparture();
    stream.sendDeparture();

    QTRY_VERIFY(peer.hasPendingDatagrams());
    const QNetworkDatagram datagram = peer.receiveDatagram();
    const auto departure = decodePacket<control_packet>(datagram.data());
    QVERIFY(departure.has_value());
    QCOMPARE(departure->type, quint8(0x05));
    QTest::qWait(20);
    QVERIFY(!peer.hasPendingDatagrams());
}

void UdpBaseTest::suppressesDepartureWithoutSessionOwnership()
{
    TestUdpBase stream;
    QUdpSocket peer;
    QVERIFY(stream.init(0));
    QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
    stream.setExpectedPeer(QHostAddress::LocalHost, peer.localPort());
    stream.allowDeparture(false);

    // Exercise far more close attempts than a real process can produce during
    // one lifecycle. A rejected connection must remain incapable of emitting
    // the control departure used by an actual session owner.
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        stream.sendDeparture();
    }
    QTest::qWait(20);
    QVERIFY(!peer.hasPendingDatagrams());
}

void UdpBaseTest::rejectsTruncatedPackets()
{
    TestUdpBase stream;
    for (int length = 0; length < CONTROL_SIZE; ++length)
    {
        stream.dataReceived(QByteArray(length, '\xff'));
    }
    QVERIFY(stream.receivedSequences().isEmpty());
    QVERIFY(stream.missingSequences().isEmpty());
}

QTEST_GUILESS_MAIN(UdpBaseTest)
#include "UdpBaseTest.moc"
