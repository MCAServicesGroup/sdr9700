#pragma once

namespace sdr9700::backend
{

inline bool receiverAudioReady(bool mainFrequencyReceived, bool mainModeReceived, bool subFrequencyReceived,
                               bool subModeReceived)
{
    return mainFrequencyReceived && mainModeReceived && subFrequencyReceived && subModeReceived;
}

} // namespace sdr9700::backend
