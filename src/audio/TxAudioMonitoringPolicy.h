#pragma once

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

} // namespace sdr9700::audio
