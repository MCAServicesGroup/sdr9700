#include "RadioBackend.h"

#include "MainSubExchangeConfirmationPolicy.h"
#include "ReceiverReadinessPolicy.h"
#include "VfoReceiverCommandRoute.h"

#include "Commander.h"
#include "CachingQueue.h"
#include "RadioRouter.h"
#include "ScopeController.h"
#include "Types.h"
#include "AppSettings.h"
#include "AudioDeviceSelection.h"
#include "DtmfGenerator.h"
#include "LogCategories.h"
#include "RadioCapabilities.h"
#include "RadioIdentities.h"
#include "UdpStatusMessages.h"
#include "ShutdownTiming.h"
#include "TransmitFrequencyPolicy.h"

#include <QMediaDevices>
#include <QHostAddress>
#include <QMetaType>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>

namespace
{
constexpr int kMaxMainSubExchangeRetries = 8;
constexpr int kMaxDualWatchRetries = 8;
constexpr int kSyncWatchdogTimeoutMs = 10000;
constexpr int kSyncReconnectDelayMs = 3000;
constexpr int kMaxSyncReconnectAttempts = 1;
constexpr int kStandbyWakeDelayMs = 10000;
constexpr int kMemoryWriteReadbackDelayMs = 250;
constexpr int kVfoStatePollIntervalMs = 250;
constexpr int kReceiverContextSettleMs = 250;
constexpr int kPttReleaseTailMs = 150;
constexpr int kPttOnConfirmationMs = 1000;
constexpr int kPttOffConfirmationRetryMs = 50;
constexpr int kPttOffConfirmationFastWindowMs = 1000;
constexpr int kPttOffConfirmationSlowRetryMs = 1000;
constexpr int kMaxTransmitDurationMs = 180000;
constexpr uchar kHardwareTxTimeoutTimer = 1; // 3 minutes, the IC-9700's shortest non-off value.
constexpr uchar kMainReceiver = 0;
constexpr uchar kSubReceiver = 1;
constexpr quint32 kTxAudioSampleRate = 16000;
constexpr quint8 kExchangeMainStatusConfirmed = 0x01;
constexpr quint8 kExchangeScopeStatusConfirmed = 0x02;
constexpr quint8 kExchangeSubFrequencyConfirmed = 0x04;
constexpr quint8 kExchangeSubModeConfirmed = 0x08;
constexpr quint8 kExchangeMainFrequencyConfirmed = 0x10;
constexpr quint8 kExchangeMainModeConfirmed = 0x20;
constexpr quint8 kExchangeAllConfirmed = kExchangeMainStatusConfirmed | kExchangeScopeStatusConfirmed |
                                         kExchangeSubFrequencyConfirmed | kExchangeSubModeConfirmed |
                                         kExchangeMainFrequencyConfirmed | kExchangeMainModeConfirmed;

struct VfoStatePollItem
{
    Funcs func{};
    bool mainOnly{false};
};

// Read one stable control per timer tick. With dual watch enabled, MAIN and
// SUB alternate, so a complete radio-authoritative refresh takes about ten
// seconds without producing the large CI-V bursts that contend with tuning,
// meters, and PTT.
constexpr std::array kVfoStatePollItems{
    VfoStatePollItem{funcFreqGet, false},         VfoStatePollItem{funcModeGet, false},
    VfoStatePollItem{funcAGCTimeConstant, false}, VfoStatePollItem{funcAttenuator, false},
    VfoStatePollItem{funcNoiseBlanker, false},    VfoStatePollItem{funcNBLevel, false},
    VfoStatePollItem{funcAutoNotch, false},       VfoStatePollItem{funcManualNotch, false},
    VfoStatePollItem{funcNoiseReduction, false},  VfoStatePollItem{funcNRLevel, false},
    VfoStatePollItem{funcPreamp, false},          VfoStatePollItem{funcRfGain, false},
    VfoStatePollItem{funcSquelch, false},         VfoStatePollItem{funcSplitStatus, false},
    VfoStatePollItem{funcReadFreqOffset, false},  VfoStatePollItem{funcToneSquelchType, false},
    VfoStatePollItem{funcToneFreq, false},        VfoStatePollItem{funcTSQLFreq, false},
    VfoStatePollItem{funcDTCSCode, false},        VfoStatePollItem{funcRFPower, true},
    VfoStatePollItem{funcCompressor, true},       VfoStatePollItem{funcXFCStatus, true},
};

quint64 memoryGroupDefaultFrequencyHz(quint16 group)
{
    switch (group)
    {
    case 1:
        return sdr9700::radioBandDefaultFrequency(band2m);
    case 2:
        return sdr9700::radioBandDefaultFrequency(band70cm);
    case 3:
        return sdr9700::radioBandDefaultFrequency(band23cm);
    default:
        return 0;
    }
}

bool populateModeInfo(const QString& mode, ModeInfo* info)
{
    if (!info)
    {
        return false;
    }

    static constexpr quint8 kDefaultFilter = 1;
    static constexpr quint8 kDataModeOff = 0;

    info->filter = kDefaultFilter;
    info->data = kDataModeOff;
    info->VFO = activeVFO;
    info->name = mode;

    if (mode == "LSB")
    {
        info->mk = modeLSB;
        info->reg = 0;
    }
    else if (mode == "USB")
    {
        info->mk = modeUSB;
        info->reg = 1;
    }
    else if (mode == "AM")
    {
        info->mk = modeAM;
        info->reg = 2;
    }
    else if (mode == "CW")
    {
        info->mk = modeCW;
        info->reg = 3;
    }
    else if (mode == "RTTY")
    {
        info->mk = modeRTTY;
        info->reg = 4;
    }
    else if (mode == "FM")
    {
        info->mk = modeFM;
        info->reg = 5;
    }
    else if (mode == "CW-R")
    {
        info->mk = modeCW_R;
        info->reg = 7;
    }
    else if (mode == "RTTY-R")
    {
        info->mk = modeRTTY_R;
        info->reg = 8;
    }
    else if (mode == "DV")
    {
        info->mk = modeDV;
        info->reg = 17;
    }
    else if (mode == "DD")
    {
        info->mk = modeDD;
        info->reg = 22;
    }
    else
    {
        return false;
    }

    return true;
}

radioInput restoreInputForModSource(int reg)
{
    return radioInput(inputUnknown, static_cast<qint8>(qBound(0, reg, 99)), QStringLiteral("Previous"));
}

void sendDisconnectSafetyCommands(Commander* commandSession, std::optional<int> originalDataOffMod,
                                  std::optional<int> originalData1Mod)
{
    if (!commandSession)
    {
        return;
    }

    // Use a conservative IC-9700 LAN disconnect order: unkey first, then stop
    // unsolicited scope data before the UDP streams close.
    commandSession->setPttActive(false);
    commandSession->receiveCommandNoReadback(funcTransceiverStatus, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommandNoReadback(funcScopeOnOff, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommandNoReadback(funcScopeDataOutput, QVariant::fromValue<bool>(false), 0);

    if (originalDataOffMod.has_value())
    {
        commandSession->receiveCommandNoReadback(funcDATAOffMod,
                                                 QVariant::fromValue(restoreInputForModSource(*originalDataOffMod)), 0);
    }
    if (originalData1Mod.has_value())
    {
        commandSession->receiveCommandNoReadback(funcDATA1Mod,
                                                 QVariant::fromValue(restoreInputForModSource(*originalData1Mod)), 0);
    }
}

} // namespace

RadioBackend::RadioBackend(QObject* parent)
    : IRadioBackend(parent), m_workerThread(new QThread(this)), m_radioDataThread(new QThread(this))
{
    qRegisterMetaType<MemoryType>("MemoryType");

    auto* mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this, &RadioBackend::refreshRxAudioDevice);
    connect(mediaDevices, &QMediaDevices::audioInputsChanged, this, &RadioBackend::refreshTxAudioDevice);

    m_workerThread->setObjectName("radio-worker");
    m_workerThread->start();

    m_radioDataThread->setObjectName("radio-data");
    m_radioDataThread->start();

    m_scopeController = new ScopeController();
    m_scopeController->moveToThread(m_radioDataThread);
    connect(m_radioDataThread, &QThread::finished, m_scopeController, &QObject::deleteLater);

    m_radioRouter = new RadioRouter();
    m_radioRouter->moveToThread(m_radioDataThread);
    connect(m_radioDataThread, &QThread::finished, m_radioRouter, &QObject::deleteLater);
    connect(m_scopeController, &ScopeController::scopeDataReceived, this,
            [this]()
            {
                // A retained frame can arrive while startup is deliberately
                // turning scope output off to synchronize VFO state. Do not
                // let that stale frame satisfy this session's scope readiness
                // or stop the subsequent enable retry loop.
                if (!m_scopeEnableRequested)
                {
                    return;
                }
                if (m_scopeSyncDegraded)
                {
                    setScopeSyncDegraded(false);
                    qInfo(logRadio()).noquote() << "Spectrum scope synchronization complete";
                }
                m_scopeDataReceived = true;
                updateReadyState();
            });
    connect(m_scopeController, &ScopeController::spectrumDataReady, this, &IRadioBackend::spectrumDataReady);

    connect(m_radioRouter, &RadioRouter::radioValueUpdated, this,
            [this](Funcs func, const QVariant& value, uchar receiver)
            {
                if (func == funcFreqGet || func == funcFreqSet || func == funcSelectedFreq ||
                    func == funcUnselectedFreq)
                {
                    const quint64 frequencyHz = value.value<Frequency>().Hz;
                    observeVfoFrequency(frequencyHz, receiver);
                    if (frequencyHz > 0 && receiver == kSubReceiver)
                    {
                        m_initialSubFrequencyReceived = true;
                    }
                }
                if ((func == funcModeGet || func == funcModeSet || func == funcSelectedMode ||
                     func == funcUnselectedMode) &&
                    receiver == kSubReceiver)
                {
                    m_initialSubModeReceived = true;
                }
                // funcVFOBandMS also reports the short-lived physical
                // selections used to route receiver-scoped CI-V work. Those
                // internal reports must not replace the operator's selected
                // VFO: doing so makes a visibly selected SUB panel send its
                // next control change to MAIN after a background read restores
                // the radio's physical context. selectVfo() owns m_activeVfo;
                // this observer remains concerned only with radio state that
                // is not an implementation detail of command routing.
                if (func == funcVFODualWatch && receiver == 0)
                {
                    m_dualWatchEnabled = value.toBool();
                }
                else if (func == funcSMeter && (m_smeterPollPending || m_smeterPollQueued))
                {
                    if (receiver != m_smeterPollPendingReceiver)
                    {
                        qWarning(logRadio()).noquote() << "Ignoring mismatched S-meter reply receiver=" << receiver
                                                       << "expected=" << m_smeterPollPendingReceiver;
                        return;
                    }

                    m_smeterPollPending = false;
                    m_smeterPollQueued = false;
                    m_smeterPollDeadlineMs = 0;
                    m_smeterPollQueuedClock.invalidate();
                    m_smeterPollPendingClock.invalidate();
                }
                updateReadyState();
                updateAudioReadyState();
                emit radioValueUpdated(func, value, receiver);
            });
    connect(m_radioRouter, &RadioRouter::frequencyReported, this,
            [this](quint64 hz)
            {
                m_initialMainFrequencyReceived = true;
                m_initialFrequencyReceived = true;
                const bool receiveFrequencyAccepted =
                    m_transmitConfiguration.confirmFrequency(hz, m_pttState.safetyActive());
                if (receiveFrequencyAccepted)
                {
                    // Keep the backend's band identity and PTT eligibility
                    // anchored to receive state. During repeater transmit the
                    // radio may report the shifted TX frequency here; the UI
                    // still receives it below for its explicit PTT/memory
                    // transition handling.
                    m_currentMainFrequencyHz = hz;
                    handleReportedFrequency(hz);
                }
                emit frequencyChanged(hz);
                updateReadyState();
                updateAudioReadyState();
            });

    m_mainSubExchangeRetryTimer = new QTimer(this);
    m_mainSubExchangeRetryTimer->setSingleShot(true);
    m_mainSubExchangeRetryTimer->setInterval(150);
    connect(
        m_mainSubExchangeRetryTimer, &QTimer::timeout, this,
        [this]()
        {
            if (!m_mainSubExchangePending)
            {
                return;
            }
            const quint8 missing = static_cast<quint8>(kExchangeAllConfirmed & ~m_mainSubExchangeConfirmations);
            if (++m_mainSubExchangeRetryCount > kMaxMainSubExchangeRetries)
            {
                qCritical(logRadio()).noquote().nospace()
                    << "MAIN/SUB exchange confirmation failed missing=" << missing
                    << " elapsedMs=" << (m_mainSubExchangeClock.isValid() ? m_mainSubExchangeClock.elapsed() : -1);
                m_mainSubExchangePending = false;
                m_mainSubExchangeDispatched = false;
                invokeOnCurrentCommander(
                    [](Commander* commandSession)
                    {
                        commandSession->finishMainSubExchangeConfirmation();
                        commandSession->receiveCommandNoReadback(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                        commandSession->receiveCommandNoReadback(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
                    });
                requestVfoState(Vfo::Main);
                requestVfoState(Vfo::Sub);
                emit mainSubExchangeFailed();
                return;
            }
            qInfo(logRadio()).noquote() << "Retrying missing MAIN/SUB exchange confirmations mask=" << missing;
            invokeOnCurrentCommander(
                [missing](Commander* commandSession)
                {
                    if (missing & kExchangeMainStatusConfirmed)
                    {
                        commandSession->receiveCommandNoReadback(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
                        commandSession->receiveCommand(funcVFOBandMS, QVariant(), 0);
                    }
                    if (missing & kExchangeScopeStatusConfirmed)
                    {
                        commandSession->receiveCommandNoReadback(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
                        commandSession->receiveCommand(funcScopeMainSub, QVariant(), 0);
                    }
                    if (missing & kExchangeMainFrequencyConfirmed)
                    {
                        commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
                    }
                    if (missing & kExchangeMainModeConfirmed)
                    {
                        commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
                    }
                    if (missing & kExchangeSubFrequencyConfirmed)
                    {
                        commandSession->requestReceiverScopedRead(funcFreqGet, kSubReceiver);
                    }
                    if (missing & kExchangeSubModeConfirmed)
                    {
                        commandSession->requestReceiverScopedRead(funcModeGet, kSubReceiver);
                    }
                });
            m_mainSubExchangeRetryTimer->start();
        });

    m_dualWatchRetryTimer = new QTimer(this);
    m_dualWatchRetryTimer->setSingleShot(true);
    m_dualWatchRetryTimer->setInterval(150);
    connect(m_dualWatchRetryTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_dualWatchTransition.pending())
                {
                    return;
                }
                if (++m_dualWatchRetryCount > kMaxDualWatchRetries)
                {
                    qCritical(logRadio()).noquote().nospace()
                        << "Dual-watch transition failed requested=" << m_dualWatchTransition.requestedEnabled()
                        << " confirmations=" << m_dualWatchTransition.confirmations() << " elapsedMs="
                        << (m_dualWatchTransitionClock.isValid() ? m_dualWatchTransitionClock.elapsed() : -1);
                    finishDualWatchTransition(false);
                    return;
                }

                const quint8 missing = m_dualWatchTransition.missingConfirmations();
                qInfo(logRadio()).noquote() << "Retrying dual-watch transition missing=" << missing;
                invokeOnCurrentCommander(
                    [enabled = m_dualWatchTransition.requestedEnabled(), missing](Commander* commandSession)
                    {
                        if (missing & sdr9700::DualWatchTransitionPolicy::kStateConfirmed)
                        {
                            commandSession->receiveCommandNoReadback(funcVFODualWatch,
                                                                     QVariant::fromValue<bool>(enabled), 0);
                            commandSession->receiveCommand(funcVFODualWatch, QVariant(), 0);
                            return;
                        }
                        if (missing & sdr9700::DualWatchTransitionPolicy::kSubFrequencyConfirmed)
                        {
                            commandSession->requestReceiverScopedRead(funcFreqGet, kSubReceiver);
                        }
                        if (missing & sdr9700::DualWatchTransitionPolicy::kSubModeConfirmed)
                        {
                            commandSession->requestReceiverScopedRead(funcModeGet, kSubReceiver);
                        }
                    });
                m_dualWatchRetryTimer->start();
            });

    m_vfoStatePollTimer = new QTimer(this);
    m_vfoStatePollTimer->setInterval(kVfoStatePollIntervalMs);
    m_vfoStatePollTimer->setTimerType(Qt::CoarseTimer);
    connect(m_vfoStatePollTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_commander || !m_radioReady || m_pttState.safetyActive() || m_mainSubExchangePending ||
                    m_dualWatchTransition.pending() || m_smeterPollPending || m_smeterPollQueued)
                {
                    return;
                }

                const VfoStatePollItem item = kVfoStatePollItems[static_cast<std::size_t>(m_vfoStatePollPhase)];
                const Vfo targetVfo = m_activeVfo;
                if (targetVfo == Vfo::Main || !item.mainOnly)
                {
                    // Stable polling participates in the same serialized
                    // receiver-scoped path as explicit confirmations. A
                    // direct MAIN read can otherwise overlap a pending SUB
                    // read of the same reply family; because plain CI-V
                    // replies carry no receiver identity, the SUB value can
                    // then be attributed to MAIN and make both UI models look
                    // identical even though the radio itself is correct.
                    invokeOnCurrentCommander(
                        [item, targetVfo](Commander* commandSession)
                        { scheduleVfoReceiverReadForCommand(commandSession, targetVfo, item.func); });
                }
                m_vfoStatePollPhase = (m_vfoStatePollPhase + 1) % static_cast<qsizetype>(kVfoStatePollItems.size());
            });

    connect(m_radioRouter, &RadioRouter::modeReported, this,
            [this](const QString& mode, int filter)
            {
                if (filter >= 1 && filter <= 3)
                {
                    m_currentMainFilter = filter;
                }
                m_initialMainModeReceived = true;
                m_initialModeReceived = true;
                emit modeChanged(mode);
                updateReadyState();
                updateAudioReadyState();
            });
    connect(m_radioRouter, &RadioRouter::repeaterOffsetChanged, this,
            [this](quint64 hz)
            {
                m_currentRepeaterOffsetHz = hz;
                m_transmitConfiguration.confirmOffset(hz);
                emit repeaterOffsetChanged(hz);
            });
    connect(m_radioRouter, &RadioRouter::toneAccessModeChanged, this, &IRadioBackend::toneAccessModeChanged);
    connect(m_radioRouter, &RadioRouter::toneFrequencyChanged, this, &IRadioBackend::toneFrequencyChanged);
    connect(m_radioRouter, &RadioRouter::dtcsCodeChanged, this, &IRadioBackend::dtcsCodeChanged);
    connect(m_radioRouter, &RadioRouter::smeterChanged, this, &IRadioBackend::smeterChanged);
    connect(m_radioRouter, &RadioRouter::nrChanged, this, &IRadioBackend::nrChanged);
    connect(m_radioRouter, &RadioRouter::nrLevelChanged, this, &IRadioBackend::nrLevelChanged);
    connect(m_radioRouter, &RadioRouter::nbChanged, this, &IRadioBackend::nbChanged);
    connect(m_radioRouter, &RadioRouter::nbLevelChanged, this, &IRadioBackend::nbLevelChanged);
    connect(m_radioRouter, &RadioRouter::preampChanged, this, &IRadioBackend::preampChanged);
    connect(m_radioRouter, &RadioRouter::preampLevelChanged, this, &IRadioBackend::preampLevelChanged);
    connect(m_radioRouter, &RadioRouter::attenuatorChanged, this, &IRadioBackend::attenuatorChanged);
    connect(m_radioRouter, &RadioRouter::autoNotchChanged, this, &IRadioBackend::autoNotchChanged);
    connect(m_radioRouter, &RadioRouter::manualNotchChanged, this, &IRadioBackend::manualNotchChanged);
    connect(m_radioRouter, &RadioRouter::compressorChanged, this, &IRadioBackend::compressorChanged);
    connect(m_radioRouter, &RadioRouter::compressorLevelChanged, this, &IRadioBackend::compressorLevelChanged);
    connect(m_radioRouter, &RadioRouter::xfcChanged, this, &IRadioBackend::xfcChanged);
    connect(m_radioRouter, &RadioRouter::ritEnabledChanged, this, &IRadioBackend::ritEnabledChanged);
    connect(m_radioRouter, &RadioRouter::ritOffsetChanged, this, &IRadioBackend::ritOffsetChanged);
    connect(m_radioRouter, &RadioRouter::agcModeChanged, this, &IRadioBackend::agcModeChanged);
    connect(m_radioRouter, &RadioRouter::rfGainChanged, this, &IRadioBackend::rfGainChanged);
    connect(m_radioRouter, &RadioRouter::txPowerChanged, this, &IRadioBackend::txPowerChanged);
    connect(m_radioRouter, &RadioRouter::squelchChanged, this, &IRadioBackend::squelchChanged);
    connect(m_radioRouter, &RadioRouter::swrMeterChanged, this, &RadioBackend::handleTransmitSwr);
    connect(m_radioRouter, &RadioRouter::powerMeterChanged, this, &IRadioBackend::powerMeterChanged);
    connect(m_radioRouter, &RadioRouter::alcChanged, this, &IRadioBackend::alcChanged);
    connect(m_radioRouter, &RadioRouter::compressionMeterChanged, this, &IRadioBackend::compressionMeterChanged);
    connect(m_radioRouter, &RadioRouter::voltageMeterChanged, this, &IRadioBackend::voltageMeterChanged);
    connect(m_radioRouter, &RadioRouter::currentMeterChanged, this, &IRadioBackend::currentMeterChanged);
    connect(m_radioRouter, &RadioRouter::duplexModeChanged, this,
            [this](duplexMode_t mode)
            {
                m_currentDuplexMode = mode;
                m_transmitConfiguration.confirmDuplexMode(mode);
                emit duplexModeChanged(mode);
            });
    connect(m_radioRouter, &RadioRouter::radioMemoryReceived, this, &IRadioBackend::radioMemoryReceived);
    connect(m_radioRouter, &RadioRouter::dataOffModChanged, this,
            [this](const radioInput& input)
            {
                if (!m_originalDataOffMod.has_value())
                {
                    m_originalDataOffMod = input.reg;
                    qInfo(logRadio()).noquote()
                        << "Captured original DATA OFF MOD source register" << *m_originalDataOffMod;
                }
            });
    connect(m_radioRouter, &RadioRouter::data1ModChanged, this,
            [this](const radioInput& input)
            {
                if (!m_originalData1Mod.has_value())
                {
                    m_originalData1Mod = input.reg;
                    qInfo(logRadio()).noquote() << "Captured original DATA1 MOD source register" << *m_originalData1Mod;
                }
            });
    connect(m_radioRouter, &RadioRouter::pttChanged, this,
            [this](bool on)
            {
                if (on && m_pttState.offPending())
                {
                    // The IC-9700 can answer the first status read before its
                    // unkey command has taken effect. Keep the confirmed TX
                    // state authoritative, but do not republish a duplicate
                    // TX transition while the short retry timer continues.
                    const bool txWasAlreadyPublished = m_pttState.confirmedActive();
                    m_pttState.confirm(true);
                    if (!txWasAlreadyPublished)
                    {
                        emit pttChanged(true);
                    }
                    qDebug(logRadio()).noquote() << "PTT status still TX after unkey request; awaiting retry";
                    return;
                }
                const bool publish = m_pttState.shouldPublishReadback(on);
                if (!on && m_pttReleaseDelayTimer)
                {
                    m_pttReleaseDelayTimer->stop();
                }
                if (m_pttOnConfirmationTimer && (on || !m_pttState.desiredActive()))
                {
                    m_pttOnConfirmationTimer->stop();
                }
                if (!on && m_pttOffConfirmationTimer)
                {
                    m_pttOffConfirmationTimer->stop();
                    m_pttOffConfirmationClock.invalidate();
                }
                if (on)
                {
                    armTransmitSafety();
                }
                else
                {
                    disarmTransmitSafety();
                }
                m_pttState.confirm(on);
                if (publish)
                {
                    emit pttChanged(on);
                }
            });
    connect(m_radioRouter, &RadioRouter::scopeDataReady, m_scopeController, &ScopeController::acceptScopeData);

    m_pttReleaseDelayTimer = new QTimer(this);
    m_pttReleaseDelayTimer->setSingleShot(true);
    m_pttReleaseDelayTimer->setInterval(kPttReleaseTailMs);
    connect(m_pttReleaseDelayTimer, &QTimer::timeout, this, &RadioBackend::sendPttOffNow);

    m_pttOnConfirmationTimer = new QTimer(this);
    m_pttOnConfirmationTimer->setSingleShot(true);
    m_pttOnConfirmationTimer->setInterval(kPttOnConfirmationMs);
    connect(
        m_pttOnConfirmationTimer, &QTimer::timeout, this,
        [this]()
        {
            if (!m_pttState.desiredActive() || m_pttState.confirmedActive())
            {
                return;
            }
            emit statusMessage(
                QStringLiteral("PTT stopped: radio did not confirm transmit within %1 ms").arg(kPttOnConfirmationMs),
                MessageSeverity::Error);
            sendPttOffNow();
        });

    m_pttOffConfirmationTimer = new QTimer(this);
    m_pttOffConfirmationTimer->setSingleShot(true);
    m_pttOffConfirmationTimer->setInterval(kPttOffConfirmationRetryMs);
    connect(m_pttOffConfirmationTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_pttState.offPending())
                {
                    return;
                }
                invokeOnCurrentCommander([](Commander* commandSession)
                                         { commandSession->receiveCommand(funcTransceiverStatus, QVariant(), 0); });
                const bool fastRetry = m_pttOffConfirmationClock.isValid() &&
                                       m_pttOffConfirmationClock.elapsed() < kPttOffConfirmationFastWindowMs;
                m_pttOffConfirmationTimer->start(fastRetry ? kPttOffConfirmationRetryMs
                                                           : kPttOffConfirmationSlowRetryMs);
            });

    m_pttMaxDurationTimer = new QTimer(this);
    m_pttMaxDurationTimer->setSingleShot(true);
    m_pttMaxDurationTimer->setInterval(kMaxTransmitDurationMs);
    connect(m_pttMaxDurationTimer, &QTimer::timeout, this,
            [this]()
            {
                forcePttOffForSafety(
                    QStringLiteral("TX stopped: %1 second transmit safety timeout").arg(kMaxTransmitDurationMs / 1000));
            });

    /*
        IC-9700 LAN TX audio startup sequence

        SDR9700 keeps the radio's LAN MOD Level as a persistent radio setting
        through the IC-9700 CI-V command model. TX startup muting is handled in
        the audio path, not by forcing the radio menu value to zero. This keeps
        the radio front panel/menu in sync with the GUI LAN MOD control.

        IC-9700 LAN scope packets carry the complete sweep in one CI-V frame.
        Commander parses that frame directly; this is independent from the TX
        audio startup sequence above.
    */
    m_bandStateRefreshTimer = new QTimer(this);
    m_bandStateRefreshTimer->setSingleShot(true);
    m_bandStateRefreshTimer->setInterval(350);
    connect(m_bandStateRefreshTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_commander || !m_radioReady)
                {
                    return;
                }
                emit statusMessage(QStringLiteral("Refreshing radio band state"), MessageSeverity::Info);
                requestInitialRadioState();
            });

    m_syncWatchdogTimer = new QTimer(this);
    m_syncWatchdogTimer->setSingleShot(true);
    m_syncWatchdogTimer->setInterval(kSyncWatchdogTimeoutMs);
    connect(m_syncWatchdogTimer, &QTimer::timeout, this, &RadioBackend::restartAfterSyncTimeout);
}

