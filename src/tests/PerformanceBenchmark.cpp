// cppcheck-suppress-file unusedFunction
// QtTest invokes private slots through the generated meta-object.
#include "AudioConverter.h"
#include "Ax25Decoder.h"
#include "PacketTypes.h"
#include "TxAudioMeterPolicy.h"
#include "SpectrumScopeCanvas.h"
#include "UdpBase.h"

#include <QAudioFormat>
#include <QImage>
#include <QPainter>
#include <QtTest>
#include <utility>

class BenchmarkUdpBase : public UdpBase
{
  public:
    void resetReceiveState()
    {
        rxSeqBuf.clear();
        m_rxSequenceOrder.clear();
        rxMissing.clear();
        receiveSequenceTrackingInitialized = false;
        highestTrackedReceiveSequence = 0;
    }
};

class PerformanceBenchmark : public QObject
{
    Q_OBJECT

  private slots:
    void convertsAudioPacket() const;
    void demodulatesAx25Audio() const;
    void rendersSpectrumFrame() const;
    void ingestsUdpDatagrams() const;
    void aggregatesTransmitMeterBlocks() const;
    void convertsAuthorizedAndUnauthorizedTransmitAudio_data() const;
    void convertsAuthorizedAndUnauthorizedTransmitAudio() const;
};

void PerformanceBenchmark::convertsAudioPacket() const
{
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    AudioConverter converter;
    QVERIFY(converter.init(format, LPCM, format, LPCM, 7, 4));

    audioPacket packet;
    packet.data = QByteArray(format.bytesForDuration(20000), '\0');
    QBENCHMARK
    {
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            QVERIFY(converter.convert(packet));
        }
    }
}

void PerformanceBenchmark::demodulatesAx25Audio() const
{
    Ax25Decoder decoder;
    const QByteArray pcm(48000 * int(sizeof(qint16)), '\0');
    QBENCHMARK
    {
        decoder.reset();
        decoder.processPcm16(pcm, 48000, 1);
    }
}

void PerformanceBenchmark::rendersSpectrumFrame() const
{
    SpectrumScopeCanvas canvas;
    canvas.resize(1200, 400);
    canvas.setFrequencyRange(144.0, 145.0);
    canvas.setDataFrequencyRange(144.0, 145.0);
    QVector<float> bins(1600, 20.0f);
    for (qsizetype index = 0; index < bins.size(); ++index)
    {
        bins[index] = float((index * 37) % 161);
    }
    canvas.updateSpectrum(bins, false);
    QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);

    QBENCHMARK
    {
        image.fill(Qt::transparent);
        QPainter painter(&image);
        canvas.paintRaster(&painter);
    }
}

void PerformanceBenchmark::ingestsUdpDatagrams() const
{
    BenchmarkUdpBase stream;
    QVector<QByteArray> datagrams;
    datagrams.reserve(5000);
    for (int sequence = 0; sequence < 5000; ++sequence)
    {
        control_packet packet{};
        packet.len = CONTROL_SIZE;
        packet.type = 0;
        packet.seq = static_cast<quint16>(sequence);
        datagrams.append(QByteArray(packet.packet, CONTROL_SIZE));
    }

    QBENCHMARK
    {
        stream.resetReceiveState();
        for (const QByteArray& datagram : std::as_const(datagrams))
        {
            stream.dataReceived(datagram);
        }
    }
}


void PerformanceBenchmark::aggregatesTransmitMeterBlocks() const
{
    // Cost of the local processed-input meter presentation: block aggregation
    // plus activity hysteresis, peak hold/decay, and the full-scale hold. One
    // iteration is 50 blocks, i.e. one second of 20 ms capture.
    using namespace sdr9700::audio;
    QVector<TxAudioMeterBlock> blocks;
    blocks.reserve(50);
    for (int index = 0; index < 50; ++index)
    {
        TxAudioMeterBlock block;
        block.peak = float(0.05 + 0.9 * double((index * 17) % 50) / 50.0);
        block.sumSquares = double(block.peak) * block.peak * 0.4 * 960.0;
        block.sampleCount = 960;
        block.fullScaleCount = (index % 25 == 0) ? 3U : 0U;
        block.valid = true;
        blocks.append(block);
    }

    QBENCHMARK
    {
        TxAudioMeterPresentation presentation;
        qint64 nowMs = 0;
        for (const TxAudioMeterBlock& block : std::as_const(blocks))
        {
            presentation.accept(block, nowMs);
            nowMs += 20;
        }
    }
}

void PerformanceBenchmark::convertsAuthorizedAndUnauthorizedTransmitAudio_data() const
{
    QTest::addColumn<bool>("authorized");
    QTest::newRow("unauthorized-meter-only") << false;
    QTest::newRow("authorized-full-conversion") << true;
}

void PerformanceBenchmark::convertsAuthorizedAndUnauthorizedTransmitAudio() const
{
    QFETCH(bool, authorized);
    QAudioFormat input;
    input.setSampleRate(48000);
    input.setChannelCount(2);
    input.setSampleFormat(QAudioFormat::Float);
    QAudioFormat output;
    output.setSampleRate(16000);
    output.setChannelCount(1);
    output.setSampleFormat(QAudioFormat::Int16);
    AudioConverter converter;
    QVERIFY(converter.init(input, LPCM, output, LPCM, 7, 4, false, true));

    audioPacket packet;
    packet.data = QByteArray(input.bytesForDuration(20000), '\0');
    packet.txEncodingState = {1, authorized, false};
    QBENCHMARK
    {
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            QVERIFY(converter.convert(packet));
        }
    }
}

QTEST_MAIN(PerformanceBenchmark)
#include "PerformanceBenchmark.moc"
