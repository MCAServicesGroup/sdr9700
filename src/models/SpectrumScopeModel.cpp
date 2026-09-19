#include "SpectrumScopeModel.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinSourceBandwidthMhz = 0.001;
constexpr double kScopeRangeToleranceMhz = 0.000001;
constexpr double kMinHeldCenterToleranceMhz = 0.00005;
constexpr double kMaxHeldCenterToleranceMhz = 0.0005;

bool normalizeRange(double* startMhz, double* endMhz)
{
    if (!startMhz || !endMhz || !std::isfinite(*startMhz) || !std::isfinite(*endMhz))
    {
        return false;
    }
    if (*endMhz < *startMhz)
    {
        std::swap(*startMhz, *endMhz);
    }
    return (*endMhz - *startMhz) >= kMinSourceBandwidthMhz;
}
} // namespace

SpectrumScopeModel::SpectrumScopeModel(QObject* parent) : QObject(parent) {}

double SpectrumScopeModel::constrainedBandwidth(double requestedBandwidthMhz) const
{
    double displayStartMhz = sourceStartMhz();
    double displayEndMhz = sourceEndMhz();
    if (m_hasFrequencyLimits)
    {
        displayStartMhz = std::max(displayStartMhz, m_limitStartMhz);
        displayEndMhz = std::min(displayEndMhz, m_limitEndMhz);
    }

    const double maximumBandwidth = std::max(kMinSourceBandwidthMhz, displayEndMhz - displayStartMhz);
    return std::clamp(requestedBandwidthMhz, kMinSourceBandwidthMhz, maximumBandwidth);
}

double SpectrumScopeModel::constrainedCenter(double requestedCenterMhz, double requestedBandwidthMhz) const
{
    double displayStartMhz = sourceStartMhz();
    double displayEndMhz = sourceEndMhz();
    if (m_hasFrequencyLimits)
    {
        displayStartMhz = std::max(displayStartMhz, m_limitStartMhz);
        displayEndMhz = std::min(displayEndMhz, m_limitEndMhz);
    }

    if (displayEndMhz <= displayStartMhz)
    {
        return requestedCenterMhz;
    }

    const double halfBandwidth = requestedBandwidthMhz / 2.0;
    const double minCenter = displayStartMhz + halfBandwidth;
    const double maxCenter = displayEndMhz - halfBandwidth;
    return std::clamp(requestedCenterMhz, std::min(minCenter, maxCenter), std::max(minCenter, maxCenter));
}

void SpectrumScopeModel::constrainDisplayRange()
{
    const double nextBandwidth = constrainedBandwidth(m_bandwidthMhz);
    const double nextCenter = constrainedCenter(m_centerMhz, nextBandwidth);
    if (std::abs(nextCenter - m_centerMhz) < 1e-9 && std::abs(nextBandwidth - m_bandwidthMhz) < 1e-9)
    {
        return;
    }

    m_centerMhz = nextCenter;
    m_bandwidthMhz = nextBandwidth;
    emit rangeChanged(m_centerMhz, m_bandwidthMhz);
}

void SpectrumScopeModel::centerOnFrequency(double freqMhz)
{
    const double nextBandwidth = constrainedBandwidth(m_bandwidthMhz);
    const double nextCenter = constrainedCenter(freqMhz, nextBandwidth);
    if (std::abs(nextCenter - m_centerMhz) < 1e-9 && std::abs(nextBandwidth - m_bandwidthMhz) < 1e-9)
    {
        return;
    }

    m_centerMhz = nextCenter;
    m_bandwidthMhz = nextBandwidth;
    emit rangeChanged(m_centerMhz, m_bandwidthMhz);
}

void SpectrumScopeModel::holdDisplayCenter(double displayCenterMhz, double expectedSourceCenterMhz)
{
    if (!std::isfinite(displayCenterMhz) || !std::isfinite(expectedSourceCenterMhz))
    {
        return;
    }

    m_heldCenterMhz = displayCenterMhz;
    m_heldSourceCenterMhz = expectedSourceCenterMhz;
    m_hasDisplayCenterHold = true;
}

void SpectrumScopeModel::clearDisplayCenterHold()
{
    m_hasDisplayCenterHold = false;
    m_heldCenterMhz = 0.0;
    m_heldSourceCenterMhz = 0.0;
}

