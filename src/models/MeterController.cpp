#include "MeterController.h"

#include <algorithm>
#include <QTimer>
#include <QtGlobal>

namespace
{
// Keep GUI updates coalesced to approximately one display frame without
// adding a noticeable delay after each 10 Hz radio meter reply.
constexpr int kMeterFlushIntervalMs = 16;
} // namespace

MeterController::MeterController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<MeterSnapshot>("MeterSnapshot");
    qRegisterMetaType<sdr9700::audio::TxAudioMeterBlock>("sdr9700::audio::TxAudioMeterBlock");
    m_txClock.start();

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kMeterFlushIntervalMs);
    m_flushTimer->setTimerType(Qt::PreciseTimer);
    connect(m_flushTimer, &QTimer::timeout, this, &MeterController::flush);
}

void MeterController::reset()
{
    if (m_flushTimer)
    {
        m_flushTimer->stop();
    }
    m_snapshot = {};
    m_dirty = false;
    m_txPresentation.reset();
    m_pendingTxBlock = {};
    emit snapshotChanged(m_snapshot);
}

void MeterController::resetTransmitMeters()
{
    m_snapshot.powerWatts = 0.0;
    m_snapshot.powerValid = false;
    m_snapshot.swr = 1.0;
    m_snapshot.swrValid = false;
    m_snapshot.alc = 0.0;
    m_snapshot.alcValid = false;
    m_snapshot.compressionDb = 0.0;
    m_snapshot.compressionValid = false;
    m_snapshot.voltageVolts = 0.0;
    m_snapshot.voltageValid = false;
    m_snapshot.currentAmps = 0.0;
    m_snapshot.currentValid = false;
    // The local processed-input meter is deliberately NOT cleared here. These
    // are radio transmit meters, which stop being current at unkey; the local
    // microphone meter must stay continuous and identical across PTT states so
    // an operator can set level before transmitting.
    scheduleFlush();
}

void MeterController::resetReceiveMeter()
{
    m_snapshot.sMeter = 0;
    m_snapshot.sMeterValid = false;
    scheduleFlush();
}

void MeterController::setSMeter(int value)
{
    m_snapshot.sMeter = std::clamp(value, 0, 255);
    m_snapshot.sMeterValid = true;
    scheduleFlush();
}

void MeterController::setPowerMeter(double watts)
{
    m_snapshot.powerWatts = std::clamp(watts, 0.0, 120.0);
    m_snapshot.powerValid = true;
    if (m_snapshot.powerWatts == 0.0)
    {
        m_snapshot.swr = 1.0;
        m_snapshot.swrValid = false;
    }
    scheduleFlush();
}

void MeterController::setSwr(double swr)
{
    m_snapshot.swr = std::clamp(swr, 1.0, 6.0);
    m_snapshot.swrValid = m_snapshot.powerValid && m_snapshot.powerWatts > 0.0;
    scheduleFlush();
}

void MeterController::setAlc(double alc)
{
    m_snapshot.alc = std::clamp(alc, 0.0, 2.0);
    m_snapshot.alcValid = true;
    scheduleFlush();
}

void MeterController::setCompressionMeter(double db)
{
    m_snapshot.compressionDb = std::clamp(db, 0.0, 25.5);
    m_snapshot.compressionValid = true;
    scheduleFlush();
}

void MeterController::setVoltageMeter(double volts)
{
    m_snapshot.voltageVolts = std::clamp(volts, 0.0, 16.0);
    m_snapshot.voltageValid = true;
    scheduleFlush();
}

void MeterController::setCurrentMeter(double amps)
{
    m_snapshot.currentAmps = std::clamp(amps, 0.0, 20.0);
    m_snapshot.currentValid = true;
    scheduleFlush();
}

void MeterController::setTransmitAudioMeter(const sdr9700::audio::TxAudioMeterBlock& block)
{
    if (!block.valid)
    {
        // The capture path reports that no measurement exists: disconnect,
        // input or converter failure, or an audio-device restart.
        resetTransmitAudioMeter();
        return;
    }

    // Blocks arriving between flushes are combined exactly: peak by maximum,
    // energy by summation before any square root, counts by summation.
    m_pendingTxBlock = sdr9700::audio::aggregate(m_pendingTxBlock, block);
    scheduleFlush();
}

void MeterController::resetTransmitAudioMeter()
{
    m_txPresentation.reset();
    m_pendingTxBlock = {};
    advanceTransmitAudioMeter();
    scheduleFlush();
}

void MeterController::advanceTransmitAudioMeter()
{
    const qint64 nowMs = m_txClock.isValid() ? m_txClock.elapsed() : 0;
    if (m_pendingTxBlock.valid)
    {
        m_txPresentation.accept(m_pendingTxBlock, nowMs);
        m_pendingTxBlock = {};
    }
    else
    {
        m_txPresentation.tick(nowMs);
    }

    const sdr9700::audio::TxAudioMeterState state = m_txPresentation.state();
    const double rmsDb = m_txPresentation.rmsDb();
    const double peakDb = m_txPresentation.heldPeakDb();
    const quint32 fullScaleCount = m_txPresentation.fullScaleCount();
    if (state != m_snapshot.txAudioState || !qFuzzyCompare(rmsDb + 1.0, m_snapshot.txAudioRmsDb + 1.0) ||
        !qFuzzyCompare(peakDb + 1.0, m_snapshot.txAudioPeakDb + 1.0) ||
        fullScaleCount != m_snapshot.txAudioFullScaleCount)
    {
        m_snapshot.txAudioState = state;
        m_snapshot.txAudioRmsDb = rmsDb;
        m_snapshot.txAudioPeakDb = peakDb;
        m_snapshot.txAudioFullScaleCount = fullScaleCount;
        m_dirty = true;
    }
}

bool MeterController::transmitAudioMeterSettling() const
{
    // Peak decay and the full-scale hold continue after capture stops, so keep
    // ticking until the meter has actually settled.
    return m_txPresentation.valid() && (m_txPresentation.active() || m_txPresentation.fullScaleCount() > 0 ||
                                        m_txPresentation.heldPeakDb() > sdr9700::audio::kMeterDisplayFloorDb);
}

void MeterController::scheduleFlush()
{
    m_dirty = true;
    if (m_flushTimer && !m_flushTimer->isActive())
    {
        m_flushTimer->start();
    }
}

void MeterController::flush()
{
    advanceTransmitAudioMeter();

    if (m_dirty)
    {
        m_dirty = false;
        emit snapshotChanged(m_snapshot);
    }

    if (transmitAudioMeterSettling() && m_flushTimer && !m_flushTimer->isActive())
    {
        m_flushTimer->start();
    }
}
