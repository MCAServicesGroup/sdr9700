#include "Ax25Decoder.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <QStringList>

namespace
{
constexpr int kBaud = 1200;
constexpr double kMarkFrequency = 1200.0;
constexpr double kSpaceFrequency = 2200.0;
constexpr int kMinimumFrameBytes = 18;

QString decodeAddress(const QByteArray& bytes, int offset, bool digipeater)
{
    QString call;
    for (int i = 0; i < 6; ++i)
    {
        const char character = static_cast<char>((static_cast<quint8>(bytes.at(offset + i)) >> 1) & 0x7f);
        if (character != ' ')
        {
            call.append(QChar::fromLatin1(character));
        }
    }
    const int ssid = (static_cast<quint8>(bytes.at(offset + 6)) >> 1) & 0x0f;
    if (ssid != 0)
    {
        call.append(QStringLiteral("-%1").arg(ssid));
    }
    if (digipeater && (static_cast<quint8>(bytes.at(offset + 6)) & 0x80U) != 0)
    {
        call.append(QLatin1Char('*'));
    }
    return call;
}

bool isValidAddress(const QByteArray& bytes, int offset)
{
    bool hasCharacter = false;
    for (int i = 0; i < 6; ++i)
    {
        const quint8 encoded = static_cast<quint8>(bytes.at(offset + i));
        if ((encoded & 0x01U) != 0)
        {
            return false;
        }
        const char character = static_cast<char>((encoded >> 1) & 0x7fU);
        if (character != ' ' && !(character >= 'A' && character <= 'Z') && !(character >= '0' && character <= '9'))
        {
            return false;
        }
        hasCharacter = hasCharacter || character != ' ';
    }
    const quint8 ssid = static_cast<quint8>(bytes.at(offset + 6));
    return hasCharacter && (ssid & 0x60U) == 0x60U;
}

QString printablePayload(const QByteArray& payload)
{
    QString result;
    result.reserve(payload.size());
    for (const char value : payload)
    {
        const quint8 byte = static_cast<quint8>(value);
        result.append(byte >= 0x20 && byte < 0x7f ? QChar::fromLatin1(value) : QChar(0x00b7));
    }
    return result;
}
} // namespace

void Ax25Decoder::configure(int sampleRate)
{
    const int samplesPerSymbol = static_cast<int>(std::lround(static_cast<double>(sampleRate) / kBaud));
    if (sampleRate == m_sampleRate && samplesPerSymbol == m_samplesPerSymbol)
    {
        return;
    }
    m_sampleRate = sampleRate;
    m_samplesPerSymbol = std::max(1, samplesPerSymbol);
    m_window.fill(0.0F, m_samplesPerSymbol);
    m_markCos.resize(m_samplesPerSymbol);
    m_markSin.resize(m_samplesPerSymbol);
    m_spaceCos.resize(m_samplesPerSymbol);
    m_spaceSin.resize(m_samplesPerSymbol);
    for (int i = 0; i < m_samplesPerSymbol; ++i)
    {
        const double time = static_cast<double>(i) / sampleRate;
        m_markCos[i] = std::cos(2.0 * std::numbers::pi * kMarkFrequency * time);
        m_markSin[i] = std::sin(2.0 * std::numbers::pi * kMarkFrequency * time);
        m_spaceCos[i] = std::cos(2.0 * std::numbers::pi * kSpaceFrequency * time);
        m_spaceSin[i] = std::sin(2.0 * std::numbers::pi * kSpaceFrequency * time);
    }
    m_lanes.resize(m_samplesPerSymbol);
    m_windowPosition = 0;
    m_windowFill = 0;
    m_sampleIndex = 0;
}

