#include "AudioHandlerQtOutput.h"

#include <cmath>

namespace
{
constexpr int kChannelLevelLogIntervalMs = 1000;

template <typename Sample, typename Magnitude>
sdr9700::audio::StereoChannelPeaks stereoPeaks(const QByteArray& data, Magnitude magnitude)
{
    if (data.size() < qsizetype(2 * sizeof(Sample)) || data.size() % qsizetype(2 * sizeof(Sample)) != 0)
    {
        return {};
    }

    const auto* samples = reinterpret_cast<const Sample*>(data.constData());
    const qsizetype sampleCount = data.size() / qsizetype(sizeof(Sample));
    float channel0 = 0.0F;
    float channel1 = 0.0F;
    for (qsizetype sample = 0; sample < sampleCount; sample += 2)
    {
        channel0 = qMax(channel0, magnitude(samples[sample]));
        channel1 = qMax(channel1, magnitude(samples[sample + 1]));
    }
    return {channel0, channel1, true};
}
} // namespace

namespace sdr9700::audio
{
StereoChannelPeaks stereoChannelPeaks(const QByteArray& data, QAudioFormat::SampleFormat sampleFormat)
{
    switch (sampleFormat)
    {
    case QAudioFormat::Int16:
        return stereoPeaks<qint16>(data, [](qint16 sample) { return qMin(1.0F, std::abs(float(sample)) / 32767.0F); });
    case QAudioFormat::Int32:
        return stereoPeaks<qint32>(data,
                                   [](qint32 sample) { return qMin(1.0F, std::abs(double(sample)) / 2147483647.0); });
    case QAudioFormat::Float:
        return stereoPeaks<float>(data, [](float sample) { return qMin(1.0F, std::abs(sample)); });
    case QAudioFormat::UInt8:
        return stereoPeaks<quint8>(data,
                                   [](quint8 sample) { return qMin(1.0F, std::abs(float(sample) - 128.0F) / 127.0F); });
    default:
        return {};
    }
}
} // namespace sdr9700::audio

bool AudioHandlerQtOutput::openDevice() noexcept
{
    audioOutput = new QAudioSink(deviceInfo, nativeFormat, this);
    connect(audioOutput, &QAudioSink::stateChanged, this, &AudioHandlerQtOutput::stateChanged);

    connect(converter, &AudioConverter::converted, this, &AudioHandlerQtOutput::onConverted);

    audioOutput->setBufferSize(
        nativeFormat.bytesForDuration(sdr9700::audio::outputBufferDurationMs(setupData.latency) * 1000LL));

    audioDevice = audioOutput->start();
    if (!audioDevice)
    {
        delete audioOutput;
        audioOutput = nullptr;
        return false;
    }

    // Pre-fill half the configured latency plus one 20 ms packet of silence
    // while reserving room for the first real packet from the network.
    {
        const int prefillBytes =
            sdr9700::audio::outputPrefillBytes(nativeFormat, audioOutput->bufferSize(), setupData.latency);
        // Unsigned 8-bit PCM is centered at 0x80; all other supported Qt
        // formats represent silence with zero bits.
        const char silenceByte = nativeFormat.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0';
        QByteArray silence(prefillBytes, silenceByte);
        audioDevice->write(silence.constData(), silence.size());
    }

    qInfo(logAudio()).noquote().nospace()
        << "Connected to Qt audio output device=" << deviceInfo.description()
        << " requestedBufferMs=" << sdr9700::audio::outputBufferDurationMs(setupData.latency)
        << " bufferMs=" << nativeFormat.durationForBytes(audioOutput->bufferSize()) / 1000 << " prefillMs="
        << nativeFormat.durationForBytes(
               sdr9700::audio::outputPrefillBytes(nativeFormat, audioOutput->bufferSize(), setupData.latency)) /
               1000;
    return true;
}

void AudioHandlerQtOutput::closeDevice() noexcept
{
    if (audioOutput)
    {
        if (audioOutput->state() != QAudio::StoppedState)
        {
            audioOutput->stop();
        }
        // dispose() marshals closeDevice() to this object's audio thread.
        // Destroy the native sink there before UdpAudio stops that thread;
        // deleteLater() can otherwise strand CoreAudio teardown on a stopped
        // event loop during a live device change.
        delete audioOutput;
        audioOutput = nullptr;
    }
    audioDevice = nullptr;
    m_pendingOutput.clear();
    m_pendingOutputOffset = 0;
}

void AudioHandlerQtOutput::incomingAudio(audioPacket packet)
{
    if (!audioDevice || packet.data.isEmpty())
    {
        return;
    }
    packet.volume = volume;
    queueForConversion(std::move(packet));
}

