#pragma once

#include <algorithm>
#include <QtGlobal>
#include <cmath>

namespace sdr9700
{
class TransmitSafetyPolicy
{
  public:
    // Require three consecutive readings at or above 3:1 SWR to reject a transient spike while protecting the radio.
    explicit TransmitSafetyPolicy(double cutoff = 3.0, int consecutiveReadings = 3)
        : m_cutoff(cutoff), m_requiredReadings(std::max(1, consecutiveReadings))
    {
    }

    bool observe(bool transmitting, double swr)
    {
        if (!transmitting || !std::isfinite(swr) || swr < m_cutoff)
        {
            reset();
            return false;
        }
        return ++m_highReadingCount >= m_requiredReadings;
    }

    void reset() { m_highReadingCount = 0; }
    int highReadingCount() const { return m_highReadingCount; }

  private:
    double m_cutoff;
    int m_requiredReadings;
    int m_highReadingCount{0};
};
} // namespace sdr9700