RadioBackend::~RadioBackend()
{
    shutdownConnection(false, false, m_standbyWakePolicy.complete());
    if (m_scopeController && m_radioDataThread && m_radioDataThread->isRunning())
    {
        QMetaObject::invokeMethod(m_scopeController, &ScopeController::reset, Qt::QueuedConnection);
    }
    if (m_radioDataThread)
    {
        m_radioDataThread->quit();
        if (!m_radioDataThread->wait(3000))
        {
            qWarning(logRadio()).noquote()
                << "[SHUTDOWN] radio-data did not stop within 3000 ms; requesting interruption";
            m_radioDataThread->requestInterruption();
            m_radioDataThread->quit();
            if (!m_radioDataThread->wait(1000))
            {
                qCritical(logRadio()).noquote()
                    << "[SHUTDOWN] radio-data did not stop after bounded shutdown; leaving thread "
                       "detached to avoid blocking the UI";
                m_radioDataThread->setParent(nullptr);
                connect(m_radioDataThread, &QThread::finished, m_radioDataThread, &QObject::deleteLater,
                        Qt::DirectConnection);
                m_radioDataThread = nullptr;
            }
        }
    }
    m_workerThread->quit();
    if (!m_workerThread->wait(3000))
    {
        qWarning(logRadio()).noquote()
            << "[SHUTDOWN] radio-worker did not stop within 3000 ms; requesting interruption";
        m_workerThread->requestInterruption();
        m_workerThread->quit();
        if (!m_workerThread->wait(1000))
        {
            qCritical(logRadio()).noquote()
                << "[SHUTDOWN] radio-worker did not stop after bounded shutdown; leaving thread "
                   "detached to avoid blocking the UI";
            m_workerThread->setParent(nullptr);
            connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater, Qt::DirectConnection);
            m_workerThread = nullptr;
        }
    }
}

