#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QQueue>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace sdr9700::audio
{

inline bool shouldCaptureTxAudio(bool audioReady, bool transmitEnabled, bool hasInputDevice)
{
    return audioReady && transmitEnabled && hasInputDevice;
}

inline bool shouldQueueMicrophoneFrameForTransmit(bool transmitActive, bool dtmfActive)
{
    return transmitActive && !dtmfActive;
}

struct TxEncodingState
{
    quint64 epoch{0};
    bool pttActive{false};
    bool dtmfActive{false};

    bool encodingAuthorized() const noexcept { return shouldQueueMicrophoneFrameForTransmit(pttActive, dtmfActive); }

    friend bool operator==(const TxEncodingState&, const TxEncodingState&) = default;
};

inline std::optional<TxEncodingState> transitionTxEncodingState(const TxEncodingState& current, bool pttActive,
                                                                bool dtmfActive)
{
    if (current.pttActive == pttActive && current.dtmfActive == dtmfActive)
    {
        return std::nullopt;
    }
    return TxEncodingState{current.epoch + 1, pttActive, dtmfActive};
}

enum class TxEncodingUpdate
{
    Ignored,
    Unchanged,
    Accepted,
};

inline TxEncodingUpdate classifyTxEncodingUpdate(const TxEncodingState& current, const TxEncodingState& incoming)
{
    if (incoming.epoch < current.epoch || (incoming.epoch == current.epoch && incoming != current))
    {
        return TxEncodingUpdate::Ignored;
    }
    if (incoming == current)
    {
        return TxEncodingUpdate::Unchanged;
    }
    return TxEncodingUpdate::Accepted;
}

struct TxAudioQueueEntry
{
    quint64 epoch{0};
    QByteArray data;
};

inline std::optional<QByteArray> takeMatchingTxAudioFrame(QQueue<TxAudioQueueEntry>& queue, quint64 epoch)
{
    while (!queue.isEmpty())
    {
        TxAudioQueueEntry entry = queue.dequeue();
        if (entry.epoch == epoch)
        {
            return std::move(entry.data);
        }
    }
    return std::nullopt;
}

} // namespace sdr9700::audio

Q_DECLARE_METATYPE(sdr9700::audio::TxEncodingState)
