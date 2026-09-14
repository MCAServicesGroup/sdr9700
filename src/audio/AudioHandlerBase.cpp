#include "AudioHandlerBase.h"

#include <QSemaphore>
#include <memory>

namespace
{
constexpr int kShutdownWaitMs = 500;
constexpr int kMaxRxConversionBacklog = 4;
constexpr int kMaxTxConversionBacklog = 8;

qreal localAudioGainFromSlider(quint8 volumeIdx)
{
    const qreal normalized = static_cast<qreal>(volumeIdx) / 255.0;
    return normalized * normalized * normalized;
}
} // namespace

AudioHandlerBase::AudioHandlerBase(QObject* parent) : QObject(parent) {}

AudioHandlerBase::~AudioHandlerBase()
{
    // Safety net: if dispose() was never called, stop the converter thread
    // now so QThread::~QThread() won't abort.
    stopConverterThread(QStringLiteral("Audio"));
}

void AudioHandlerBase::stopConverterThread(const QString& roleName)
{
    if (!converterThread)
    {
        converter = nullptr;
        return;
    }

    qDebug(logAudio()).noquote().nospace() << "[SHUTDOWN] converterThread->quit role=" << roleName;
    converterThread->quit();
    if (!converterThread->wait(kShutdownWaitMs))
    {
        qWarning(logAudio()).noquote().nospace()
            << "[SHUTDOWN] converterThread did not stop timeoutMs=" << kShutdownWaitMs << " role=" << roleName
            << "; requesting interruption";
        converterThread->requestInterruption();
        converterThread->quit();
        if (!converterThread->wait(kShutdownWaitMs))
        {
            qCritical(logAudio()).noquote().nospace()
                << "[SHUTDOWN] converterThread did not stop after bounded shutdown role=" << roleName;
            converterThread->setParent(nullptr);
            connect(converterThread, &QThread::finished, converterThread, &QObject::deleteLater, Qt::DirectConnection);
            converterThread = nullptr;
            converter = nullptr;
            return;
        }
    }
    qDebug(logAudio()).noquote().nospace() << "[SHUTDOWN] converterThread done role=" << roleName;
    delete converterThread;
    converterThread = nullptr;
    converter = nullptr;
}

void AudioHandlerBase::dispose()
{
    qDebug(logAudio()).noquote().nospace()
        << "[SHUTDOWN] dispose enter role=" << role() << " onCorrectThread=" << (QThread::currentThread() == thread());
    // Run disposal on this object's thread to avoid races with audio callbacks.
    if (QThread::currentThread() != thread())
    {
        qDebug(logAudio()).noquote() << "[SHUTDOWN] dispose() marshaling to audio thread";
        auto disposeDone = std::make_shared<QSemaphore>();
        const bool queued = QMetaObject::invokeMethod(
            this,
            [this, disposeDone]()
            {
                dispose();
                disposeDone->release();
            },
            Qt::QueuedConnection);
        if (!queued || !disposeDone->tryAcquire(1, kShutdownWaitMs))
        {
            qWarning(logAudio()).noquote().nospace()
                << "[SHUTDOWN] dispose did not finish timeoutMs=" << kShutdownWaitMs << " role=" << role();
        }
        return;
    }

    bool expected = false;
    if (!disposed.compare_exchange_strong(expected, true))
    {
        qDebug(logAudio()).noquote() << "[SHUTDOWN] dispose() already disposed, returning";
        return;
    }

    // Stop the converter thread before taking devMutex: the converter thread
    // may need devMutex to process its final work, so joining while holding
    // the lock would deadlock.
    if (converterThread)
    {
        disconnect(this, &AudioHandlerBase::sendToConverter, converter, &AudioConverter::process);
        disconnect(converter, &AudioConverter::converted, nullptr, nullptr);
        disconnect(converter, &AudioConverter::conversionCycleFinished, this,
                   &AudioHandlerBase::onConversionCycleFinished);
        m_conversionQueue.clear();
        m_conversionBusy = false;
        stopConverterThread(role());
    }

    qDebug(logAudio()).noquote().nospace() << "[SHUTDOWN] dispose locking devMutex role=" << role();
    QMutexLocker lock(&devMutex);
    qDebug(logAudio()).noquote().nospace() << "[SHUTDOWN] dispose calling closeDevice role=" << role();
    closeDevice();
    qDebug(logAudio()).noquote().nospace() << "[SHUTDOWN] dispose closeDevice done role=" << role();

    if (underTimer)
    {
        underTimer->stop();
        underTimer->deleteLater();
        underTimer = nullptr;
    }

    qDebug(logAudio()).noquote().nospace() << "[SHUTDOWN] dispose complete role=" << role();
}

