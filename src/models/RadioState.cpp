#include "RadioState.h"

#include "backend/IRadioBackend.h"

#include <algorithm>

namespace sdr9700
{

namespace
{
Vfo vfoForReceiver(uchar receiver)
{
    return receiver == 1 ? Vfo::Sub : Vfo::Main;
}
} // namespace

RadioState::RadioState(IRadioBackend* backend, QObject* parent) : QObject(parent)
{
    if (!backend)
    {
        return;
    }

    // Use the confirmation stream rather than the change-only cache stream.
    // A band transition clears receiver-specific fields here, but CachingQueue
    // deliberately retains its protocol cache. If the newly read offset or
    // tone equals that retained value, radioValueUpdated is suppressed as an
    // unchanged cache entry even though the confirmation is essential to
    // repopulating this receiver snapshot.
    connect(backend, &IRadioBackend::radioValueConfirmed, this, &RadioState::applyRadioValue);
    connect(backend, &IRadioBackend::readyChanged, this, &RadioState::setReady);
    connect(backend, &IRadioBackend::pttChanged, this, &RadioState::setTransmitting);
    connect(backend, &IRadioBackend::disconnected, this, &RadioState::invalidateSession);
}

const RadioState::Receiver& RadioState::receiver(Vfo vfo) const
{
    return m_receivers.at(receiverIndex(vfo));
}

const RadioState::BandRecall* RadioState::bandRecall(Vfo vfo, availableBands band) const
{
    const int bandIndex = radioBandUiIndex(band);
    if (bandIndex < 0)
    {
        return nullptr;
    }
    return &m_bandRecall.at(receiverIndex(vfo)).at(static_cast<std::size_t>(bandIndex));
}

std::size_t RadioState::receiverIndex(Vfo vfo)
{
    return vfo == Vfo::Sub ? 1U : 0U;
}

RadioState::BandRecall* RadioState::mutableBandRecall(Vfo vfo, availableBands band)
{
    const int bandIndex = radioBandUiIndex(band);
    if (bandIndex < 0)
    {
        return nullptr;
    }
    return &m_bandRecall.at(receiverIndex(vfo)).at(static_cast<std::size_t>(bandIndex));
}

void RadioState::applyRadioValue(Funcs func, const QVariant& value, uchar receiverId)
{
    // These values describe radio-global controls rather than either receiver.
    // Apply them before receiver validation because some reports, notably AF
    // gain, deliberately use the receiver-less 0xff identity.
    switch (func)
    {
    case funcAfGain:
        m_shared.afGain = std::clamp(value.toInt(), 0, 255);
        emit sharedStateChanged();
        return;
    case funcRFPower:
        m_shared.txPower = std::clamp(value.toInt(), 0, 255);
        emit sharedStateChanged();
        return;
    case funcLANModLevel:
        m_shared.lanModLevel = std::clamp(value.toInt(), 0, 255);
        emit sharedStateChanged();
        return;
    case funcCompressor:
        m_shared.compressorEnabled = value.toBool();
        emit sharedStateChanged();
        return;
    case funcCompressorLevel:
        m_shared.compressorLevel = std::clamp(value.toInt(), 0, 255);
        emit sharedStateChanged();
        return;
    default:
        break;
    }
    if (receiverId > 1)
    {
        return;
    }
    if (receiverId == 1 && m_shared.dualWatchEnabled == std::optional<bool>(false))
    {
        // Replies already queued before Dual Watch was disabled can arrive
        // after the OFF confirmation. They no longer describe a live SUB
        // receiver and must not repopulate the invalidated snapshot. The
        // confirmed ON transition is observed before its explicit SUB
        // identity reads, so normal repopulation resumes when SUB is live.
        return;
    }

    const Vfo vfo = vfoForReceiver(receiverId);
    Receiver& state = m_receivers.at(receiverIndex(vfo));
    BandRecall* recall = mutableBandRecall(vfo, state.band);
    bool receiverChanged = false;
    bool recallChanged = false;

    switch (func)
    {
    case funcFreqGet:
    case funcFreqSet:
    case funcSelectedFreq:
    {
        const quint64 hz = value.value<Frequency>().Hz;
        const availableBands reportedBand = radioBandForFrequency(hz);
        if (hz == 0 || reportedBand == bandUnknown)
        {
            return;
        }
        if (state.band != reportedBand)
        {
            // A frequency crossing is the only source-confirmed indication
            // that this logical receiver entered another amateur band. Clear
            // the receiver-specific operating values so replies from the old
            // band cannot be copied into the new band's recall record.
            state = Receiver{};
            state.band = reportedBand;
        }
        state.frequencyHz = hz;
        recall = mutableBandRecall(vfo, reportedBand);
        recall->frequencyHz = hz;
        receiverChanged = true;
        recallChanged = true;
        break;
    }
    case funcModeGet:
    case funcModeSet:
    case funcSelectedMode:
    {
        const ModeInfo info = value.value<ModeInfo>();
        const QString mode = info.name.trimmed().toUpper();
        if (mode.isEmpty())
        {
            return;
        }
        state.mode = mode;
        state.filter = info.filter;
        if (recall)
        {
            recall->mode = mode;
            recall->filter = info.filter;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    }
    case funcSplitStatus:
        state.duplexMode = value.value<duplexMode_t>();
        if (recall)
        {
            recall->duplexMode = *state.duplexMode;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcReadFreqOffset:
        state.repeaterOffsetHz = value.value<Frequency>().Hz;
        if (recall)
        {
            recall->repeaterOffsetHz = *state.repeaterOffsetHz;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcToneSquelchType:
        state.toneAccessMode = value.value<RptrAccessData>().accessMode;
        if (recall)
        {
            recall->toneAccessMode = *state.toneAccessMode;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcToneFreq:
        state.toneFrequency = value.value<ToneInfo>().tone;
        if (recall)
        {
            recall->toneFrequency = *state.toneFrequency;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcTSQLFreq:
        state.toneSquelchFrequency = value.value<ToneInfo>().tone;
        if (recall)
        {
            recall->toneSquelchFrequency = *state.toneSquelchFrequency;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcDTCSCode:
        state.dtcsCode = value.value<ToneInfo>().tone;
        if (recall)
        {
            recall->dtcsCode = *state.dtcsCode;
            recallChanged = true;
        }
        receiverChanged = true;
        break;
    case funcAGCTimeConstant:
        state.agcMode = std::clamp(value.toInt(), 0, 3);
        receiverChanged = true;
        break;
    case funcAttenuator:
        state.attenuatorEnabled = value.toInt() != 0;
        receiverChanged = true;
        break;
    case funcNoiseBlanker:
        state.nbEnabled = value.toBool();
        receiverChanged = true;
        break;
    case funcNBLevel:
        state.nbLevel = std::clamp(value.toInt(), 0, 255);
        receiverChanged = true;
        break;
    case funcAutoNotch:
        state.autoNotchEnabled = value.toBool();
        receiverChanged = true;
        break;
    case funcManualNotch:
        state.manualNotchEnabled = value.toBool();
        receiverChanged = true;
        break;
    case funcNoiseReduction:
        state.nrEnabled = value.toBool();
        receiverChanged = true;
        break;
    case funcNRLevel:
        state.nrLevel = std::clamp(value.toInt(), 0, 255);
        receiverChanged = true;
        break;
    case funcPreamp:
        state.preampLevel = std::clamp(value.toInt(), 0, 3);
        receiverChanged = true;
        break;
    case funcRfGain:
        state.rfGain = std::clamp(value.toInt(), 0, 255);
        receiverChanged = true;
        break;
    case funcSquelch:
        state.squelch = std::clamp(value.toInt(), 0, 255);
        receiverChanged = true;
        break;
    case funcVFOBandMS:
        m_shared.selectedVfo = value.toBool() ? Vfo::Sub : Vfo::Main;
        emit sharedStateChanged();
        return;
    case funcVFODualWatch:
        m_shared.dualWatchEnabled = value.toBool();
        if (!*m_shared.dualWatchEnabled)
        {
            // With Dual Watch disabled the radio no longer provides a live
            // SUB receiver. Invalidate that snapshot while retaining its
            // session-local band recall for a later re-enable.
            invalidateReceiver(Vfo::Sub);
        }
        emit sharedStateChanged();
        return;
    case funcDialLock:
        m_shared.dialLockEnabled = value.toBool();
        emit sharedStateChanged();
        return;
    default:
        return;
    }

    if (receiverChanged)
    {
        emit receiverStateChanged(vfo);
    }
    if (recallChanged)
    {
        emit bandRecallChanged(vfo, state.band);
    }
}

void RadioState::setReady(bool ready)
{
    if (m_shared.ready == ready)
    {
        return;
    }
    m_shared.ready = ready;
    if (!ready)
    {
        invalidateReceiver(Vfo::Main);
        invalidateReceiver(Vfo::Sub);
        m_shared.selectedVfo.reset();
        m_shared.dualWatchEnabled.reset();
        m_shared.dialLockEnabled.reset();
        m_shared.afGain.reset();
        m_shared.txPower.reset();
        m_shared.lanModLevel.reset();
        m_shared.compressorEnabled.reset();
        m_shared.compressorLevel.reset();
    }
    emit sharedStateChanged();
}

void RadioState::setTransmitting(bool transmitting)
{
    if (m_shared.transmitting == transmitting)
    {
        return;
    }
    m_shared.transmitting = transmitting;
    emit sharedStateChanged();
}

void RadioState::invalidateReceiver(Vfo vfo)
{
    m_receivers.at(receiverIndex(vfo)) = Receiver{};
    emit receiverStateChanged(vfo);
}

void RadioState::invalidateSession()
{
    m_shared = Shared{};
    m_bandRecall = {};
    invalidateReceiver(Vfo::Main);
    invalidateReceiver(Vfo::Sub);
    emit sharedStateChanged();
}

} // namespace sdr9700
