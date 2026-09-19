#include "SystemStats.h"

#include <QList>
#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include <QFile>
#elif defined(Q_OS_MAC)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/task_info.h>
#endif

#include <algorithm>

SystemStats SystemStatsProvider::sample()
{
    SystemStats stats;
    const std::optional<CpuTicks> currentCpuTicks = readCpuTicks();
    if (currentCpuTicks)
    {
        if (m_previousCpuTicks)
        {
            stats.cpuPercent = calculateCpuPercent(*m_previousCpuTicks, *currentCpuTicks);
        }
        m_previousCpuTicks = currentCpuTicks;
    }
    stats.processResidentBytes = readProcessResidentBytes();
    return stats;
}

std::optional<double> SystemStatsProvider::calculateCpuPercent(const CpuTicks& previous, const CpuTicks& current)
{
    if (current.active < previous.active || current.idle < previous.idle)
    {
        return std::nullopt;
    }

    const quint64 activeDelta = current.active - previous.active;
    const quint64 idleDelta = current.idle - previous.idle;
    const quint64 totalDelta = activeDelta + idleDelta;
    if (totalDelta == 0)
    {
        return std::nullopt;
    }

    return std::clamp(100.0 * static_cast<double>(activeDelta) / static_cast<double>(totalDelta), 0.0, 100.0);
}

std::optional<CpuTicks> SystemStatsProvider::readCpuTicks() const
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    return parseLinuxCpuTicks(file.readLine());
#elif defined(Q_OS_MAC)
    natural_t cpuCount = 0;
    natural_t infoCount = 0;
    processor_info_array_t info = nullptr;
    const host_t host = mach_host_self();
    const kern_return_t result = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &cpuCount, &info, &infoCount);
    mach_port_deallocate(mach_task_self(), host);
    if (result != KERN_SUCCESS || !info)
    {
        return std::nullopt;
    }

    quint64 active = 0;
    quint64 idle = 0;
    const auto* loads = reinterpret_cast<processor_cpu_load_info_t>(info);
    for (natural_t cpu = 0; cpu < cpuCount; ++cpu)
    {
        active += loads[cpu].cpu_ticks[CPU_STATE_USER];
        active += loads[cpu].cpu_ticks[CPU_STATE_SYSTEM];
        active += loads[cpu].cpu_ticks[CPU_STATE_NICE];
        idle += loads[cpu].cpu_ticks[CPU_STATE_IDLE];
    }

    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), infoCount * sizeof(integer_t));
    return CpuTicks{active, idle};
#else
    return std::nullopt;
#endif
}

std::optional<CpuTicks> SystemStatsProvider::parseLinuxCpuTicks(const QByteArray& cpuLine)
{
    const QList<QByteArray> parts = cpuLine.simplified().split(' ');
    if (parts.isEmpty() || parts.first() != "cpu")
    {
        return std::nullopt;
    }

    QList<quint64> values;
    values.reserve(parts.size() - 1);
    for (qsizetype index = 1; index < parts.size(); ++index)
    {
        bool valid = false;
        const quint64 value = parts[index].toULongLong(&valid);
        if (!valid)
        {
            return std::nullopt;
        }
        values.append(value);
    }
    if (values.size() < 4)
    {
        return std::nullopt;
    }

    const quint64 idle = values[3] + (values.size() > 4 ? values[4] : 0);
    quint64 total = 0;
    for (qsizetype index = 0; index < values.size(); ++index)
    {
        // Linux reports guest and guest_nice time within user and nice as well
        // as in fields 8 and 9, so exclude those duplicate fields.
        if (index != 8 && index != 9)
        {
            total += values[index];
        }
    }
    return CpuTicks{total - idle, idle};
}

std::optional<quint64> SystemStatsProvider::readProcessResidentBytes() const
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/status"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    const QList<QByteArray> lines = file.readAll().split('\n');
    const auto line =
        std::find_if(lines.cbegin(), lines.cend(), [](const QByteArray& value) { return value.startsWith("VmRSS:"); });
    if (line == lines.cend())
    {
        return std::nullopt;
    }

    const QList<QByteArray> parts = line->simplified().split(' ');
    if (parts.size() < 2)
    {
        return std::nullopt;
    }
    return parts[1].toULongLong() * 1024;
#elif defined(Q_OS_MAC)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &infoCount);
    if (result != KERN_SUCCESS)
    {
        return std::nullopt;
    }
    return static_cast<quint64>(info.resident_size);
#else
    return std::nullopt;
#endif
}