void RadioBackend::connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass)
{
    // Operator-initiated and ordinary reconnects begin a new bootstrap policy.
    // Internal retries keep the existing policy so repeated silent CI-V paths
    // advance RetrySession -> Wake -> Fail instead of restarting forever.
    const bool previousRadioWasReady = m_standbyWakePolicy.complete();
    const bool bootstrapReconnect = m_bootstrapReconnectPending;
    m_bootstrapReconnectPending = false;
    if (!bootstrapReconnect)
    {
        ++m_bootstrapGeneration;
        m_standbyWakePolicy.reset();
    }
    if (!m_syncReconnectPending)
    {
        m_syncReconnectAttempts = 0;
    }
    m_syncReconnectPending = false;
    // Must be called from the main thread only; m_commander is not guarded by a mutex.
    if (m_commander)
    {
        // Replacing one session is not an operator-requested disconnect. Reset
        // backend readiness without publishing a transient Disconnected state
        // that could start MainWindow's automatic reconnect timer.
        shutdownConnection(false, false, previousRadioWasReady);
    }
    // Internal bootstrap replacements are implementation details of the same
    // operator-requested connection attempt. Republishing "Connecting" here
    // used to overwrite the more useful "Waking radio" status message during the
    // bounded standby delay, making the UI appear to bounce between states.
    // Preserve the current lifecycle message until the command plane either
    // becomes usable or the attempt reaches its final failure.
    if (!bootstrapReconnect)
    {
        emit connectionStageChanged(ConnectionStage::Connecting, QStringLiteral("Connecting to radio"));
    }

    QHostAddress radioAddress;
    if (radioAddress.setAddress(host) && radioAddress.protocol() != QAbstractSocket::IPv4Protocol)
    {
        radioAddress.clear();
    }
    if (radioAddress.isNull())
    {
        const QString message = QStringLiteral("Invalid radio IPv4 address");
        emit connectionStageChanged(ConnectionStage::Failed, message);
        emit errorOccurred(ErrorCode::ConnectionFailed, message);
        return;
    }
    if (port == 0 || port > kIcomLanControlPortMax)
    {
        const QString message = QStringLiteral("Invalid LAN control port");
        emit connectionStageChanged(ConnectionStage::Failed, message);
        emit errorOccurred(ErrorCode::ConnectionFailed, message);
        return;
    }

    m_connectionHost = host;
    m_connectionPort = port;
    m_connectionUser = user;
    m_connectionPass = pass;
    m_originalDataOffMod.reset();
    m_originalData1Mod.reset();
    m_selectedRadioMemory.reset();
    m_lastUserVisibleNetworkMessage.clear();

    const quint64 session = ++m_sessionId;
    m_sessionActive = std::make_shared<std::atomic_bool>(true);
    const auto sessionActive = m_sessionActive;
    m_lanModLevel = qBound(0, AppSettings::instance().value("LANModLevel", m_lanModLevel).toInt(), 255);

    m_commander = new Commander();
    Commander* commandSession = m_commander;
    m_commander->moveToThread(m_workerThread);

    connect(m_commander, &RadioCommander::lanReady, this,
            [this, session, commandSession]()
            {
                if (isCurrentSession(session, commandSession))
                {
                    onLanReady();
                }
            });
    connect(m_commander, &RadioCommander::havePortError, this,
            [this, session, commandSession](errorType err)
            {
                if (isCurrentSession(session, commandSession))
                {
                    onPortError(err);
                }
            });
    connect(m_commander, &RadioCommander::haveStatusUpdate, this,
            [this, session, commandSession](networkStatus status)
            {
                if (isCurrentSession(session, commandSession))
                {
                    onNetworkStatus(status);
                }
            });
    connect(m_commander, &RadioCommander::haveSessionHeartbeat, this,
            [this, session, commandSession]()
            {
                if (isCurrentSession(session, commandSession))
                {
                    emit sessionHeartbeat();
                }
            });
    connect(m_commander, &RadioCommander::radioReplyReceived, this,
            [this, session, commandSession](Funcs func, const QVariant& value, uchar receiver)
            {
                if (!isCurrentSession(session, commandSession))
                {
                    return;
                }

                emit radioValueConfirmed(func, value, receiver);

                if (m_dualWatchTransition.pending())
                {
                    if (func == funcVFODualWatch && m_dualWatchTransition.observeState(value.toBool()))
                    {
                        if (!m_dualWatchTransition.requestedEnabled())
                        {
                            selectMainVfoForCommand(commandSession);
                            finishDualWatchTransition(true);
                        }
                        else if (!m_dualWatchIdentityRequested)
                        {
                            m_dualWatchIdentityRequested = true;
                            requestSubVfoIdentityForCommand(commandSession);
                        }
                    }
                    else if (m_dualWatchTransition.requestedEnabled() &&
                             (func == funcFreq || func == funcFreqTR || func == funcFreqGet || func == funcFreqSet ||
                              func == funcSelectedFreq) &&
                             receiver == kSubReceiver)
                    {
                        m_dualWatchTransition.observeSubFrequency();
                    }
                    else if (m_dualWatchTransition.requestedEnabled() &&
                             (func == funcMode || func == funcModeTR || func == funcModeGet || func == funcModeSet ||
                              func == funcSelectedMode || func == funcDataModeWithFilter) &&
                             receiver == kSubReceiver)
                    {
                        m_dualWatchTransition.observeSubMode();
                    }

                    if (m_dualWatchTransition.complete())
                    {
                        finishDualWatchTransition(true);
                    }
                }

                if (!m_mainSubExchangePending || !m_mainSubExchangeDispatched)
                {
                    return;
                }
                if (func == funcVFOBandMS && !value.toBool())
                {
                    m_mainSubExchangeConfirmations |= kExchangeMainStatusConfirmed;
                }
                else if (func == funcScopeMainSub && !value.toBool())
                {
                    m_mainSubExchangeConfirmations |= kExchangeScopeStatusConfirmed;
                }
                else if ((func == funcFreq || func == funcFreqTR || func == funcFreqGet || func == funcFreqSet ||
                          func == funcSelectedFreq) &&
                         receiver == kMainReceiver)
                {
                    m_mainSubExchangeConfirmations |= kExchangeMainFrequencyConfirmed;
                }
                else if ((func == funcMode || func == funcModeTR || func == funcModeGet || func == funcModeSet ||
                          func == funcSelectedMode || func == funcDataModeWithFilter) &&
                         receiver == kMainReceiver &&
                         sdr9700::backend::exchangeModeMayConfirm(m_mainSubExchangeConfirmations,
                                                                  kExchangeMainFrequencyConfirmed))
                {
                    // As with SUB below, mode is only authoritative after the
                    // exchanged receiver's new frequency has established its
                    // band and invalidated the old band's dependent fields.
                    m_mainSubExchangeConfirmations |= kExchangeMainModeConfirmed;
                }
                else if ((func == funcFreq || func == funcFreqTR || func == funcFreqGet || func == funcFreqSet ||
                          func == funcSelectedFreq) &&
                         receiver == kSubReceiver)
                {
                    m_mainSubExchangeConfirmations |= kExchangeSubFrequencyConfirmed;
                }
                else if ((func == funcMode || func == funcModeTR || func == funcModeGet || func == funcModeSet ||
                          func == funcSelectedMode || func == funcDataModeWithFilter) &&
                         receiver == kSubReceiver &&
                         sdr9700::backend::exchangeModeMayConfirm(m_mainSubExchangeConfirmations,
                                                                  kExchangeSubFrequencyConfirmed))
                {
                    // A mode reply that precedes the exchanged SUB frequency
                    // still describes an ambiguous receiver/band snapshot.
                    // RadioState deliberately clears band-dependent fields
                    // when the subsequent frequency establishes a different
                    // band. Do not count that early mode toward completion;
                    // the retry timer will request mode again after frequency
                    // is known, ensuring that an unlocked UI always has a
                    // coherent SUB frequency, mode, and filter tuple.
                    m_mainSubExchangeConfirmations |= kExchangeSubModeConfirmed;
                }
                if (m_mainSubExchangeConfirmations == kExchangeAllConfirmed)
                {
                    m_mainSubExchangePending = false;
                    m_mainSubExchangeDispatched = false;
                    m_mainSubExchangeRetryTimer->stop();
                    m_mainSubExchangeRetryCount = 0;
                    invokeOnCurrentCommander(
                        [](Commander* commandSession)
                        {
                            commandSession->finishMainSubExchangeConfirmation();
                            schedulePostExchangeSettingsForCommand(commandSession);
                        });
                    m_meterPollTuneHoldoff.restart();
                    qInfo(logRadio()).noquote()
                        << "MAIN/SUB exchange confirmed on physical MAIN elapsedMs="
                        << (m_mainSubExchangeClock.isValid() ? m_mainSubExchangeClock.elapsed() : -1);
                    emit mainSubExchangeCompleted();
                }
            });
    connect(m_commander, &Commander::mainSubExchangeDispatched, this,
            [this, session, commandSession]()
            {
                if (!isCurrentSession(session, commandSession) || !m_mainSubExchangePending)
                {
                    return;
                }
                m_mainSubExchangeDispatched = true;
                m_mainSubExchangeConfirmations = 0;
                m_mainSubExchangeRetryCount = 0;
                m_mainSubExchangeRetryTimer->start();
                qInfo(logRadio()).noquote()
                    << "MAIN/SUB exchange dispatched after gate waitMs="
                    << (m_mainSubExchangeClock.isValid() ? m_mainSubExchangeClock.elapsed() : -1);
            });
    connect(m_commander, &Commander::commandTransmitted, this,
            [this, session, commandSession](Funcs func, uchar receiver, qint64 replyTimeoutMs)
            {
                if (!isCurrentSession(session, commandSession) || func != funcSMeter || !m_smeterPollQueued ||
                    receiver != m_smeterPollPendingReceiver)
                {
                    return;
                }
                m_smeterPollQueued = false;
                m_smeterPollPending = true;
                // Receiver-less SUB reads temporarily select the radio's SUB
                // context. Keep that physical-context hold short even when the
                // general adaptive reply timeout grows under transport jitter.
                m_smeterPollDeadlineMs = std::min<qint64>(replyTimeoutMs, 300);
                m_smeterPollQueuedClock.invalidate();
                m_smeterPollPendingClock.start();
            });
    connect(m_commander, &RadioCommander::haveAudioData, this,
            [this, session, commandSession](audioPacket pkt)
            {
                if (isCurrentSession(session, commandSession))
                {
                    onHaveAudioData(pkt);
                }
            });
    connect(m_commander, &RadioCommander::haveTxAudioMeter, this,
            [this, session, commandSession](const sdr9700::audio::TxAudioMeterBlock& block)
            {
                if (!isCurrentSession(session, commandSession))
                {
                    return;
                }
                emit txAudioMeterChanged(block);
            });

    // All radio-to-UI data (frequency, mode, S-meter, scope) flows through the
    // CachingQueue's batched sendValues signal.  Connect once per connectToRadio()
    // call; disconnect the previous session's queue connection first because
    // CachingQueue is a process-wide singleton.
    CachingQueue* const q = CachingQueue::getInstance();
    if (m_queueSendValuesConnection)
    {
        QObject::disconnect(m_queueSendValuesConnection);
        m_queueSendValuesConnection = {};
    }
    RadioRouter* router = m_radioRouter;
    m_routerQueueSession = router->beginQueueSession();
    const quint64 routerQueueSession = m_routerQueueSession;
    m_queueSendValuesConnection = connect(
        q, &CachingQueue::sendValues, router,
        [router, sessionActive, routerQueueSession](const QVector<CacheItem>& items)
        {
            if (!sessionActive->load(std::memory_order_acquire))
            {
                return;
            }
            router->enqueueBatch(items, routerQueueSession);
        },
        Qt::DirectConnection);

    // IC-9700 default LAN ports: control=50001, serial=50002, audio=50003
    UdpConnectionSettings udpSettings;
    udpSettings.ipAddress = host;
    udpSettings.controlLANPort = port;   // typically 50001
    udpSettings.civLANPort = port + 1;   // 50002
    udpSettings.audioLANPort = port + 2; // 50003
    udpSettings.scopeLANPort = port + 3; // 50004
    udpSettings.username = user;
    // passcode() applies the IC-9700 LAN proprietary XOR encoding (not encryption).
    passcode(pass, udpSettings.passwordEncoded);
    udpSettings.halfDuplex = false;
    udpSettings.adminLogin = false;
    // Resolve fresh device snapshots for each connection. A cached QAudioDevice
    // can refer to a device that disconnected or ceased to be the system default.
    refreshRxAudioDevice();
    refreshTxAudioDevice();

    // IC-9700 LAN wire values for LPCM 16-bit signed audio.
    static constexpr quint8 kLpcmMono16 = 0x04;
    static constexpr quint8 kLpcmStereo16 = 0x10;
    static constexpr quint16 kIc9700CivAddress = 0xA2;
    const int playbackChannels = qBound(1, AppSettings::instance().value("audioOutputChannels", 2).toInt(), 2);
    // Always request both IC-9700 receiver channels. Mono playback is a local
    // downmix; requesting the radio's one-channel codec discards SUB entirely.
    m_rxChannelCount = 2;
    const int outputVolume = qBound(0, AppSettings::instance().value("volumeLevel", 128).toInt(), 255);

    audioSetup rxSetup;
    rxSetup.type = qtAudio;
    rxSetup.isinput = false;
    rxSetup.sampleRate = m_rxSampleRate;
    rxSetup.latency = 80;
    rxSetup.codec = kLpcmStereo16;
    rxSetup.resampleQuality = 4;
    rxSetup.localAFgain = static_cast<quint8>(outputVolume);
    rxSetup.playbackChannels = static_cast<quint8>(playbackChannels);
    rxSetup.port = m_rxDevice;

    audioSetup txSetup;
    txSetup.type = qtAudio;
    txSetup.isinput = true;
    txSetup.sampleRate = kTxAudioSampleRate;
    txSetup.latency = 80;
    txSetup.codec = kLpcmMono16;
    txSetup.resampleQuality = 4;
    txSetup.localAFgain = 255;
    txSetup.port = m_txDevice;

    // commSetup must be invoked on the worker thread
    QMetaObject::invokeMethod(
        m_commander,
        [sessionActive, commandSession, udpSettings, rxSetup, txSetup]()
        {
            if (!sessionActive->load(std::memory_order_acquire))
            {
                return;
            }
            commandSession->commSetup(kIc9700CivAddress, udpSettings, rxSetup, txSetup);
        },
        Qt::QueuedConnection);
}

void RadioBackend::disconnectFromRadio()
{
    const bool radioWasReady = m_standbyWakePolicy.complete();
    ++m_bootstrapGeneration;
    m_bootstrapReconnectPending = false;
    m_standbyWakePolicy.reset();
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    emit connectionStageChanged(ConnectionStage::Disconnecting, QStringLiteral("Disconnecting from radio"));
    stopLocalAudio();
    shutdownConnection(true, true, radioWasReady);
    m_connectionHost.clear();
    m_connectionPort = 0;
    m_connectionUser.clear();
    m_connectionPass.clear();
}

void RadioBackend::refreshRxAudioDevice()
{
    const QByteArray savedID =
        QByteArray::fromBase64(AppSettings::instance().value("audioOutputDeviceID").toString().toLatin1());
    const QAudioDevice selected =
        sdr9700::selectAudioDevice(QMediaDevices::audioOutputs(), savedID, QMediaDevices::defaultAudioOutput());
    m_rxDevice = selected;
    // Forward every device-list refresh, even when Qt reports the same device
    // ID. UdpAudio uses this event to clear a previous device-open failure latch.
    invokeOnCurrentCommander([selected](Commander* commander) { commander->setRxAudioDevice(selected); });
}

void RadioBackend::refreshTxAudioDevice()
{
    const QByteArray savedID =
        QByteArray::fromBase64(AppSettings::instance().value("audioInputDeviceID").toString().toLatin1());
    setTxAudioDevice(
        sdr9700::selectAudioDevice(QMediaDevices::audioInputs(), savedID, QMediaDevices::defaultAudioInput()));
}

void RadioBackend::setRxAudioDevice(const QAudioDevice& dev)
{
    if (m_rxDevice == dev)
    {
        return;
    }
    m_rxDevice = dev;
    invokeOnCurrentCommander([dev](Commander* commander) { commander->setRxAudioDevice(dev); });
}

void RadioBackend::setTxAudioDevice(const QAudioDevice& dev)
{
    if (m_txDevice == dev)
    {
        return;
    }
    m_txDevice = dev;
    invokeOnCurrentCommander([dev](Commander* commander) { commander->setTxAudioDevice(dev); });
}

void RadioBackend::stopLocalAudio()
{
    if (!m_commander || !m_sessionActive || !m_sessionActive->load(std::memory_order_acquire))
    {
        return;
    }

    Commander* commandSession = m_commander;
    auto stopDone = std::make_shared<QSemaphore>();
    const bool queued = QMetaObject::invokeMethod(
        commandSession,
        [commandSession, stopDone]()
        {
            commandSession->stopLocalAudio();
            stopDone->release();
        },
        Qt::QueuedConnection);
    if (!queued || !stopDone->tryAcquire(1, sdr9700::shutdownTiming::kStopLocalAudioTimeoutMs))
    {
        qWarning(logAudio()).noquote() << "Timed out waiting for local audio playback to stop";
        return;
    }
    qInfo(logAudio()).noquote() << "[SHUTDOWN] local playback stopped";
}

