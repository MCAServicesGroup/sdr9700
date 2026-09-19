#include "RadioModel.h"
#include "RadioState.h"
#include "VfoModel.h"
#include "SpectrumScopeModel.h"
#include "MeterController.h"
#include "core/LogCategories.h"
#include "backend/RadioBackend.h"

#include <QDebug>

namespace
{
}

RadioModel::RadioModel(QObject* parent) : QObject(parent)
{
    m_backend = new RadioBackend(this);
    m_radioState = new sdr9700::RadioState(m_backend, this);
    m_vfo = new VfoModel(m_backend, this);
    m_spectrumScope = new SpectrumScopeModel(this);
    m_meterController = new MeterController(this);

    connect(m_backend, &IRadioBackend::connected, this, &RadioModel::onBackendConnected);
    connect(m_backend, &IRadioBackend::disconnected, this, &RadioModel::onBackendDisconnected);
    connect(m_backend, &IRadioBackend::readyChanged, this, &RadioModel::onBackendReadyChanged);
    connect(m_backend, &IRadioBackend::scopeSyncDegradedChanged, this, &RadioModel::scopeSyncDegradedChanged);
    connect(m_backend, &IRadioBackend::connectionStageChanged, this, &RadioModel::connectionStageChanged);
    connect(m_backend, &IRadioBackend::errorOccurred, this, &RadioModel::onBackendError);
    connect(m_backend, &IRadioBackend::statusMessage, this, &RadioModel::statusMessage);
    connect(m_backend, &IRadioBackend::frequencyChanged, this, &RadioModel::onFrequencyChanged);
    connect(m_backend, &IRadioBackend::modeChanged, this, &RadioModel::onModeChanged);
    connect(m_backend, &IRadioBackend::smeterChanged, m_meterController, &MeterController::setSMeter);
    connect(m_backend, &IRadioBackend::powerMeterChanged, m_meterController, &MeterController::setPowerMeter);
    connect(m_backend, &IRadioBackend::swrChanged, m_meterController, &MeterController::setSwr);
    connect(m_backend, &IRadioBackend::alcChanged, m_meterController, &MeterController::setAlc);
    connect(m_backend, &IRadioBackend::compressionMeterChanged, m_meterController,
            &MeterController::setCompressionMeter);
    connect(m_backend, &IRadioBackend::voltageMeterChanged, m_meterController, &MeterController::setVoltageMeter);
    connect(m_backend, &IRadioBackend::currentMeterChanged, m_meterController, &MeterController::setCurrentMeter);
    connect(m_backend, &IRadioBackend::agcModeChanged, m_vfo, &VfoModel::applyAgcMode);
    connect(m_backend, &IRadioBackend::autoNotchChanged, m_vfo, &VfoModel::applyAutoNotch);
    connect(m_backend, &IRadioBackend::manualNotchChanged, m_vfo, &VfoModel::applyManualNotch);
    connect(m_backend, &IRadioBackend::compressorChanged, m_vfo, &VfoModel::applyCompressor);
    connect(m_backend, &IRadioBackend::compressorLevelChanged, m_vfo, &VfoModel::applyCompressorLevel);
    connect(m_backend, &IRadioBackend::xfcChanged, m_vfo, &VfoModel::applyXfcEnabled);
    connect(m_backend, &IRadioBackend::ritEnabledChanged, m_vfo, &VfoModel::applyRitEnabled);
    connect(m_backend, &IRadioBackend::ritOffsetChanged, m_vfo, &VfoModel::applyRitOffset);
    connect(m_backend, &IRadioBackend::nrChanged, m_vfo, &VfoModel::applyNrEnabled);
    connect(m_backend, &IRadioBackend::nrLevelChanged, m_vfo, &VfoModel::applyNrLevel);
    connect(m_backend, &IRadioBackend::nbChanged, m_vfo, &VfoModel::applyNbEnabled);
    connect(m_backend, &IRadioBackend::nbLevelChanged, m_vfo, &VfoModel::applyNbLevel);
    connect(m_backend, &IRadioBackend::preampChanged, m_vfo, &VfoModel::applyPreampEnabled);
    connect(m_backend, &IRadioBackend::preampLevelChanged, m_vfo, &VfoModel::applyPreampLevel);
    connect(m_backend, &IRadioBackend::attenuatorChanged, m_vfo, &VfoModel::applyAttenuatorEnabled);
    connect(m_backend, &IRadioBackend::rfGainChanged, m_vfo, &VfoModel::applyRfGain);
    connect(m_backend, &IRadioBackend::squelchChanged, m_vfo, &VfoModel::applySquelch);
    connect(m_backend, &IRadioBackend::txPowerChanged, m_vfo, &VfoModel::applyTxPower);
    connect(m_backend, &IRadioBackend::duplexModeChanged, m_vfo, &VfoModel::applyDuplexMode);
    connect(m_backend, &IRadioBackend::repeaterOffsetChanged, m_vfo, &VfoModel::applyRepeaterOffsetHz);
    connect(m_backend, &IRadioBackend::toneAccessModeChanged, m_vfo, &VfoModel::applyToneAccessMode);
    connect(m_backend, &IRadioBackend::toneFrequencyChanged, m_vfo, &VfoModel::applyToneFrequency);
    connect(m_backend, &IRadioBackend::dtcsCodeChanged, m_vfo, &VfoModel::applyDtcsCode);
    connect(m_backend, &IRadioBackend::spectrumDataReady, this, &RadioModel::onSpectrumDataReady);
    connect(m_backend, &IRadioBackend::pttChanged, this, &RadioModel::onPttChanged);
    connect(m_backend, &IRadioBackend::networkQualityChanged, this, &RadioModel::networkQualityChanged);
    connect(m_backend, &IRadioBackend::sessionHeartbeat, this, &RadioModel::sessionHeartbeat);
    connect(m_backend, &IRadioBackend::txAudioMeterChanged, m_meterController, &MeterController::setTransmitAudioMeter);
    connect(m_backend, &IRadioBackend::radioMemoryReceived, this, &RadioModel::radioMemoryReceived);
    connect(m_backend, &IRadioBackend::audioDataReady, this, &RadioModel::audioDataReady);
    connect(m_meterController, &MeterController::snapshotChanged, this, &RadioModel::onMeterSnapshotChanged);
}