void AudioHandlerBase::reportError(const QString& msg)
{
    qCritical(logAudio()).noquote() << role() << msg;
}

bool AudioHandlerBase::negotiateFormat(int minSampleRate)
{
    const QAudioFormat preferred = getNativeFormat();

    if (preferred.channelCount() < 1 || preferred.sampleRate() < 1)
    {
        reportError("Cannot initialize audio device, preferred format is invalid");
        return false;
    }

    QList<int> channelCandidates;
    const int preferredChannels = qBound(1, preferred.channelCount(), 2);
    if (!setupData.isinput && setupData.playbackChannels > 0)
    {
        channelCandidates.append(qBound(1, int(setupData.playbackChannels), 2));
    }
    else if (radioFormat.channelCount() == 2)
    {
        channelCandidates.append(2);
    }
    if (!channelCandidates.contains(preferredChannels))
    {
        channelCandidates.append(preferredChannels);
    }
    if (!channelCandidates.contains(1))
    {
        channelCandidates.append(1);
    }

    QList<int> sampleRateCandidates;
    if (preferred.sampleRate() < minSampleRate)
    {
        sampleRateCandidates.append(minSampleRate);
    }
    sampleRateCandidates.append(preferred.sampleRate());

    QList<QAudioFormat::SampleFormat> formatCandidates;
    if (preferred.sampleFormat() != QAudioFormat::UInt8 && preferred.sampleFormat() != QAudioFormat::Unknown)
    {
        formatCandidates.append(preferred.sampleFormat());
    }
    for (const QAudioFormat::SampleFormat format :
         {QAudioFormat::Int16, QAudioFormat::Float, QAudioFormat::Int32, QAudioFormat::UInt8})
    {
        if (!formatCandidates.contains(format))
        {
            formatCandidates.append(format);
        }
    }

    for (int channels : channelCandidates)
    {
        for (int sampleRate : sampleRateCandidates)
        {
            for (QAudioFormat::SampleFormat sampleFormat : formatCandidates)
            {
                QAudioFormat candidate = preferred;
                candidate.setChannelCount(channels);
                candidate.setSampleRate(sampleRate);
                candidate.setSampleFormat(sampleFormat);
                if (isFormatSupported(candidate))
                {
                    nativeFormat = candidate;
                    qDebug(logAudio()).noquote().nospace()
                        << role() << " selected format channels=" << nativeFormat.channelCount()
                        << " sampleRate=" << nativeFormat.sampleRate() << " format=" << nativeFormat.sampleFormat();
                    return true;
                }
            }
        }
    }

    reportError("Cannot initialize audio device, no supported PCM format was found");
    nativeFormat = {};
    return false;
}