void RadioBackend::shutdownConnection(bool emitDisconnectedSignal, bool emitDisconnectedStage, bool sendSafetyCommands)
{
    if (!m_commander)
    {
        return;
    }

    if (m_sessionActive)
    {
        m_sessionActive->store(false, std::memory_order_release);
    }
    ++m_sessionId;
    if (m_queueSendValuesConnection)
    {
        QObject::disconnect(m_queueSendValuesConnection);
        m_queueSendValuesConnection = {};
    }
    if (m_radioRouter && m_routerQueueSession != 0)
    {
        m_radioRouter->cancelQueueSession(m_routerQueueSession);
        m_routerQueueSession = 0;
    }
    if (m_pttReleaseDelayTimer)
    {
        m_pttReleaseDelayTimer->stop();
    }
    if (m_pttOnConfirmationTimer)
    {
        m_pttOnConfirmationTimer->stop();
    }
    if (m_pttOffConfirmationTimer)
    {
        m_pttOffConfirmationTimer->stop();
    }
    m_pttOffConfirmationClock.invalidate();
    disarmTransmitSafety();
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->stop();
    }
    m_pttState.reset();
    m_activeVfo = Vfo::Main;
    m_dualWatchEnabled = false;
    const bool dualWatchTransitionWasPending = m_dualWatchTransition.pending();
    m_dualWatchTransition.reset();
    m_dualWatchIdentityRequested = false;
    m_dualWatchRetryCount = 0;
    m_dualWatchTransitionClock.invalidate();
    if (m_dualWatchRetryTimer)
    {
        m_dualWatchRetryTimer->stop();
    }
    if (dualWatchTransitionWasPending)
    {
        emit dualWatchTransitionPendingChanged(false);
    }
    m_vfoStatePollPhase = 0;
    m_smeterPollPending = false;
    m_smeterPollQueued = false;
    m_smeterPollDeadlineMs = 0;
    m_meterDiagnostics = {};
    m_smeterPollQueuedClock.invalidate();
    m_smeterPollPendingClock.invalidate();
    m_smeterPollTick = 0;
    m_mainSubExchangePending = false;
    m_mainSubExchangeDispatched = false;
    m_mainSubExchangeConfirmations = 0;
    m_mainSubExchangeRetryCount = 0;
    m_mainSubExchangeClock.invalidate();
    if (m_mainSubExchangeRetryTimer)
    {
        m_mainSubExchangeRetryTimer->stop();
    }

    if (m_smeterPollTimer)
    {
        m_smeterPollTimer->stop();
        m_smeterPollTimer->deleteLater();
        m_smeterPollTimer = nullptr;
    }
    if (m_vfoStatePollTimer)
    {
        m_vfoStatePollTimer->stop();
    }
    if (m_initialStateRetryTimer)
    {
        m_initialStateRetryTimer->stop();
        m_initialStateRetryTimer->deleteLater();
        m_initialStateRetryTimer = nullptr;
    }
    if (m_scopeRetryTimer)
    {
        m_scopeRetryTimer->stop();
        m_scopeRetryTimer->deleteLater();
        m_scopeRetryTimer = nullptr;
    }
    if (m_bandStateRefreshTimer)
    {
        m_bandStateRefreshTimer->stop();
    }

    Commander* commandSession = m_commander;
    const std::optional<int> originalDataOffMod = m_originalDataOffMod;
    const std::optional<int> originalData1Mod = m_originalData1Mod;

    if (QThread::currentThread() == m_commander->thread())
    {
        if (sendSafetyCommands)
        {
            sendDisconnectSafetyCommands(m_commander, originalDataOffMod, originalData1Mod);
        }
        m_commander->closeComm();
    }
    else
    {
        auto closeDone = std::make_shared<QSemaphore>();
        const bool queued = QMetaObject::invokeMethod(
            m_commander,
            [commandSession, closeDone, originalDataOffMod, originalData1Mod, sendSafetyCommands]()
            {
                if (sendSafetyCommands)
                {
                    sendDisconnectSafetyCommands(commandSession, originalDataOffMod, originalData1Mod);
                }
                commandSession->closeComm();
                closeDone->release();
            },
            Qt::QueuedConnection);
        if (!queued || !closeDone->tryAcquire(1, sdr9700::shutdownTiming::kConnectionShutdownTimeoutMs))
        {
            qWarning(logRadio()).noquote().nospace()
                << "[SHUTDOWN] closeComm() did not finish within "
                << sdr9700::shutdownTiming::kConnectionShutdownTimeoutMs << " ms; continuing disconnect";
        }
    }
    commandSession->deleteLater();
    m_commander = nullptr;
    m_radioReady = false;
    m_scopeDataReceived = false;
    m_scopeEnableRequested = false;
    setScopeSyncDegraded(false);
    resetScopeController();
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialSubFrequencyReceived = false;
    m_initialSubModeReceived = false;
    m_audioReady = false;
    m_initialStateRequested = false;
    m_postReadyStateRequested = false;
    m_currentBandKey = -1;
    m_currentMainFrequencyHz = 0;
    m_currentSubFrequencyHz = 0;
    m_sameBandRefreshPolicy.reset();
    m_currentDuplexMode = dmSimplex;
    m_currentRepeaterOffsetHz = 0;
    m_transmitConfiguration.reset();
    m_txMeterPollTick = 0;
    m_smeterPollTick = 0;
    m_originalDataOffMod.reset();
    m_originalData1Mod.reset();
    m_lastUserVisibleNetworkMessage.clear();
    emit readyChanged(false);
    if (emitDisconnectedSignal)
    {
        emit disconnected();
    }
    if (emitDisconnectedStage)
    {
        emit connectionStageChanged(ConnectionStage::Disconnected, QStringLiteral("Radio disconnected"));
    }
}

void RadioBackend::setFrequencyHz(quint64 hz)
{
    m_meterPollTuneHoldoff.restart();
    m_transmitConfiguration.requestFrequency(hz);
    const bool transferSelectedMemory = m_selectedRadioMemory.has_value();
    m_selectedRadioMemory.reset();

    Frequency f;
    f.Hz = hz;
    f.MHzDouble = hz / 1e6;
    f.VFO = activeVFO;
    invokeOnCurrentCommander(
        [f, transferSelectedMemory](Commander* commandSession)
        {
            if (transferSelectedMemory)
            {
                commandSession->receiveCommand(funcMemoryToVFO, QVariant(), 0);
                commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(f), 0);
                commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
                commandSession->receiveCommand(funcModeGet, QVariant(), 0);
                return;
            }
            commandSession->scheduleInteractiveAction(
                funcFreqSet, 0,
                [commandSession, f]()
                {
                    selectMainVfoForCommand(commandSession);
                    commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(f), 0);
                    scheduleVfoReceiverReadForCommand(commandSession, Vfo::Main, funcFreqGet);
                });
        });
}

void RadioBackend::setMode(const QString& mode)
{
    const bool transferSelectedMemory = m_selectedRadioMemory.has_value();
    m_selectedRadioMemory.reset();

    if (!m_commander)
    {
        return;
    }

    ModeInfo mi;
    if (!populateModeInfo(mode, &mi))
    {
        qWarning(logRadio()).noquote() << "Ignoring unsupported mode selection:" << mode;
        return;
    }
    mi.filter = static_cast<quint8>(qBound(1, m_currentMainFilter, 3));

    invokeOnCurrentCommander(
        [mi, transferSelectedMemory](Commander* commandSession)
        {
            if (transferSelectedMemory)
            {
                commandSession->receiveCommand(funcMemoryToVFO, QVariant(), 0);
            }
            else
            {
                selectMainVfoForCommand(commandSession);
            }
            commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(mi), 0);
            commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
            commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
        });
}

void RadioBackend::setFilterWidth(int lowHz, int highHz)
{
    Q_UNUSED(lowHz)
    Q_UNUSED(highHz)
    // IC-9700 filter select is via CI-V 0x26 (filter 1/2/3); deferred to v1.1.
}

void RadioBackend::setNrEnabled(bool on)
{
    routeVfoReceiverCommand(m_activeVfo, funcNoiseReduction, [on](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcNoiseReduction, QVariant::fromValue(on), receiver); });
}

void RadioBackend::setNrLevel(int level)
{
    const ushort rawLevel = static_cast<ushort>(qRound((qBound(1, level, 15) - 1) * 255.0 / 14.0));
    scheduleVfoReceiverCommand(
        m_activeVfo, funcNRLevel, [rawLevel](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommand(funcNRLevel, QVariant::fromValue(rawLevel), receiver); });
}

void RadioBackend::setNbEnabled(bool on)
{
    routeVfoReceiverCommand(m_activeVfo, funcNoiseBlanker, [on](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcNoiseBlanker, QVariant::fromValue(on), receiver); });
}

void RadioBackend::setNbLevel(int level)
{
    const ushort rawLevel = static_cast<ushort>(qRound((qBound(1, level, 10) - 1) * 255.0 / 9.0));
    scheduleVfoReceiverCommand(
        m_activeVfo, funcNBLevel, [rawLevel](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommand(funcNBLevel, QVariant::fromValue(rawLevel), receiver); });
}

void RadioBackend::setPreampEnabled(bool on)
{
    setPreampLevel(on ? 1 : 0);
}

void RadioBackend::setPreampLevel(int level)
{
    const uchar val = static_cast<uchar>(qBound(0, level, 3));
    routeVfoReceiverCommand(m_activeVfo, funcPreamp,
                            [val](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcPreamp, QVariant::fromValue(val), receiver);
                                commandSession->receiveCommand(funcAttenuator, QVariant(), receiver);
                            });
}

void RadioBackend::setAttenuatorEnabled(bool on)
{
    const uchar val = on ? 10 : 0;
    routeVfoReceiverCommand(m_activeVfo, funcAttenuator,
                            [val](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcAttenuator, QVariant::fromValue(val), receiver);
                                commandSession->receiveCommand(funcPreamp, QVariant(), receiver);
                            });
}

void RadioBackend::setAfGain(int level)
{
    invokeOnCurrentCommander([level](Commander* commandSession)
                             { commandSession->receiveCommand(funcAfGain, QVariant(level), 0xff); });
}

void RadioBackend::setRfGain(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    scheduleVfoReceiverCommand(m_activeVfo, funcRfGain, [bounded](Commander* commandSession, uchar receiver)
                               { commandSession->receiveCommand(funcRfGain, QVariant::fromValue(bounded), receiver); });
}

void RadioBackend::setTxPower(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    scheduleVfoReceiverCommand(
        Vfo::Main, funcRFPower, [bounded](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommand(funcRFPower, QVariant::fromValue(bounded), receiver); });
}

void RadioBackend::setTuningStep(int step)
{
    const uchar val = static_cast<uchar>(qBound(0, step, 11));
    routeVfoReceiverCommand(m_activeVfo, funcTuningStep, [val](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcTuningStep, QVariant::fromValue(val), receiver); });
}

void RadioBackend::selectVfo(Vfo vfo)
{
    // This is the stable operator selection used by all UI-originated VFO
    // commands and state polling. Commander may temporarily select either
    // physical receiver to execute a scoped transaction, but those routing
    // details must never alter this value.
    m_activeVfo = vfo;
    invokeOnCurrentCommander(
        [vfo](Commander* commandSession)
        {
            commandSession->setOperatorSelectedVfo(vfo == Vfo::Sub ? vfoSub : vfoMain);
            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(vfo == Vfo::Sub), 0);
        });
}

void RadioBackend::exchangeMainSub()
{
    if (!m_commander || !m_dualWatchEnabled || m_mainSubExchangePending || m_dualWatchTransition.pending())
    {
        qWarning(logRadio()).noquote() << "Ignoring MAIN/SUB exchange commanderAvailable=" << (m_commander != nullptr)
                                       << " dualWatchEnabled=" << m_dualWatchEnabled
                                       << " exchangePending=" << m_mainSubExchangePending
                                       << " dualWatchTransitionPending=" << m_dualWatchTransition.pending();
        return;
    }

    m_meterPollTuneHoldoff.restart();
    m_mainSubExchangeClock.restart();
    qInfo(logRadio()).noquote() << "MAIN/SUB exchange requested";
    m_mainSubExchangePending = true;
    m_mainSubExchangeDispatched = false;
    m_mainSubExchangeConfirmations = 0;
    m_mainSubExchangeRetryCount = 0;
    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            // IC-9700 native MAIN/SUB exchange moves the VFO operating
            // context, but RF gain remains attached to the physical MAIN and
            // SUB receivers. Preserve that radio-authoritative behavior. If a
            // future design wants RF gain to follow the logical VFO boxes, it
            // must explicitly snapshot and restore both receiver values after
            // this command rather than assuming 07 B0 exchanges them.
            // Commander owns the whole receiver-context transaction. It will
            // not exchange the radio until both ambiguous reply families are
            // idle, so the SUB identity reads cannot be stranded behind an
            // older request after the physical swap has already happened.
            commandSession->requestMainSubExchange();
        });
}

void RadioBackend::setVfoFrequencyHz(Vfo vfo, quint64 hz)
{
    m_meterPollTuneHoldoff.restart();
    if (vfo == Vfo::Main)
    {
        setFrequencyHz(hz);
        return;
    }

    Frequency frequency;
    frequency.Hz = hz;
    frequency.MHzDouble = hz / 1e6;
    frequency.VFO = activeVFO;
    scheduleVfoReceiverCommand(
        Vfo::Sub, funcFreqSet, [frequency](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(frequency), receiver); });
    // The IC-9700 can acknowledge the routed set but return no frequency
    // payload when a SUB read is placed in the same select/read/restore burst.
    // Confirm after the receiver-context write has had a short settling
    // interval. This retries only the read, never the already accepted write.
    QTimer::singleShot(100, this,
                       [this]()
                       {
                           invokeOnCurrentCommander(
                               [](Commander* commandSession)
                               { scheduleVfoReceiverReadForCommand(commandSession, Vfo::Sub, funcFreqGet); });
                       });
    QTimer::singleShot(600, this,
                       [this]()
                       {
                           invokeOnCurrentCommander(
                               [](Commander* commandSession)
                               { scheduleVfoReceiverReadForCommand(commandSession, Vfo::Sub, funcFreqGet); });
                       });
}

void RadioBackend::scheduleVfoReceiverReadForCommand(Commander* commandSession, Vfo vfo, Funcs func)
{
    const uchar receiver = sdr9700::backend::receiverForVfo(vfo);
    commandSession->scheduleConfirmatoryAction(func, receiver, [commandSession, func, receiver]()
                                               { commandSession->requestReceiverScopedRead(func, receiver); });
}

void RadioBackend::schedulePostExchangeSettingsForCommand(Commander* commandSession)
{
    if (!commandSession)
    {
        return;
    }

    static constexpr std::array kSettings{
        funcSplitStatus, funcReadFreqOffset, funcToneSquelchType, funcToneFreq, funcTSQLFreq, funcDTCSCode,
    };
    for (const uchar receiver : {kMainReceiver, kSubReceiver})
    {
        commandSession->scheduleConfirmatoryAction(funcSplitStatus, receiver,
                                                   [commandSession, receiver]()
                                                   {
                                                       commandSession->executeReceiverScopedAction(
                                                           receiver,
                                                           [commandSession, receiver]()
                                                           {
                                                               for (const Funcs func : kSettings)
                                                               {
                                                                   commandSession->receiveCommand(func, QVariant(),
                                                                                                  receiver);
                                                               }
                                                           });
                                                   });
    }
}

void RadioBackend::requestVfoState(Vfo vfo)
{
    if (vfo == Vfo::Sub)
    {
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                requestSubVfoStateForCommand(commandSession);
                commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            });
        return;
    }

    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            selectMainVfoForCommand(commandSession);
            commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
            commandSession->receiveCommand(funcModeGet, QVariant(), 0);
            for (const Funcs func :
                 {funcAGCTimeConstant, funcAttenuator,     funcNoiseBlanker, funcNBLevel,        funcAutoNotch,
                  funcManualNotch,     funcNoiseReduction, funcNRLevel,      funcPreamp,         funcRfGain,
                  funcSquelch,         funcRFPower,        funcSplitStatus,  funcReadFreqOffset, funcToneSquelchType,
                  funcToneFreq,        funcTSQLFreq,       funcDTCSCode,     funcCompressor,     funcXFCStatus})
            {
                commandSession->receiveCommand(func, QVariant(), 0);
            }
        });
}

void RadioBackend::routeVfoReceiverCommand(Vfo vfo, Funcs func, const std::function<void(Commander*, uchar)>& command)
{
    invokeOnCurrentCommander(
        [vfo, func, command](Commander* commandSession)
        {
            const uchar receiver = sdr9700::backend::receiverForVfo(vfo);
            commandSession->scheduleInteractiveAction(func, receiver,
                                                      [commandSession, command, receiver]()
                                                      {
                                                          commandSession->executeReceiverScopedAction(
                                                              receiver, [commandSession, command, receiver]()
                                                              { command(commandSession, receiver); });
                                                      });
        });
}

void RadioBackend::scheduleVfoReceiverCommand(Vfo vfo, Funcs func,
                                              const std::function<void(Commander*, uchar)>& command)
{
    const uchar receiver = sdr9700::backend::receiverForVfo(vfo);
    invokeOnCurrentCommander(
        [func, receiver, command](Commander* commandSession)
        {
            commandSession->scheduleInteractiveAction(func, receiver,
                                                      [commandSession, command, receiver]()
                                                      {
                                                          commandSession->executeReceiverScopedAction(
                                                              receiver, [commandSession, command, receiver]()
                                                              { command(commandSession, receiver); });
                                                      });
        });
}

