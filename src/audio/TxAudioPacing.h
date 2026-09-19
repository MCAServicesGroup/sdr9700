#pragma once

#include <QtGlobal>

#include <algorithm>

namespace sdr9700::audio
{
constexpr qint64 kTxAudioFrameIntervalMs = 20;
constexpr qint64 kMaximumTxAudioFramesPerTick = 3;

struct TxAudioPumpDecision
{
    qint64 framesDue{1};
    qint64 framesAccountedFor{1};
};

inline TxAudioPumpDecision txAudioPumpDecision(qint64 elapsedMs, qint64 framesSent)
{
    const qint64 elapsedFrames = std::max<qint64>(0, elapsedMs) / kTxAudioFrameIntervalMs;
    const qint64 framesAccountedFor = 1 + elapsedFrames;
    return {std::clamp(framesAccountedFor - framesSent, qint64(1), kMaximumTxAudioFramesPerTick), framesAccountedFor};
}
} // namespace sdr9700::audio
