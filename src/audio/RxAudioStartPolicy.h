#pragma once

namespace sdr9700::audio
{
class RxAudioStartPolicy
{
  public:
    bool shouldStart(bool audioReady, bool workerExists, bool deviceAvailable) const
    {
        return audioReady && !workerExists && deviceAvailable && !m_initializationBlocked;
    }

    void initializationFailed() { m_initializationBlocked = true; }
    void deviceRefreshed() { m_initializationBlocked = false; }
    void reset() { m_initializationBlocked = false; }
    bool initializationBlocked() const { return m_initializationBlocked; }

  private:
    bool m_initializationBlocked{false};
};
} // namespace sdr9700::audio
