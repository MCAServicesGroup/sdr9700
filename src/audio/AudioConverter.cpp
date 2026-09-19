#include "AudioConverter.h"
#include "LogCategories.h"

#include <algorithm>
#include <cmath>
#include <speex/speex_resampler.h>
#include <cstring>

namespace
{
using MapFUn = Eigen::Map<Eigen::VectorXf, Eigen::Unaligned>;
using MapI16Un = Eigen::Map<Eigen::Matrix<qint16, Eigen::Dynamic, 1>, Eigen::Unaligned>;
using MapI32Un = Eigen::Map<Eigen::Matrix<qint32, Eigen::Dynamic, 1>, Eigen::Unaligned>;
using MapU8Un = Eigen::Map<Eigen::Matrix<quint8, Eigen::Dynamic, 1>, Eigen::Unaligned>;

bool byteCountMatchesSampleSize(const QByteArray& data, int sampleBytes)
{
    return sampleBytes > 0 && data.size() % sampleBytes == 0;
}

bool sampleCountMatchesChannels(qsizetype sampleCount, int channelCount)
{
    return channelCount > 0 && sampleCount % channelCount == 0;
}
} // namespace

AudioConverter::AudioConverter(QObject* parent) : QObject(parent) {}

bool AudioConverter::init(QAudioFormat inputFormat, codecType inputCodec, QAudioFormat outputFormat,
                          codecType outputCodec, quint8 encoderComplexity, quint8 converterResampleQuality,
                          bool stereoToDualMono, bool measureLocalInputMeter)
{

    releaseCodecState();
    inFormat = inputFormat;
    inCodec = inputCodec;
    outFormat = outputFormat;
    outCodec = outputCodec;
    opusComplexity = encoderComplexity;
    resampleQuality = converterResampleQuality;
    mixStereoToDualMono = stereoToDualMono;
    measureInputMeter = measureLocalInputMeter;

    qInfo(logAudioConverter).noquote() << "Starting AudioConverter() Input:" << inputFormat.channelCount()
                                       << "Channels of" << inputCodec << inputFormat.sampleRate()
                                       << inputFormat.sampleFormat() << "Output:" << outputFormat.channelCount()
                                       << "Channels of" << outputCodec << outputFormat.sampleRate()
                                       << outputFormat.sampleFormat();

    initialized = false;
    if (inputFormat.channelCount() <= 0 || outputFormat.channelCount() <= 0 || inputFormat.sampleRate() <= 0 ||
        outputFormat.sampleRate() <= 0)
    {
        const QString message = QStringLiteral("Invalid audio converter format");
        qCritical(logAudioConverter()).noquote()
            << message << "input channels" << inputFormat.channelCount() << "input rate" << inputFormat.sampleRate()
            << "output channels" << outputFormat.channelCount() << "output rate" << outputFormat.sampleRate();
        emit initFailed(message);
        return false;
    }

    if (inputCodec == OPUS)
    {
        int opus_err = 0;
        opusDecoder = opus_decoder_create(inputFormat.sampleRate(), inputFormat.channelCount(), &opus_err);
        qInfo(logAudioConverter()).noquote() << "Creating opus decoder: " << opus_strerror(opus_err);
        if (opusDecoder == nullptr)
        {
            const QString message =
                QStringLiteral("Could not create Opus decoder: %1").arg(QString::fromLatin1(opus_strerror(opus_err)));
            qCritical(logAudioConverter()).noquote() << message;
            emit initFailed(message);
            return false;
        }
    }

    if (outputCodec == OPUS)
    {
        int opus_err = 0;
        opusEncoder = opus_encoder_create(outputFormat.sampleRate(), outputFormat.channelCount(),
                                          OPUS_APPLICATION_AUDIO, &opus_err);
        if (opusEncoder)
        {
            opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(64000));
            opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(1));
            opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
            opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(encoderComplexity));
        }

        qInfo(logAudioConverter()).noquote() << "Creating opus encoder: " << opus_strerror(opus_err);
        if (opusEncoder == nullptr)
        {
            const QString message =
                QStringLiteral("Could not create Opus encoder: %1").arg(QString::fromLatin1(opus_strerror(opus_err)));
            qCritical(logAudioConverter()).noquote() << message;
            emit initFailed(message);
            return false;
        }
    }

    if (inputFormat.sampleRate() != outputFormat.sampleRate())
    {
        int resampleError = 0;
        unsigned int ratioNum;
        unsigned int ratioDen;
        resampler = speex_resampler_init(outputFormat.channelCount(), inputFormat.sampleRate(),
                                         outputFormat.sampleRate(), converterResampleQuality, &resampleError);
        if (resampler == nullptr || resampleError != RESAMPLER_ERR_SUCCESS)
        {
            const QString message = QStringLiteral("Could not create Speex resampler: %1")
                                        .arg(QString::fromLatin1(speex_resampler_strerror(resampleError)));
            qCritical(logAudioConverter()).noquote() << message;
            emit initFailed(message);
            return false;
        }
        speex_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
        // Speex stores the ratio as input/output (num/den), so output/input = den/num.
        resampleRatio = static_cast<double>(ratioDen) / ratioNum;
        qInfo(logAudioConverter()).noquote()
            << "speex_resampler_init() returned: " << resampleError << " resampleRatio: " << resampleRatio;
    }
    initialized = true;
    return true;
}

