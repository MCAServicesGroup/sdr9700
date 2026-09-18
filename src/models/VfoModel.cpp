#include "VfoModel.h"
#include "backend/IRadioBackend.h"

#include <QtGlobal>

VfoModel::VfoModel(IRadioBackend* backend, QObject* parent) : QObject(parent), m_backend(backend) {}

QStringList VfoModel::availableModes()
{
    return {"FM", "USB", "LSB", "AM", "CW", "CW-R", "RTTY", "DV", "DD"};
}

void VfoModel::setFrequencyHz(quint64 hz)
{
    if (m_backend)
    {
        m_backend->setFrequencyHz(hz);
    }
}

void VfoModel::setMode(const QString& modeName)
{
    if (m_backend)
    {
        m_backend->setMode(modeName);
    }
}

void VfoModel::setFilterWidth(int lowHz, int highHz)
{
    if (m_filterLow == lowHz && m_filterHigh == highHz)
    {
        return;
    }
    m_filterLow = lowHz;
    m_filterHigh = highHz;
    emit filterChanged(lowHz, highHz);
    if (m_backend)
    {
        m_backend->setFilterWidth(lowHz, highHz);
    }
}

void VfoModel::setRfGain(int level)
{
    if (m_backend)
    {
        m_backend->setRfGain(qBound(0, level, 255));
    }
}

void VfoModel::setAfGain(int level)
{
    m_afGain = level;
    emit afGainChanged(level);
    if (m_backend)
    {
        m_backend->setAfGain(level);
    }
}

void VfoModel::setSquelch(bool on, int level)
{
    if (m_backend)
    {
        m_backend->setSquelch(on, qBound(0, level, 255));
    }
}

void VfoModel::setNrEnabled(bool on)
{
    if (m_backend)
    {
        m_backend->setNrEnabled(on);
    }
}

void VfoModel::setNrLevel(int level)
{
    if (m_backend)
    {
        m_backend->setNrLevel(qBound(1, level, 15));
    }
}

void VfoModel::setNbEnabled(bool on)
{
    if (m_backend)
    {
        m_backend->setNbEnabled(on);
    }
}

void VfoModel::setNbLevel(int level)
{
    if (m_backend)
    {
        m_backend->setNbLevel(qBound(1, level, 10));
    }
}

void VfoModel::setPreampLevel(int level)
{
    level = qBound(0, level, 3);
    if (m_backend)
    {
        m_backend->setPreampLevel(level);
    }
}

void VfoModel::setAttenuatorEnabled(bool on)
{
    if (m_backend)
    {
        m_backend->setAttenuatorEnabled(on);
    }
}

void VfoModel::setTxPower(int level)
{
    if (m_backend)
    {
        m_backend->setTxPower(qBound(0, level, 255));
    }
}

void VfoModel::setAgcMode(const QString& modeName)
{
    if (m_backend)
    {
        m_backend->setAgcMode(modeName);
    }
}

void VfoModel::setAutoNotch(bool on)
{
    if (m_backend)
    {
        m_backend->setAutoNotch(on);
    }
}

void VfoModel::setManualNotch(bool on)
{
    if (m_backend)
    {
        m_backend->setManualNotch(on);
    }
}

void VfoModel::setCompressor(bool on)
{
    if (m_backend)
    {
        m_backend->setCompressor(on);
    }
}

void VfoModel::setCompressorLevel(int level)
{
    if (m_backend)
    {
        m_backend->setCompressorLevel(qBound(0, level, 255));
    }
}

void VfoModel::setXfcEnabled(bool on)
{
    if (m_backend)
    {
        m_backend->setXfcEnabled(on);
    }
}

void VfoModel::setRitEnabled(bool on)
{
    if (m_backend)
    {
        m_backend->setRitEnabled(on);
    }
}

void VfoModel::setRitOffset(short hz)
{
    hz = qBound(static_cast<short>(-999), hz, static_cast<short>(999));
    if (m_backend)
    {
        m_backend->setRitOffset(hz);
    }
}

void VfoModel::applyRitEnabled(bool on)
{
    if (m_ritOn == on)
    {
        return;
    }
    m_ritOn = on;
    emit ritChanged(on, m_ritHz);
}

void VfoModel::applyRitOffset(short hz)
{
    hz = qBound(static_cast<short>(-999), hz, static_cast<short>(999));
    if (m_ritHz == hz)
    {
        return;
    }
    m_ritHz = hz;
    emit ritChanged(m_ritOn, hz);
}

void VfoModel::applyAutoNotch(bool on)
{
    m_autoNotchOn = on;
    emit autoNotchChanged(on);
}

void VfoModel::applyManualNotch(bool on)
{
    m_manualNotchOn = on;
    emit manualNotchChanged(on);
}

void VfoModel::applyCompressor(bool on)
{
    m_compressorOn = on;
    emit compressorChanged(on);
}

void VfoModel::applyCompressorLevel(int level)
{
    level = qBound(0, level, 255);
    const bool wasKnown = m_compressorLevel.has_value();
    if (wasKnown && *m_compressorLevel == level)
    {
        return;
    }
    m_compressorLevel = level;
    if (!wasKnown)
    {
        emit compressorLevelKnownChanged(true);
    }
    emit compressorLevelChanged(level);
}

void VfoModel::clearCompressorLevel()
{
    if (!m_compressorLevel.has_value())
    {
        return;
    }
    m_compressorLevel.reset();
    emit compressorLevelKnownChanged(false);
}

void VfoModel::applyXfcEnabled(bool on)
{
    if (m_xfcOn == on)
    {
        return;
    }
    m_xfcOn = on;
    emit xfcChanged(on);
}

