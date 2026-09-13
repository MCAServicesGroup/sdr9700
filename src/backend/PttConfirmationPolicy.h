#pragma once

namespace sdr9700
{
class PttConfirmationPolicy
{
  public:
    bool requestOn()
    {
        const bool shouldSend = !m_desiredActive || m_offPending;
        m_desiredActive = true;
        m_offPending = false;
        return shouldSend;
    }

    void requestOff()
    {
        m_desiredActive = false;
        m_offPending = true;
    }

    void confirm(bool active)
    {
        m_confirmedActive = active;
        if (!active)
        {
            m_offPending = false;
        }
    }

    void reset()
    {
        m_confirmedActive = false;
        m_desiredActive = false;
        m_offPending = false;
    }

    bool confirmedActive() const { return m_confirmedActive; }
    bool desiredActive() const { return m_desiredActive; }
    bool offPending() const { return m_offPending; }
    bool safetyActive() const { return m_confirmedActive || m_desiredActive || m_offPending; }
    bool transmitMetersActive() const { return m_desiredActive || (m_confirmedActive && !m_offPending); }
    bool shouldPublishReadback(bool active) const { return active != m_confirmedActive || (active && m_offPending); }

  private:
    bool m_confirmedActive{false};
    bool m_desiredActive{false};
    bool m_offPending{false};
};
} // namespace sdr9700