bool AudioHandlerBase::init(const audioSetup& setup)
{
    QMutexLocker lock(&devMutex);
    if (initialized)
    {
        return true;
    }

    setupData = setup;
    radioFormat = toQAudioFormat(setup.codec, setup.sampleRate);

    codec = codecType::LPCM;
    if (setup.codec == 0x01 || setup.codec == 0x20)
    {
        codec = codecType::PCMU;
    }
    else if (setup.codec == 0x40 || setup.codec == 0x41)
    {
        codec = codecType::OPUS;
    }
    if (setup.port.isNull() && setup.port.description().isEmpty() && setup.portInt == -1)
    {
        reportError("Audio device is NULL, check device selection in settings");
        emit initFailed();
        return false;
    }
    else
    {
        deviceInfo = setup.port;
    }

    if (!negotiateFormat(48000))
    {
        emit initFailed();
        return false;
    }

    converter = new AudioConverter();
    const QAudioFormat converterInputFormat = setup.isinput ? nativeFormat : radioFormat;
    const codecType converterInputCodec = setup.isinput ? codecType::LPCM : codec;
    const QAudioFormat converterOutputFormat = setup.isinput ? radioFormat : nativeFormat;
    const codecType converterOutputCodec = setup.isinput ? codec : codecType::LPCM;
    // Initialize before moving the converter to its worker thread. The previous
    // queued initialization could race the first QAudio readyRead callback and
    // silently drop the beginning of a transmission.
    if (!converter->init(converterInputFormat, converterInputCodec, converterOutputFormat, converterOutputCodec, 7,
                         setup.resampleQuality))
    {
        delete converter;
        converter = nullptr;
        reportError("Failed to initialize audio converter");
        emit initFailed();
        return false;
    }
    converterThread = new QThread(this);
    converterThread->setObjectName(role() == "Input" ? "audioConvIn()" : "audioConvOut()");
    converter->moveToThread(converterThread);

    connect(this, &AudioHandlerBase::sendToConverter, converter, &AudioConverter::process);
    connect(converter, &AudioConverter::conversionCycleFinished, this, &AudioHandlerBase::onConversionCycleFinished);
    connect(converterThread, &QThread::finished, converter, &QObject::deleteLater);
    connect(converter, &AudioConverter::initFailed, this,
            [this](const QString& message)
            {
                reportError(message);
                emit initFailed();
            });

    // HighPriority gives audio prompt service without the scheduler starvation
    // risk of TimeCriticalPriority on a busy single/dual-core Linux host.
    converterThread->start(QThread::HighPriority);

    underTimer = new QTimer(this);
    underTimer->setSingleShot(true);
    connect(underTimer, &QTimer::timeout, this, &AudioHandlerBase::clearUnderrun);

    setVolume(setup.localAFgain);

    if (!openDevice())
    {
        reportError("Failed to open device");
        closeDevice();
        if (underTimer)
        {
            underTimer->stop();
            underTimer->deleteLater();
            underTimer = nullptr;
        }
        if (converterThread)
        {
            stopConverterThread(role());
        }
        emit initFailed();
        return false;
    }

    initialized = true;
    qInfo(logAudio()).noquote() << role() << "thread id" << QThread::currentThreadId();
    return true;
}

void AudioHandlerBase::start()
{
    QMutexLocker lock(&devMutex);
}

void AudioHandlerBase::stop()
{
    QMutexLocker lock(&devMutex);
    closeDevice();
}

void AudioHandlerBase::setVolume(quint8 volumeIdx)
{
    volume = localAudioGainFromSlider(volumeIdx);
}

void AudioHandlerBase::changeLatency(quint16 newLatencyMs)
{
    QMutexLocker lock(&devMutex);
    setupData.latency = newLatencyMs;
}

void AudioHandlerBase::stateChanged(QAudio::State state)
{
    switch (state)
    {
    case QAudio::IdleState:
        isUnderrun.store(true, std::memory_order_relaxed);
        if (underTimer && underTimer->isActive())
        {
            underTimer->stop();
        }
        break;
    case QAudio::ActiveState:
        if (underTimer && !underTimer->isActive())
        {
            underTimer->start(500);
        }
        break;
    default:
        break;
    }
}

void AudioHandlerBase::clearUnderrun()
{
    isUnderrun.store(false, std::memory_order_relaxed);
}

void AudioHandlerBase::queueForConversion(audioPacket audio)
{
    if (!converter || disposed.load(std::memory_order_acquire))
    {
        return;
    }

    if (!m_conversionBusy)
    {
        m_conversionBusy = true;
        emit sendToConverter(std::move(audio));
        return;
    }

    const int limit = setupData.isinput ? kMaxTxConversionBacklog : kMaxRxConversionBacklog;
    if (m_conversionQueue.size() >= limit)
    {
        // Keep bounded latency under overload. Dropping the oldest not-yet-
        // converted frame preserves the order of all surviving frames and is
        // preferable to transmitting/playing increasingly stale audio.
        m_conversionQueue.dequeue();
        isOverrun.store(true, std::memory_order_relaxed);
    }
    m_conversionQueue.enqueue(std::move(audio));
}

void AudioHandlerBase::onConversionCycleFinished()
{
    if (disposed.load(std::memory_order_acquire) || !converter)
    {
        m_conversionQueue.clear();
        m_conversionBusy = false;
        return;
    }
    if (m_conversionQueue.isEmpty())
    {
        m_conversionBusy = false;
        return;
    }

    emit sendToConverter(m_conversionQueue.dequeue());
}
