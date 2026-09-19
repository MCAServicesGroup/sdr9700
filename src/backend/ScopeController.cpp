#include "ScopeController.h"
#include "LogCategories.h"
#include "ScopeAdapter.h"

#include <algorithm>
#include <QElapsedTimer>
#include <QTimer>
#include <limits>
#include <utility>

namespace
{
constexpr qint64 kScopeFrameStallWarningMs = 500;
} // namespace

ScopeController::ScopeController(QObject* parent) : QObject(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setTimerType(Qt::PreciseTimer);
    m_flushTimer->setInterval(sdr9700::spectrumFrameIntervalMs(m_framesPerSecond));
    connect(m_flushTimer, &QTimer::timeout, this, &ScopeController::flushLatestFrame);
    qInfo(logSpectrumScope()).noquote().nospace()
        << "Spectrum frame pacing fps=" << m_framesPerSecond << " intervalMs=" << m_flushTimer->interval();
}

void ScopeController::setFramesPerSecond(int requestedFramesPerSecond)
{
    const int normalized = sdr9700::normalizedSpectrumFramesPerSecond(requestedFramesPerSecond);
    if (m_framesPerSecond == normalized)
    {
        return;
    }

    m_framesPerSecond = normalized;
    m_flushTimer->setInterval(sdr9700::spectrumFrameIntervalMs(normalized));
    qInfo(logSpectrumScope()).noquote().nospace()
        << "Spectrum frame pacing fps=" << m_framesPerSecond << " intervalMs=" << m_flushTimer->interval();
    if (m_flushTimer->isActive())
    {
        m_flushTimer->stop();
    }
    if (m_pacingClock.isValid())
    {
        m_pacingPolicy.setFramesPerSecond(m_framesPerSecond, m_pacingClock.nsecsElapsed());
    }
    else
    {
        m_pacingPolicy.setFramesPerSecond(m_framesPerSecond, 0);
    }
    scheduleFlush();
}

void ScopeController::reset()
{
    if (m_flushTimer)
    {
        m_flushTimer->stop();
    }
    m_pendingFrame = {};
    m_hasPendingFrame = false;
    m_frameArrivalClock.invalidate();
    m_pacingClock.invalidate();
    m_pacingPolicy.reset();
}

void ScopeController::acceptScopeData(const ScopeData& data)
{
    if (!data.valid || data.data.isEmpty())
    {
        return;
    }

    if (m_frameArrivalClock.isValid() && m_frameArrivalClock.elapsed() >= kScopeFrameStallWarningMs)
    {
        qWarning(logSpectrumScope()).noquote().nospace()
            << "Scope frame arrival stalled elapsedMs=" << m_frameArrivalClock.elapsed()
            << " receiver=" << data.receiver << " start=" << data.startFreq << " end=" << data.endFreq;
    }
    m_frameArrivalClock.restart();

    m_pendingFrame = data;
    m_hasPendingFrame = true;
    scheduleFlush();
}

void ScopeController::scheduleFlush()
{
    if (!m_flushTimer || m_flushTimer->isActive() || !m_hasPendingFrame)
    {
        return;
    }

    if (!m_pacingClock.isValid())
    {
        m_pacingClock.start();
        // Queue the first frame at zero delay so additional frames delivered
        // in the same event-loop turn still coalesce to the newest one.
        m_flushTimer->start(0);
        return;
    }

    const qint64 remainingNs = m_pacingPolicy.nanosecondsUntilEmission(m_pacingClock.nsecsElapsed());
    if (remainingNs <= 0)
    {
        m_flushTimer->start(0);
        return;
    }
    constexpr qint64 kNanosecondsPerMillisecond = 1'000'000;
    const qint64 remainingMs = (remainingNs + kNanosecondsPerMillisecond - 1) / kNanosecondsPerMillisecond;
    m_flushTimer->start(int(std::min<qint64>(remainingMs, std::numeric_limits<int>::max())));
}

void ScopeController::flushLatestFrame()
{
    if (!m_hasPendingFrame)
    {
        return;
    }

    // Move the latest pending frame out of the controller. Scope data arrives
    // continuously, and this avoids copying the raw CI-V byte buffer once per
    // flushed frame. The member is reset immediately so acceptScopeData() can
    // safely replace it before the next timer flush. If a future Qt metatype
    // change requires the emitted source object to remain intact, copying here
    // is the intentionally localized fallback.
    const ScopeData frame = std::move(m_pendingFrame);
    m_pendingFrame = {};
    m_hasPendingFrame = false;
    const qint64 nowNs = m_pacingClock.nsecsElapsed();
    m_pacingPolicy.markEmitted(nowNs);
    emit scopeDataReceived();

    // Reuse the conversion buffer between frames. The queued signal delivery
    // below still gives receivers their own safe copy when crossing threads,
    // but this removes one allocation from the radio-data hot path.
    ScopeAdapter::toLevels(frame.data, &m_levelsScratch);
    if (logSpectrumScope().isDebugEnabled())
    {
        static QElapsedTimer statsTimer;
        if (!statsTimer.isValid() || statsTimer.elapsed() >= 1000)
        {
            int rawMin = std::numeric_limits<int>::max();
            int rawMax = std::numeric_limits<int>::min();
            int rawZeros = 0;
            qint64 rawTotal = 0;
            for (const unsigned char raw : frame.data)
            {
                const int value = static_cast<int>(raw);
                rawMin = std::min(rawMin, value);
                rawMax = std::max(rawMax, value);
                rawTotal += value;
                if (value == 0)
                {
                    ++rawZeros;
                }
            }
            qDebug(logSpectrumScope()).noquote().nospace()
                << "Spectrum scope stats start=" << frame.startFreq << " end=" << frame.endFreq << " rawMin=" << rawMin
                << " rawMax=" << rawMax << " rawAverage=" << (double(rawTotal) / double(frame.data.size()))
                << " rawZeros=" << rawZeros << " dataLen=" << frame.data.size();
            statsTimer.restart();
        }
    }

    emit spectrumDataReady(m_levelsScratch, frame.startFreq, frame.endFreq, frame.oor);
}
