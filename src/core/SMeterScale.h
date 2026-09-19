#pragma once

#include <cmath>
#include <QtGlobal>
#include <array>

namespace sdr9700
{
struct SMeterScalePoint
{
    int raw;
    int display;
};

inline constexpr std::array<SMeterScalePoint, 9> kSMeterScalePoints{
    {{0, 0}, {10, 16}, {30, 49}, {60, 81}, {90, 113}, {120, 146}, {160, 182}, {201, 219}, {241, 255}}};

// Convert the preserved raw radio value only at the presentation boundary.
// The final point explicitly makes Icom level 0241 and higher full scale.
inline int sMeterDisplayValue(int rawValue)
{
    if (rawValue <= kSMeterScalePoints.front().raw)
    {
        return kSMeterScalePoints.front().display;
    }
    if (rawValue >= kSMeterScalePoints.back().raw)
    {
        return kSMeterScalePoints.back().display;
    }

    for (std::size_t index = 1; index < kSMeterScalePoints.size(); ++index)
    {
        const SMeterScalePoint& upper = kSMeterScalePoints[index];
        if (rawValue <= upper.raw)
        {
            const SMeterScalePoint& lower = kSMeterScalePoints[index - 1];
            const double fraction = static_cast<double>(rawValue - lower.raw) / (upper.raw - lower.raw);
            return static_cast<int>(std::lround(lower.display + fraction * (upper.display - lower.display)));
        }
    }
    return kSMeterScalePoints.back().display;
}

inline int sMeterDisplayPercent(int rawValue)
{
    return sMeterDisplayValue(rawValue) * 100 / kSMeterScalePoints.back().display;
}
} // namespace sdr9700
