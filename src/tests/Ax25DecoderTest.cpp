#include "Ax25Decoder.h"

#include <QTest>
#include <cmath>
#include <numbers>

namespace
{
QByteArray encodedAddress(const QByteArray& call, int ssid, bool last, bool repeated = false)
{
    QByteArray padded = call.leftJustified(6, ' ', true);
    QByteArray result;
    for (const char value : padded)
    {
        result.append(static_cast<char>(static_cast<quint8>(value) << 1));
    }
    result.append(static_cast<char>(0x60U | ((ssid & 0x0f) << 1) | (last ? 1 : 0) | (repeated ? 0x80U : 0U)));
    return result;
}

QVector<bool> hdlcBits(const QByteArray& frame)
{
    QVector<bool> bits;
    auto appendByte = [&bits](quint8 value)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            bits.append((value & (1U << bit)) != 0);
        }
    };
    appendByte(0x7e);
    int ones = 0;
    for (const char value : frame)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            const bool one = (static_cast<quint8>(value) & (1U << bit)) != 0;
            bits.append(one);
            if (one)
            {
                if (++ones == 5)
                {
                    bits.append(false);
                    ones = 0;
                }
            }
            else
            {
                ones = 0;
            }
        }
    }
    appendByte(0x7e);
    return bits;
}

QVector<bool> nrziTones(const QVector<bool>& bits)
{
    QVector<bool> tones;
    bool tone = false;
    tones.append(tone);
    for (const bool bit : bits)
    {
        if (!bit)
        {
            tone = !tone;
        }
        tones.append(tone);
    }
    return tones;
}

QByteArray pcmForTones(const QVector<bool>& tones, int channels)
{
    constexpr int sampleRate = 48000;
    constexpr int samplesPerSymbol = sampleRate / 1200;
    QByteArray pcm;
    pcm.reserve(tones.size() * samplesPerSymbol * channels * 2);
    double phase = 0.0;
    for (const bool mark : tones)
    {
        const double increment = 2.0 * std::numbers::pi * (mark ? 1200.0 : 2200.0) / sampleRate;
        for (int sampleIndex = 0; sampleIndex < samplesPerSymbol; ++sampleIndex)
        {
            const qint16 sample = static_cast<qint16>(std::sin(phase) * 20000.0);
            phase += increment;
            for (int channel = 0; channel < channels; ++channel)
            {
                pcm.append(static_cast<char>(sample & 0xff));
                pcm.append(static_cast<char>((static_cast<quint16>(sample) >> 8) & 0xff));
            }
        }
    }
    return pcm;
}

QByteArray opposingStereoPcmForTones(const QVector<bool>& tones)
{
    QByteArray pcm = pcmForTones(tones, 1);
    QByteArray stereo;
    stereo.reserve(pcm.size() * 2);
    for (qsizetype offset = 0; offset + 1 < pcm.size(); offset += 2)
    {
        const quint8 low = static_cast<quint8>(pcm.at(offset));
        const quint8 high = static_cast<quint8>(pcm.at(offset + 1));
        const qint16 sample = static_cast<qint16>(low | (static_cast<quint16>(high) << 8));
        const qint16 opposite = static_cast<qint16>(-sample);
        stereo.append(pcm.at(offset));
        stereo.append(pcm.at(offset + 1));
        stereo.append(static_cast<char>(opposite & 0xff));
        stereo.append(static_cast<char>((static_cast<quint16>(opposite) >> 8) & 0xff));
    }
    return stereo;
}
} // namespace

class Ax25DecoderTest : public QObject
{
    Q_OBJECT

  private slots:
    void crcKnownCheck();
    void decodesUiFrame();
    void rejectsBadFcs();
    void countsMalformedCandidate();
    void preservesRepeatedDigipeaterState();
    void acceptsLegitimateRepeatedFrames();
    void decodesStereoAudio();
    void decodesSelectedStereoReceiverWithoutMixing();
};

void Ax25DecoderTest::crcKnownCheck()
{
    QCOMPARE(Ax25Decoder::frameCheckSequence(QByteArrayLiteral("123456789")), quint16(0x906e));
}