void RadioBackend::setVfoMode(Vfo vfo, const QString& mode)
{
    ModeInfo modeInfo;
    if (!populateModeInfo(mode, &modeInfo))
    {
        qWarning(logRadio()).noquote() << "Ignoring unsupported VFO mode selection:" << mode;
        return;
    }
    modeInfo.filter = static_cast<quint8>(qBound(1, m_currentMainFilter, 3));
    routeVfoReceiverCommand(vfo, funcModeSet,
                            [modeInfo](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(modeInfo),
                                                                         receiver);
                                commandSession->receiveCommand(funcModeGet, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoFilter(Vfo vfo, const QString& mode, int filter)
{
    ModeInfo modeInfo;
    if (!populateModeInfo(mode, &modeInfo))
    {
        qWarning(logRadio()).noquote() << "Ignoring filter selection for unsupported VFO mode:" << mode;
        return;
    }
    modeInfo.filter = static_cast<quint8>(qBound(1, filter, 3));
    routeVfoReceiverCommand(vfo, funcModeSet,
                            [modeInfo](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(modeInfo),
                                                                         receiver);
                                commandSession->receiveCommand(funcModeGet, QVariant(), receiver);
                            });
}

void RadioBackend::applyVfoBandRecall(Vfo vfo, const VfoBandRecallRequest& recall)
{
    if (recall.frequencyHz == 0 || m_pttState.safetyActive())
    {
        qWarning(logRadio()).noquote() << "Ignoring band recall without a frequency or while transmit safety is active";
        return;
    }

    ModeInfo modeInfo;
    const bool hasMode =
        recall.mode.has_value() && recall.filter.has_value() && populateModeInfo(*recall.mode, &modeInfo);
    if (hasMode)
    {
        modeInfo.filter = static_cast<quint8>(qBound(1, *recall.filter, 3));
    }

    // MAIN is the IC-9700 transmit path. Mark its frequency, duplex mode, and
    // offset as pending before any CI-V is queued so PTT cannot race ahead of
    // the confirming readbacks at the end of this restore.
    if (vfo == Vfo::Main)
    {
        m_transmitConfiguration.requestFrequency(recall.frequencyHz);
        if (recall.duplexMode.has_value())
        {
            m_transmitConfiguration.requestDuplexMode(*recall.duplexMode);
        }
        if (recall.repeaterOffsetHz.has_value())
        {
            m_transmitConfiguration.requestOffset(*recall.repeaterOffsetHz);
        }
    }

    Frequency frequency;
    frequency.Hz = recall.frequencyHz;
    frequency.MHzDouble = recall.frequencyHz / 1e6;
    frequency.VFO = activeVFO;

    Frequency offset;
    offset.Hz = recall.repeaterOffsetHz.value_or(0);
    offset.MHzDouble = offset.Hz / 1e6;
    offset.VFO = activeVFO;

    scheduleVfoReceiverCommand(
        vfo, funcFreqSet,
        [recall, frequency, offset, modeInfo, hasMode](Commander* commandSession, uchar receiver)
        {
            // A direct frequency change can cause the IC-9700 to load another
            // band's simplex defaults. Keep the entire restore in one routed
            // receiver context, set frequency first, and then reassert every
            // confirmed field remembered for that receiver/band.
            commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(frequency), receiver);
            if (hasMode)
            {
                commandSession->receiveCommandNoReadback(funcModeSet, QVariant::fromValue(modeInfo), receiver);
            }
            if (recall.repeaterOffsetHz.has_value())
            {
                commandSession->receiveCommandNoReadback(funcSendFreqOffset, QVariant::fromValue(offset), receiver);
            }
            if (recall.duplexMode.has_value())
            {
                commandSession->receiveCommandNoReadback(funcSplitStatus, QVariant::fromValue(*recall.duplexMode),
                                                         receiver);
            }
            if (recall.toneFrequency.has_value())
            {
                commandSession->receiveCommandNoReadback(
                    funcToneFreq, QVariant::fromValue(ToneInfo(*recall.toneFrequency)), receiver);
            }
            if (recall.toneSquelchFrequency.has_value())
            {
                commandSession->receiveCommandNoReadback(
                    funcTSQLFreq, QVariant::fromValue(ToneInfo(*recall.toneSquelchFrequency)), receiver);
            }
            if (recall.dtcsCode.has_value())
            {
                commandSession->receiveCommandNoReadback(funcDTCSCode, QVariant::fromValue(ToneInfo(*recall.dtcsCode)),
                                                         receiver);
            }
            if (recall.toneAccessMode.has_value())
            {
                RptrAccessData access;
                access.accessMode = *recall.toneAccessMode;
                commandSession->receiveCommandNoReadback(funcToneSquelchType, QVariant::fromValue(access), receiver);
            }

            // The radio remains authoritative. Schedule each confirmation as
            // its own receiver-scoped transaction. A reply-family drain may
            // delay a read beyond this routed callback's MAIN restore; the
            // scheduled helper reselects the intended receiver at actual
            // dispatch time so a delayed SUB confirmation cannot read MAIN
            // while still being attributed to SUB.
            scheduleVfoReceiverReadForCommand(commandSession, receiver == kSubReceiver ? Vfo::Sub : Vfo::Main,
                                              funcFreqGet);
            scheduleVfoReceiverReadForCommand(commandSession, receiver == kSubReceiver ? Vfo::Sub : Vfo::Main,
                                              funcModeGet);
            for (const Funcs func :
                 {funcSplitStatus, funcReadFreqOffset, funcToneSquelchType, funcToneFreq, funcTSQLFreq, funcDTCSCode})
            {
                scheduleVfoReceiverReadForCommand(commandSession, receiver == kSubReceiver ? Vfo::Sub : Vfo::Main,
                                                  func);
            }
        });
}

void RadioBackend::setVfoAgcMode(Vfo vfo, const QString& mode)
{
    uchar agc = mode == QStringLiteral("fast") ? 1 : mode == QStringLiteral("slow") ? 3 : 2;
    routeVfoReceiverCommand(vfo, funcAGCTimeConstant,
                            [agc](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcAGCTimeConstant, QVariant::fromValue(agc),
                                                                         receiver);
                                commandSession->receiveCommand(funcAGCTimeConstant, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoAttenuatorEnabled(Vfo vfo, bool on)
{
    const uchar value = on ? 10 : 0;
    routeVfoReceiverCommand(vfo, funcAttenuator,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcAttenuator, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcAttenuator, QVariant(), receiver);
                                commandSession->receiveCommand(funcPreamp, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNbEnabled(Vfo vfo, bool on)
{
    routeVfoReceiverCommand(vfo, funcNoiseBlanker,
                            [on](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcNoiseBlanker, QVariant::fromValue(on),
                                                                         receiver);
                                commandSession->receiveCommand(funcNoiseBlanker, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNbLevel(Vfo vfo, int level)
{
    const ushort value = static_cast<ushort>(qRound((qBound(1, level, 10) - 1) * 255.0 / 9.0));
    routeVfoReceiverCommand(vfo, funcNBLevel,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcNBLevel, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcNBLevel, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNotch(Vfo vfo, VfoNotch notch)
{
    routeVfoReceiverCommand(vfo, funcAutoNotch,
                            [notch](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(
                                    funcAutoNotch, QVariant::fromValue(notch == VfoNotch::Auto), receiver);
                                commandSession->receiveCommandNoReadback(
                                    funcManualNotch, QVariant::fromValue(notch == VfoNotch::Manual), receiver);
                                commandSession->receiveCommand(funcAutoNotch, QVariant(), receiver);
                                commandSession->receiveCommand(funcManualNotch, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNrEnabled(Vfo vfo, bool on)
{
    routeVfoReceiverCommand(vfo, funcNoiseReduction,
                            [on](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcNoiseReduction, QVariant::fromValue(on),
                                                                         receiver);
                                commandSession->receiveCommand(funcNoiseReduction, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoNrLevel(Vfo vfo, int level)
{
    const ushort value = static_cast<ushort>(qRound((qBound(1, level, 15) - 1) * 255.0 / 14.0));
    routeVfoReceiverCommand(vfo, funcNRLevel,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcNRLevel, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcNRLevel, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoPreampLevel(Vfo vfo, int level)
{
    const uchar value = static_cast<uchar>(qBound(0, level, 3));
    routeVfoReceiverCommand(vfo, funcPreamp,
                            [value](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommandNoReadback(funcPreamp, QVariant::fromValue(value),
                                                                         receiver);
                                commandSession->receiveCommand(funcPreamp, QVariant(), receiver);
                                commandSession->receiveCommand(funcAttenuator, QVariant(), receiver);
                            });
}

void RadioBackend::setVfoRfGain(Vfo vfo, int level)
{
    const ushort value = static_cast<ushort>(qBound(0, level, 255));
    qDebug(logRadio()).noquote().nospace()
        << "Receiver level request control=RFG vfo=" << (vfo == Vfo::Main ? "MAIN" : "SUB")
        << " receiver=" << int(sdr9700::backend::receiverForVfo(vfo)) << " raw=" << value;
    scheduleVfoReceiverCommand(vfo, funcRfGain,
                               [value](Commander* commandSession, uchar receiver)
                               {
                                   commandSession->receiveCommandNoReadback(funcRfGain, QVariant::fromValue(value),
                                                                            receiver);
                                   commandSession->receiveCommand(funcRfGain, QVariant(), receiver);
                               });
}

void RadioBackend::setVfoSquelch(Vfo vfo, int level)
{
    const ushort value = static_cast<ushort>(qBound(0, level, 255));
    qDebug(logRadio()).noquote().nospace()
        << "Receiver level request control=SQL vfo=" << (vfo == Vfo::Main ? "MAIN" : "SUB")
        << " receiver=" << int(sdr9700::backend::receiverForVfo(vfo)) << " raw=" << value;
    scheduleVfoReceiverCommand(vfo, funcSquelch,
                               [value](Commander* commandSession, uchar receiver)
                               {
                                   commandSession->receiveCommandNoReadback(funcSquelch, QVariant::fromValue(value),
                                                                            receiver);
                                   commandSession->receiveCommand(funcSquelch, QVariant(), receiver);
                               });
}

bool RadioBackend::setDualWatchEnabled(bool on)
{
    if (!m_commander || m_dualWatchTransition.pending() || m_mainSubExchangePending || m_pttState.safetyActive())
    {
        return false;
    }

    if (!m_dualWatchTransition.request(on))
    {
        return false;
    }
    m_dualWatchIdentityRequested = false;
    m_dualWatchRetryCount = 0;
    m_dualWatchTransitionClock.restart();
    emit dualWatchTransitionPendingChanged(true);
    qInfo(logRadio()).noquote() << "Dual-watch transition requested enabled=" << on;

    invokeOnCurrentCommander(
        [on](Commander* commandSession)
        {
            commandSession->receiveCommandNoReadback(funcVFODualWatch, QVariant::fromValue<bool>(on), 0);
            commandSession->receiveCommand(funcVFODualWatch, QVariant(), 0);
        });
    m_dualWatchRetryTimer->start();
    return true;
}

void RadioBackend::finishDualWatchTransition(bool success)
{
    if (!m_dualWatchTransition.pending())
    {
        return;
    }

    const bool enabled = m_dualWatchTransition.requestedEnabled();
    const quint8 confirmations = m_dualWatchTransition.confirmations();
    m_dualWatchRetryTimer->stop();
    m_dualWatchRetryCount = 0;
    m_dualWatchIdentityRequested = false;
    m_meterPollTuneHoldoff.restart();
    qInfo(logRadio()).noquote().nospace()
        << "Dual-watch transition completed enabled=" << enabled << " success=" << success
        << " confirmations=" << confirmations
        << " elapsedMs=" << (m_dualWatchTransitionClock.isValid() ? m_dualWatchTransitionClock.elapsed() : -1);
    m_dualWatchTransition.reset();
    emit dualWatchTransitionPendingChanged(false);

    if (!success)
    {
        emit statusMessage(QStringLiteral("Dual-watch state refresh failed"), MessageSeverity::Warning);
        return;
    }
    if (enabled)
    {
        scheduleSubVfoControlRefresh();
    }
}

void RadioBackend::scheduleSubVfoControlRefresh()
{
    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            for (const Funcs func :
                 {funcAGCTimeConstant, funcAttenuator, funcNoiseBlanker, funcNBLevel, funcAutoNotch, funcManualNotch,
                  funcNoiseReduction, funcNRLevel, funcPreamp, funcRfGain, funcSquelch, funcSplitStatus,
                  funcReadFreqOffset, funcToneSquelchType, funcToneFreq, funcTSQLFreq, funcDTCSCode})
            {
                scheduleVfoReceiverReadForCommand(commandSession, Vfo::Sub, func);
            }
        });
}

void RadioBackend::setSquelch(bool on, int level)
{
    // On IC-9700, squelch level 0 = fully open, >0 = active.
    // Setting funcSquelch with 0 disables it; non-zero enables + sets level.
    const ushort squelchVal = on ? qMax<ushort>(1, static_cast<ushort>(qBound(0, level, 255))) : 0;
    scheduleVfoReceiverCommand(
        m_activeVfo, funcSquelch, [squelchVal](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommand(funcSquelch, QVariant::fromValue(squelchVal), receiver); });
}

void RadioBackend::setAgcMode(const QString& mode)
{
    if (!m_commander)
    {
        return;
    }
    uchar agc = 2; // MID
    if (mode == "fast")
    {
        agc = 1;
    }
    else if (mode == "mid")
    {
        agc = 2;
    }
    else if (mode == "slow")
    {
        agc = 3;
    }
    routeVfoReceiverCommand(
        m_activeVfo, funcAGCTimeConstant, [agc](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommand(funcAGCTimeConstant, QVariant::fromValue(agc), receiver); });
}

void RadioBackend::setAutoNotch(bool on)
{
    routeVfoReceiverCommand(m_activeVfo, funcAutoNotch, [on](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcAutoNotch, QVariant::fromValue(on), receiver); });
}

void RadioBackend::setManualNotch(bool on)
{
    routeVfoReceiverCommand(m_activeVfo, funcManualNotch, [on](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcManualNotch, QVariant::fromValue(on), receiver); });
}

void RadioBackend::setRitEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcRitStatus, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setDialLockEnabled(bool on)
{
    invokeOnCurrentCommander(
        [on](Commander* commandSession)
        {
            commandSession->scheduleInteractiveAction(funcDialLock, 0,
                                                      [commandSession, on]()
                                                      {
                                                          commandSession->receiveCommandNoReadback(
                                                              funcDialLock, QVariant::fromValue<bool>(on), 0);
                                                          commandSession->receiveCommand(funcDialLock, QVariant(), 0);
                                                      });
        });
}

void RadioBackend::setRitOffset(short hz)
{
    const short bounded = qBound(static_cast<short>(-999), hz, static_cast<short>(999));
    invokeOnCurrentCommander(
        [bounded](Commander* commandSession)
        {
            commandSession->scheduleInteractiveAction(
                funcRitFreq, 0, [commandSession, bounded]()
                { commandSession->receiveCommand(funcRitFreq, QVariant::fromValue<short>(bounded), 0); });
        });
}

void RadioBackend::setCompressor(bool on)
{
    routeVfoReceiverCommand(Vfo::Main, funcCompressor, [on](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcCompressor, QVariant::fromValue(on), receiver); });
}

void RadioBackend::setCompressorLevel(int level)
{
    const ushort bounded = static_cast<ushort>(qBound(0, level, 255));
    scheduleVfoReceiverCommand(
        Vfo::Main, funcCompressorLevel, [bounded](Commander* commandSession, uchar receiver)
        { commandSession->receiveCommand(funcCompressorLevel, QVariant::fromValue(bounded), receiver); });
}

void RadioBackend::setXfcEnabled(bool on)
{
    routeVfoReceiverCommand(Vfo::Main, funcXFCStatus, [on](Commander* commandSession, uchar receiver)
                            { commandSession->receiveCommand(funcXFCStatus, QVariant::fromValue(on), receiver); });
}

void RadioBackend::setDuplexMode(duplexMode_t mode)
{
    m_transmitConfiguration.requestDuplexMode(mode);
    routeVfoReceiverCommand(m_activeVfo, funcSplitStatus,
                            [mode](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcSplitStatus, QVariant::fromValue(mode), receiver);
                                commandSession->receiveCommand(funcSplitStatus, QVariant(), receiver);
                            });
}

void RadioBackend::setRepeaterOffsetHz(quint64 hz)
{
    m_transmitConfiguration.requestOffset(hz);
    Frequency offset;
    offset.Hz = hz;
    offset.MHzDouble = hz / 1e6;
    offset.VFO = activeVFO;
    routeVfoReceiverCommand(m_activeVfo, funcSendFreqOffset,
                            [offset](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcSendFreqOffset, QVariant::fromValue(offset),
                                                               receiver);
                                commandSession->receiveCommand(funcReadFreqOffset, QVariant(), receiver);
                            });
}

void RadioBackend::setToneAccessMode(rptAccessTxRx_t mode)
{
    RptrAccessData access;
    access.accessMode = mode;
    routeVfoReceiverCommand(m_activeVfo, funcToneSquelchType,
                            [access](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcToneSquelchType, QVariant::fromValue(access),
                                                               receiver);
                                commandSession->receiveCommand(funcToneSquelchType, QVariant(), receiver);
                            });
}

void RadioBackend::setToneFrequency(ushort tone)
{
    ToneInfo info(tone);
    routeVfoReceiverCommand(m_activeVfo, funcToneFreq,
                            [info](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcToneFreq, QVariant::fromValue(info), receiver);
                                commandSession->receiveCommand(funcTSQLFreq, QVariant::fromValue(info), receiver);
                                commandSession->receiveCommand(funcToneFreq, QVariant(), receiver);
                                commandSession->receiveCommand(funcTSQLFreq, QVariant(), receiver);
                            });
}

void RadioBackend::setDtcsCode(ushort code)
{
    ToneInfo info(code);
    routeVfoReceiverCommand(m_activeVfo, funcDTCSCode,
                            [info](Commander* commandSession, uchar receiver)
                            {
                                commandSession->receiveCommand(funcDTCSCode, QVariant::fromValue(info), receiver);
                                commandSession->receiveCommand(funcDTCSCode, QVariant(), receiver);
                            });
}

void RadioBackend::selectMainVfoForCommand(Commander* commandSession)
{
    if (!commandSession)
    {
        return;
    }
    commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
}

void RadioBackend::requestSubVfoStateForCommand(Commander* commandSession)
{
    if (!commandSession)
    {
        return;
    }
    requestSubVfoIdentityForCommand(commandSession, false);

    // Continue reading the receiver controls needed during startup or an
    // explicit full state refresh while SUB remains selected.
    for (const Funcs func :
         {funcAGCTimeConstant, funcAttenuator, funcNoiseBlanker, funcNBLevel, funcAutoNotch, funcManualNotch,
          funcNoiseReduction, funcNRLevel, funcPreamp, funcRfGain, funcSquelch, funcSplitStatus, funcReadFreqOffset,
          funcToneSquelchType, funcToneFreq, funcTSQLFreq, funcDTCSCode})
    {
        commandSession->receiveCommand(func, QVariant(), 1);
    }
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
}

void RadioBackend::requestSubVfoIdentityForCommand(Commander* commandSession, bool restoreMain, bool requestFrequency,
                                                   bool requestMode)
{
    if (!commandSession)
    {
        return;
    }
    // IC-9700 CI-V has no direct MAIN/SUB receiver prefix. Select SUB,
    // correlate the current-frequency/mode replies as receiver 1, and restore
    // MAIN before other application commands continue.
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoSub), 0);
    if (requestFrequency)
    {
        commandSession->receiveCommand(funcFreqGet, QVariant(), 1);
    }
    if (requestMode)
    {
        commandSession->receiveCommand(funcModeGet, QVariant(), 1);
    }
    if (restoreMain)
    {
        commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
    }
}

void RadioBackend::requestVfoFrequenciesForCommand(Commander* commandSession)
{
    if (!commandSession)
    {
        return;
    }

    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
    commandSession->receiveCommand(funcFreqGet, QVariant(), 0);
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoSub), 0);
    commandSession->receiveCommand(funcFreqGet, QVariant(), 1);
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
}

void RadioBackend::observeVfoFrequency(quint64 hz, uchar receiver)
{
    if (hz == 0)
    {
        return;
    }

    if (receiver == 0)
    {
        m_currentMainFrequencyHz = hz;
    }
    else if (receiver == 1)
    {
        m_currentSubFrequencyHz = hz;
    }
    else
    {
        return;
    }

    // An exchange necessarily passes through a transient where one cached
    // frequency has moved and the other has not. Do not mistake that expected
    // intermediate state for duplicated VFO contents or inject another
    // receiver-selection sequence into the exchange. The first frequency
    // report after the exchange settles evaluates the pair normally.
    const bool receiverContextSettling =
        m_meterPollTuneHoldoff.isValid() && m_meterPollTuneHoldoff.elapsed() < kReceiverContextSettleMs;
    if (m_mainSubExchangePending || m_dualWatchTransition.pending() || receiverContextSettling)
    {
        m_sameBandRefreshPolicy.reset();
        return;
    }

    if (!m_sameBandRefreshPolicy.observe(m_currentMainFrequencyHz, m_currentSubFrequencyHz))
    {
        return;
    }

    qWarning(logRadio()).noquote() << "Duplicate VFO frequency detected; refreshing frequencies main_hz="
                                   << m_currentMainFrequencyHz << "sub_hz=" << m_currentSubFrequencyHz;
    invokeOnCurrentCommander([](Commander* commandSession) { requestVfoFrequenciesForCommand(commandSession); });
}

void RadioBackend::selectMemoryBandForCommand(Commander* commandSession, quint16 group, Vfo targetVfo)
{
    if (!commandSession)
    {
        return;
    }

    const quint64 hz = memoryGroupDefaultFrequencyHz(group);
    if (hz == 0)
    {
        qWarning(logRadio()).noquote() << "Ignoring memory band selection for unsupported group" << group;
        return;
    }

    Frequency frequency;
    frequency.Hz = hz;
    frequency.MHzDouble = hz / 1e6;
    frequency.VFO = activeVFO;

    // IC-9700 memory channels are scoped by band. Select the requested VFO and
    // tune it inside the desired band before entering memory mode so command
    // 08h resolves channel N against the intended memory group without moving
    // the other receiver.
    commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(targetVfo == Vfo::Sub), 0);
    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(targetVfo == Vfo::Sub ? vfoSub : vfoMain),
                                   0);
    commandSession->receiveCommand(funcVFOModeSelect, QVariant(), 0);
    commandSession->receiveCommandNoReadback(funcFreqSet, QVariant::fromValue(frequency), 0);
}

void RadioBackend::selectMemoryForCommand(Commander* commandSession, quint16 group, quint16 channel, bool prepareBand)
{
    if (!commandSession)
    {
        return;
    }
    if (memoryGroupDefaultFrequencyHz(group) == 0)
    {
        qWarning(logRadio()).noquote() << "Ignoring memory channel selection for unsupported group" << group;
        return;
    }

    if (prepareBand)
    {
        // The legacy prepareBand path is used only by MAIN-oriented callers.
        // Target-aware Memory Manager activation prepares its selected VFO
        // explicitly and passes false here.
        selectMemoryBandForCommand(commandSession, group, Vfo::Main);
    }
    // Command 08h carries the channel number. When prepareBand is false the
    // caller has already selected the desired band and is avoiding VFO tuning
    // commands in a timing-sensitive path such as PTT.
    commandSession->receiveCommand(funcMemoryMode, QVariant(), 0);
    commandSession->receiveCommand(funcMemoryMode, QVariant::fromValue(static_cast<uint>(channel)), 0);
}

void RadioBackend::setScopeEnabled(bool on)
{
    invokeOnCurrentCommander([=](Commander* commandSession)
                             { commandSession->receiveCommand(funcScopeOnOff, QVariant::fromValue<bool>(on), 0); });
}

void RadioBackend::setScopeSpanHz(quint64 hz)
{
    if (!m_commander)
    {
        return;
    }

    static constexpr quint64 kSupportedSpans[] = {2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000};
    quint64 selected = kSupportedSpans[std::size(kSupportedSpans) - 1];
    for (const quint64 span : kSupportedSpans)
    {
        selected = span;
        if (hz <= span)
        {
            break;
        }
    }

    centerSpanData span;
    span.freq = selected;
    span.name = QString::number(selected);
    const uchar receiver = sdr9700::backend::receiverForVfo(m_activeVfo);
    invokeOnCurrentCommander(
        [span, receiver](Commander* commandSession)
        { commandSession->receiveCommand(funcScopeSpan, QVariant::fromValue<centerSpanData>(span), receiver); });
}

void RadioBackend::setScopeMode(int mode)
{
    const uchar bounded = static_cast<uchar>(qBound(0, mode, 1));
    const uchar receiver = sdr9700::backend::receiverForVfo(m_activeVfo);
    invokeOnCurrentCommander(
        [bounded, receiver](Commander* commandSession)
        { commandSession->receiveCommand(funcScopeMode, QVariant::fromValue<uchar>(bounded), receiver); });
}

void RadioBackend::setScopeVfo(Vfo vfo)
{
    const bool sub = vfo == Vfo::Sub;
    invokeOnCurrentCommander([sub](Commander* commandSession)
                             { commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(sub), 0); });
}

void RadioBackend::setScopeFixedRangeHz(quint64 startHz, quint64 endHz)
{
    if (endHz <= startHz)
    {
        return;
    }

    static constexpr uchar kPanScopeEdge = 1;
    SpectrumBounds bounds(startHz / 1e6, endHz / 1e6, kPanScopeEdge);
    const uchar receiver = sdr9700::backend::receiverForVfo(m_activeVfo);
    invokeOnCurrentCommander(
        [bounds, receiver](Commander* commandSession)
        {
            commandSession->receiveCommand(funcScopeFixedEdgeFreq, QVariant::fromValue<SpectrumBounds>(bounds), 0);
            commandSession->receiveCommand(funcScopeEdge, QVariant::fromValue<uchar>(bounds.edge), receiver);
            commandSession->receiveCommand(funcScopeMode, QVariant::fromValue<uchar>(1), receiver);
        });
}

void RadioBackend::setSpectrumFramesPerSecond(int framesPerSecond)
{
    QMetaObject::invokeMethod(
        m_scopeController, [controller = m_scopeController, framesPerSecond]()
        { controller->setFramesPerSecond(framesPerSecond); }, Qt::QueuedConnection);
}

bool RadioBackend::setPtt(bool on)
{
    if (!m_commander)
    {
        return false;
    }

    if (on)
    {
        if (!m_audioReady)
        {
            emit statusMessage(QStringLiteral("PTT blocked: waiting for MAIN and SUB audio readiness"),
                               MessageSeverity::Error);
            return false;
        }
        if (m_dualWatchTransition.pending())
        {
            emit statusMessage(QStringLiteral("PTT blocked: waiting for dual-watch transition"),
                               MessageSeverity::Error);
            return false;
        }
        if (m_transmitConfiguration.confirmationPending())
        {
            emit statusMessage(QStringLiteral("PTT blocked: waiting for radio settings confirmation"),
                               MessageSeverity::Error);
            return false;
        }
        if (!m_transmitConfiguration.transmitFrequencyAllowed())
        {
            emit statusMessage(QStringLiteral("PTT blocked: transmit frequency outside band limits"),
                               MessageSeverity::Error);
            return false;
        }

        if (m_pttReleaseDelayTimer)
        {
            m_pttReleaseDelayTimer->stop();
        }
        if (m_pttOffConfirmationTimer)
        {
            m_pttOffConfirmationTimer->stop();
        }
        m_pttOffConfirmationClock.invalidate();
        if (!m_pttState.requestOn())
        {
            return true;
        }

        emit pttRequestAccepted(true);
        armTransmitSafety();
        const auto selectedMemory = m_selectedRadioMemory;
        qInfo(logRadio()).noquote().nospace()
            << "PTT route target=MAIN memory=" << (selectedMemory.has_value() ? "yes" : "no");
        invokeOnCurrentCommander(
            [selectedMemory](Commander* commandSession)
            {
                if (selectedMemory)
                {
                    const auto [group, channel] = *selectedMemory;
                    // The selected memory already established its band. Do
                    // not run selectMemoryBandForCommand() here: that sequence
                    // enters VFO mode and publishes a band-default frequency
                    // before returning to memory mode.
                    selectMemoryForCommand(commandSession, group, channel, false);
                }
                else
                {
                    commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
                }
                commandSession->setPttActive(true);
                commandSession->receiveCommand(funcTransceiverStatus, QVariant::fromValue<bool>(true), 0);
                if (selectedMemory)
                {
                    const auto [group, channel] = *selectedMemory;
                    selectMemoryForCommand(commandSession, group, channel, false);
                    commandSession->receiveCommand(funcSelectedFreq, QVariant(), 0);
                    commandSession->receiveCommand(funcSelectedMode, QVariant(), 0);
                }
            });
        if (m_pttOnConfirmationTimer)
        {
            m_pttOnConfirmationTimer->start();
        }
        return true;
    }
    else
    {
        if (m_pttReleaseDelayTimer && m_pttReleaseDelayTimer->isActive())
        {
            return true;
        }

        if (!m_pttState.confirmedActive() && !m_pttState.desiredActive())
        {
            sendPttOffNow();
            return true;
        }

        // Keep the radio keyed briefly so the final TX audio frames already
        // buffered locally or in the LAN path can reach the transmitter.
        if (m_pttReleaseDelayTimer)
        {
            qDebug(logRadio()).noquote() << "PTT release requested; scheduling unkey tailMs=" << kPttReleaseTailMs;
            m_pttReleaseDelayTimer->start();
        }
        else
        {
            sendPttOffNow();
        }
        return true;
    }
}

void RadioBackend::sendPttOffNow()
{
    if (!m_commander)
    {
        return;
    }

    if (m_pttReleaseDelayTimer)
    {
        m_pttReleaseDelayTimer->stop();
    }
    if (m_pttOnConfirmationTimer)
    {
        m_pttOnConfirmationTimer->stop();
    }
    // Always send an unkey request. If local state ever gets stale, suppressing
    // this command can leave the radio transmitting until disconnect.
    qDebug(logRadio()).noquote() << "Dispatching PTT off command";
    emit pttRequestAccepted(false);
    m_pttState.requestOff();
    if (m_pttOffConfirmationTimer)
    {
        m_pttOffConfirmationClock.restart();
        m_pttOffConfirmationTimer->start(kPttOffConfirmationRetryMs);
    }
    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            commandSession->setPttActive(false);
            // The immediate automatic readback races the radio's physical
            // unkey transition and repeatedly reports stale TX. Send the set
            // without readback; the short confirmation timer above performs
            // the first authoritative query after the radio has settled.
            commandSession->receiveCommandNoReadback(funcTransceiverStatus, QVariant::fromValue<bool>(false), 0);
        });
}

void RadioBackend::armTransmitSafety()
{
    if (!m_pttMaxDurationTimer)
    {
        return;
    }

    if (!m_pttMaxDurationTimer->isActive())
    {
        m_transmitSafetyPolicy.reset();
        m_pttMaxDurationTimer->start();
    }
}

void RadioBackend::disarmTransmitSafety()
{
    if (m_pttMaxDurationTimer)
    {
        m_pttMaxDurationTimer->stop();
    }
    m_transmitSafetyPolicy.reset();
}

void RadioBackend::resetScopeController()
{
    if (!m_scopeController)
    {
        return;
    }

    if (QThread::currentThread() == m_scopeController->thread())
    {
        m_scopeController->reset();
        return;
    }

    QMetaObject::invokeMethod(m_scopeController, &ScopeController::reset, Qt::QueuedConnection);
}

void RadioBackend::forcePttOffForSafety(const QString& message)
{
    if (!m_commander || !m_pttState.safetyActive())
    {
        disarmTransmitSafety();
        return;
    }

    qWarning(logRadio()).noquote() << message;
    emit statusMessage(message, MessageSeverity::Warning);
    sendPttOffNow();
}

void RadioBackend::handleTransmitSwr(double swr)
{
    emit swrChanged(swr);

    if (!m_pttState.safetyActive())
    {
        m_transmitSafetyPolicy.observe(false, swr);
        return;
    }

    if (!m_transmitSafetyPolicy.observe(true, swr))
    {
        return;
    }

    forcePttOffForSafety(QStringLiteral("TX stopped: SWR %1 exceeded safety cutoff").arg(swr, 0, 'f', 2));
}

void RadioBackend::sendDtmf(const QString& digits)
{
    if (!m_commander || digits.isEmpty())
    {
        return;
    }

    const QByteArray pcm = sdr9700::audio::generateDtmfPcm(digits, kTxAudioSampleRate);
    if (pcm.isEmpty())
    {
        return;
    }

    // Queue the full PCM buffer to UdpAudio in a single call. receiveAudioData
    // will substitute it for mic audio frame-by-frame while DTMF is active.
    // Both queueDtmfPcm and receiveAudioData run on udpHandlerThread, so there
    // is no race between the buffer being written and consumed.
    invokeOnCurrentCommander([pcm](Commander* c) { c->sendDtmfPcm(pcm); });
}

void RadioBackend::pollFrequency()
{
    invokeOnCurrentCommander([](Commander* commandSession)
                             { scheduleVfoReceiverReadForCommand(commandSession, Vfo::Main, funcFreqGet); });
}

void RadioBackend::selectVfoMode()
{
    const bool transferSelectedMemory = m_selectedRadioMemory.has_value();
    m_selectedRadioMemory.reset();
    invokeOnCurrentCommander(
        [transferSelectedMemory](Commander* commandSession)
        {
            commandSession->receiveCommand(transferSelectedMemory ? funcMemoryToVFO : funcVFOModeSelect, QVariant(), 0);
        });
}

void RadioBackend::selectRadioMemory(quint16 group, quint16 channel, Vfo targetVfo)
{
    m_selectedRadioMemory = std::make_pair(group, channel);
    const uint memoryAddress = (static_cast<uint>(group) << 16) | static_cast<uint>(channel);
    bool prepareBand = true;
    const quint64 targetFrequencyHz = targetVfo == Vfo::Sub ? m_currentSubFrequencyHz : m_currentMainFrequencyHz;
    if (const sdr9700::RadioBandDef* definition =
            sdr9700::radioBandDefinition(sdr9700::radioBandForFrequency(targetFrequencyHz)))
    {
        prepareBand = definition->memGroup != group;
    }
    routeVfoReceiverCommand(
        targetVfo, funcMemoryMode,
        [group, channel, memoryAddress, prepareBand, targetVfo](Commander* commandSession, uchar receiver)
        {
            // Command 08h acts on the physical receiver context. Route the
            // entire activation through the same scoped transaction as other
            // receiver controls, even when no band preparation is needed.
            // Memory channel numbers are band-scoped on the IC-9700. Only use
            // the intermediate band-routing tune when changing bands; within
            // the current band, selecting command 08h directly avoids an
            // unnecessary frequency transition.
            if (prepareBand)
            {
                selectMemoryBandForCommand(commandSession, group, targetVfo);
            }
            selectMemoryForCommand(commandSession, group, channel, false);
            commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memoryAddress), receiver);
            commandSession->receiveCommand(funcFreqGet, QVariant(), receiver);
            commandSession->receiveCommand(funcModeGet, QVariant(), receiver);
            commandSession->receiveCommand(funcSplitStatus, QVariant(), receiver);
            commandSession->receiveCommand(funcReadFreqOffset, QVariant(), receiver);
            commandSession->receiveCommand(funcToneSquelchType, QVariant(), receiver);
            commandSession->receiveCommand(funcToneFreq, QVariant(), receiver);
            commandSession->receiveCommand(funcTSQLFreq, QVariant(), receiver);
            commandSession->receiveCommand(funcDTCSCode, QVariant(), receiver);
        });
}

