#pragma once

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QAudio>
#include <QAudioFormat>
#include <QIODevice>
#include <QQueue>
#include <atomic>
#include <cstring>

#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>

#include "PacketTypes.h"
#include "AudioConverter.h"
#include "LogCategories.h"

class AudioHandlerBase : public QObject
{
    Q_OBJECT
  public:
    explicit AudioHandlerBase(QObject* parent = nullptr);
    ~AudioHandlerBase() override;

    int latencyMs() const noexcept { return static_cast<int>(currentLatency.load(std::memory_order_relaxed)); }
    quint16 amplitudePeak() const noexcept
    {
        return static_cast<quint16>(amplitude.load(std::memory_order_relaxed) * 255.0f);
    }

    void dispose();

  signals:
    void initFailed();
    void haveAudioData(const audioPacket& data);
    void haveLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 configuredLatency, quint16 measuredLatency,
                    bool underrun, bool overrun);
    void sendToConverter(audioPacket audio);

  public slots:
    bool init(const audioSetup& setup);
    void setVolume(quint8 volumeIdx);
    void changeLatency(quint16 newLatencyMs);
    virtual void incomingAudio(audioPacket) {}
    void stateChanged(QAudio::State state);
    void clearUnderrun();
    void onConversionCycleFinished();

  protected:
    virtual bool openDevice() noexcept = 0;

    virtual void closeDevice() = 0;
    virtual QString role() const = 0;

    bool negotiateFormat(int minSampleRate = 48000);
    void stopConverterThread(const QString& roleName);

    virtual QAudioFormat getNativeFormat() = 0;
    virtual bool isFormatSupported(QAudioFormat format) = 0;

    void reportError(const QString& msg);
    void queueForConversion(audioPacket audio);

  protected:
    audioSetup setupData{};
    QAudioFormat radioFormat;
    QAudioFormat nativeFormat;

    QAudioDevice deviceInfo;

    AudioConverter* converter{nullptr};
    QThread* converterThread{nullptr};
    QTimer* underTimer{nullptr};

    codecType codec{codecType::LPCM};

    std::atomic<int> currentLatency{0};
    std::atomic<float> amplitude{0.0f};
    std::atomic<bool> isUnderrun{false};
    std::atomic<bool> isOverrun{false};

    QElapsedTimer lastReceived;

    qreal volume{1.0};

    bool initialized{false};

    QMutex devMutex;
    std::atomic<bool> disposed{false};
    audioPacket tempBuf;
    QQueue<audioPacket> m_conversionQueue;
    bool m_conversionBusy{false};
};
