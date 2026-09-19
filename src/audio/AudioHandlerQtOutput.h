#pragma once
#include "AudioHandlerBase.h"

#include <algorithm>

namespace sdr9700::audio
{
struct StereoChannelPeaks
{
    float channel0{0.0F};
    float channel1{0.0F};
    bool valid{false};
};

constexpr int kOutputPrefillHeadroomMs = 20;
constexpr int kOutputBufferCapacityHeadroomMs = 40;

inline int outputBufferDurationMs(int configuredLatencyMs)
{
    return std::max(0, configuredLatencyMs) + kOutputBufferCapacityHeadroomMs;
}

inline int outputPrefillBytes(const QAudioFormat& format, int bufferSize, int configuredLatencyMs)
{
    const int boundedBufferSize = std::max(0, bufferSize);
    const int boundedLatencyMs = std::max(0, configuredLatencyMs);
    const int prefillDurationMs = std::min(boundedLatencyMs, boundedLatencyMs / 2 + kOutputPrefillHeadroomMs);
    const qint64 targetBytes = format.bytesForDuration(prefillDurationMs * 1000LL);
    const qint64 reservedBytes = format.bytesForDuration(kOutputPrefillHeadroomMs * 1000LL);
    const int bytesPerFrame = std::max(1, format.bytesPerFrame());
    const qint64 availablePrefillBytes = std::max<qint64>(0, boundedBufferSize - reservedBytes);
    const int boundedTargetBytes = static_cast<int>(std::min(availablePrefillBytes, std::max<qint64>(0, targetBytes)));
    return boundedTargetBytes - boundedTargetBytes % bytesPerFrame;
}

StereoChannelPeaks stereoChannelPeaks(const QByteArray& data, QAudioFormat::SampleFormat sampleFormat);
} // namespace sdr9700::audio

class AudioHandlerQtOutput : public AudioHandlerBase
{
    Q_OBJECT

  public:
    explicit AudioHandlerQtOutput(QObject* parent = nullptr) : AudioHandlerBase(parent) {}
    ~AudioHandlerQtOutput() override { dispose(); }
    QString role() const override { return QStringLiteral("Output"); }

  public slots:
    void incomingAudio(audioPacket packet) override;

  protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    QAudioFormat getNativeFormat() override;
    bool isFormatSupported(QAudioFormat f) override;

  private:
    void writeToOutputDevice(const QByteArray& data, float amplitudePeak, float amplitudeRms);
    void drainPendingOutput();

    QAudioSink* audioOutput{nullptr};

    QIODevice* audioDevice{nullptr};
    QByteArray m_pendingOutput;
    qsizetype m_pendingOutputOffset{0};
    QElapsedTimer m_channelLevelLogTimer;

  private slots:
    void onConverted(const audioPacket& audio);
};