RadioModel::~RadioModel() = default;

void RadioModel::connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass)
{
    m_backend->connectToRadio(host, port, user, pass);
}

void RadioModel::disconnectFromRadio()
{
    m_backend->disconnectFromRadio();
}

void RadioModel::setRxAudioDevice(const QAudioDevice& dev)
{
    m_backend->setRxAudioDevice(dev);
}

void RadioModel::setTxAudioDevice(const QAudioDevice& dev)
{
    m_backend->setTxAudioDevice(dev);
}

void RadioModel::stopLocalAudio()
{
    m_backend->stopLocalAudio();
}

void RadioModel::setLanModLevel(int level)
{
    m_backend->setLanModLevel(level);
}

void RadioModel::setTuningStep(int step)
{
    m_backend->setTuningStep(step);
}

void RadioModel::selectVfoMode()
{
    m_backend->selectVfoMode();
}

void RadioModel::selectRadioMemory(quint16 group, quint16 channel, Vfo targetVfo)
{
    m_backend->selectRadioMemory(group, channel, targetVfo);
}

void RadioModel::requestRadioMemory(quint16 group, quint16 channel)
{
    m_backend->requestRadioMemory(group, channel);
}

void RadioModel::requestSatelliteMemory(quint16 channel)
{
    m_backend->requestSatelliteMemory(channel);
}

void RadioModel::writeRadioMemory(MemoryType memory)
{
    m_backend->writeRadioMemory(memory);
}

void RadioModel::onBackendConnected()
{
    m_connected = true;
    m_ready = false;
    emit connectionChanged(true);
    emit readyChanged(false);
    // Status message is already emitted by RadioBackend::onLanReady(); no duplicate here.
}

void RadioModel::onBackendDisconnected()
{
    m_connected = false;
    m_ready = false;
    m_meterController->reset();
    m_vfo->clearCompressorLevel();
    emit connectionChanged(false);
    emit readyChanged(false);
    onPttChanged(false);
}

void RadioModel::onBackendError(ErrorCode code, const QString& msg)
{
    emit errorOccurred(code, msg);
    if (m_connected)
    {
        m_connected = false;
        m_ready = false;
        m_meterController->reset();
        m_vfo->clearCompressorLevel();
        emit connectionChanged(false);
        emit readyChanged(false);
    }
}

void RadioModel::onBackendReadyChanged(bool ready)
{
    qInfo(logGui()).noquote().nospace() << "RadioModel observed backend readyChanged=" << ready
                                        << " connected=" << m_connected << " currentReady=" << m_ready;
    if (m_ready == ready)
    {
        return;
    }

    m_ready = ready;
    emit readyChanged(ready);
}

void RadioModel::onFrequencyChanged(quint64 hz)
{
    m_vfo->applyFrequency(hz);
}

void RadioModel::onModeChanged(const QString& mode)
{
    m_vfo->applyMode(mode);
}

void RadioModel::onMeterSnapshotChanged(const MeterSnapshot& snapshot)
{
    m_smeter = snapshot.sMeter;
    emit smeterChanged(snapshot.sMeter);
    if (snapshot.powerValid)
    {
        emit powerMeterChanged(snapshot.powerWatts);
    }
    if (snapshot.swrValid)
    {
        emit swrChanged(snapshot.swr);
    }
    if (snapshot.alcValid)
    {
        emit alcChanged(snapshot.alc);
    }
    if (snapshot.compressionValid)
    {
        emit compressionMeterChanged(snapshot.compressionDb);
    }
    if (snapshot.voltageValid)
    {
        emit voltageMeterChanged(snapshot.voltageVolts);
    }
    if (snapshot.currentValid)
    {
        emit currentMeterChanged(snapshot.currentAmps);
    }
    emit meterSnapshotChanged(snapshot);
}

void RadioModel::onPttChanged(bool on)
{
    m_vfo->applyPtt(on);
    if (on)
    {
        // Receive-strength samples are no longer current while the radio is
        // transmitting and receive-meter polling is suspended.
        m_meterController->resetReceiveMeter();
    }
    else
    {
        m_meterController->resetTransmitMeters();
    }
    if (m_transmitting == on)
    {
        return;
    }

    m_transmitting = on;
    emit transmittingChanged(on);
    emit pttChanged(on);
}

void RadioModel::onSpectrumDataReady(const QVector<float>& levels, double start, double end, bool outOfRange)
{
    if (!m_ready)
    {
        return;
    }
    emit spectrumActivity();
    m_spectrumScope->ingestSpectrum(levels, start, end, outOfRange);
}