QVector<Ax25Frame> Ax25Decoder::processPcm16(const QByteArray& pcm, int sampleRate, int channelCount)
{
    QVector<Ax25Frame> frames;
    if (sampleRate < kBaud * 4 || channelCount < 1 || channelCount > 2 || pcm.size() < channelCount * 2)
    {
        return frames;
    }
    configure(sampleRate);

    const qsizetype frameBytes = channelCount * 2;
    const qsizetype sampleFrames = pcm.size() / frameBytes;
    double squaredSum = 0.0;
    for (qsizetype frame = 0; frame < sampleFrames; ++frame)
    {
        qint32 mixed = 0;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const qsizetype offset = frame * frameBytes + channel * 2;
            const quint8 low = static_cast<quint8>(pcm.at(offset));
            const quint8 high = static_cast<quint8>(pcm.at(offset + 1));
            mixed += static_cast<qint16>(low | (static_cast<quint16>(high) << 8));
        }
        const float sample = static_cast<float>(mixed) / static_cast<float>(channelCount * 32768);
        squaredSum += static_cast<double>(sample) * sample;
        m_window[m_windowPosition] = sample;
        m_windowPosition = (m_windowPosition + 1) % m_samplesPerSymbol;
        m_windowFill = std::min(m_windowFill + 1, m_samplesPerSymbol);

        if (m_windowFill == m_samplesPerSymbol)
        {
            double markI = 0.0;
            double markQ = 0.0;
            double spaceI = 0.0;
            double spaceQ = 0.0;
            for (int i = 0; i < m_samplesPerSymbol; ++i)
            {
                const double value = m_window[(m_windowPosition + i) % m_samplesPerSymbol];
                markI += value * m_markCos[i];
                markQ += value * m_markSin[i];
                spaceI += value * m_spaceCos[i];
                spaceQ += value * m_spaceSin[i];
            }
            const bool tone = markI * markI + markQ * markQ >= spaceI * spaceI + spaceQ * spaceQ;
            const int laneIndex = static_cast<int>(m_sampleIndex % static_cast<quint64>(m_samplesPerSymbol));
            acceptTone(m_lanes[laneIndex], tone, frames);
        }
        ++m_sampleIndex;
    }

    const double rms = sampleFrames > 0 ? std::sqrt(squaredSum / sampleFrames) : 0.0;
    const double dbfs = rms > 0.0 ? 20.0 * std::log10(rms) : -80.0;
    const int instantaneousLevel = std::clamp(static_cast<int>(std::lround((dbfs + 60.0) * (100.0 / 60.0))), 0, 100);
    m_stats.audioLevel = std::max(instantaneousLevel, static_cast<int>(std::lround(m_stats.audioLevel * 0.85)));
    return frames;
}

QVector<Ax25Frame> Ax25Decoder::processNrziTones(const QVector<bool>& tones)
{
    QVector<Ax25Frame> frames;
    TimingLane lane;
    for (const bool tone : tones)
    {
        acceptTone(lane, tone, frames);
        ++m_sampleIndex;
    }
    return frames;
}

void Ax25Decoder::acceptTone(TimingLane& lane, bool tone, QVector<Ax25Frame>& frames)
{
    if (!lane.haveTone)
    {
        lane.haveTone = true;
        lane.previousTone = tone;
        return;
    }
    const bool bit = tone == lane.previousTone;
    lane.previousTone = tone;
    acceptBit(lane.hdlc, bit, frames);
}

void Ax25Decoder::acceptBit(HdlcState& state, bool bit, QVector<Ax25Frame>& frames)
{
    state.shiftRegister = static_cast<quint8>((state.shiftRegister >> 1) | (bit ? 0x80U : 0U));
    if (state.inFrame)
    {
        state.rawBits.append(bit);
        if (state.rawBits.size() > 16384)
        {
            state.inFrame = false;
            state.rawBits.clear();
        }
    }
    if (state.shiftRegister == 0x7eU)
    {
        if (state.inFrame && state.rawBits.size() > 8)
        {
            finishFrame(state.rawBits.mid(0, state.rawBits.size() - 8), frames);
        }
        state.inFrame = true;
        state.rawBits.clear();
    }
}

void Ax25Decoder::finishFrame(const QVector<bool>& rawBits, QVector<Ax25Frame>& frames)
{
    QByteArray bytes;
    quint8 currentByte = 0;
    int bitCount = 0;
    int consecutiveOnes = 0;
    bool malformed = false;
    for (const bool bit : rawBits)
    {
        if (bit)
        {
            ++consecutiveOnes;
            if (consecutiveOnes > 6)
            {
                malformed = true;
                break;
            }
        }
        else
        {
            if (consecutiveOnes == 5)
            {
                consecutiveOnes = 0;
                continue;
            }
            consecutiveOnes = 0;
        }
        if (bit)
        {
            currentByte |= static_cast<quint8>(1U << bitCount);
        }
        ++bitCount;
        if (bitCount == 8)
        {
            bytes.append(static_cast<char>(currentByte));
            currentByte = 0;
            bitCount = 0;
        }
    }
    if (malformed || bitCount != 0 || bytes.size() < kMinimumFrameBytes)
    {
        return;
    }
    const QByteArray frame = bytes.left(bytes.size() - 2);
    const quint16 expected = static_cast<quint8>(bytes.at(bytes.size() - 2)) |
                             (static_cast<quint16>(static_cast<quint8>(bytes.back())) << 8);
    if (frameCheckSequence(frame) != expected)
    {
        if (isDistinctCandidate(&m_lastRejectedSampleIndex, &m_haveRejectedSample))
        {
            ++m_stats.candidates;
            ++m_stats.fcsFailures;
            m_lastRejectedSampleIndex = m_sampleIndex;
            m_haveRejectedSample = true;
        }
        return;
    }
    bool structurallyValid = false;
    Ax25Frame decoded = parseFrame(frame, &structurallyValid);
    if (!structurallyValid)
    {
        if (isDistinctCandidate(&m_lastMalformedSampleIndex, &m_haveMalformedSample))
        {
            ++m_stats.candidates;
            ++m_stats.malformed;
        }
        return;
    }
    const quint64 duplicateWindow = static_cast<quint64>(std::max(1, m_samplesPerSymbol * 2));
    if (m_haveFrameSample && frame == m_lastFrame && m_sampleIndex >= m_lastFrameSampleIndex &&
        m_sampleIndex - m_lastFrameSampleIndex <= duplicateWindow)
    {
        return;
    }
    m_lastFrame = frame;
    m_lastFrameSampleIndex = m_sampleIndex;
    m_haveFrameSample = true;
    decoded.receivedAt = QDateTime::currentDateTime();
    decoded.protocol = QStringLiteral("AX.25 (1200)");
    ++m_stats.candidates;
    ++m_stats.decoded;
    frames.append(decoded);
}