void SpectrumScopeModel::setFrequencyLimits(double lowerMhz, double upperMhz)
{
    if (!normalizeRange(&lowerMhz, &upperMhz))
    {
        clearFrequencyLimits();
        return;
    }
    if (m_hasFrequencyLimits && std::abs(m_limitStartMhz - lowerMhz) < 1e-9 &&
        std::abs(m_limitEndMhz - upperMhz) < 1e-9)
    {
        return;
    }

    m_limitStartMhz = lowerMhz;
    m_limitEndMhz = upperMhz;
    m_hasFrequencyLimits = true;
    constrainDisplayRange();
}

void SpectrumScopeModel::clearFrequencyLimits()
{
    if (!m_hasFrequencyLimits)
    {
        return;
    }

    m_hasFrequencyLimits = false;
    m_limitStartMhz = 0.0;
    m_limitEndMhz = 0.0;
    constrainDisplayRange();
}

void SpectrumScopeModel::ingestSpectrum(const QVector<float>& levels, double lowerMhz, double upperMhz, bool outOfRange)
{
    // Minimum change in MHz that warrants a rangeChanged emission.
    static constexpr double kRangeChangeTolerance = 0.0001;

    const bool reversedRange = std::isfinite(lowerMhz) && std::isfinite(upperMhz) && upperMhz < lowerMhz;
    if (!normalizeRange(&lowerMhz, &upperMhz))
    {
        return;
    }
    // Update range tracking from the radio-provided spectrum bounds.
    const double incomingCenter = (lowerMhz + upperMhz) / 2.0;
    const double incomingBw = upperMhz - lowerMhz;
    if (m_hasDisplayCenterHold)
    {
        if (m_heldCenterMhz < lowerMhz - kScopeRangeToleranceMhz ||
            m_heldCenterMhz > upperMhz + kScopeRangeToleranceMhz)
        {
            return;
        }

        // While click-to-tune is waiting for the radio to report the new source
        // center, nearby old scope frames can still contain the clicked
        // frequency. Drawing those stale frames under the optimistic display
        // center makes the signal appear to slide away from the click point.
        //
        // The held display center and the expected radio/source center are
        // intentionally separate. Near a band edge the UI clamps the visible
        // range so the scale stays inside the band, while the IC-9700 still
        // reports scope frames centered on the requested VFO. Tying acceptance
        // to the clamped display center rejects valid edge-of-band frames.
        // If field testing shows another click-to-tune drift case, capture both
        // incomingCenter and m_heldSourceCenterMhz before changing this logic.
        const double binToleranceMhz =
            levels.size() > 1 ? incomingBw / static_cast<double>(levels.size() - 1) * 0.25 : kMinHeldCenterToleranceMhz;
        const double heldCenterToleranceMhz =
            std::clamp(binToleranceMhz, kMinHeldCenterToleranceMhz, kMaxHeldCenterToleranceMhz);
        if (std::abs(incomingCenter - m_heldSourceCenterMhz) > heldCenterToleranceMhz)
        {
            return;
        }
    }
    m_sourceCenterMhz = incomingCenter;
    m_sourceBandwidthMhz = incomingBw;

    double displayCenter = m_hasDisplayCenterHold ? m_heldCenterMhz : incomingCenter;
    double displayBandwidth = incomingBw;
    displayBandwidth = constrainedBandwidth(displayBandwidth);
    displayCenter = constrainedCenter(displayCenter, displayBandwidth);

    if (std::abs(displayCenter - m_centerMhz) > kRangeChangeTolerance ||
        std::abs(displayBandwidth - m_bandwidthMhz) > kRangeChangeTolerance)
    {
        m_centerMhz = displayCenter;
        m_bandwidthMhz = displayBandwidth;
        emit rangeChanged(m_centerMhz, m_bandwidthMhz);
    }

    if (reversedRange)
    {
        QVector<float> normalizedLevels = levels;
        std::reverse(normalizedLevels.begin(), normalizedLevels.end());
        emit spectrumReady(normalizedLevels, lowerMhz, upperMhz, outOfRange);
        return;
    }

    emit spectrumReady(levels, lowerMhz, upperMhz, outOfRange);
}
