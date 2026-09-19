#include "AudioHandlerQtInput.h"

#include <cstring>

bool AudioHandlerQtInput::openDevice() noexcept
{
    // Install the complete state before start() can synchronously expose the
    // first readyRead callback. Device replacement therefore begins in the
    // current PTT/DTMF epoch rather than a connection-time default.
    updateTxEncodingState(setupData.initialTxEncodingState);
    audioInput = new QAudioSource(deviceInfo, nativeFormat, this);
    connect(audioInput, &QAudioSource::stateChanged, this, &AudioHandlerQtInput::onInputStateChanged);

    connect(converter, &AudioConverter::converted, this, &AudioHandlerQtInput::onConverted);
    connect(converter, &AudioConverter::conversionFailed, this, &AudioHandlerQtInput::invalidateTransmitMeter);

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
        pkt.txEncodingState = m_txEncodingState;
        std::memcpy(&pkt.guid, setupData.guid, GUIDLEN);
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

    // The transmit meter consumes the typed block measured after gain and
    // channel mixing. The legacy 8-bit amplitude fields are not published from
    // the capture path: a quantised 0-255 level has a -48.1 dBFS lowest nonzero
    // step, which cannot support a truthful dBFS display.
    emit haveTxMeter(audio.inputMeter, setupData.latency, static_cast<quint16>(latencyMs()), isUnderrun.load(),
                     isOverrun.load());

    if (!audio.data.isEmpty())
    {
        emit haveAudioData(audio);
    }
}

void AudioHandlerQtInput::updateTxEncodingState(sdr9700::audio::TxEncodingState state)
{
    using sdr9700::audio::TxEncodingUpdate;
    const TxEncodingUpdate update = sdr9700::audio::classifyTxEncodingUpdate(m_txEncodingState, state);
    if (update != TxEncodingUpdate::Accepted)
    {
        return;
    }

    m_txEncodingState = state;
    // The handler owns this queue. Clear it in the same ordered worker-thread
    // operation that installs the new epoch; the one in-flight block is
    // rejected downstream if it completes after the transition.
    m_conversionQueue.clear();
}

void AudioHandlerQtInput::onInputStateChanged(QAudio::State state)
{
    stateChanged(state);
    if (state == QAudio::StoppedState && audioInput && audioInput->error() != QAudio::NoError &&
        !disposed.load(std::memory_order_acquire))
    {
        invalidateTransmitMeter();
    }
}

void AudioHandlerQtInput::invalidateTransmitMeter()
{
    emit haveTxMeter({}, setupData.latency, static_cast<quint16>(latencyMs()), isUnderrun.load(), isOverrun.load());
}

QAudioFormat AudioHandlerQtInput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool AudioHandlerQtInput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