AudioConverter::~AudioConverter()
{

    qInfo(logAudioConverter).noquote() << "Closing AudioConverter() Input:" << inFormat.channelCount() << "Channels of"
                                       << inCodec << inFormat.sampleRate() << inFormat.sampleFormat()
                                       << "Output:" << outFormat.channelCount() << "Channels of" << outCodec
                                       << outFormat.sampleRate() << outFormat.sampleFormat();

    releaseCodecState();
}

void AudioConverter::process(audioPacket audio)
{
    // Always report completion, including malformed/dropped packets, so the
    // bounded producer queue can advance and cannot become permanently busy.
    if (!convert(std::move(audio)))
    {
        emit conversionFailed();
    }
    emit conversionCycleFinished();
}

void AudioConverter::releaseCodecState()
{
    if (opusEncoder != nullptr)
    {
        qInfo(logAudioConverter()).noquote() << "Destroying opus encoder";
        opus_encoder_destroy(opusEncoder);
        opusEncoder = nullptr;
    }

    if (opusDecoder != nullptr)
    {
        qInfo(logAudioConverter()).noquote() << "Destroying opus decoder";
        opus_decoder_destroy(opusDecoder);
        opusDecoder = nullptr;
    }

    if (resampler != nullptr)
    {
        speex_resampler_destroy(resampler);
        resampler = nullptr;
        qDebug(logAudioConverter()).noquote() << "Resampler closed";
    }
    resampleRatio = 1.0;
}

