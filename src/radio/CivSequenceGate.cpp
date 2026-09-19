#include "CivSequenceGate.h"

#include <algorithm>

qint16 CivSequenceGate::distance(quint16 from, quint16 to)
{
    return static_cast<qint16>(to - from);
}

CivSequenceGateResult CivSequenceGate::accept(quint16 sequence, const QByteArray& payload)
{
    if (m_recentSequences.contains(sequence))
    {
        ++m_diagnostics.duplicatesSuppressed;
        return std::nullopt;
    }

    if (m_started && distance(m_expected, sequence) < 0)
    {
        ++m_diagnostics.reordered;
    }
    else
    {
        m_expected = static_cast<quint16>(sequence + 1);
    }
    m_started = true;

    m_recentSequences.insert(sequence);
    m_recentSequenceOrder.enqueue(sequence);
    while (m_recentSequenceOrder.size() > kRecentSequenceWindow)
    {
        m_recentSequences.remove(m_recentSequenceOrder.dequeue());
    }

    ++m_diagnostics.delivered;
    m_diagnostics.highWaterMark = std::max(m_diagnostics.highWaterMark, m_recentSequences.size());
    return payload;
}

void CivSequenceGate::reset()
{
    m_started = false;
    m_expected = 0;
    m_recentSequenceOrder.clear();
    m_recentSequences.clear();
    m_diagnostics = {};
}

const CivSequenceGateDiagnostics& CivSequenceGate::diagnostics() const
{
    return m_diagnostics;
}
