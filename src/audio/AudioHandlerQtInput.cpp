#include "AudioHandlerQtInput.h"

bool AudioHandlerQtInput::openDevice() noexcept
{
    audioInput = new QAudioSource(deviceInfo, nativeFormat, this);
    connect(audioInput, &QAudioSource::stateChanged, this, &AudioHandlerQtInput::stateChanged);

    connect(converter, &AudioConverter::converted, this, &AudioHandlerQtInput::onConverted);

    audioInput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));

    audioDevice = audioInput->start();
    if (!audioDevice)
    {
        delete audioInput;
        audioInput = nullptr;
        return false;
    }

    connect(audioDevice, &QIODevice::readyRead, this, &AudioHandlerQtInput::onReadyRead, Qt::UniqueConnection);
    tempBuf.data.reserve(nativeFormat.bytesForDuration(setupData.blockSize * 2000));
    qInfo(logAudio()).noquote() << "Connected to Qt audio input device" << deviceInfo.description();
    return true;
}

void AudioHandlerQtInput::closeDevice() noexcept
{
    if (audioInput)
    {
        if (audioInput->state() != QAudio::StoppedState)
        {
            audioInput->stop();
        }
        // Keep native device destruction on the live audio thread. The caller
        // stops that thread immediately after dispose(), so deferred deletion
        // is not guaranteed to run.
        delete audioInput;
        audioInput = nullptr;
    }
    audioDevice = nullptr;
    tempBuf.data.clear();
    m_bufferReadOffset = 0;
}

void AudioHandlerQtInput::onReadyRead()
{
    if (!audioDevice)
    {
        return;
    }
    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    if (bytesPerBlock <= 0)
    {
        return;
    }

    const qint64 availableBytes = audioDevice->bytesAvailable();
    if (availableBytes <= 0)
    {
        return;
    }
    const qsizetype previousSize = tempBuf.data.size();
    tempBuf.data.resize(previousSize + availableBytes);
    const qint64 bytesRead = audioDevice->read(tempBuf.data.data() + previousSize, availableBytes);
    if (bytesRead <= 0)
    {
        tempBuf.data.resize(previousSize);
        return;
    }
    tempBuf.data.resize(previousSize + bytesRead);

    while (tempBuf.data.size() - m_bufferReadOffset >= bytesPerBlock)
    {
        audioPacket pkt;
        pkt.createdAtMs = audioMonotonicTimestampMs();
        pkt.sent = 0;
        pkt.volume = volume;
        memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data = QByteArray(tempBuf.data.constData() + m_bufferReadOffset, bytesPerBlock);
        m_bufferReadOffset += bytesPerBlock;
        queueForConversion(std::move(pkt));
    }

    if (m_bufferReadOffset > 0 && m_bufferReadOffset >= tempBuf.data.size() / 2)
    {
        tempBuf.data.remove(0, m_bufferReadOffset);
        m_bufferReadOffset = 0;
    }
}

void AudioHandlerQtInput::onConverted(const audioPacket& audio)
{
    if (audio.data.isEmpty())
    {
        return;
    }

    emit haveAudioData(audio);

    if (lastReceived.isValid() && lastReceived.elapsed() > 100)
    {
        qDebug(logAudio()).noquote() << role() << "Time since last audio packet" << lastReceived.elapsed()
                                     << "Expected around" << setupData.blockSize;
    }
    if (!lastReceived.isValid())
    {
        lastReceived.start();
    }
    else
    {
        lastReceived.restart();
    }

    amplitude.store(audio.amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), static_cast<quint16>(audio.amplitudeRMS * 255.0f), setupData.latency,
                    static_cast<quint16>(latencyMs()), isUnderrun.load(), isOverrun.load());
}

QAudioFormat AudioHandlerQtInput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool AudioHandlerQtInput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
