#pragma once

#include "RadioCapabilities.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace sdr9700
{
inline quint64 roundFrequencyToStep(quint64 hz, quint64 stepHz)
{
    return stepHz <= 1 ? hz : ((hz + stepHz / 2) / stepHz) * stepHz;
}

inline quint64 clampFrequencyToBand(quint64 hz, quint64 referenceHz)
{
    availableBands band = radioBandForFrequency(referenceHz);
    if (band == bandUnknown)
    {
        band = radioBandForFrequency(hz);
    }
    quint64 startHz = 0;
    quint64 endHz = 0;
    return band != bandUnknown && radioBandEdges(band, &startHz, &endHz) ? std::clamp(hz, startHz, endHz) : hz;
}

inline quint64 clampScopeCenterToBand(quint64 hz, quint64 referenceHz, double bandwidthMhz)
{
    availableBands band = radioBandForFrequency(referenceHz);
    if (band == bandUnknown)
    {
        band = radioBandForFrequency(hz);
    }
    quint64 startHz = 0;
    quint64 endHz = 0;
    if (band == bandUnknown || !radioBandEdges(band, &startHz, &endHz))
    {
        return hz;
    }

    const double halfBandwidthHz = std::max(0.0, bandwidthMhz) * 500000.0;
    const double minCenterHz = double(startHz) + halfBandwidthHz;
    const double maxCenterHz = double(endHz) - halfBandwidthHz;
    const double centerHz =
        maxCenterHz >= minCenterHz ? std::clamp(double(hz), minCenterHz, maxCenterHz) : (double(startHz) + endHz) / 2.0;
    return static_cast<quint64>(std::llround(centerHz));
}
} // namespace sdr9700