void RadioBackend::requestRadioMemory(quint16 group, quint16 channel)
{
    const uint memoryAddress = (static_cast<uint>(group) << 16) | static_cast<uint>(channel);
    invokeOnCurrentCommander(
        [memoryAddress](Commander* commandSession)
        { commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memoryAddress), 0); });
}

void RadioBackend::requestSatelliteMemory(quint16 channel)
{
    invokeOnCurrentCommander(
        [channel](Commander* commandSession)
        { commandSession->receiveCommand(funcSatelliteMemory, QVariant::fromValue(static_cast<uint>(channel)), 0); });
}

void RadioBackend::writeRadioMemory(MemoryType memory)
{
    const uint memoryAddress = (static_cast<uint>(memory.group) << 16) | static_cast<uint>(memory.channel);
    invokeOnCurrentCommander(
        [memory, memoryAddress](Commander* commandSession)
        {
            commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memory), 0);
            QTimer::singleShot(
                kMemoryWriteReadbackDelayMs, commandSession, [commandSession, memoryAddress]()
                { commandSession->receiveCommand(funcMemoryContents, QVariant::fromValue(memoryAddress), 0); });
        });
}

void RadioBackend::requestInitialRadioState()
{
    if (!m_commander)
    {
        return;
    }

    const bool firstRequest = !m_initialStateRequested;
    const auto requestMainVfoState = [this]()
    {
        if (!m_commander)
        {
            return;
        }
        invokeOnCurrentCommander(
            [](Commander* commandSession)
            {
                // Use raw current-VFO 03/04 reads for connection readiness.
                // In LAN startup captures the IC-9700 replied to scope/status
                // traffic but did not answer selected-VFO 25/26 probes before
                // the watchdog expired, causing an unnecessary reconnect loop.
                commandSession->readCurrentFrequencyAndMode();
            });
    };

    if (firstRequest)
    {
        m_initialStateRequested = true;
        requestMainVfoState();
    }

    if (m_initialMainFrequencyReceived && m_initialMainModeReceived)
    {
        return;
    }

    if (firstRequest)
    {
        // The first request already queued the critical VFO probe before the
        // full status snapshot above. Retries use requestMainVfoState() below.
        return;
    }

    qDebug(logRadio()).noquote().nospace()
        << "Retrying initial MAIN VFO state frequencyReceived=" << m_initialMainFrequencyReceived
        << " modeReceived=" << m_initialMainModeReceived;
    requestMainVfoState();
}