void Ax25DecoderTest::decodesUiFrame()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 7, true);
    frame.append(char(0x03));
    frame.append(char(0xf0));
    frame.append("Test packet");
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));

    Ax25Decoder decoder;
    const QVector<Ax25Frame> decoded = decoder.processNrziTones(nrziTones(hdlcBits(frame)));
    QCOMPARE(decoded.size(), 1);
    QCOMPARE(decoded.first().source, QStringLiteral("N0CALL-7"));
    QCOMPARE(decoded.first().destination, QStringLiteral("APRS"));
    QCOMPARE(decoded.first().type, QStringLiteral("UI"));
    QCOMPARE(decoded.first().payload, QStringLiteral("Test packet"));
    QCOMPARE(decoded.first().protocol, QStringLiteral("AX.25 (1200)"));
    QCOMPARE(decoder.stats().decoded, quint64(1));
    QCOMPARE(decoder.stats().candidates, quint64(1));
}

void Ax25DecoderTest::rejectsBadFcs()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, true);
    frame.append(QByteArray::fromHex("03f00000"));
    Ax25Decoder decoder;
    QCOMPARE(decoder.processNrziTones(nrziTones(hdlcBits(frame))).size(), 0);
    QCOMPARE(decoder.stats().fcsFailures, quint64(1));
}

void Ax25DecoderTest::countsMalformedCandidate()
{
    QByteArray frame(20, '\0');
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));
    Ax25Decoder decoder;
    QCOMPARE(decoder.processNrziTones(nrziTones(hdlcBits(frame))).size(), 0);
    QCOMPARE(decoder.stats().candidates, quint64(1));
    QCOMPARE(decoder.stats().malformed, quint64(1));
}

void Ax25DecoderTest::preservesRepeatedDigipeaterState()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, false) +
                       encodedAddress("WIDE1", 1, false, true) + encodedAddress("WIDE2", 2, true);
    frame.append(QByteArray::fromHex("03f0"));
    frame.append("Path test");
    bool valid = false;
    const Ax25Frame decoded = Ax25Decoder::parseFrame(frame, &valid);
    QVERIFY(valid);
    QCOMPARE(decoded.path, QStringLiteral("WIDE1-1*,WIDE2-2"));
}

void Ax25DecoderTest::acceptsLegitimateRepeatedFrames()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, true);
    frame.append(QByteArray::fromHex("03f0"));
    frame.append("Repeated packet");
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));
    const QVector<bool> packet = nrziTones(hdlcBits(frame));
    QVector<bool> stream = packet;
    stream.append(packet);

    Ax25Decoder decoder;
    QCOMPARE(decoder.processNrziTones(stream).size(), 2);
    QCOMPARE(decoder.stats().decoded, quint64(2));
}

void Ax25DecoderTest::decodesStereoAudio()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, true);
    frame.append(QByteArray::fromHex("03f0"));
    frame.append("Audio test");
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));

    Ax25Decoder decoder;
    const QVector<Ax25Frame> decoded = decoder.processPcm16(pcmForTones(nrziTones(hdlcBits(frame)), 2), 48000, 2, 0);
    QVERIFY(!decoded.isEmpty());
    QCOMPARE(decoded.first().payload, QStringLiteral("Audio test"));
    QVERIFY(decoder.stats().audioLevel > 0);
    QCOMPARE(decoder.stats().candidates, quint64(1));
}

void Ax25DecoderTest::decodesSelectedStereoReceiverWithoutMixing()
{
    QByteArray frame = encodedAddress("APRS", 0, false) + encodedAddress("N0CALL", 0, true);
    frame.append(QByteArray::fromHex("03f0"));
    frame.append("Selected receiver");
    const quint16 fcs = Ax25Decoder::frameCheckSequence(frame);
    frame.append(static_cast<char>(fcs & 0xff));
    frame.append(static_cast<char>(fcs >> 8));
    const QByteArray pcm = opposingStereoPcmForTones(nrziTones(hdlcBits(frame)));

    for (const int receiverChannel : {0, 1})
    {
        Ax25Decoder decoder;
        const QVector<Ax25Frame> decoded = decoder.processPcm16(pcm, 48000, 2, receiverChannel);
        QVERIFY(!decoded.isEmpty());
        QCOMPARE(decoded.first().payload, QStringLiteral("Selected receiver"));
    }
}

QTEST_APPLESS_MAIN(Ax25DecoderTest)
#include "Ax25DecoderTest.moc"