bool Ax25Decoder::isDistinctCandidate(quint64* lastSampleIndex, bool* haveSample) const
{
    const quint64 rejectionWindow = static_cast<quint64>(std::max(1, m_samplesPerSymbol * 2));
    if (*haveSample && m_sampleIndex >= *lastSampleIndex && m_sampleIndex - *lastSampleIndex <= rejectionWindow)
    {
        return false;
    }
    *lastSampleIndex = m_sampleIndex;
    *haveSample = true;
    return true;
}

quint16 Ax25Decoder::frameCheckSequence(const QByteArray& bytes)
{
    quint16 crc = 0xffff;
    for (const char value : bytes)
    {
        crc ^= static_cast<quint8>(value);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1U) != 0 ? static_cast<quint16>((crc >> 1) ^ 0x8408U) : static_cast<quint16>(crc >> 1);
        }
    }
    return static_cast<quint16>(crc ^ 0xffffU);
}

Ax25Frame Ax25Decoder::parseFrame(const QByteArray& bytes, bool* valid)
{
    Ax25Frame frame;
    frame.bytes = bytes;
    bool ok = bytes.size() >= 16;
    int offset = 0;
    QStringList addresses;
    while (ok)
    {
        if (offset + 7 > bytes.size() || addresses.size() >= 10)
        {
            ok = false;
            break;
        }
        if (!isValidAddress(bytes, offset))
        {
            ok = false;
            break;
        }
        addresses.append(decodeAddress(bytes, offset, addresses.size() >= 2));
        const bool last = (static_cast<quint8>(bytes.at(offset + 6)) & 0x01U) != 0;
        offset += 7;
        if (last)
        {
            break;
        }
    }
    if (addresses.size() < 2 || offset >= bytes.size())
    {
        ok = false;
    }
    if (ok)
    {
        frame.destination = addresses.at(0);
        frame.source = addresses.at(1);
        if (addresses.size() > 2)
        {
            frame.path = addresses.mid(2).join(QLatin1Char(','));
        }
        const quint8 control = static_cast<quint8>(bytes.at(offset++));
        if (control == 0x03)
        {
            frame.type = QStringLiteral("UI");
        }
        else if ((control & 0x01U) == 0)
        {
            frame.type = QStringLiteral("I");
        }
        else if ((control & 0x03U) == 0x01U)
        {
            frame.type = QStringLiteral("S");
        }
        else
        {
            frame.type = QStringLiteral("U");
        }
        if ((control == 0x03 || (control & 0x01U) == 0) && offset < bytes.size())
        {
            ++offset; // PID
        }
        frame.payload = printablePayload(bytes.mid(offset));
    }
    if (valid)
    {
        *valid = ok;
    }
    return frame;
}

void Ax25Decoder::reset()
{
    m_sampleRate = 0;
    m_samplesPerSymbol = 0;
    m_sampleIndex = 0;
    m_window.clear();
    m_markCos.clear();
    m_markSin.clear();
    m_spaceCos.clear();
    m_spaceSin.clear();
    m_lanes.clear();
    m_stats = {};
    m_lastFrame.clear();
    m_lastFrameSampleIndex = 0;
    m_haveFrameSample = false;
    m_lastRejectedSampleIndex = 0;
    m_haveRejectedSample = false;
    m_lastMalformedSampleIndex = 0;
    m_haveMalformedSample = false;
}
