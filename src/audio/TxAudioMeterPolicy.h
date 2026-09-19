#pragma once

#include <QMetaType>
#include <QtGlobal>

#include <cmath>

namespace sdr9700::audio
{

// Per-block local microphone measurement produced by AudioConverter after
// application gain and channel mixing, and before resampling. This carries
// measurement properties only: activity, peak hold, and decay are presentation
// policy derived on the GUI thread, so no timing or display state enters the
// audio pipeline.
struct TxAudioMeterBlock
{
    float peak{0.0F};
    double sumSquares{0.0};
    quint32 sampleCount{0};
    quint32 fullScaleCount{0};
    bool valid{false};
};

// Local processed-input meter constants, ratified by the maintainer on
// 2026-09-18. These describe the operator's local capture chain and are a local
// recording recommendation, not an IC-9700 requirement: this meter cannot
// report radio modulation depth. For SSB the radio-side drive indication is the
// ALC meter; AM, FM, and DV require Monitor or another receiver.
inline constexpr float kFullScaleMagnitude = 1.0F;
inline constexpr double kMeterDisplayFloorDb = -60.0;
inline constexpr double kMeterDisplayCeilingDb = 0.0;
inline constexpr double kHeadroomRmsMinDb = -24.0;
inline constexpr double kHeadroomRmsMaxDb = -12.0;
inline constexpr double kHeadroomPeakMinDb = -12.0;
inline constexpr double kHeadroomPeakMaxDb = -3.0;
inline constexpr double kNearFullScaleDb = -1.0;
inline constexpr double kActivityOnDb = -50.0;
inline constexpr double kActivityOffDb = -55.0;
inline constexpr int kActivityOffHoldMs = 1500;
inline constexpr int kPeakHoldMs = 1000;
inline constexpr double kPeakDecayDbPerSec = 20.0;
inline constexpr int kFullScaleCountHoldMs = 3000;

// Convert a sample magnitude to dBFS. Exact zero has no logarithm, so callers
// distinguish digital silence through sampleCount and sumSquares before using
// this value. Post-mix floats are not clamped to unity, but the nominal scale
// is deliberately not extended above 0 dBFS: overage is reported through the
// full-scale sample count instead.
inline double dbfsFromMagnitude(double magnitude)
{
    if (!(magnitude > 0.0))
    {
        return kMeterDisplayFloorDb;
    }
    return qBound(kMeterDisplayFloorDb, 20.0 * std::log10(magnitude), kMeterDisplayCeilingDb);
}

inline double blockRms(const TxAudioMeterBlock& block)
{
    if (block.sampleCount == 0)
    {
        return 0.0;
    }
    return std::sqrt(block.sumSquares / static_cast<double>(block.sampleCount));
}

// Combine two measurement blocks exactly: peak by maximum, energy by summation
// before any square root, counts by summation. Carrying sampleCount keeps the
// equal-block-size assumption out of this interface.
inline TxAudioMeterBlock aggregate(const TxAudioMeterBlock& lhs, const TxAudioMeterBlock& rhs)
{
    if (!lhs.valid)
    {
        return rhs;
    }
    if (!rhs.valid)
    {
        return lhs;
    }

    TxAudioMeterBlock combined;
    combined.peak = qMax(lhs.peak, rhs.peak);
    combined.sumSquares = lhs.sumSquares + rhs.sumSquares;
    combined.sampleCount = lhs.sampleCount + rhs.sampleCount;
    combined.fullScaleCount = lhs.fullScaleCount + rhs.fullScaleCount;
    combined.valid = true;
    return combined;
}

enum class TxAudioMeterState
{
    // No sample yet, disconnected, input failure, converter failure, or an
    // audio-device restart that has not yet produced a measurement.
    Invalid,
    NoActivity,
    SignalPresent,
    RecommendedHeadroom,
    NearFullScale,
    FullScaleDetected,
};

// Derives activity hysteresis, peak hold and decay, and the full-scale hold
// from measurement blocks. Deliberately free of Qt timers: the caller supplies
// a monotonic millisecond clock so the behaviour is deterministic under test.
//
// Lifecycle: this state is invalidated by disconnect, input failure, converter
// failure, and audio-device restart. It is never invalidated by PTT or DTMF
// transitions, which govern encoding authorization only; the local meter must
// stay continuous and identical across PTT states.
class TxAudioMeterPresentation
{
  public:
    void reset()
    {
        m_valid = false;
        m_active = false;
        m_rmsDb = kMeterDisplayFloorDb;
        m_peakDb = kMeterDisplayFloorDb;
        m_heldPeakDb = kMeterDisplayFloorDb;
        m_fullScaleCount = 0;
        m_haveClock = false;
        m_activityOffSinceMs = 0;
        m_activityFalling = false;
        m_peakHoldUntilMs = 0;
        m_fullScaleClearAtMs = 0;
        m_lastUpdateMs = 0;
    }