void VfoModel::applyAgcMode(const QString& modeName)
{
    m_agcMode = modeName;
    emit agcModeChanged(modeName);
}

void VfoModel::setDuplexMode(duplexMode_t duplexMode)
{
    if (m_backend)
    {
        m_backend->setDuplexMode(duplexMode);
    }
}

void VfoModel::setRepeaterOffsetHz(quint64 hz)
{
    if (m_backend)
    {
        m_backend->setRepeaterOffsetHz(hz);
    }
}

void VfoModel::setToneAccessMode(rptAccessTxRx_t toneAccessMode)
{
    if (m_backend)
    {
        m_backend->setToneAccessMode(toneAccessMode);
    }
}

void VfoModel::setToneFrequency(ushort tone)
{
    if (m_backend)
    {
        m_backend->setToneFrequency(tone);
    }
}

void VfoModel::setDtcsCode(ushort code)
{
    if (m_backend)
    {
        m_backend->setDtcsCode(code);
    }
}

bool VfoModel::setPtt(bool on)
{
    if (m_backend)
    {
        return m_backend->setPtt(on);
    }
    return false;
}

void VfoModel::sendDtmf(const QString& digits)
{
    if (m_backend)
    {
        m_backend->sendDtmf(digits);
    }
}

void VfoModel::applyFrequency(quint64 hz)
{
    if (m_freqHz.has_value() && *m_freqHz == hz)
    {
        return;
    }
    m_freqHz = hz;
    emit frequencyChanged(hz);
}

void VfoModel::applyMode(const QString& modeName)
{
    if (m_mode.has_value() && *m_mode == modeName)
    {
        return;
    }
    m_mode = modeName;
    emit modeChanged(modeName);
}

void VfoModel::applyPtt(bool on)
{
    if (m_txActive == on)
    {
        return;
    }
    m_txActive = on;
    emit txActiveChanged(on);
    if (!on && m_backend)
    {
        // A repeater key-up can make the IC-9700 report its temporary shifted
        // transmit frequency through the ordinary selected-frequency path.
        // Refresh immediately after confirmed unkey instead of waiting up to
        // one full background-poll cycle for the receive frequency to return.
        m_backend->pollFrequency();
    }
}

void VfoModel::applyNrEnabled(bool on)
{
    if (m_nrOn == on)
    {
        return;
    }
    m_nrOn = on;
    emit nrChanged(on, m_nrLevel);
}

void VfoModel::applyNrLevel(int level)
{
    level = qBound(0, level, 15);
    if (m_nrLevel == level)
    {
        return;
    }
    m_nrLevel = level;
    emit nrChanged(m_nrOn, level);
}

void VfoModel::applyNbEnabled(bool on)
{
    if (m_nbOn == on)
    {
        return;
    }
    m_nbOn = on;
    emit nbChanged(on, m_nbLevel);
}

void VfoModel::applyNbLevel(int level)
{
    level = qBound(0, level, 10);
    if (m_nbLevel == level)
    {
        return;
    }
    m_nbLevel = level;
    emit nbChanged(m_nbOn, level);
}

void VfoModel::applyPreampEnabled(bool on)
{
    applyPreampLevel(on ? qMax(1, m_preampLevel) : 0);
}

void VfoModel::applyPreampLevel(int level)
{
    level = qBound(0, level, 3);
    const bool on = level != 0;
    if (m_preampLevel == level && m_preampOn == on)
    {
        return;
    }
    m_preampLevel = level;
    m_preampOn = on;
    emit preampLevelChanged(level);
    emit preampChanged(on);
}

void VfoModel::applyAttenuatorEnabled(bool on)
{
    if (m_attenuatorOn == on)
    {
        return;
    }
    m_attenuatorOn = on;
    emit attenuatorChanged(on);
}

void VfoModel::applyRfGain(int level)
{
    level = qBound(0, level, 255);
    if (m_rfGain.has_value() && *m_rfGain == level)
    {
        return;
    }
    m_rfGain = level;
    emit rfGainChanged(level);
}

void VfoModel::applySquelch(bool on, int level)
{
    level = qBound(0, level, 255);
    if (m_squelch.has_value() && m_squelch->on == on && m_squelch->level == level)
    {
        return;
    }
    m_squelch = {on, level};
    emit squelchChanged(on, level);
}

void VfoModel::applyTxPower(int level)
{
    level = qBound(0, level, 255);
    if (m_txPower.has_value() && *m_txPower == level)
    {
        return;
    }
    m_txPower = level;
    emit txPowerChanged(level);
}

void VfoModel::applyDuplexMode(duplexMode_t duplexMode)
{
    if (m_duplexMode == duplexMode)
    {
        return;
    }
    m_duplexMode = duplexMode;
    emit duplexModeChanged(duplexMode);
}

void VfoModel::applyRepeaterOffsetHz(quint64 hz)
{
    if (m_repeaterOffsetHz == hz)
    {
        return;
    }
    m_repeaterOffsetHz = hz;
    emit repeaterOffsetChanged(hz);
}

void VfoModel::applyToneAccessMode(rptAccessTxRx_t toneAccessMode)
{
    if (m_toneAccessMode == toneAccessMode)
    {
        return;
    }
    m_toneAccessMode = toneAccessMode;
    emit toneAccessModeChanged(toneAccessMode);
}

void VfoModel::applyToneFrequency(ushort tone)
{
    if (m_toneFrequency == tone)
    {
        return;
    }
    m_toneFrequency = tone;
    emit toneFrequencyChanged(tone);
}

void VfoModel::applyDtcsCode(ushort code)
{
    if (m_dtcsCode == code)
    {
        return;
    }
    m_dtcsCode = code;
    emit dtcsCodeChanged(code);
}
