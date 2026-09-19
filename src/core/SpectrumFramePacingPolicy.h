#pragma once

#include "SpectrumFrameRate.h"

#include <algorithm>
#include <QtGlobal>

namespace sdr9700
{
class SpectrumFramePacingPolicy
{
  public:
    explicit SpectrumFramePacingPolicy(int framesPerSecond = kDefaultSpectrumFramesPerSecond)
        : m_framesPerSecond(normalizedSpectrumFramesPerSecond(framesPerSecond))
    {
    }

    int framesPerSecond() const { return m_framesPerSecond; }

    void setFramesPerSecond(int requestedFramesPerSecond, qint64 nowNs)
    {
        m_framesPerSecond = normalizedSpectrumFramesPerSecond(requestedFramesPerSecond);
        if (m_started)
        {
            m_nextEmissionDeadlineNs = nowNs + spectrumFrameIntervalNanoseconds(m_framesPerSecond);
        }
    }

    qint64 nanosecondsUntilEmission(qint64 nowNs) const
    {
        return m_started ? std::max<qint64>(0, m_nextEmissionDeadlineNs - nowNs) : 0;
    }

    void markEmitted(qint64 nowNs)
    {
        const qint64 intervalNs = spectrumFrameIntervalNanoseconds(m_framesPerSecond);
        if (!m_started)
        {
            m_started = true;
            m_nextEmissionDeadlineNs = nowNs + intervalNs;
            return;
        }

        m_nextEmissionDeadlineNs += intervalNs;
        if (m_nextEmissionDeadlineNs <= nowNs)
        {
            m_nextEmissionDeadlineNs = nowNs + intervalNs;
        }
    }

    void reset()
    {
        m_started = false;
        m_nextEmissionDeadlineNs = 0;
    }

  private:
    int m_framesPerSecond{kDefaultSpectrumFramesPerSecond};
    qint64 m_nextEmissionDeadlineNs{0};
    bool m_started{false};
};
} // namespace sdr9700