void RadioBackend::requestPostReadyRadioState()
{
    if (!m_commander)
    {
        return;
    }

    const ushort lanModLevel = static_cast<ushort>(m_lanModLevel);
    invokeOnCurrentCommander(
        [lanModLevel](Commander* commandSession)
        {
            const radioInput lanInput(inputLAN, 5, QStringLiteral("LAN"));
            commandSession->receiveCommand(funcDATAOffMod, QVariant(), 0);
            commandSession->receiveCommand(funcDATA1Mod, QVariant(), 0);
            commandSession->receiveCommandNoReadback(funcDATAOffMod, QVariant::fromValue(lanInput), 0);
            commandSession->receiveCommandNoReadback(funcDATA1Mod, QVariant::fromValue(lanInput), 0);

            commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(lanModLevel), 0);
            commandSession->receiveCommand(funcSatelliteMode, QVariant::fromValue<bool>(false), 0);
            // Keep both operating sides active for the independent MAIN and SUB
            // VFO controllers. MAIN remains selected for the existing control,
            // memory, and Spectrum Scope paths after the targeted SUB readback.
            commandSession->receiveCommand(funcVFODualWatch, QVariant::fromValue<bool>(true), 0);
            requestSubVfoStateForCommand(commandSession);
            commandSession->receiveCommand(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcTimeOutTimer, QVariant::fromValue<uchar>(kHardwareTxTimeoutTimer), 0);
            qInfo(logRadio()).noquote()
                << "Configured IC-9700 transmit modulation source for LAN audio and MAIN/SUB dual-watch operation";
            qInfo(logRadio()).noquote() << "Setting hardware TX timeout timer to 3 minutes";

            const Funcs statusCommands[] = {
                funcTransceiverStatus,
                funcRfGain,
                funcRFPower,
                funcSquelch,
                funcNoiseReduction,
                funcNoiseBlanker,
                funcPreamp,
                funcAttenuator,
                funcAutoNotch,
                funcManualNotch,
                funcSplitStatus,
                funcReadFreqOffset,
                funcToneSquelchType,
                funcRepeaterTone,
                funcRepeaterTSQL,
                funcRepeaterDTCS,
                funcRepeaterCSQL,
                funcToneFreq,
                funcTSQLFreq,
                funcDTCSCode,
                funcCompressor,
                funcCompressorLevel,
                funcXFCStatus,
                funcAGCTimeConstant,
                funcTuningStep,
                // RIT remains polled so radio-authoritative state is retained,
                // but its UI is intentionally deferred during the redesign.
                funcRitStatus,
                funcRitFreq,
                funcMonitor,
                funcVox,
                funcIPPlus,
                funcDialLock,
            };

            for (const Funcs command : statusCommands)
            {
                commandSession->scheduleStartupRead(command, 0);
            }
        });
}

void RadioBackend::updateReadyState()
{
    const bool mainControlReady = m_initialMainFrequencyReceived && m_initialMainModeReceived;
    if (mainControlReady && !m_postReadyStateRequested)
    {
        m_postReadyStateRequested = true;
        if (m_initialStateRetryTimer)
        {
            m_initialStateRetryTimer->stop();
        }
        const quint64 session = m_sessionId.load(std::memory_order_relaxed);
        Commander* commandSession = m_commander;
        // Keep the broad post-MAIN snapshot behind the existing stability
        // delay, but do not publish readiness until its targeted SUB frequency
        // and mode reads have completed. MemoryController starts its initial
        // sweep only from that full-ready signal, so memory traffic cannot
        // contend with either receiver's bootstrap.
        QTimer::singleShot(3000, this,
                           [this, session, commandSession]()
                           {
                               if (isCurrentSession(session, commandSession) && m_initialMainFrequencyReceived &&
                                   m_initialMainModeReceived)
                               {
                                   requestPostReadyRadioState();
                               }
                           });
    }

    // Spectrum data and memory synchronization are background activities. The
    // radio becomes operable only after both receiver identities are known.
    const bool ready = sdr9700::backend::receiverStateReady(m_initialMainFrequencyReceived, m_initialMainModeReceived,
                                                            m_initialSubFrequencyReceived, m_initialSubModeReceived);
    if (m_radioReady == ready)
    {
        return;
    }

    m_radioReady = ready;
    qInfo(logRadio()).noquote().nospace()
        << "Radio backend readiness changed ready=" << ready
        << " mainFrequencyReceived=" << m_initialMainFrequencyReceived
        << " mainModeReceived=" << m_initialMainModeReceived
        << " subFrequencyReceived=" << m_initialSubFrequencyReceived << " subModeReceived=" << m_initialSubModeReceived
        << " scopeReceived=" << m_scopeDataReceived << " scopeDegraded=" << m_scopeSyncDegraded;
    emit readyChanged(ready);
    if (ready)
    {
        m_syncReconnectAttempts = 0;
        m_syncReconnectPending = false;
        if (!m_scopeDataReceived)
        {
            setScopeSyncDegraded(true);
        }
        if (m_syncWatchdogTimer)
        {
            m_syncWatchdogTimer->stop();
        }
        if (m_initialStateRetryTimer)
        {
            m_initialStateRetryTimer->stop();
        }
        m_vfoStatePollPhase = 0;
        if (m_vfoStatePollTimer)
        {
            m_vfoStatePollTimer->start();
        }
        emit connectionStageChanged(ConnectionStage::Ready, {});
        updateAudioReadyState();
    }
}

void RadioBackend::updateAudioReadyState()
{
    if (m_audioReady || !m_commander ||
        !sdr9700::backend::receiverStateReady(m_initialMainFrequencyReceived, m_initialMainModeReceived,
                                              m_initialSubFrequencyReceived, m_initialSubModeReceived))
    {
        return;
    }

    m_audioReady = true;
    qInfo(logRadio()).noquote().nospace()
        << "Audio readiness confirmed mainFrequencyReceived=" << m_initialMainFrequencyReceived
        << " mainModeReceived=" << m_initialMainModeReceived
        << " subFrequencyReceived=" << m_initialSubFrequencyReceived << " subModeReceived=" << m_initialSubModeReceived;
    invokeOnCurrentCommander([](Commander* commandSession) { commandSession->enableAudio(); });
}

void RadioBackend::setScopeSyncDegraded(bool degraded)
{
    if (m_scopeSyncDegraded == degraded)
    {
        return;
    }

    m_scopeSyncDegraded = degraded;
    emit scopeSyncDegradedChanged(degraded);
}

void RadioBackend::restartAfterSyncTimeout()
{
    if (!m_commander || m_radioReady)
    {
        return;
    }
    if (sdr9700::backend::receiverStateReady(m_initialMainFrequencyReceived, m_initialMainModeReceived,
                                             m_initialSubFrequencyReceived, m_initialSubModeReceived) &&
        !m_scopeDataReceived)
    {
        // The normal readiness path no longer waits on Spectrum Scope data, so
        // reaching this branch means a queued readiness update was delayed. Keep
        // it as a defensive fallback instead of reconnecting a usable radio.
        setScopeSyncDegraded(true);
        qWarning(logRadio()).noquote() << "Radio control sync completed but Spectrum Scope data did not arrive within"
                                       << kSyncWatchdogTimeoutMs << "ms; enabling controls while scope retry continues";
        updateReadyState();
        return;
    }
    if (m_connectionHost.isEmpty() || m_connectionPort == 0)
    {
        qWarning(logRadio()).noquote() << "Radio sync timed out, but no saved connection target is available";
        return;
    }

    const QString host = m_connectionHost;
    const quint16 port = m_connectionPort;
    const QString user = m_connectionUser;
    const QString pass = m_connectionPass;

    qWarning(logRadio()).noquote().nospace()
        << "Radio sync did not complete timeoutMs=" << kSyncWatchdogTimeoutMs
        << " scopeReceived=" << m_scopeDataReceived << " mainFrequencyReceived=" << m_initialMainFrequencyReceived
        << " mainModeReceived=" << m_initialMainModeReceived
        << " subFrequencyReceived=" << m_initialSubFrequencyReceived << " subModeReceived=" << m_initialSubModeReceived;
    if (m_syncReconnectAttempts >= kMaxSyncReconnectAttempts)
    {
        // A repeated failure here means LAN control authenticated but the CI-V
        // stream never produced frequency/mode readback. Reconnecting forever
        // hammers the IC-9700 LAN server and leaves the GUI stuck in Syncing,
        // so stop cleanly and let the operator choose when to retry or reboot.
        qWarning(logRadio()).noquote() << "Radio sync retry limit reached; stopping automatic reconnect loop";
        const QString message = QStringLiteral("Radio sync timed out; reconnect or restart the radio");
        shutdownConnection(true, false);
        emit connectionStageChanged(ConnectionStage::Failed, message);
        return;
    }

    ++m_syncReconnectAttempts;
    // Use the normal close path so the IC-9700 receives stream close/token
    // cleanup before the reconnect attempt. Publish Reconnecting after cleanup
    // so an internal Disconnected transition cannot overwrite the useful state.
    shutdownConnection(true, false);
    emit connectionStageChanged(ConnectionStage::Reconnecting, QStringLiteral("Radio sync timed out; reconnecting"));

    m_syncReconnectPending = true;
    QTimer::singleShot(kSyncReconnectDelayMs, this,
                       [this, host, port, user, pass]()
                       {
                           if (m_connectionHost != host || m_connectionPort != port)
                           {
                               m_syncReconnectPending = false;
                               return;
                           }
                           connectToRadio(host, port, user, pass);
                       });
}

bool RadioBackend::isCurrentSession(quint64 session, const Commander* commandSession) const
{
    Q_UNUSED(commandSession);
    // Session ID alone identifies the connection epoch. m_commander is not
    // read here because it is only safe to access on the main thread.
    return session == m_sessionId.load(std::memory_order_acquire);
}

void RadioBackend::invokeOnCurrentCommander(const std::function<void(Commander*)>& command)
{
    if (!m_commander)
    {
        return;
    }

    Commander* commandSession = m_commander;
    const auto sessionActive = m_sessionActive;
    if (!sessionActive)
    {
        return;
    }
    QMetaObject::invokeMethod(
        commandSession,
        [sessionActive, commandSession, command]()
        {
            if (!sessionActive->load(std::memory_order_acquire))
            {
                return;
            }
            command(commandSession);
        },
        Qt::QueuedConnection);
}

void RadioBackend::handleReportedFrequency(quint64 hz)
{
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    const int bandKey = band != bandUnknown ? static_cast<int>(band) : -1;
    if (bandKey == m_currentBandKey)
    {
        return;
    }

    const bool hadKnownBand = m_currentBandKey != -1;
    m_currentBandKey = bandKey;

    // MAIN/SUB exchange already performs its own ordered MAIN and SUB
    // identity refresh. Its MAIN frequency necessarily crosses bands; do not
    // turn that expected result into a second, full startup-style state poll.
    if (m_mainSubExchangePending)
    {
        return;
    }

    if (!hadKnownBand || bandKey == -1 || !m_radioReady || !m_bandStateRefreshTimer)
    {
        return;
    }

    qInfo(logRadio()).noquote() << "Detected IC-9700 band change; scheduling radio state refresh";
    m_bandStateRefreshTimer->start();
}

void RadioBackend::sendLanModLevel(int level)
{
    if (!m_commander)
    {
        return;
    }

    const ushort val = static_cast<ushort>(qBound(0, level, 255));

    if (QThread::currentThread() == m_commander->thread())
    {
        m_commander->receiveCommand(funcLANModLevel, QVariant::fromValue(val), 0);
        return;
    }

    invokeOnCurrentCommander([val](Commander* commandSession)
                             { commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(val), 0); });
}

void RadioBackend::setLanModLevel(int level)
{
    m_lanModLevel = qBound(0, level, 255);
    sendLanModLevel(m_lanModLevel);
}