    void accept(const TxAudioMeterBlock& block, qint64 nowMs)
    {
        if (!block.valid || block.sampleCount == 0)
        {
            tick(nowMs);
            return;
        }

        m_valid = true;
        m_rmsDb = dbfsFromMagnitude(blockRms(block));
        m_peakDb = dbfsFromMagnitude(static_cast<double>(block.peak));

        advanceHolds(nowMs);
        updateActivity(m_rmsDb, nowMs);

        if (m_peakDb >= m_heldPeakDb)
        {
            m_heldPeakDb = m_peakDb;
            m_peakHoldUntilMs = nowMs + kPeakHoldMs;
        }

        if (block.fullScaleCount > 0)
        {
            m_fullScaleCount += block.fullScaleCount;
            m_fullScaleClearAtMs = nowMs + kFullScaleCountHoldMs;
        }

        m_lastUpdateMs = nowMs;
        m_haveClock = true;
    }

    // Advance holds and decay when no new block has arrived.
    void tick(qint64 nowMs)
    {
        if (!m_valid)
        {
            return;
        }
        advanceHolds(nowMs);
        updateActivity(m_rmsDb, nowMs);
        m_lastUpdateMs = nowMs;
        m_haveClock = true;
    }

    bool valid() const { return m_valid; }
    bool active() const { return m_active; }
    double rmsDb() const { return m_rmsDb; }
    double heldPeakDb() const { return m_heldPeakDb; }
    quint32 fullScaleCount() const { return m_fullScaleCount; }

    TxAudioMeterState state() const
    {
        if (!m_valid)
        {
            return TxAudioMeterState::Invalid;
        }
        // Clipping and near-clipping outrank activity so a warning is never
        // hidden by the speech gap that follows it.
        if (m_fullScaleCount > 0)
        {
            return TxAudioMeterState::FullScaleDetected;
        }
        if (m_heldPeakDb >= kNearFullScaleDb)
        {
            return TxAudioMeterState::NearFullScale;
        }
        if (!m_active)
        {
            return TxAudioMeterState::NoActivity;
        }
        if (m_rmsDb >= kHeadroomRmsMinDb && m_rmsDb <= kHeadroomRmsMaxDb && m_heldPeakDb >= kHeadroomPeakMinDb &&
            m_heldPeakDb <= kHeadroomPeakMaxDb)
        {
            return TxAudioMeterState::RecommendedHeadroom;
        }
        return TxAudioMeterState::SignalPresent;
    }

  private:
    void advanceHolds(qint64 nowMs)
    {
        if (!m_haveClock)
        {
            return;
        }
        // Decay only over the part of the interval that fell outside the hold
        // window. Using the whole interval would over-decay the first update
        // after the hold expires, dropping the held peak by up to a full
        // inter-update period's worth of decay at the instant it is released.
        const qint64 decayFromMs = qMax(m_lastUpdateMs, m_peakHoldUntilMs);
        const qint64 decayMs = nowMs - decayFromMs;
        if (decayMs > 0)
        {
            const double decayDb = kPeakDecayDbPerSec * (static_cast<double>(decayMs) / 1000.0);
            m_heldPeakDb = qMax(kMeterDisplayFloorDb, m_heldPeakDb - decayDb);
        }
        if (m_fullScaleCount > 0 && nowMs >= m_fullScaleClearAtMs)
        {
            m_fullScaleCount = 0;
        }
    }

    void updateActivity(double currentRmsDb, qint64 nowMs)
    {
        if (m_active)
        {
            if (currentRmsDb < kActivityOffDb)
            {
                if (!m_activityFalling)
                {
                    m_activityFalling = true;
                    m_activityOffSinceMs = nowMs;
                }
                else if (nowMs - m_activityOffSinceMs >= kActivityOffHoldMs)
                {
                    m_active = false;
                    m_activityFalling = false;
                }
            }
            else
            {
                m_activityFalling = false;
            }
            return;
        }

        if (currentRmsDb >= kActivityOnDb)
        {
            m_active = true;
            m_activityFalling = false;
        }
    }

    bool m_valid{false};
    bool m_active{false};
    bool m_haveClock{false};
    bool m_activityFalling{false};
    double m_rmsDb{kMeterDisplayFloorDb};
    double m_peakDb{kMeterDisplayFloorDb};
    double m_heldPeakDb{kMeterDisplayFloorDb};
    quint32 m_fullScaleCount{0};
    qint64 m_activityOffSinceMs{0};
    qint64 m_peakHoldUntilMs{0};
    qint64 m_fullScaleClearAtMs{0};
    qint64 m_lastUpdateMs{0};
};

} // namespace sdr9700::audio

Q_DECLARE_METATYPE(sdr9700::audio::TxAudioMeterBlock)
