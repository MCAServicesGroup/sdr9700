#pragma once

#include <array>

namespace sdr9700
{
inline constexpr std::array kSpectrumFramesPerSecondPresets{10, 15, 20, 25, 30};
inline constexpr int kDefaultSpectrumFramesPerSecond = 30;

inline constexpr bool isSupportedSpectrumFramesPerSecond(int framesPerSecond)
{
    for (const int preset : kSpectrumFramesPerSecondPresets)
    {
        if (framesPerSecond == preset)
        {
            return true;
        }
    }
    return false;
}

inline constexpr int normalizedSpectrumFramesPerSecond(int framesPerSecond)
{
    return isSupportedSpectrumFramesPerSecond(framesPerSecond) ? framesPerSecond : kDefaultSpectrumFramesPerSecond;
}

inline constexpr int spectrumFrameIntervalMs(int framesPerSecond)
{
    const int normalized = normalizedSpectrumFramesPerSecond(framesPerSecond);
    return (1000 + normalized - 1) / normalized;
}
} // namespace sdr9700