void RadioBackend::onLanReady()
{
    const quint64 session = m_sessionId.load(std::memory_order_relaxed);
    Commander* commandSession = m_commander;
    if (!isCurrentSession(session, commandSession))
    {
        return;
    }

    // UdpHandler emits lanReady only after the directed Transceiver ID reply,
    // so reaching this point proves the command plane is awake and usable.
    m_standbyWakePolicy.commandPlaneReady();

    m_radioReady = false;
    m_scopeDataReceived = false;
    setScopeSyncDegraded(false);
    resetScopeController();
    m_initialFrequencyReceived = false;
    m_initialModeReceived = false;
    m_initialMainFrequencyReceived = false;
    m_initialMainModeReceived = false;
    m_initialSubFrequencyReceived = false;
    m_initialSubModeReceived = false;
    m_audioReady = false;
    m_initialStateRequested = false;
    m_postReadyStateRequested = false;
    m_txMeterPollTick = 0;
    m_smeterPollTick = 0;
    emit readyChanged(false);

    invokeOnCurrentCommander(
        [](Commander* commandSession)
        {
            commandSession->setRadioID(0xA2);

            // Scope output is persistent on the IC-9700. If the previous
            // session died while scope streaming was enabled, the radio can
            // flood the fresh CI-V socket before basic 03/04 VFO readback
            // completes. Quiet scope data first; onLanReady() enables it again
            // only after frequency and mode have been confirmed.
            //
            // CI-V has no non-mutating query for whether the radio is in VFO
            // or memory mode, nor for the selected memory channel. Establish a
            // deterministic startup state by explicitly selecting MAIN VFO
            // mode before reading its frequency and mode.
            commandSession->receiveCommandNoReadback(funcScopeOnOff, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommandNoReadback(funcScopeDataOutput, QVariant::fromValue<bool>(false), 0);
            commandSession->receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
            commandSession->receiveCommand(funcVFOModeSelect, QVariant(), 0);
            commandSession->readCurrentFrequencyAndMode();
        });

    if (m_initialStateRetryTimer)
    {
        m_initialStateRetryTimer->stop();
        m_initialStateRetryTimer->deleteLater();
    }
    m_initialStateRetryTimer = new QTimer(this);
    m_initialStateRetryTimer->setInterval(1000);
    connect(m_initialStateRetryTimer, &QTimer::timeout, this,
            [this, session, commandSession]()
            {
                if (!isCurrentSession(session, commandSession) || m_radioReady)
                {
                    if (m_initialStateRetryTimer)
                    {
                        m_initialStateRetryTimer->stop();
                    }
                    return;
                }
                requestInitialRadioState();
            });

    QTimer::singleShot(250, this,
                       [this, session, commandSession]()
                       {
                           if (!isCurrentSession(session, commandSession))
                           {
                               return;
                           }
                           requestInitialRadioState();
                           if (m_initialStateRetryTimer && !m_radioReady)
                           {
                               m_initialStateRetryTimer->start();
                           }
                       });

    emit connected();
    emit connectionStageChanged(ConnectionStage::SyncingRadioState, QStringLiteral("Synchronizing radio"));
    if (m_syncWatchdogTimer)
    {
        m_syncWatchdogTimer->start();
    }

    // Retry scope-enable commands until CI-V is established and scope data
    // actually starts flowing. The CI-V stream opens asynchronously after the
    // control handshake, so a fixed one-shot delay can expire before the radio
    // is ready to process the command. ScopeController sets
    // m_scopeDataReceived on the first complete frame, which stops this retry
    // path and prevents needless configuration traffic afterward.
    if (m_scopeRetryTimer)
    {
        m_scopeRetryTimer->stop();
        m_scopeRetryTimer->deleteLater();
    }
    m_scopeRetryTimer = new QTimer(this);
    m_scopeRetryTimer->setInterval(1000);

    auto sendScopeEnable = [this, session, commandSession]()
    {
        if (!isCurrentSession(session, commandSession))
        {
            return;
        }
        if (!m_initialMainFrequencyReceived || !m_initialMainModeReceived)
        {
            return;
        }
        const ushort lanModLevel = static_cast<ushort>(m_lanModLevel);
        m_scopeEnableRequested = true;
        invokeOnCurrentCommander(
            [lanModLevel](Commander* commandSession)
            {
                commandSession->receiveCommand(funcScopeOnOff, QVariant::fromValue<bool>(true), 0);
                commandSession->receiveCommand(funcScopeDataOutput, QVariant::fromValue<bool>(true), 0);
                commandSession->receiveCommand(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
                centerSpanData span;
                span.freq = 500000;
                span.name = QStringLiteral("500 kHz");
                commandSession->receiveCommand(funcScopeSpan, QVariant::fromValue<centerSpanData>(span), 0);
                commandSession->receiveCommand(funcLANModLevel, QVariant::fromValue(lanModLevel), 0);
            });
    };

    connect(m_scopeRetryTimer, &QTimer::timeout, this,
            [this, session, commandSession, sendScopeEnable]()
            {
                if (!isCurrentSession(session, commandSession) || m_scopeDataReceived)
                {
                    m_scopeRetryTimer->stop();
                    return;
                }
                sendScopeEnable();
            });

    // First attempt shortly after the LAN CI-V stream opens, then retry until
    // scope data arrives. The send helper intentionally waits for current VFO
    // frequency/mode first so readiness cannot be starved by scope streaming.
    QTimer::singleShot(250, this,
                       [this, session, commandSession, sendScopeEnable]()
                       {
                           if (!isCurrentSession(session, commandSession))
                           {
                               return;
                           }
                           if (m_scopeRetryTimer)
                           {
                               m_scopeRetryTimer->start();
                           }
                           if (m_scopeDataReceived)
                           {
                               return;
                           }
                           sendScopeEnable();
                       });

    // Poll front-panel meters at 10 Hz; the IC-9700 only sends meter data on request.
    if (m_smeterPollTimer)
    {
        m_smeterPollTimer->stop();
        m_smeterPollTimer->deleteLater();
    }
    m_smeterPollTimer = new QTimer(this);
    m_smeterPollTimer->setInterval(100);
    m_smeterPollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_smeterPollTimer, &QTimer::timeout, this,
            [this, session, commandSession]()
            {
                if (!isCurrentSession(session, commandSession) || !m_radioReady)
                {
                    return;
                }
                if (m_pttState.transmitMetersActive())
                {
                    const int pollTick = m_txMeterPollTick++;
                    invokeOnCurrentCommander(
                        [pollTick](Commander* commandSession)
                        {
                            commandSession->scheduleMeterRead(funcSWRMeter, kMainReceiver);
                            commandSession->scheduleMeterRead(funcPowerMeter, kMainReceiver);
                            commandSession->scheduleMeterRead(funcALCMeter, kMainReceiver);
                            if (pollTick % 2 == 0)
                            {
                                commandSession->scheduleMeterRead(funcCompMeter, kMainReceiver);
                            }
                            if (pollTick % 5 == 0)
                            {
                                commandSession->scheduleMeterRead(funcVdMeter, kMainReceiver);
                                commandSession->scheduleMeterRead(funcIdMeter, kMainReceiver);
                            }
                        });
                    return;
                }

                m_txMeterPollTick = 0;
                if (m_smeterPollPending)
                {
                    const qint64 elapsedMs = m_smeterPollPendingClock.isValid() ? m_smeterPollPendingClock.elapsed()
                                                                                : m_smeterPollDeadlineMs;
                    if (!sdr9700::backend::meterPollDeadlineExpired(m_smeterPollPendingClock.isValid(), elapsedMs,
                                                                    m_smeterPollDeadlineMs))
                    {
                        return;
                    }

                    const uchar timedOutReceiver = m_smeterPollPendingReceiver;
                    invokeOnCurrentCommander([timedOutReceiver](Commander* commandSession)
                                             { commandSession->abortSMeterRead(timedOutReceiver); });
                    m_smeterPollPending = false;
                    m_smeterPollPendingClock.invalidate();
                    ++m_meterDiagnostics.pollTimeouts;
                    qWarning(logRadio()).noquote().nospace()
                        << "S-meter poll timed out receiver=" << m_smeterPollPendingReceiver
                        << " elapsedMs=" << elapsedMs << " deadlineMs=" << m_smeterPollDeadlineMs
                        << " timeoutCount=" << m_meterDiagnostics.pollTimeouts;
                    m_smeterPollDeadlineMs = 0;
                    return;
                }
                if (m_smeterPollQueued)
                {
                    static constexpr qint64 kQueuedSMeterDeadlineMs = 3000;
                    const qint64 queuedElapsedMs =
                        m_smeterPollQueuedClock.isValid() ? m_smeterPollQueuedClock.elapsed() : kQueuedSMeterDeadlineMs;
                    if (sdr9700::backend::meterPollDeadlineExpired(m_smeterPollQueuedClock.isValid(), queuedElapsedMs,
                                                                   kQueuedSMeterDeadlineMs))
                    {
                        m_smeterPollQueued = false;
                        m_smeterPollQueuedClock.invalidate();
                        ++m_meterDiagnostics.pollTimeouts;
                        qWarning(logRadio()).noquote().nospace()
                            << "S-meter poll dispatch timed out receiver=" << m_smeterPollPendingReceiver
                            << " elapsedMs=" << queuedElapsedMs << " deadlineMs=" << kQueuedSMeterDeadlineMs
                            << " timeoutCount=" << m_meterDiagnostics.pollTimeouts;
                    }
                    return;
                }
                if (m_dualWatchTransition.pending())
                {
                    return;
                }

                static constexpr qint64 kPostTuneMeterHoldoffMs = 250;
                const bool tuningHoldoffActive =
                    m_meterPollTuneHoldoff.isValid() && m_meterPollTuneHoldoff.elapsed() < kPostTuneMeterHoldoffMs;
                if (!sdr9700::backend::receiverMeterPollAllowed(m_radioReady, m_pttState.safetyActive(),
                                                                m_mainSubExchangePending, tuningHoldoffActive))
                {
                    return;
                }

                const Vfo activeVfo = m_activeVfo;
                const Vfo targetVfo =
                    sdr9700::backend::meterPollTarget(activeVfo, m_dualWatchEnabled, m_smeterPollTick++);
                const uchar receiver = sdr9700::backend::receiverForVfo(targetVfo);
                m_smeterPollQueued = true;
                m_smeterPollQueuedClock.start();
                m_smeterPollPendingReceiver = receiver;
                m_smeterPollDeadlineMs = 0;
                // CI-V 15 02 has no receiver byte. Sample the inactive side in
                // the same serialized receiver-scoped path as every other
                // receiver-less transaction. This is required even when the
                // target matches the logical UI selection because a background
                // transaction may have temporarily selected the other receiver.
                // A direct select/read/restore burst here could steal context
                // between another transaction's select and write.
                invokeOnCurrentCommander([receiver](Commander* commandSession)
                                         { commandSession->scheduleSMeterRead(receiver); });
            });
    m_smeterPollTimer->start();
}

void RadioBackend::onPortError(errorType err)
{
    if (err.code == ErrorCode::CommandPlaneUnavailable)
    {
        handleCommandPlaneUnavailable();
        return;
    }

    const QString message = sdr9700::connectionErrorMessage(err);

    // A different transport failure supersedes any delayed standby-wake
    // callback. Without advancing the generation, that callback could reopen
    // the just-failed target after this handler deliberately cleared it.
    ++m_bootstrapGeneration;
    m_bootstrapReconnectPending = false;

    // Tear down the failed transport without emitting a generic disconnect
    // status message. The typed error below is the authoritative explanation and also
    // drives MainWindow's reconnect policy.
    shutdownConnection(false, false, m_standbyWakePolicy.complete());
    m_connectionHost.clear();
    m_connectionPort = 0;
    m_connectionUser.clear();
    m_connectionPass.clear();
    emit connectionStageChanged(
        err.code == ErrorCode::Disconnected ? ConnectionStage::Disconnected : ConnectionStage::Failed, message);
    emit errorOccurred(err.code, message);
}

void RadioBackend::handleCommandPlaneUnavailable()
{
    // This signal is intentionally narrower than "disconnected": LAN login,
    // token negotiation, and both media handshakes succeeded, but the radio did
    // not answer our directed CI-V identity query. A crashed predecessor and a
    // radio in standby look identical at this boundary, so exhaust one clean
    // session replacement before sending the state-changing power-on command.
    switch (m_standbyWakePolicy.commandPlaneUnavailable())
    {
    case sdr9700::StandbyWakePolicy::Action::Continue:
        return;
    case sdr9700::StandbyWakePolicy::Action::RetrySession:
        qInfo(logRadio()).noquote()
            << "IC-9700 command plane was silent; retrying once with a fresh LAN session before standby wake";
        // A single clean-session retry distinguishes a crashed predecessor
        // from standby, but it requires no operator action and normally lasts
        // only a few seconds. Keep it in the diagnostic log instead of
        // replacing the persistent Connecting status message with transient chatter.
        reconnectBootstrapSession();
        return;
    case sdr9700::StandbyWakePolicy::Action::Wake:
    {
        const int wakeAttempt = m_standbyWakePolicy.wakeAttempts();
        qInfo(logRadio()).noquote().nospace() << "Sending IC-9700 standby wake attempt=" << wakeAttempt;
        emit connectionStageChanged(ConnectionStage::WaitingForRadio, QStringLiteral("Waking radio from standby mode"));
        invokeOnCurrentCommander([](Commander* commandSession) { commandSession->sendStandbyWake(); });

        // The radio needs time to boot its CI-V command plane. Keep the current
        // media pipe alive long enough to deliver the wake frame, then replace
        // the provisional session without sending ordinary radio-state safety
        // commands into a command plane that has not answered us.
        const quint64 generation = m_bootstrapGeneration;
        // An operator disconnect or a new connection request increments the
        // generation. That makes this delayed callback harmless if intent
        // changes while the IC-9700 is waking.
        QTimer::singleShot(kStandbyWakeDelayMs, this,
                           [this, generation]()
                           {
                               if (generation != m_bootstrapGeneration)
                               {
                                   return;
                               }
                               reconnectBootstrapSession();
                           });
        return;
    }
    case sdr9700::StandbyWakePolicy::Action::Fail:
    {
        const QString message = QStringLiteral("Unable to connect to radio");
        qWarning(logRadio()).noquote() << "IC-9700 command plane remained unavailable after bounded standby wake";
        shutdownConnection(true, false, false);
        emit connectionStageChanged(ConnectionStage::Failed, message);
        // Keep this typed as command-plane exhaustion. MainWindow retries
        // ordinary connection failures, which would reset this policy and turn
        // the deliberately bounded wake sequence into an infinite loop.
        emit errorOccurred(ErrorCode::CommandPlaneUnavailable, message);
        return;
    }
    }
}

void RadioBackend::reconnectBootstrapSession()
{
    if (m_connectionHost.isEmpty() || m_connectionPort == 0)
    {
        return;
    }

    const QString host = m_connectionHost;
    const quint16 port = m_connectionPort;
    const QString user = m_connectionUser;
    const QString pass = m_connectionPass;
    const quint64 generation = m_bootstrapGeneration;

    // This provisional session never proved CI-V readiness. Close only the LAN
    // resources it actually owns; PTT/data-mode safety writes would be both
    // ineffective and misleading while the command plane is silent.
    shutdownConnection(false, false, false);
    m_bootstrapReconnectPending = true;
    // Preserve the policy epoch across this internal connection replacement.
    // connectToRadio() consumes m_bootstrapReconnectPending rather than
    // resetting the attempt counters as it would for a new operator request.
    QTimer::singleShot(0, this,
                       [this, generation, host, port, user, pass]()
                       {
                           if (generation != m_bootstrapGeneration || !m_bootstrapReconnectPending)
                           {
                               return;
                           }
                           connectToRadio(host, port, user, pass);
                       });
}

void RadioBackend::onNetworkStatus(networkStatus status)
{
    const int ms = static_cast<int>(status.networkLatency);
    if (ms > 0)
    {
        emit networkQualityChanged(ms);
    }
    if (status.userVisibleMessage)
    {
        const QString message = status.message.trimmed();
        if (!message.isEmpty() && message != m_lastUserVisibleNetworkMessage)
        {
            m_lastUserVisibleNetworkMessage = message;
            if (status.connectionStage != ConnectionStage::Unchanged)
            {
                emit connectionStageChanged(status.connectionStage, message);
            }
            else
            {
                emit statusMessage(message, status.messageSeverity);
            }
        }
    }
}

void RadioBackend::onHaveAudioData(const audioPacket& pkt)
{
    // IC-9700 stereo LPCM16 carries MAIN on channel 0 and SUB on channel 1.
    // Keep the operator-selected receiver with the block so consumers do not
    // combine two independent radio signals.
    const int receiverChannel = m_activeVfo == Vfo::Sub ? 1 : 0;
    emit audioDataReady(pkt.data, static_cast<int>(m_rxSampleRate), m_rxChannelCount, receiverChannel);
}