void AudioHandlerQtOutput::onConverted(const audioPacket& audio)
{
    if (!audioOutput || !audioDevice || audio.data.isEmpty())
    {
        return;
    }
    const qint64 packetAgeMs = audio.createdAtMs > 0 ? audioMonotonicTimestampMs() - audio.createdAtMs : 0;
    if (packetAgeMs > setupData.latency * 1.5)
    {
        return;
    }
    if (nativeFormat.channelCount() == 2 && logAudio().isDebugEnabled() &&
        (!m_channelLevelLogTimer.isValid() || m_channelLevelLogTimer.elapsed() >= kChannelLevelLogIntervalMs))
    {
        m_channelLevelLogTimer.restart();
        const sdr9700::audio::StereoChannelPeaks peaks =
            sdr9700::audio::stereoChannelPeaks(audio.data, nativeFormat.sampleFormat());
        if (peaks.valid)
        {
            qDebug(logAudio()).noquote().nospace()
                << "RX stereo peaks channel0=" << peaks.channel0 << " channel1=" << peaks.channel1;
        }
    }
    writeToOutputDevice(audio.data, audio.seq, audio.amplitudePeak, audio.amplitudeRMS);
}

void AudioHandlerQtOutput::writeToOutputDevice(const QByteArray& data, quint32 seq, float amplitudePeak,
                                               float amplitudeRms)
{
    Q_UNUSED(seq);
    if (!audioOutput || !audioDevice)
    {
        return;
    }

    // Recover from underrun: re-prime the buffer with silence so the device
    // has a cushion before real audio resumes, preventing click cascades.
    if (isUnderrun.load(std::memory_order_relaxed))
    {
        const int prefillBytes =
            sdr9700::audio::outputPrefillBytes(nativeFormat, audioOutput->bufferSize(), setupData.latency);
        const int freeBytes = static_cast<int>(audioOutput->bytesFree());
        const int silenceBytes = qMin(prefillBytes, freeBytes);
        if (silenceBytes > 0)
        {
            const char silenceByte = nativeFormat.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0';
            QByteArray silence(silenceBytes, silenceByte);
            audioDevice->write(silence.constData(), silence.size());
        }
        isUnderrun.store(false, std::memory_order_relaxed);
        if (underTimer && underTimer->isActive())
        {
            underTimer->stop();
        }
    }

    qint64 buffered = audioOutput->bufferSize() - audioOutput->bytesFree();
    int devLatencyMs = static_cast<int>(nativeFormat.durationForBytes(buffered) / 1000);
    int pipelineMs = lastReceived.isValid() ? static_cast<int>(lastReceived.elapsed()) : 0;
    int newLatency = pipelineMs + devLatencyMs;
    int prev = currentLatency.load(std::memory_order_relaxed);
    currentLatency.store(static_cast<int>(prev * 0.8 + newLatency * 0.2), std::memory_order_relaxed);

    if (m_pendingOutputOffset > 0)
    {
        m_pendingOutput.remove(0, m_pendingOutputOffset);
        m_pendingOutputOffset = 0;
    }
    m_pendingOutput.append(data);

    // QAudioSink's push device may accept only part of a frame. Retain that
    // tail for the next 20 ms callback instead of discarding it. Cap the local
    // backlog at two sink buffers so a blocked device cannot grow memory or
    // turn a transient stall into seconds of delayed playback.
    const qsizetype maxPendingBytes = qMax<qsizetype>(audioOutput->bufferSize() * 2, data.size());
    if (m_pendingOutput.size() > maxPendingBytes)
    {
        qsizetype removeBytes = m_pendingOutput.size() - maxPendingBytes;
        const int bytesPerFrame = qMax(1, nativeFormat.bytesPerFrame());
        removeBytes = ((removeBytes + bytesPerFrame - 1) / bytesPerFrame) * bytesPerFrame;
        m_pendingOutput.remove(0, qMin(removeBytes, m_pendingOutput.size()));
        isOverrun.store(true, std::memory_order_relaxed);
    }
    drainPendingOutput();

    lastReceived.restart();
    amplitude.store(amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(this->amplitudePeak(), static_cast<quint16>(amplitudeRms * 255.0f), setupData.latency,
                    currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

void AudioHandlerQtOutput::drainPendingOutput()
{
    if (!audioOutput || !audioDevice)
    {
        return;
    }

    while (m_pendingOutputOffset < m_pendingOutput.size())
    {
        const qint64 freeBytes = audioOutput->bytesFree();
        if (freeBytes <= 0)
        {
            return;
        }
        const qint64 remaining = m_pendingOutput.size() - m_pendingOutputOffset;
        const qint64 requested = qMin(freeBytes, remaining);
        const qint64 written = audioDevice->write(m_pendingOutput.constData() + m_pendingOutputOffset, requested);
        if (written <= 0)
        {
            return;
        }
        m_pendingOutputOffset += written;
    }

    m_pendingOutput.clear();
    m_pendingOutputOffset = 0;
    isOverrun.store(false, std::memory_order_relaxed);
}

QAudioFormat AudioHandlerQtOutput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool AudioHandlerQtOutput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