bool AudioConverter::convert(audioPacket audio)
{

    if (!initialized)
    {
        qWarning(logAudioConverter()).noquote() << "AudioConverter::convert() called before successful initialization";
        return false;
    }

    // Even when input and output formats match, run the normal conversion path
    // so audio level measurement and packet accounting stay consistent.
    if (audio.data.size() > 0)
    {
        const bool decodedToFloat = (inCodec == OPUS || inCodec == PCMU);
        QAudioFormat::SampleFormat sampleFormat = decodedToFloat ? QAudioFormat::Float : inFormat.sampleFormat();

        if (inCodec == OPUS)
        {
            auto* in = reinterpret_cast<quint8*>(audio.data.data());

            int nSamples = opus_packet_get_nb_samples(in, audio.data.size(), inFormat.sampleRate());
            if (nSamples == -1)
            {
                return false;
            }
            scratchIn.resize(nSamples * sizeof(float) * inFormat.channelCount());
            auto* out = reinterpret_cast<float*>(scratchIn.data());

            int ret = opus_decode_float(opusDecoder, in, audio.data.size(), out, nSamples, 0);
            if (ret < 0)
            {
                qWarning(logAudioConverter()).noquote() << "Opus decode failed:" << opus_strerror(ret);
                return false;
            }
            if (ret > nSamples)
            {
                qWarning(logAudioConverter()).noquote()
                    << "Opus decode returned more samples than requested:" << ret << "requested:" << nSamples;
                return false;
            }
            if (ret != nSamples)
            {
                qDebug(logAudio()).noquote()
                    << "opus_decode_float: returned:" << ret << "samples, expected:" << nSamples;
                scratchIn.resize(ret * int(sizeof(float)) * inFormat.channelCount());
            }
            audio.data.swap(scratchIn);
        }
        else if (inCodec == PCMU)
        {
            // Bit-exact G.711 mu-law to float32 [-1, 1].
            scratchIn.resize(audio.data.size() * int(sizeof(float)));

            const quint8* in = reinterpret_cast<const quint8*>(audio.data.constData());
            float* out = reinterpret_cast<float*>(scratchIn.data());

            for (int i = 0; i < audio.data.size(); ++i)
            {
                const quint8 u = ~in[i];

                const int sign = (u & 0x80) ? -1 : 1;
                const int exp = (u >> 4) & 0x07; // 3 bits
                const int mant = (u & 0x0F);     // 4 bits

                // Reconstruct 16-bit PCM per G.711: ((mant<<3)+0x84) << exp) - 0x84
                int t = ((mant << 3) + 0x84) << exp; // 0x84 = 132 (bias)
                int s = (t - 0x84) * sign;           // signed 16-bit range

                // Scale to float; use 32768.0f so full-scale maps symmetrically
                out[i] = std::clamp(s / 32768.0f, -1.0f, 1.0f);
            }

            audio.data.swap(scratchIn);
        }
        Eigen::VectorXf& samplesF = scratchSamples;
        if (sampleFormat == QAudioFormat::Int32)
        {
            if (!byteCountMatchesSampleSize(audio.data, int(sizeof(qint32))))
            {
                qWarning(logAudioConverter()).noquote()
                    << "Dropping malformed Int32 audio packet with byte count" << audio.data.size();
                return false;
            }
            const int n = audio.data.size() / int(sizeof(qint32));
            MapI32Un s32(reinterpret_cast<qint32*>(audio.data.data()), n);
            // Keep using Eigen's cast/divide semantics, but write into a
            // reusable member buffer to avoid one heap allocation per audio
            // packet. This is intentionally byte-equivalent at the output side.
            samplesF.resize(n);
            samplesF = s32.cast<float>() / float(std::numeric_limits<qint32>::max());
        }
        else if (sampleFormat == QAudioFormat::Int16)
        {
            if (!byteCountMatchesSampleSize(audio.data, int(sizeof(qint16))))
            {
                qWarning(logAudioConverter()).noquote()
                    << "Dropping malformed Int16 audio packet with byte count" << audio.data.size();
                return false;
            }
            const int n = audio.data.size() / int(sizeof(qint16));
            MapI16Un s16(reinterpret_cast<qint16*>(audio.data.data()), n);
            samplesF.resize(n);
            samplesF = s16.cast<float>() / float(std::numeric_limits<qint16>::max());
        }
        else if (sampleFormat == QAudioFormat::UInt8)
        {
            const int count = audio.data.size() / int(sizeof(quint8));
            MapU8Un u8(reinterpret_cast<quint8*>(audio.data.data()), count);
            scratchF.resize(count);
            scratchF = (u8.cast<float>().array() - 128.0f) / 127.0f;
            samplesF.resize(count);
            samplesF = scratchF;
        }
        else if (sampleFormat == QAudioFormat::Float)
        {
            if (!byteCountMatchesSampleSize(audio.data, int(sizeof(float))))
            {
                qWarning(logAudioConverter()).noquote()
                    << "Dropping malformed float audio packet with byte count" << audio.data.size();
                return false;
            }
            const int n = audio.data.size() / int(sizeof(float));
            MapFUn f(reinterpret_cast<float*>(audio.data.data()), n);
            samplesF.resize(n);
            samplesF = f;
        }
        else
        {
            qWarning(logAudioConverter()).noquote() << "Unsupported input sample format:" << sampleFormat;
            return false;
        }

        if (samplesF.size() > 0)

        {
            if (!sampleCountMatchesChannels(samplesF.size(), inFormat.channelCount()))
            {
                qWarning(logAudioConverter()).noquote() << "Dropping malformed audio packet with" << samplesF.size()
                                                        << "samples for" << inFormat.channelCount() << "input channels";
                return false;
            }

            audio.amplitudePeak = samplesF.array().abs().maxCoeff();
            audio.amplitudeRMS = std::sqrt((samplesF.array() * samplesF.array()).mean());

            samplesF *= audio.volume;

            if (inFormat.channelCount() == 2 && outFormat.channelCount() == 1)
            {
                scratchChannelMix.resize(samplesF.size() / 2);
                const Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>> left(samplesF.data(), samplesF.size() / 2);
                const Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>> right(samplesF.data() + 1,
                                                                                  samplesF.size() / 2);
                // Average both channels. Keeping only the left channel made a
                // right-only microphone/source silent on transmit.
                scratchChannelMix = (left + right) * 0.5f;
                samplesF.swap(scratchChannelMix);
            }
            else if (inFormat.channelCount() == 1 && outFormat.channelCount() == 2)
            {
                scratchChannelMix.resize(samplesF.size() * 2);
                Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>>(scratchChannelMix.data(), samplesF.size()) =
                    samplesF;
                Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>>(scratchChannelMix.data() + 1, samplesF.size()) =
                    samplesF;
                samplesF.swap(scratchChannelMix);
            }
            else if (inFormat.channelCount() == 2 && outFormat.channelCount() == 2 && mixStereoToDualMono)
            {
                const int frameCount = samplesF.size() / 2;
                scratchChannelMix.resize(frameCount);
                const Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>> left(samplesF.data(), frameCount);
                const Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>> right(samplesF.data() + 1, frameCount);
                scratchChannelMix = (left + right) * 0.5f;
                Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>>(samplesF.data(), frameCount) = scratchChannelMix;
                Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2>>(samplesF.data() + 1, frameCount) =
                    scratchChannelMix;
            }

            // Local processed-input measurement for the transmit meter. This
            // tap is deliberately after application gain and channel mixing and
            // before resampling:
            //  - after mixing, because equal and opposite stereo channels sum
            //    to silence at the radio while each channel alone still shows a
            //    large level, which a pre-mix tap would report as healthy audio;
            //  - after gain, so the reading cannot silently depend on the
            //    transmit gain happening to be unity;
            //  - before resampling, so the identical tap is available whether or
            //    not the transmit encode path runs. The reading must not change
            //    when PTT is pressed.
            // Full scale is counted here rather than at the output clamp because
            // the clamp only runs while encoding; post-mix samples are not
            // clamped, so they can legitimately exceed unity.
            if (measureInputMeter)
            {
                const Eigen::Index sampleCount = samplesF.size();
                float peakMagnitude = 0.0F;
                double sumSquares = 0.0;
                quint32 fullScaleCount = 0;
                for (Eigen::Index index = 0; index < sampleCount; ++index)
                {
                    const float magnitude = std::fabs(samplesF[index]);
                    peakMagnitude = std::max(peakMagnitude, magnitude);
                    sumSquares += static_cast<double>(magnitude) * magnitude;
                    if (magnitude >= sdr9700::audio::kFullScaleMagnitude)
                    {
                        ++fullScaleCount;
                    }
                }
                audio.inputMeter.peak = peakMagnitude;
                audio.inputMeter.sumSquares = sumSquares;
                audio.inputMeter.sampleCount = static_cast<quint32>(sampleCount);
                audio.inputMeter.fullScaleCount = fullScaleCount;
                audio.inputMeter.valid = true;
            }

            // Keep capture and the post-mix meter live before PTT, but avoid
            // resampling, encoding, and output construction when this block's
            // capture epoch was not authorized for voice transmission.
            if (measureInputMeter && !audio.txEncodingState.encodingAuthorized())
            {
                audio.data.clear();
                emit converted(audio);
                return true;
            }

            if (resampler != nullptr && resampleRatio != 1.0)
            {
                if (!sampleCountMatchesChannels(samplesF.size(), outFormat.channelCount()))
                {
                    qWarning(logAudioConverter()).noquote()
                        << "Dropping malformed audio packet with" << samplesF.size() << "samples for"
                        << outFormat.channelCount() << "output channels before resampling";
                    return false;
                }
                quint32 inFrames = static_cast<quint32>(samplesF.size() / outFormat.channelCount());
                quint32 outFrames = static_cast<quint32>(std::ceil(double(inFrames) * resampleRatio));
                if (outFrames == 0)
                {
                    return false;
                }
                scratchOut.resize(int(outFrames) * outFormat.channelCount() * int(sizeof(float)));
                const float* in = reinterpret_cast<const float*>(samplesF.data());
                auto* out = reinterpret_cast<float*>(scratchOut.data());
                const quint32 requestedOutFrames = outFrames;

                int err = 0;
                if (outFormat.channelCount() == 1)
                {
                    err = speex_resampler_process_float(resampler, 0, in, &inFrames, out, &outFrames);
                }
                else
                {
                    err = speex_resampler_process_interleaved_float(resampler, in, &inFrames, out, &outFrames);
                }

                if (err)
                {
                    qInfo(logAudioConverter()).noquote()
                        << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
                    return false;
                }
                if (outFrames < requestedOutFrames)
                {
                    scratchOut.resize(int(outFrames) * outFormat.channelCount() * int(sizeof(float)));
                }
                samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(scratchOut.data()),
                                                       int(outFrames) * outFormat.channelCount());
            }

            if (outCodec == OPUS)
            {
                if (!sampleCountMatchesChannels(samplesF.size(), outFormat.channelCount()))
                {
                    qWarning(logAudioConverter()).noquote()
                        << "Dropping malformed audio packet with" << samplesF.size() << "samples for"
                        << outFormat.channelCount() << "output channels before Opus encode";
                    return false;
                }
                const float* in = reinterpret_cast<const float*>(samplesF.data());
                scratchOut.resize(1275);
                auto* out = reinterpret_cast<quint8*>(scratchOut.data());

                int nbBytes = opus_encode_float(opusEncoder, in, (samplesF.size() / outFormat.channelCount()), out,
                                                scratchOut.length());
                if (nbBytes < 0)
                {
                    qInfo(logAudioConverter()).noquote()
                        << "Opus encode failed:" << opus_strerror(nbBytes) << "Num Samples:" << samplesF.size();
                    return false;
                }
                else
                {
                    scratchOut.resize(nbBytes);
                    audio.data.swap(scratchOut);
                }
            }
            else if (outCodec == PCMU)
            {
                const float* in = reinterpret_cast<const float*>(samplesF.data());
                const int n = samplesF.size();

                scratchOut.resize(n);
                quint8* dst = reinterpret_cast<quint8*>(scratchOut.data());

                constexpr int BIAS = 0x84; // 132
                constexpr int CLIP = 32635;
                static constexpr int seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

                for (int i = 0; i < n; ++i)
                {
                    float xf = in[i];
                    if (xf > 1.0f)
                    {
                        xf = 1.0f;
                    }
                    if (xf < -1.0f)
                    {
                        xf = -1.0f;
                    }
                    int s = int(std::lrintf(xf * 32767.0f));
                    if (s > 32767)
                    {
                        s = 32767;
                    }
                    if (s < -32768)
                    {
                        s = -32768;
                    }

                    int pcm = s;
                    int mask;
                    if (pcm < 0)
                    {
                        pcm = BIAS - pcm;
                        mask = 0x7F;
                    }
                    else
                    {
                        pcm = BIAS + pcm;
                        mask = 0xFF;
                    }
                    const int maxp = BIAS + CLIP;
                    if (pcm > maxp)
                    {
                        pcm = maxp;
                    }

                    int seg = 0;
                    while (seg < 8 && pcm > seg_end[seg])
                    {
                        ++seg;
                    }
                    if (seg > 7)
                    {
                        seg = 7;
                    }

                    const int mant = (pcm >> (seg + 3)) & 0x0F;
                    const quint8 u = quint8(((seg << 4) | mant) ^ mask);

                    dst[i] = u;
                }

                audio.data.swap(scratchOut);
            }
            else
            {
                audio.data.clear();

                if (outFormat.sampleFormat() == QAudioFormat::UInt8)
                {
                    scratchOutputFloat.resize(samplesF.size());
                    scratchOutputFloat = samplesF.array().max(-1.0f).min(1.0f);
                    scratchOutputU8.resize(scratchOutputFloat.size());
                    scratchOutputU8 = ((scratchOutputFloat.array() * 127.0f) + 128.0f).round().cast<quint8>();
                    scratchOut.resize(int(scratchOutputU8.size() * sizeof(quint8)));
                    std::memcpy(scratchOut.data(), scratchOutputU8.data(), size_t(scratchOutputU8.size()));
                    audio.data.swap(scratchOut);
                }
                else if (outFormat.sampleFormat() == QAudioFormat::Int16)
                {
                    scratchOutputFloat.resize(samplesF.size());
                    scratchOutputFloat = samplesF.array().max(-1.0f).min(1.0f);
                    scratchOutputI16.resize(scratchOutputFloat.size());
                    scratchOutputI16 =
                        (scratchOutputFloat * float(std::numeric_limits<qint16>::max())).array().round().cast<qint16>();
                    scratchOut.resize(int(scratchOutputI16.size() * sizeof(qint16)));
                    std::memcpy(scratchOut.data(), scratchOutputI16.data(),
                                size_t(scratchOutputI16.size()) * sizeof(qint16));
                    audio.data.swap(scratchOut);
                }
                else if (outFormat.sampleFormat() == QAudioFormat::Int32)
                {
                    scratchOutputFloat.resize(samplesF.size());
                    scratchOutputFloat = samplesF.array().max(-1.0f).min(1.0f);
                    scratchOutputI32.resize(scratchOutputFloat.size());
                    scratchOutputI32 =
                        (scratchOutputFloat * float(std::numeric_limits<qint32>::max())).array().round().cast<qint32>();
                    scratchOut.resize(int(scratchOutputI32.size() * sizeof(qint32)));
                    std::memcpy(scratchOut.data(), scratchOutputI32.data(),
                                size_t(scratchOutputI32.size()) * sizeof(qint32));
                    audio.data.swap(scratchOut);
                }
                else if (outFormat.sampleFormat() == QAudioFormat::Float)
                {
                    scratchOut.resize(int(samplesF.size()) * int(sizeof(float)));
                    std::memcpy(scratchOut.data(), samplesF.data(), size_t(scratchOut.size()));
                    audio.data.swap(scratchOut);
                }
                else
                {
                    qWarning(logAudioConverter()).noquote()
                        << "Unsupported output sample format:" << outFormat.sampleFormat();
                    return false;
                }
            }
        }
        else
        {
            qDebug(logAudioConverter()).noquote() << "Detected empty packet";
        }
    }
    emit converted(audio);
    return true;
}
