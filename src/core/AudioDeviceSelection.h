#pragma once

#include <QByteArray>
#include <QList>

namespace sdr9700
{
// An empty preference follows the system default. Keep an unavailable saved ID
// in settings so reconnecting that device restores the operator's chosen route.
// Device only needs id(); this also allows testing hotplug without audio hardware.
template <typename Device>
Device selectAudioDevice(const QList<Device>& devices, const QByteArray& preferredID, const Device& defaultDevice)
{
    if (!preferredID.isEmpty())
    {
        for (const Device& device : devices)
        {
            if (device.id() == preferredID)
            {
                return device;
            }
        }
    }
    return defaultDevice;
}
} // namespace sdr9700
