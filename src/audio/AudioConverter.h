#pragma once

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QDebug>
#include <QAudioFormat>

#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioSink>
#include <chrono>

#include <opus.h>
#include <Eigen/Eigen>
#include <Eigen/Dense>

#include "Types.h"
#include "TxAudioMeterPolicy.h"

struct SpeexResamplerState_;
using SpeexResamplerState = struct SpeexResamplerState_;

#include "PacketTypes.h"

struct audioPacket
{
    quint32 seq = 0;
    // Packet age is used to discard stale playback data. steady_clock is
    // process-wide and monotonic, so this remains valid across midnight and
    // wall-clock/NTP adjustments while packets cross audio threads.
    qint64 createdAtMs = 0;
    quint16 sent = 0;
    QByteArray data;
    quint8 guid[GUIDLEN]{};
    float amplitudePeak = 0.0f;
    float amplitudeRMS = 0.0f;
    qreal volume = 1.0;
    // Local processed-input measurement, populated only on the capture path.
    // The receive path leaves this invalid and continues to use amplitudePeak
    // and amplitudeRMS through the shared haveLevels signal.
    sdr9700::audio::TxAudioMeterBlock inputMeter;
};

struct audioSetup
{
    audioType type{qtAudio};
    QString name;
    quint16 latency{0};
    quint8 codec{0};
    bool ulaw{false};
    bool isinput{false};
    quint32 sampleRate{0};
    QAudioDevice port;
    int portInt{0};
    quint8 resampleQuality{0};
    quint8 localAFgain{0};
    // Zero selects the output device's normal channel negotiation. Receive
    // audio can request one local channel while retaining a stereo radio codec.
    quint8 playbackChannels{0};
    // Duration, in milliseconds, represented by one converted audio block.
    // The default matches the IC-9700 LAN audio packet cadence.
    quint16 blockSize{20};
    quint8 guid[GUIDLEN]{};
};

class AudioConverter : public QObject
{
    Q_OBJECT

  public:
    explicit AudioConverter(QObject* parent = nullptr);
    ~AudioConverter();

  public slots:
    bool init(QAudioFormat inputFormat, codecType inputCodec, QAudioFormat outputFormat, codecType outputCodec,
              quint8 encoderComplexity, quint8 converterResampleQuality, bool stereoToDualMono = false,
              bool measureLocalInputMeter = false);
    bool convert(audioPacket audio);
    void process(audioPacket audio);

  signals:
    void converted(audioPacket audio);
    void conversionCycleFinished();
    void conversionFailed();
    void initFailed(QString message);

  protected:
    QAudioFormat inFormat;
    QAudioFormat outFormat;
    OpusEncoder* opusEncoder = nullptr;
    OpusDecoder* opusDecoder = nullptr;
    SpeexResamplerState* resampler = nullptr;
    quint8 opusComplexity{0};
    quint8 resampleQuality = 4;
    double resampleRatio = 1.0;
    quint32 lastAudioSequence{0};
    codecType inCodec{LPCM};
    codecType outCodec{LPCM};
    bool initialized = false;
    bool mixStereoToDualMono = false;
    bool measureInputMeter = false;
    QByteArray scratchIn;
    QByteArray scratchOut;
    Eigen::VectorXf scratchF;
    // Reusable conversion buffers for the per-packet audio hot path. These
    // replace short-lived local Eigen vectors but intentionally preserve the
    // existing conversion formulas and ordering. Backout point for audio
    // regressions: remove these members and restore local vectors in convert().
    Eigen::VectorXf scratchSamples;
    Eigen::VectorXf scratchChannelMix;
    // Reusable output conversion buffers. These only replace per-packet local
    // Eigen temporaries in the final float-to-device-format stage; if TX/RX
    // audio quality regresses, the backout is to remove these members and
    // restore the local vectors in the non-Opus output branch of convert().
    Eigen::VectorXf scratchOutputFloat;
    Eigen::Matrix<quint8, Eigen::Dynamic, 1> scratchOutputU8;
    Eigen::Matrix<qint16, Eigen::Dynamic, 1> scratchOutputI16;
    Eigen::Matrix<qint32, Eigen::Dynamic, 1> scratchOutputI32;

    void releaseCodecState();
};

inline qint64 audioMonotonicTimestampMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}


// Eigen vectors cross thread boundaries in queued Qt signal delivery.
Q_DECLARE_METATYPE(Eigen::VectorXf)

static inline QAudioFormat toQAudioFormat(quint8 codec, quint32 sampleRate)
{
    QAudioFormat format;

    // IC-9700 LAN codec ids:
    // 0x01 uLaw 1ch 8bit, 0x02 PCM 1ch 8bit, 0x04 PCM 1ch 16bit
    // 0x08 PCM 2ch 8bit, 0x10 PCM 2ch 16bit, 0x20 uLaw 2ch 8bit
    // 0x40 Opus 1ch, 0x41 Opus 2ch
    format.setSampleRate(sampleRate);

    if (codec == 0x01 || codec == 0x20)
    {
        format.setSampleFormat(QAudioFormat::Float);
    }

    if (codec == 0x02 || codec == 0x08)
    {
        format.setSampleFormat(QAudioFormat::UInt8);
    }
    if (codec == 0x08 || codec == 0x10 || codec == 0x20 || codec == 0x41)
    {
        format.setChannelCount(2);
    }
    else
    {
        format.setChannelCount(1);
    }

    if (codec == 0x04 || codec == 0x10)
    {
        format.setSampleFormat(QAudioFormat::Int16);
    }

    if (codec == 0x40 || codec == 0x41)
    {
        format.setSampleFormat(QAudioFormat::Float);
    }
    return format;
}
