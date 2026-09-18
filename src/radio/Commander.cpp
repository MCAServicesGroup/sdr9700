#include "Commander.h"
#include <QDebug>
#include <QMetaType>
#include <QSemaphore>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <utility>

#include "RadioCapabilities.h"
#include "RadioIdentities.h"
#include "ShutdownTiming.h"
#include "LogCategories.h"

// Parses IC-9700 CI-V frames and forms outbound CI-V commands.
// Encoding follows the local IC-9700 reference material in resources/.

namespace
{
constexpr qsizetype kMaxDeferredReplyReads = 64;
// Startup status reads are already bounded and coalesced, so a 10 ms cadence
// keeps visible controls responsive without restoring the former unpaced burst.
constexpr int kScheduledCommandIntervalMs = 10;
constexpr int kSchedulerDiagnosticsIntervalMs = 30000;
constexpr qsizetype kMaxScheduledCommands = 64;
constexpr int kMaxConsecutiveMeterDispatches = 3;
constexpr int kMaxConsecutiveInteractiveDispatches = 3;
constexpr qint64 kMeterQueueBudgetMs = 100;
constexpr qint64 kBackgroundStarvationCeilingMs = 1500;
constexpr qint64 kScopeAssemblyLifetimeMs = 250;

QString logToken(QString value)
{
    value.removeIf([](QChar character) { return character.isSpace(); });
    return value;
}

Funcs canonicalReplyFunc(Funcs func)
{
    switch (func)
    {
    case funcFreq:
    case funcFreqTR:
    case funcFreqGet:
    case funcSelectedFreq:
        return funcFreqGet;
    case funcMode:
    case funcModeTR:
    case funcModeGet:
    case funcSelectedMode:
        return funcModeGet;
    default:
        return func;
    }
}

bool repliesMatch(Funcs pending, Funcs incoming)
{
    return canonicalReplyFunc(pending) == canonicalReplyFunc(incoming);
}

bool replyDoesNotRequireReceiverCorrelation(Funcs func)
{
    switch (func)
    {
    case funcVFOBandMS:
    case funcScopeMainSub:
        // These are global MAIN/SUB state values, not receiver-scoped reads.
        return true;
    case funcScopeWaveData:
        // Scope payloads carry their receiver identity in the decoded frame.
        return true;
    default:
        return false;
    }
}

bool commandExpectsCorrelatedReply(Funcs func)
{
    switch (func)
    {
    case funcVFOASelect:
    case funcVFOBSelect:
    case funcVFOSwapMS:
    case funcVFOMainSelect:
    case funcVFOSubSelect:
        // The IC-9700 does not provide a useful value reply for VFO-select or
        // MAIN/SUB swap actions. These commands must not occupy the
        // receiver-less reply gate until an unrelated frame expires them, and
        // repeated swaps must never be coalesced into one delayed action.
        return false;
    default:
        return true;
    }
}
} // namespace

Commander::Commander(RadioCommander* parent) : RadioCommander(parent)
{
    m_pendingCommandClock.start();
    m_scheduledCommandTimer = new QTimer(this);
    m_scheduledCommandTimer->setSingleShot(true);
    m_scheduledCommandTimer->setInterval(kScheduledCommandIntervalMs);
    connect(m_scheduledCommandTimer, &QTimer::timeout, this, &Commander::dispatchNextScheduledCommand);
    m_schedulerDiagnosticsTimer = new QTimer(this);
    m_schedulerDiagnosticsTimer->setInterval(kSchedulerDiagnosticsIntervalMs);
    connect(m_schedulerDiagnosticsTimer, &QTimer::timeout, this, &Commander::logSchedulerDiagnostics);
    m_replyDrainTimer = new QTimer(this);
    m_replyDrainTimer->setSingleShot(true);
    connect(m_replyDrainTimer, &QTimer::timeout, this, &Commander::dispatchDeferredReplyReads);
    qInfo(logRadio()).noquote() << "creating instance of Commander()";
}

Commander::Commander(quint8 guid[GUIDLEN], RadioCommander* parent) : RadioCommander(parent)
{
    Q_ASSERT(guid != nullptr);
    qInfo(logRadio()).noquote() << "creating instance of Commander() with GUID";
    memcpy(this->guid, guid, GUIDLEN);
    m_pendingCommandClock.start();
    m_scheduledCommandTimer = new QTimer(this);
    m_scheduledCommandTimer->setSingleShot(true);
    m_scheduledCommandTimer->setInterval(kScheduledCommandIntervalMs);
    connect(m_scheduledCommandTimer, &QTimer::timeout, this, &Commander::dispatchNextScheduledCommand);
    m_schedulerDiagnosticsTimer = new QTimer(this);
    m_schedulerDiagnosticsTimer->setInterval(kSchedulerDiagnosticsIntervalMs);
    connect(m_schedulerDiagnosticsTimer, &QTimer::timeout, this, &Commander::logSchedulerDiagnostics);
    m_replyDrainTimer = new QTimer(this);
    m_replyDrainTimer->setSingleShot(true);
    connect(m_replyDrainTimer, &QTimer::timeout, this, &Commander::dispatchDeferredReplyReads);
}

Commander::~Commander()
{
    qInfo(logRadio()).noquote() << "[SHUTDOWN] ~Commander enter";

    emit requestRadioSelection(QList<radio_cap_packet>());

    shutdownComm();

    qDebug(logRadio()).noquote() << "[SHUTDOWN] ~Commander complete";
}

void Commander::commSetup(quint16 radioCivAddr, UdpConnectionSettings settings, audioSetup rxSetup, audioSetup txSetup)
{
    this->settings = settings;
    civAddr = radioCivAddr;

    if (udp != nullptr)
    {
        closeComm();
    }
    m_shutdownComplete = false;

    udp = new UdpHandler(settings, rxSetup, txSetup);

    udpHandlerThread = new QThread(this);
    udpHandlerThread->setObjectName("UdpHandler()");

    udp->moveToThread(udpHandlerThread);

    connect(this, &Commander::initUdpHandler, udp, &UdpHandler::init);
    connect(udpHandlerThread, &QThread::finished, udp, &QObject::deleteLater);
    udpHandlerThread->start();

    emit initUdpHandler();

    connect(udp, &UdpHandler::haveDataFromPort, this, &Commander::handleNewData);
    connect(this, &Commander::dataForComm, udp, &UdpHandler::receiveDataFromUserToRadio);
    connect(udp, &UdpHandler::haveAudioData, this, &Commander::receiveAudioData);

    connect(this, &Commander::haveChangeLatency, udp, &UdpHandler::changeLatency);
    connect(this, &Commander::haveSetVolume, udp, &UdpHandler::setVolume);
    connect(udp, &UdpHandler::haveNetworkError, this, &Commander::handlePortError);
    connect(udp, &UdpHandler::haveNetworkStatus, this, &Commander::handleStatusUpdate);
    connect(udp, &UdpHandler::haveNetworkStatus, this,
            [this](const networkStatus& status) { m_rttEstimator.observe(status.networkLatency); });
    connect(udp, &UdpHandler::sessionHeartbeat, this, &RadioCommander::haveSessionHeartbeat);
    connect(udp, &UdpHandler::haveNetworkAudioLevels, this, &Commander::handleNetworkAudioLevels);
    connect(udp, &UdpHandler::requestRadioSelection, this, &Commander::radioSelection);
    connect(udp, &UdpHandler::setRadioUsage, this, &Commander::radioUsage);
    connect(this, &Commander::selectedRadio, udp, &UdpHandler::setCurrentRadio);
    connect(this, &Commander::requestEnableAudio, udp, &UdpHandler::enableAudio);
    connect(this, &Commander::standbyWakeHoldStarted, udp, &UdpHandler::beginStandbyWakeHold);
    connect(udp, &UdpHandler::streamReady, this, &Commander::lanReady);

    commonSetup();
}

void Commander::closeComm()
{
    shutdownComm();
}

void Commander::shutdownComm()
{
    if (m_shutdownComplete)
    {
        return;
    }

    qDebug(logRadio()).noquote() << "[SHUTDOWN] closeComm() enter";
    if (udpHandlerThread != nullptr)
    {
        if (udp)
        {
            qDebug(logRadio()).noquote() << "[SHUTDOWN] closeComm() calling udp->shutdown()";
            auto shutdownDone = std::make_shared<QSemaphore>();
            UdpHandler* udpSession = udp;
            const bool invoked = QMetaObject::invokeMethod(
                udp,
                [udpSession, shutdownDone]()
                {
                    udpSession->shutdown();
                    shutdownDone->release();
                },
                Qt::QueuedConnection);
            if (!invoked || !shutdownDone->tryAcquire(1, sdr9700::shutdownTiming::kUdpHandlerShutdownTimeoutMs))
            {
                qWarning(logRadio()).noquote().nospace()
                    << "[SHUTDOWN] UdpHandler shutdown did not finish within "
                    << sdr9700::shutdownTiming::kUdpHandlerShutdownTimeoutMs << " ms";
            }
        }
        qDebug(logRadio()).noquote() << "[SHUTDOWN] closeComm() udpHandlerThread->quit()";
        udpHandlerThread->quit();
        qDebug(logRadio()).noquote() << "[SHUTDOWN] closeComm() udpHandlerThread->wait(3000)";
        if (!udpHandlerThread->wait(sdr9700::shutdownTiming::kUdpThreadStopTimeoutMs))
        {
            qWarning(logRadio()).noquote() << "[SHUTDOWN] closeComm() udpHandlerThread did not stop within 3000 ms; "
                                              "requesting interruption";
            udpHandlerThread->requestInterruption();
            udpHandlerThread->quit();
            if (!udpHandlerThread->wait(sdr9700::shutdownTiming::kUdpThreadInterruptTimeoutMs))
            {
                qCritical(logRadio()).noquote() << "[SHUTDOWN] closeComm() udpHandlerThread did not stop after bounded "
                                                   "shutdown; leaving thread detached";
                udpHandlerThread->setParent(nullptr);
                connect(udpHandlerThread, &QThread::finished, udpHandlerThread, &QObject::deleteLater,
                        Qt::DirectConnection);
                udpHandlerThread = nullptr;
            }
        }
        else
        {
            qDebug(logRadio()).noquote() << "[SHUTDOWN] closeComm() udpHandlerThread done";
        }
        if (udpHandlerThread != nullptr)
        {
            delete udpHandlerThread;
            udpHandlerThread = nullptr;
        }
    }
    if (queue != nullptr)
    {
        queue->resetSessionState();
    }
    m_pendingReplies.clear();
    m_deferredReplyReads.clear();
    m_receiverScopedReadActive = false;
    m_mainSubExchangeQueued = false;
    m_mainSubExchangeConfirmationPending = false;
    m_replyFamilyDrains.clear();
    m_replyDrainTimer->stop();
    m_rttEstimator.reset();
    resetScheduledCommands();
    mainScopeData = {};
    subScopeData = {};
    m_scopeAssemblyClocks[0].invalidate();
    m_scopeAssemblyClocks[1].invalidate();
    m_expectedScopeSequences[0] = 0;
    m_expectedScopeSequences[1] = 0;
    udp = nullptr;
    m_shutdownComplete = true;
}

void Commander::commonSetup()
{

    setCIVAddr(civAddr);
    payloadSuffix = QByteArray("\xFD");

    lookingForRadio = true;
    m_pendingReplies.clear();
    m_deferredReplyReads.clear();
    m_receiverScopedReadActive = false;
    m_mainSubExchangeQueued = false;
    m_replyFamilyDrains.clear();
    m_replyDrainTimer->stop();
    m_rttEstimator.reset();
    m_correlationDiagnostics = {};
    resetScheduledCommands();
    m_schedulerDiagnostics = {};
    m_lastLoggedSchedulerDiagnostics = {};
    m_schedulerDiagnosticsTimer->start();
    mainScopeData = {};
    subScopeData = {};
    m_scopeAssemblyClocks[0].invalidate();
    m_scopeAssemblyClocks[1].invalidate();
    m_expectedScopeSequences[0] = 0;
    m_expectedScopeSequences[1] = 0;
    m_pendingCommandClock.start();

    // Minimal commands used before the built-in IC-9700 capability table is loaded.
    radioCaps.commands.clear();
    radioCaps.commandsReverse.clear();
    radioCaps.commands.insert(funcTransceiverId,
                              FuncType(funcTransceiverId, QString("Transceiver ID"), QByteArrayLiteral("\x19\x00"), 0,
                                       0, false, false, true, false, 1, false));
    radioCaps.commandsReverse.insert(QByteArrayLiteral("\x19\x00"), funcTransceiverId);

    radioCaps.commands.insert(funcPowerControl,
                              FuncType(funcPowerControl, QString("Power Control"), QByteArrayLiteral("\x18"), 0, 0,
                                       false, false, true, false, 1, false));
    radioCaps.commandsReverse.insert(QByteArrayLiteral("\x18"), funcPowerControl);

    connect(queue, &CachingQueue::haveCommand, this, &Commander::receiveCommand, Qt::UniqueConnection);
    oldScopeMode = 0xff;
}

CommanderSchedulerDiagnostics Commander::schedulerDiagnostics() const
{
    CommanderSchedulerDiagnostics diagnostics = m_schedulerDiagnostics;
    diagnostics.queuedCommands = m_scheduledCommands.size();
    return diagnostics;
}

void Commander::setOperatorSelectedVfo(vfo_t vfo)
{
    m_operatorSelectedVfo = vfo == vfoSub ? vfoSub : vfoMain;
}

void Commander::scheduleMeterRead(Funcs func, uchar receiver)
{
    enqueueScheduledRead(ScheduledCommandClass::Meter, func, receiver);
}

void Commander::scheduleSMeterRead(uchar receiver)
{
    enqueueScheduledAction(ScheduledCommandClass::Meter, funcSMeter, receiver,
                           [this, receiver]()
                           {
                               discardExpiredPendingReplies();
                               if (replyFamilyBlocked(funcSMeter))
                               {
                                   // Do not enter a receiver-scoped transaction until the
                                   // S-meter family can actually reach the wire. Otherwise a
                                   // deferred read has no transmission timeout to release the
                                   // physical MAIN/SUB context and scheduler gate.
                                   scheduleSMeterRead(receiver);
                                   return;
                               }
                               // The selected VFO already provides the requested
                               // receiver identity. Avoid repeatedly re-selecting
                               // SUB while it is the operator-selected receiver:
                               // those CI-V writes add needless LAN traffic during
                               // the real-time audio stream.
                               if (receiver == 0 || m_operatorSelectedVfo == vfoSub)
                               {
                                   receiveCommand(funcSMeter, QVariant(), receiver);
                                   return;
                               }

                               Q_ASSERT(!m_receiverScopedReadActive);
                               m_receiverScopedReadActive = true;
                               m_smeterScopedReadActive = true;
                               m_smeterScopedReceiver = receiver;
                               receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoSub), 0);
                               QTimer::singleShot(50, this,
                                                  [this, receiver]()
                                                  {
                                                      if (!m_shutdownComplete && m_smeterScopedReadActive)
                                                      {
                                                          receiveCommand(funcSMeter, QVariant(), receiver);
                                                      }
                                                  });
                           });
}

void Commander::abortSMeterRead(uchar receiver)
{
    // A SUB read records its physical receiver context here. The backend's
    // receiver argument remains the fallback for MAIN reads and for the narrow
    // race where Commander's own deadline has already closed the scoped read.
    Q_ASSERT(!m_smeterScopedReadActive || m_smeterScopedReceiver == receiver);
    const uchar timedOutReceiver = m_smeterScopedReadActive ? m_smeterScopedReceiver : receiver;
    if (timedOutReceiver == 1)
    {
        ++m_correlationDiagnostics.subSMeterTimeouts;
    }
    else
    {
        ++m_correlationDiagnostics.mainSMeterTimeouts;
    }
    discardPendingReplies(funcSMeter);
    finishSMeterRead();
}

void Commander::scheduleStartupRead(Funcs func, uchar receiver)
{
    enqueueScheduledRead(ScheduledCommandClass::StartupRead, func, receiver);
}

void Commander::scheduleInteractiveAction(Funcs func, uchar receiver, std::function<void()> action)
{
    enqueueScheduledAction(ScheduledCommandClass::InteractiveSet, func, receiver, std::move(action));
}

void Commander::scheduleConfirmatoryAction(Funcs func, uchar receiver, std::function<void()> action)
{
    enqueueScheduledAction(ScheduledCommandClass::ConfirmatoryRead, func, receiver, std::move(action));
}

void Commander::executeReceiverScopedAction(uchar receiver, std::function<void()> action)
{
    Q_ASSERT(!m_receiverScopedReadActive);
    m_receiverScopedReadActive = true;
    const vfo_t targetVfo = receiver == 1 ? vfoSub : vfoMain;
    receiveCommand(funcSelectVFO, QVariant::fromValue(targetVfo), 0);
    QTimer::singleShot(50, this,
                       [this, targetVfo, action = std::move(action)]()
                       {
                           if (!m_shutdownComplete)
                           {
                               action();
                               if (targetVfo != m_operatorSelectedVfo)
                               {
                                   receiveCommand(funcSelectVFO, QVariant::fromValue(m_operatorSelectedVfo), 0);
                               }
                           }
                           QTimer::singleShot(25, this, &Commander::finishReceiverScopedAction);
                       });
}

void Commander::enqueueScheduledAction(ScheduledCommandClass commandClass, Funcs func, uchar receiver,
                                       std::function<void()> action)
{
    const auto existing = std::find_if(
        m_scheduledCommands.begin(), m_scheduledCommands.end(),
        [commandClass, func, receiver](const ScheduledCommand& command)
        { return command.commandClass == commandClass && command.func == func && command.receiver == receiver; });
    if (existing != m_scheduledCommands.end())
    {
        existing->action = std::move(action);
        ++m_schedulerDiagnostics.coalescedCommands;
        return;
    }
    if (m_scheduledCommands.size() >= kMaxScheduledCommands)
    {
        ++m_schedulerDiagnostics.droppedCommands;
        qWarning(logRadio()).noquote().nospace()
            << "CI-V scheduler full depth=" << m_scheduledCommands.size()
            << " dropping interactive=" << funcString[func] << " receiver=" << receiver;
        return;
    }
    m_scheduledCommands.append({commandClass, func, receiver, std::move(action), m_pendingCommandClock.elapsed()});
    m_schedulerDiagnostics.highWaterMark = std::max(m_schedulerDiagnostics.highWaterMark, m_scheduledCommands.size());
    if (!m_scheduledCommandTimer->isActive())
    {
        m_scheduledCommandTimer->start();
    }
}

void Commander::enqueueScheduledRead(ScheduledCommandClass commandClass, Funcs func, uchar receiver)
{
    const Funcs canonicalFunc = canonicalReplyFunc(func);
    const auto duplicate = std::find_if(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(),
                                        [commandClass, canonicalFunc, receiver](const ScheduledCommand& command)
                                        {
                                            return command.commandClass == commandClass &&
                                                   canonicalReplyFunc(command.func) == canonicalFunc &&
                                                   command.receiver == receiver;
                                        });
    if (duplicate != m_scheduledCommands.cend())
    {
        ++m_schedulerDiagnostics.coalescedCommands;
        return;
    }

    if (m_scheduledCommands.size() >= kMaxScheduledCommands)
    {
        ++m_schedulerDiagnostics.droppedCommands;
        qWarning(logRadio()).noquote().nospace() << "CI-V scheduler full depth=" << m_scheduledCommands.size()
                                                 << " dropping=" << funcString[func] << " receiver=" << receiver;
        return;
    }

    m_scheduledCommands.append({commandClass, func, receiver, {}, m_pendingCommandClock.elapsed()});
    m_schedulerDiagnostics.highWaterMark = std::max(m_schedulerDiagnostics.highWaterMark, m_scheduledCommands.size());
    if (!m_scheduledCommandTimer->isActive())
    {
        m_scheduledCommandTimer->start();
    }
}

void Commander::dispatchNextScheduledCommand()
{
    if (m_scheduledCommands.isEmpty() || m_shutdownComplete)
    {
        return;
    }

    qsizetype selected = 0;
    const auto interactive =
        std::find_if(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(), [](const ScheduledCommand& command)
                     { return command.commandClass == ScheduledCommandClass::InteractiveSet; });
    const auto nonInteractive =
        std::find_if(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(), [](const ScheduledCommand& command)
                     { return command.commandClass != ScheduledCommandClass::InteractiveSet; });
    if (m_mainSubExchangeConfirmationPending || m_receiverScopedReadActive)
    {
        m_scheduledCommandTimer->start();
        return;
    }
    const qint64 nowMs = m_pendingCommandClock.elapsed();
    bool meterQueued = false;
    bool meterOverdue = false;
    bool backgroundStarved = false;
    for (const ScheduledCommand& command : m_scheduledCommands)
    {
        if (command.commandClass == ScheduledCommandClass::Meter)
        {
            meterQueued = true;
            meterOverdue = meterOverdue || nowMs - command.enqueuedAtMs >= kMeterQueueBudgetMs;
        }
        else if (command.commandClass != ScheduledCommandClass::InteractiveSet)
        {
            const qint64 referenceMs = std::max(command.enqueuedAtMs, m_lastBackgroundDispatchMs);
            backgroundStarved = backgroundStarved || nowMs - referenceMs >= kBackgroundStarvationCeilingMs;
        }
    }
    // Alternate aged background work with meters, but keep the fairness guard
    // from becoming an indefinite hold when meters replenish faster than the
    // link can dispatch them.
    const bool yieldToMeters = (m_backgroundSinceMeter || meterOverdue) && !backgroundStarved;
    if (interactive != m_scheduledCommands.cend() &&
        (m_consecutiveInteractiveDispatches < kMaxConsecutiveInteractiveDispatches ||
         nonInteractive == m_scheduledCommands.cend()))
    {
        selected = std::distance(m_scheduledCommands.cbegin(), interactive);
    }
    else if (yieldToMeters && meterQueued)
    {
        const auto meter =
            std::find_if(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(), [](const ScheduledCommand& command)
                         { return command.commandClass == ScheduledCommandClass::Meter; });
        selected = std::distance(m_scheduledCommands.cbegin(), meter);
    }
    else if (nonInteractive != m_scheduledCommands.cend())
    {
        selected = std::distance(m_scheduledCommands.cbegin(), nonInteractive);
    }

    if (m_scheduledCommands.at(selected).commandClass != ScheduledCommandClass::InteractiveSet &&
        m_consecutiveMeterDispatches >= kMaxConsecutiveMeterDispatches)
    {
        const auto startup =
            std::find_if(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(), [](const ScheduledCommand& command)
                         { return command.commandClass == ScheduledCommandClass::StartupRead; });
        if (startup != m_scheduledCommands.cend())
        {
            selected = std::distance(m_scheduledCommands.cbegin(), startup);
        }
    }

    const ScheduledCommand command = m_scheduledCommands.takeAt(selected);
    if (command.commandClass == ScheduledCommandClass::InteractiveSet)
    {
        ++m_consecutiveInteractiveDispatches;
    }
    else
    {
        m_consecutiveInteractiveDispatches = 0;
    }
    if (command.commandClass == ScheduledCommandClass::Meter)
    {
        m_backgroundSinceMeter = false;
        ++m_consecutiveMeterDispatches;
    }
    else
    {
        if (command.commandClass != ScheduledCommandClass::InteractiveSet)
        {
            m_backgroundSinceMeter = true;
            m_lastBackgroundDispatchMs = nowMs;
        }
        m_consecutiveMeterDispatches = 0;
    }
    ++m_schedulerDiagnostics.dispatchedCommands;
    m_dispatchingScheduledCommand = true;
    if (command.action)
    {
        command.action();
    }
    else
    {
        receiveCommand(command.func, QVariant(), command.receiver);
    }
    m_dispatchingScheduledCommand = false;

    if (m_mainSubExchangeQueued)
    {
        dispatchDeferredReplyReads();
    }
    if (!m_scheduledCommands.isEmpty())
    {
        m_scheduledCommandTimer->start();
    }
}

void Commander::logSchedulerDiagnostics()
{
    const CommanderSchedulerDiagnostics diagnostics = schedulerDiagnostics();
    const CommanderCorrelationDiagnostics correlation = correlationDiagnostics();
    const CachingQueueDiagnostics cacheQueue = queue->diagnostics();
    qInfo(logRadio()).noquote().nospace()
        << "CivScheduler::Metrics queued=" << diagnostics.queuedCommands << " highWater=" << diagnostics.highWaterMark
        << " dispatched=" << diagnostics.dispatchedCommands << " coalesced=" << diagnostics.coalescedCommands
        << " dropped=" << diagnostics.droppedCommands << " pendingReplies=" << correlation.pendingReplies
        << " pendingHighWater=" << correlation.pendingReplyHighWaterMark
        << " pendingOverflows=" << correlation.pendingReplyOverflows
        << " pendingExpirations=" << correlation.pendingReplyExpirations
        << " unattributedDirectedValues=" << correlation.unattributedDirectedValues
        << " correlationLostDuringParse=" << correlation.correlationLostDuringParse
        << " deferredReads=" << correlation.deferredReplyReads
        << " droppedDeferredReads=" << correlation.droppedReplyReads << " cacheQueueDepth=" << cacheQueue.depth
        << " cacheQueueHighWater=" << cacheQueue.highWaterMark << " cacheQueueOldestMs=" << cacheQueue.oldestItemAgeMs
        << " cacheQueueDropped=" << cacheQueue.droppedForCapacity
        << " transmittedFrames=" << diagnostics.transmittedFrames << " scheduledFrames=" << diagnostics.scheduledFrames
        << " directFrames=" << diagnostics.directFrames << " sMeterMainReplies=" << correlation.mainSMeterReplies
        << " sMeterMainRequests=" << correlation.mainSMeterRequests
        << " sMeterMainTimeouts=" << correlation.mainSMeterTimeouts
        << " sMeterSubReplies=" << correlation.subSMeterReplies
        << " sMeterSubRequests=" << correlation.subSMeterRequests
        << " sMeterSubTimeouts=" << correlation.subSMeterTimeouts << " intervalDispatched="
        << (diagnostics.dispatchedCommands - m_lastLoggedSchedulerDiagnostics.dispatchedCommands)
        << " intervalCoalesced=" << (diagnostics.coalescedCommands - m_lastLoggedSchedulerDiagnostics.coalescedCommands)
        << " intervalDropped=" << (diagnostics.droppedCommands - m_lastLoggedSchedulerDiagnostics.droppedCommands)
        << " intervalFrames=" << (diagnostics.transmittedFrames - m_lastLoggedSchedulerDiagnostics.transmittedFrames);
    m_lastLoggedSchedulerDiagnostics = diagnostics;
}

void Commander::resetScheduledCommands()
{
    m_scheduledCommandTimer->stop();
    m_schedulerDiagnosticsTimer->stop();
    m_scheduledCommands.clear();
    m_consecutiveMeterDispatches = 0;
    m_consecutiveInteractiveDispatches = 0;
    m_backgroundSinceMeter = false;
    m_lastBackgroundDispatchMs = -1;
    m_dispatchingScheduledCommand = false;
    m_mainSubExchangeConfirmationPending = false;
    m_receiverScopedReadActive = false;
    m_smeterScopedReadActive = false;
    m_smeterScopedReceiver = 0xff;
    m_operatorSelectedVfo = vfoMain;
}

void Commander::prepDataAndSend(QByteArray data)
{
    data.prepend(payloadPrefix);
    data.append(payloadSuffix);

    if (data[4] != '\x15')
    {
        // Meter polling is high-volume; keep CI-V traffic useful by suppressing it.
        qInfo(logRadioTraffic()).noquote().nospace()
            << "CivData::TX len=" << data.size() << " hex=" << QString::fromLatin1(data.toHex(' '));
    }
    ++m_schedulerDiagnostics.transmittedFrames;
    if (m_dispatchingScheduledCommand)
    {
        ++m_schedulerDiagnostics.scheduledFrames;
    }
    else
    {
        ++m_schedulerDiagnostics.directFrames;
    }
    emit dataForComm(data);
}

void Commander::sendStandbyWake()
{
    // IC-9700 CI-V Reference Guide, command 18 01: at 115200 baud the
    // power-on frame requires a long FE synchronization fill before the
    // standard CI-V envelope. The guide calls for approximately 119 FE bytes;
    // 150 extra bytes safely exceeds that approximate minimum over LAN.
    static constexpr int kExtraPreambleBytes = 150;
    QByteArray frame(kExtraPreambleBytes, '\xFE');
    frame.append(payloadPrefix);
    frame.append("\x18\x01", 2);
    frame.append('\xFD');

    // Pause the provisional session's ordinary health watchdog before sending
    // power-on. RadioBackend owns the longer, bounded boot hold and reconnects
    // this transport afterward; silence during that interval is intentional.
    emit standbyWakeHoldStarted();
    qInfo(logRadioTraffic()).noquote().nospace()
        << "CivData::TX len=" << frame.size() << " hex=" << QString::fromLatin1(frame.toHex(' '))
        << " purpose=standby-wake";
    ++m_schedulerDiagnostics.transmittedFrames;
    ++m_schedulerDiagnostics.directFrames;
    emit dataForComm(frame);
}

void Commander::rememberPendingReply(Funcs func, uchar receiver)
{
    static constexpr int kMaxPendingReplies = 64;

    if (func == funcNone)
    {
        return;
    }

    discardExpiredPendingReplies();
    const qint64 now = m_pendingCommandClock.elapsed();
    const qint64 lifetimeMs = m_rttEstimator.replyTimeoutMs();
    m_pendingReplies.append(PendingReply{func, receiver, now, now + lifetimeMs});
    m_correlationDiagnostics.pendingReplies = m_pendingReplies.size();
    m_correlationDiagnostics.pendingReplyHighWaterMark =
        qMax(m_correlationDiagnostics.pendingReplyHighWaterMark, m_pendingReplies.size());
    if (m_pendingReplies.size() > kMaxPendingReplies)
    {
        const qsizetype dropped = m_pendingReplies.size() - kMaxPendingReplies;
        m_pendingReplies.remove(0, dropped);
        m_correlationDiagnostics.pendingReplyOverflows += dropped;
        m_correlationDiagnostics.pendingReplies = m_pendingReplies.size();
        qCritical(logRadio()).noquote().nospace()
            << "CI-V pending reply overflow dropped=" << dropped << " depth=" << m_pendingReplies.size();
    }

    QTimer::singleShot(lifetimeMs + 1, this, [this]() { discardExpiredPendingReplies(); });
}

bool Commander::pendingReplyReceiver(Funcs func, uchar* receiver)
{
    if (!receiver)
    {
        return false;
    }

    discardExpiredPendingReplies();
    for (int i = 0; i < m_pendingReplies.size(); ++i)
    {
        if (repliesMatch(m_pendingReplies.at(i).func, func))
        {
            *receiver = m_pendingReplies.at(i).receiver;
            return true;
        }
    }

    return false;
}

bool Commander::takePendingReplyReceiver(Funcs func, uchar* receiver)
{
    if (!receiver)
    {
        return false;
    }

    discardExpiredPendingReplies();
    for (int i = 0; i < m_pendingReplies.size(); ++i)
    {
        if (repliesMatch(m_pendingReplies.at(i).func, func))
        {
            *receiver = m_pendingReplies.at(i).receiver;
            m_pendingReplies.removeAt(i);
            m_correlationDiagnostics.pendingReplies = m_pendingReplies.size();
            beginReplyFamilyDrain(func, m_rttEstimator.resolvedDrainMs());
            return true;
        }
    }

    return false;
}

void Commander::discardPendingReplies(Funcs func)
{
    const qsizetype previousSize = m_pendingReplies.size();
    m_pendingReplies.erase(std::remove_if(m_pendingReplies.begin(), m_pendingReplies.end(),
                                          [func](const PendingReply& reply) { return repliesMatch(reply.func, func); }),
                           m_pendingReplies.end());
    m_correlationDiagnostics.pendingReplies = m_pendingReplies.size();
    if (m_pendingReplies.size() != previousSize)
    {
        beginReplyFamilyDrain(func, m_rttEstimator.abandonedDrainMs());
    }
}

void Commander::discardExpiredPendingReplies()
{
    const qint64 now = m_pendingCommandClock.elapsed();

    // A command reply that arrives after this window can no longer be routed
    // confidently to MAIN or SUB. Discarding stale hints is safer than applying
    // an unsolicited update to a receiver selected several seconds earlier.
    const qsizetype previousSize = m_pendingReplies.size();
    QVector<Funcs> expiredFamilies;
    m_pendingReplies.erase(std::remove_if(m_pendingReplies.begin(), m_pendingReplies.end(),
                                          [now, &expiredFamilies](const PendingReply& reply)
                                          {
                                              if (reply.expiresAtMs > now)
                                              {
                                                  return false;
                                              }
                                              expiredFamilies.append(reply.func);
                                              return true;
                                          }),
                           m_pendingReplies.end());
    const qsizetype expired = previousSize - m_pendingReplies.size();
    m_correlationDiagnostics.pendingReplyExpirations += expired;
    m_correlationDiagnostics.pendingReplies = m_pendingReplies.size();
    for (const Funcs func : std::as_const(expiredFamilies))
    {
        beginReplyFamilyDrain(func, m_rttEstimator.abandonedDrainMs());
    }
}

bool Commander::replyFamilyBlocked(Funcs func) const
{
    const Funcs canonical = canonicalReplyFunc(func);
    const bool live =
        std::any_of(m_pendingReplies.cbegin(), m_pendingReplies.cend(),
                    [canonical](const PendingReply& reply) { return canonicalReplyFunc(reply.func) == canonical; });
    if (live)
    {
        return true;
    }
    const qint64 now = m_pendingCommandClock.elapsed();
    return std::any_of(m_replyFamilyDrains.cbegin(), m_replyFamilyDrains.cend(),
                       [canonical, now](const ReplyFamilyDrain& drain)
                       { return canonicalReplyFunc(drain.func) == canonical && drain.untilMs > now; });
}

bool Commander::replyFamilyDraining(Funcs func) const
{
    const Funcs canonical = canonicalReplyFunc(func);
    const qint64 now = m_pendingCommandClock.elapsed();
    return std::any_of(m_replyFamilyDrains.cbegin(), m_replyFamilyDrains.cend(),
                       [canonical, now](const ReplyFamilyDrain& drain)
                       { return canonicalReplyFunc(drain.func) == canonical && drain.untilMs > now; });
}

bool Commander::deferReplyReadIfBlocked(Funcs func, uchar receiver)
{
    if (!replyFamilyBlocked(func))
    {
        return false;
    }
    return enqueueDeferredReplyRead(func, receiver);
}

bool Commander::enqueueDeferredReplyRead(Funcs func, uchar receiver)
{
    const auto duplicate = std::find_if(m_deferredReplyReads.cbegin(), m_deferredReplyReads.cend(),
                                        [func, receiver](const DeferredReplyRead& read)
                                        { return read.func == func && read.receiver == receiver; });
    if (duplicate != m_deferredReplyReads.cend())
    {
        ++m_correlationDiagnostics.coalescedReplyReads;
        return true;
    }
    if (m_deferredReplyReads.size() >= kMaxDeferredReplyReads)
    {
        ++m_correlationDiagnostics.droppedReplyReads;
        qWarning(logRadio()).noquote().nospace()
            << "CI-V deferred reply-read queue full depth=" << m_deferredReplyReads.size()
            << " dropping=" << funcString[func] << " receiver=" << receiver;
        return true;
    }
    m_deferredReplyReads.append({func, receiver});
    ++m_correlationDiagnostics.deferredReplyReads;
    return true;
}

void Commander::beginReplyFamilyDrain(Funcs func, qint64 durationMs)
{
    const Funcs canonical = canonicalReplyFunc(func);
    const qint64 deadline = m_pendingCommandClock.elapsed() + durationMs;
    auto drain =
        std::find_if(m_replyFamilyDrains.begin(), m_replyFamilyDrains.end(),
                     [canonical](const ReplyFamilyDrain& item) { return canonicalReplyFunc(item.func) == canonical; });
    if (drain == m_replyFamilyDrains.end())
    {
        m_replyFamilyDrains.append({canonical, deadline});
    }
    else
    {
        drain->untilMs = std::max(drain->untilMs, deadline);
    }
    m_replyDrainTimer->start(static_cast<int>(durationMs));
}

void Commander::dispatchDeferredReplyReads()
{
    if (m_receiverScopedReadActive)
    {
        m_replyDrainTimer->start(20);
        return;
    }
    const qint64 now = m_pendingCommandClock.elapsed();
    m_replyFamilyDrains.erase(std::remove_if(m_replyFamilyDrains.begin(), m_replyFamilyDrains.end(),
                                             [now](const ReplyFamilyDrain& drain) { return drain.untilMs <= now; }),
                              m_replyFamilyDrains.end());

    const bool interactiveSetPending =
        std::any_of(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(), [](const ScheduledCommand& command)
                    { return command.commandClass == ScheduledCommandClass::InteractiveSet; });
    if (m_mainSubExchangeQueued && !interactiveSetPending && !replyFamilyBlocked(funcFreqGet) &&
        !replyFamilyBlocked(funcModeGet))
    {
        dispatchMainSubExchange();
        return;
    }

    for (qsizetype index = 0; index < m_deferredReplyReads.size();)
    {
        const DeferredReplyRead read = m_deferredReplyReads.at(index);
        const Funcs family = canonicalReplyFunc(read.func);
        const bool exchangeConfirmationRead = read.receiver == 1 && (family == canonicalReplyFunc(funcFreqGet) ||
                                                                     family == canonicalReplyFunc(funcModeGet));
        if (m_mainSubExchangeConfirmationPending && !exchangeConfirmationRead)
        {
            ++index;
            continue;
        }
        if (replyFamilyBlocked(read.func))
        {
            ++index;
            continue;
        }
        m_deferredReplyReads.removeAt(index);
        dispatchReceiverScopedRead(read.func, read.receiver);
        break;
    }

    if (!m_deferredReplyReads.isEmpty() || m_mainSubExchangeQueued)
    {
        m_replyDrainTimer->start(static_cast<int>(m_rttEstimator.resolvedDrainMs()));
    }
}

void Commander::requestMainSubExchange()
{
    if (m_mainSubExchangeQueued)
    {
        return;
    }
    m_mainSubExchangeQueued = true;
    const bool interactiveSetPending =
        std::any_of(m_scheduledCommands.cbegin(), m_scheduledCommands.cend(), [](const ScheduledCommand& command)
                    { return command.commandClass == ScheduledCommandClass::InteractiveSet; });
    if (!interactiveSetPending && !replyFamilyBlocked(funcFreqGet) && !replyFamilyBlocked(funcModeGet))
    {
        dispatchMainSubExchange();
    }
}

void Commander::requestReceiverScopedRead(Funcs func, uchar receiver)
{
    discardExpiredPendingReplies();
    if (m_receiverScopedReadActive)
    {
        enqueueDeferredReplyRead(func, receiver);
        return;
    }
    if (deferReplyReadIfBlocked(func, receiver))
    {
        return;
    }

    dispatchReceiverScopedRead(func, receiver);
}

void Commander::dispatchReceiverScopedRead(Funcs func, uchar receiver)
{
    executeReceiverScopedAction(receiver, [this, func, receiver]() { receiveCommand(func, QVariant(), receiver); });
}

void Commander::finishReceiverScopedAction()
{
    m_receiverScopedReadActive = false;
    dispatchDeferredReplyReads();
    if (!m_scheduledCommands.isEmpty() && !m_scheduledCommandTimer->isActive())
    {
        m_scheduledCommandTimer->start();
    }
}

void Commander::finishSMeterRead()
{
    if (!m_smeterScopedReadActive)
    {
        return;
    }

    const uchar scopedReceiver = m_smeterScopedReceiver;
    m_smeterScopedReadActive = false;
    m_smeterScopedReceiver = 0xff;
    const bool wasDispatchingScheduledCommand = m_dispatchingScheduledCommand;
    m_dispatchingScheduledCommand = true;
    if (scopedReceiver == 1 && m_operatorSelectedVfo != vfoSub)
    {
        receiveCommand(funcSelectVFO, QVariant::fromValue(m_operatorSelectedVfo), 0);
    }
    receiveCommandNoReadback(funcScopeMainSub, QVariant::fromValue<bool>(m_operatorSelectedVfo == vfoSub), 0);
    m_dispatchingScheduledCommand = wasDispatchingScheduledCommand;
    QTimer::singleShot(25, this, &Commander::finishReceiverScopedAction);
}

void Commander::dispatchMainSubExchange()
{
    Q_ASSERT(m_mainSubExchangeQueued);
    Q_ASSERT(!replyFamilyBlocked(funcFreqGet));
    Q_ASSERT(!replyFamilyBlocked(funcModeGet));
    m_mainSubExchangeQueued = false;
    m_mainSubExchangeConfirmationPending = true;

    receiveCommand(funcVFOSwapMS, QVariant(), 0);
    receiveCommand(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoMain), 0);
    receiveCommand(funcSelectedFreq, QVariant(), 0);
    receiveCommand(funcSelectedMode, QVariant(), 0);
    receiveCommandNoReadback(funcVFOBandMS, QVariant::fromValue<bool>(false), 0);
    receiveCommandNoReadback(funcScopeMainSub, QVariant::fromValue<bool>(false), 0);
    receiveCommand(funcSelectedFreq, QVariant(), 0);
    receiveCommand(funcVFOBandMS, QVariant(), 0);
    receiveCommand(funcScopeMainSub, QVariant(), 0);
    emit mainSubExchangeDispatched();

    // The swap acknowledgement does not prove that both newly exchanged
    // receiver contexts are already readable. Let the radio apply the swap,
    // then obtain SUB identity through the serialized receiver-scoped path.
    // The old immediate SUB-select/read/restore burst could lose the mode
    // reply or have another routed operation steal its physical context.
    QTimer::singleShot(75, this,
                       [this]()
                       {
                           if (!m_shutdownComplete && m_mainSubExchangeConfirmationPending)
                           {
                               requestReceiverScopedRead(funcFreqGet, 1);
                               requestReceiverScopedRead(funcModeGet, 1);
                           }
                       });
}

void Commander::finishMainSubExchangeConfirmation()
{
    m_mainSubExchangeConfirmationPending = false;
    m_deferredReplyReads.erase(std::remove_if(m_deferredReplyReads.begin(), m_deferredReplyReads.end(),
                                              [](const DeferredReplyRead& read)
                                              {
                                                  const Funcs family = canonicalReplyFunc(read.func);
                                                  return read.receiver == 1 &&
                                                         (family == canonicalReplyFunc(funcFreqGet) ||
                                                          family == canonicalReplyFunc(funcModeGet));
                                              }),
                               m_deferredReplyReads.end());
    if (!m_scheduledCommands.isEmpty() && !m_scheduledCommandTimer->isActive())
    {
        m_scheduledCommandTimer->start();
    }
    if (!m_deferredReplyReads.isEmpty() || m_mainSubExchangeQueued)
    {
        m_replyDrainTimer->start(static_cast<int>(m_rttEstimator.resolvedDrainMs()));
    }
}

CommanderCorrelationDiagnostics Commander::correlationDiagnostics() const
{
    CommanderCorrelationDiagnostics diagnostics = m_correlationDiagnostics;
    diagnostics.pendingReplies = m_pendingReplies.size();
    diagnostics.resolvedReplyDrainMs = m_rttEstimator.resolvedDrainMs();
    diagnostics.abandonedReplyDrainMs = m_rttEstimator.abandonedDrainMs();
    diagnostics.replyTimeoutMs = m_rttEstimator.replyTimeoutMs();
    diagnostics.rttSampleCount = m_rttEstimator.sampleCount();
    return diagnostics;
}

FuncType Commander::getCommand(Funcs func, QByteArray& payload, int value, uchar receiver)
{
    FuncType cmd;
    // INT_MIN marks a get request and is outside supported set-value ranges.
    auto it = radioCaps.commands.find(func);
    if (it != radioCaps.commands.end())
    {
        if (value == INT_MIN || (value >= it.value().minVal && value <= it.value().maxVal))
        {
            if (radioCaps.hasCommand29 && it.value().cmd29)
            {
                // CI-V command 29h prefixes receiver-scoped commands.
                payload.append('\x29');
                payload.append(static_cast<uchar>(receiver));
            }
            else if (!radioCaps.hasCommand29 && receiver && receiver != 0xff)
            {
                // CI-V 29h is required for direct receiver-scoped writes.
                // Scope commands remain valid because the payload carries receiver data.
                switch (func)
                {
                case funcFreqGet:
                case funcFreqSet:
                case funcModeGet:
                case funcModeSet:
                case funcSMeter:
                case funcSWRMeter:
                case funcPowerMeter:
                case funcALCMeter:
                case funcAfGain:
                case funcRfGain:
                case funcSquelch:
                case funcNoiseReduction:
                case funcNRLevel:
                case funcNoiseBlanker:
                case funcNBLevel:
                case funcRFPower:
                case funcScopeMode:
                case funcScopeSpan:
                case funcScopeRef:
                case funcScopeHold:
                case funcScopeSpeed:
                case funcScopeRBW:
                case funcScopeVBW:
                case funcScopeCenterType:
                case funcScopeEdge:
                    break;
                default:
                    qDebug(logRadio()).noquote()
                        << "Radio has no Command29, removing command:" << funcString[func] << "VFO" << receiver;
                    queue->del(func, receiver);
                    break;
                }
            }
            payload.append(it.value().data);
            cmd = it.value();
        }
        else if (value != INT_MIN)
        {
            qDebug(logRadio()).noquote() << QString("Value %1 for %2 is outside of allowed range (%3-%4)")
                                                .arg(value)
                                                .arg(funcString[func])
                                                .arg(it.value().minVal)
                                                .arg(it.value().maxVal);
        }
    }
    else
    {
        // The built-in IC-9700 table does not support this command.
        qDebug(logRadio()).noquote() << "Removing unsupported command from queue" << funcString[func] << "VFO"
                                     << receiver;
        queue->del(func, receiver);
    }
    return cmd;
}

QByteArray Commander::makeFreqPayload(Frequency freq, uchar numchars)
{
    QByteArray result;
    quint64 freqInt = freq.Hz;

    if (numchars == 5 && freq.Hz >= 1E10)
    {
        // Retain six-byte CI-V frequency encoding for protocol-range safety above 10 GHz.
        numchars = 6;
    }

    for (int i = 0; i < numchars; i++)
    {
        quint8 a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10) << 4;

        freqInt /= 10;

        result.append(a);
    }

    return result;
}

QByteArray Commander::makeFreqPayload(double frequency)
{
    quint64 freqInt = (quint64)(frequency * 1E6);

    QByteArray result;
    int numchars = 5;
    if (freqInt >= 1E10)
    {
        numchars = 6;
    }

    for (int i = 0; i < numchars; i++)
    {
        quint8 a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10) << 4;

        freqInt /= 10;

        result.append(a);
    }
    return result;
}

QByteArray Commander::encodeTone(quint16 tone) const
{
    return encodeTone(tone, false, false);
}

QByteArray Commander::encodeTone(quint16 tone, bool tinv, bool rinv) const
{
    // CTCSS and DTCS use the same packed decimal tone payload.
    QByteArray enct;

    quint8 inv = 0;
    inv |= static_cast<quint8>(rinv);
    inv |= static_cast<quint8>(tinv) << 4;

    enct.append(inv);

    quint8 hundreds = tone / 1000;
    quint8 tens = (tone - (hundreds * 1000)) / 100;
    quint8 ones = (tone - (hundreds * 1000) - (tens * 100)) / 10;
    quint8 dec = (tone - (hundreds * 1000) - (tens * 100) - (ones * 10));

    enct.append(tens | (hundreds << 4));
    enct.append(dec | (ones << 4));

    return enct;
}

ToneInfo Commander::decodeTone(const QByteArray& eTone)
{
    // index:  00 01  02 03 04
    // CTCSS:  1B 01  00 12 73 = PL 127.3, decode as 1273
    // D(T)CS: 1B 01  TR 01 23 = T/R Invert bits + DCS code 123

    ToneInfo t;
    if (eTone.length() < 3)
    {
        return t;
    }

    ushort tone = (eTone.at(2) & 0x0f);
    tone += ((eTone.at(2) & 0xf0) >> 4) * 10;
    tone += (eTone.at(1) & 0x0f) * 100;
    tone += ((eTone.at(1) & 0xf0) >> 4) * 1000;

    // Always set the decoded value first so DCS codes (which are not in the
    // CTCSS list) don't fall through to the default-constructed tone of 670.
    t.tone = tone;

    const auto toneIt = std::find_if(radioCaps.ctcss.cbegin(), radioCaps.ctcss.cend(),
                                     [tone](const ToneInfo& info) { return info.tone == tone; });
    if (toneIt != radioCaps.ctcss.cend())
    {
        t = *toneIt;
    }

    if ((eTone.at(0) & 0x01) == 0x01)
    {
        t.tinv = true;
    }
    if ((eTone.at(0) & 0x10) == 0x10)
    {
        t.rinv = true;
    }

    return t;
}

void Commander::setCIVAddr(quint16 newCivAddr)
{
    // The controller CI-V address is defined in the header.

    civAddr = newCivAddr;
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append((char)newCivAddr);
    payloadPrefix.append((char)compCivAddr);
}

void Commander::handleNewData(const QByteArray& data)
{
    const bool scopeDataFrame = data.size() > 64 && static_cast<uchar>(data[4]) == 0x27;
    if (!scopeDataFrame)
    {
        qInfo(logRadioTraffic()).noquote().nospace()
            << "CivData::RX len=" << data.size() << " hex=" << QString::fromLatin1(data.toHex(' '));
    }
    // Spectrum Scope frames arrive continuously and are hundreds of bytes long.
    // Logging every frame hides startup and memory-sync evidence, which is
    // exactly what we need when diagnosing radio readiness hangs. Suppress only
    // the log line; the parser still receives the complete frame.
    parseData(data);
}

void Commander::parseData(const QByteArray& dataInput)
{
    int index = 0;

    QList<QByteArray> dataList = dataInput.split('\xFD');
    QByteArray data;
    if (dataList.last().isEmpty())
    {
        dataList.removeLast();
    }
    for (index = 0; index < dataList.count(); index++)
    {
        data = dataList[index];
        data.append('\xFD');

        // Echoed command:
        // fe fe 94 e0 ...... fd

        // Query reply:
        // fe fe e0 94 ...... fd

        // Radio-initiated update:
        // fe fe 00 94 ...... fd

        if (data.length() < 4)
        {
            if (data.length())
            {
                qDebug(logRadio()).noquote()
                    << "Short CI-V fragment while parsing LAN data:" << data.length() << "bytes";
            }
            // Keep parsing the remaining byte stream. A short fragment can
            // appear ahead of a valid frame after LAN packet coalescing or
            // recovery from a malformed frame.
            continue;
        }

        if (!data.startsWith("\xFE\xFE"))
        {
            // Recover a frame that lost one leading FE byte.
            if (data.startsWith('\xFE'))
            {
                data.prepend('\xFE');
                parseData(data);
            }
            continue;
        }

        incomingCIVAddr = data[03] & 0xff; // track the CIV of the sender.

        switch (data[02])
        {
        case (char)0xE0:
        case (char)compCivAddr:
            // Query reply addressed to this controller.
            payloadIn = data.right(data.length() - 4);
            if (payloadIn.contains("\xFE"))
            {
                break;
            }
            parseCommand(FrameOrigin::SolicitedReply);
            if (!radioPoweredOn && !payloadIn.isEmpty())
            {
                queue->receiveValue(funcPowerControl, QVariant::fromValue<bool>(true), 0);
                radioPoweredOn = true;
            }

            break;
        case '\x00':
            // Radio-initiated update.
            if ((quint8)data[03] == compCivAddr)
            {
                // Echo of a local broadcast request.
                if (radioPoweredOn)
                {
                    qDebug(logRadioTraffic()).noquote() << "Broadcast CI-V echo received" << data.toHex(' ');
                    qWarning(logRadio()).noquote()
                        << "CI-V broadcast was echoed without a directed reply; command plane may be unavailable";
                    queue->receiveValue(funcPowerControl, QVariant::fromValue<bool>(false), 0);
                    radioPoweredOn = false;
                }
            }
            else
            {
                payloadIn = data.right(data.length() - 4);
                if (payloadIn.contains("\xFE"))
                {
                    break;
                }
                parseCommand(FrameOrigin::UnsolicitedBroadcast);
            }
            break;
        default:
            // Ignore frames addressed to other CI-V devices.
            break;
        }
    }
}

Commander::ReplyParseResult Commander::parseFrequencyReply(Funcs& func, QVariant& value, uchar& receiver)
{
    uchar vfo = 0;
    if (func == funcFreq)
    {
        if (payloadIn.isEmpty())
        {
            qWarning(logRadio()).noquote() << "Ignoring short CI-V payload for" << funcString[func] << "required" << 1
                                           << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
            return ReplyParseResult::Malformed;
        }
        receiver = static_cast<uchar>(payloadIn.at(0));
        payloadIn.remove(0, 1);
    }

    if (func == funcFreqTR || func == funcFreqGet)
    {
        if (radioCaps.commands.contains(funcFreq))
        {
            func = funcFreq;
        }
        else if (radioCaps.commands.contains(funcSelectedFreq))
        {
            func = funcSelectedFreq;
        }
        else
        {
            func = funcFreqGet;
        }
    }
    else if (func == funcUnselectedFreq)
    {
        vfo = 1;
    }

    if (payloadIn.size() < 5)
    {
        qWarning(logRadio()).noquote() << "Ignoring short CI-V payload for" << funcString[func] << "required" << 5
                                       << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
        return ReplyParseResult::Malformed;
    }

    value.setValue(parseFreqData(payloadIn, vfo));
    return ReplyParseResult::Parsed;
}

Commander::ReplyParseResult Commander::parseModeReply(Funcs& func, QVariant& value, uchar& receiver)
{
    uchar vfo = 0;
    if (func == funcMode)
    {
        if (payloadIn.isEmpty())
        {
            qWarning(logRadio()).noquote() << "Ignoring short CI-V payload for" << funcString[func] << "required" << 1
                                           << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
            return ReplyParseResult::Malformed;
        }
        receiver = static_cast<uchar>(payloadIn.at(0));
        payloadIn.remove(0, 1);
    }

    const Funcs originalFunc = func;
    // Normalize alternate mode reply commands to the command SDR9700 caches.
    if (func == funcModeTR || func == funcModeGet || func == funcDataModeWithFilter)
    {
        if (radioCaps.commands.contains(funcMode))
        {
            func = funcMode;
        }
        else if (radioCaps.commands.contains(funcSelectedMode))
        {
            func = funcSelectedMode;
        }
        else
        {
            func = funcModeGet;
        }
    }
    else if (func == funcUnselectedMode)
    {
        vfo = 1;
    }

    ModeInfo mode;
    // This lookup supplements the reply currently being parsed. It must not
    // schedule a refresh: the fresh mode value is already in hand and will be
    // written to the cache when parsing completes.
    const CacheItem cachedMode = queue->peekCache(func, receiver);
    if (cachedMode.value.isValid())
    {
        mode = cachedMode.value.value<ModeInfo>();
    }

    if (originalFunc == funcDataModeWithFilter)
    {
        if (payloadIn.size() < 2)
        {
            qWarning(logRadio()).noquote()
                << "Ignoring short CI-V payload for" << funcString[originalFunc] << "required" << 2 << "got"
                << payloadIn.size() << "data:" << payloadIn.toHex(' ');
            return ReplyParseResult::Malformed;
        }
        mode.filter = bcdHexToUChar(payloadIn.at(1));
        mode.data = bcdHexToUChar(payloadIn.at(0));
    }
    else
    {
        if (payloadIn.isEmpty())
        {
            qWarning(logRadio()).noquote()
                << "Ignoring short CI-V payload for" << funcString[originalFunc] << "required" << 1 << "got"
                << payloadIn.size() << "data:" << payloadIn.toHex(' ');
            return ReplyParseResult::Malformed;
        }
        if (!payloadIn.isEmpty())
        {
            mode.reg = bcdHexToUChar(payloadIn.at(0));
        }
        if (payloadIn.size() == 2)
        {
            mode.filter = payloadIn.at(1);
        }
        if (payloadIn.size() == 3)
        {
            mode.data = payloadIn.at(1);
            mode.filter = payloadIn.at(2);
        }
    }

    mode = parseMode(mode.reg, mode.data, mode.filter, receiver, vfo);
    mode.VFO = selVFO_t(receiver);
    value.setValue(mode);
    return ReplyParseResult::Parsed;
}

bool Commander::replyPayloadTooShort(Funcs func, int requiredBytes) const
{
    if (payloadIn.size() >= requiredBytes)
    {
        return false;
    }
    qWarning(logRadio()).noquote() << "Ignoring short CI-V payload for" << funcString[func] << "required"
                                   << requiredBytes << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
    return true;
}

Commander::ReplyParseResult Commander::parseLevelMeterReply(Funcs func, QVariant& value)
{
    switch (func)
    {
    case funcAfGain:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0), payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    // Two-byte CI-V levels with SDR9700-specific unit mappings.
    case funcKeySpeed:
    {
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        uchar level = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
        value.setValue<ushort>(round((level / 6.071) + 6));
        return ReplyParseResult::Parsed;
    }
    case funcCwPitch:
    {
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        uchar level = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
        value.setValue<ushort>(round((((600.0 / 255.0) * level) + 300) / 5.0) * 5.0);
        return ReplyParseResult::Parsed;
    }
    // CI-V group 15: meter readings.
    case funcSMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        {
            const quint8 rawS = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
            value.setValue(static_cast<int>(rawS));
            return ReplyParseResult::Parsed;
        }
    case funcCenterMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(getMeterCal(meterCenter, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        return ReplyParseResult::Parsed;
    case funcPowerMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(getMeterCal(meterPower, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        return ReplyParseResult::Parsed;
    case funcSWRMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        {
            const quint8 rawSwr = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
            const double swr = getMeterCal(meterSWR, rawSwr);
            qDebug(logRadioTraffic()).noquote().nospace()
                << "CivData::Decoded command=SWRMeter raw=" << static_cast<int>(rawSwr) << " calibrated=" << swr;
            value.setValue(swr);
            return ReplyParseResult::Parsed;
        }
    case funcALCMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        {
            const quint8 rawAlc = bcdHexToUChar(payloadIn.at(0), payloadIn.at(1));
            const double alc = getMeterCal(meterALC, rawAlc);
            qDebug(logRadioTraffic()).noquote().nospace()
                << "CivData::Decoded command=ALCMeter raw=" << static_cast<int>(rawAlc) << " calibrated=" << alc;
            value.setValue(alc);
            return ReplyParseResult::Parsed;
        }
    case funcCompMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(getMeterCal(meterComp, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        return ReplyParseResult::Parsed;
    case funcVdMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(getMeterCal(meterVoltage, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        return ReplyParseResult::Parsed;
    case funcIdMeter:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(getMeterCal(meterCurrent, bcdHexToUChar(payloadIn.at(0), payloadIn.at(1))));
        return ReplyParseResult::Parsed;

    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
    case funcBackLightLevel:

    case funcBeepLevel:
    case funcBeepMain:
    case funcBeepSub:

    case funcRFSQLControl:
    case funcTXDelayHF:
    case funcTXDelay50m:
    case funcTimeOutCIV:

        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0), payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    case funcAGC:
    case funcAGCTimeConstant:
    case funcBreakIn:
    case funcPreamp:
    case funcManualNotchWidth:
    case funcSSBTXBandwidth:
    case funcRoofingFilter:
    case funcFilterShape:
    // Tone-control registers under CI-V 1A05.
    case funcSSBRXBass:
    case funcSSBRXTreble:
    case funcAMRXBass:
    case funcAMRXTreble:
    case funcFMRXBass:
    case funcFMRXTreble:
    case funcSSBTXBass:
    case funcSSBTXTreble:
    case funcAMTXBass:
    case funcAMTXTreble:
    case funcFMTXBass:
    case funcFMTXTreble:
    case funcTimeOutTimer:
    case funcBandEdgeBeep:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        return ReplyParseResult::Parsed;

    // RX audio passband limits.
    case funcSSBRXHPFLPF:
    case funcAMRXHPFLPF:
    case funcFMRXHPFLPF:
    case funcCWRXHPFLPF:
    case funcRTTYRXHPFLPF:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(LpfHpf(ushort(payloadIn.at(0)) * 100, ushort(payloadIn.at(1)) * 100));
        return ReplyParseResult::Parsed;
    case funcAbsoluteMeter:
    {
        if (replyPayloadTooShort(func, 4))
        {
            return ReplyParseResult::Malformed;
        }
        MeterKind m;
        m.value = double(bcdHexToUInt(payloadIn.at(0), payloadIn.at(1))) / 10.0;
        if (payloadIn.at(2) != '\0')
        {
            m.value = -m.value;
        }
        if (payloadIn.at(3) == 0)
        {
            m.type = meterdBu;
        }
        else if (payloadIn.at(3) == 1)
        {
            m.type = meterdBuEMF;
        }
        else if (payloadIn.at(3) == 2)
        {
            m.type = meterdBm;
        }
        else
        {
            qWarning(logRadio()).noquote() << "Unknown meter type received!";
            m.type = meterNone;
        }
        value.setValue(m);
        return ReplyParseResult::Parsed;
    }
    case funcMeterType:
    {
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        meter_t m;
        if (payloadIn.at(0) == 0)
        {
            m = meterS;
        }
        else if (payloadIn.at(0) == 1)
        {
            m = meterdBu;
        }
        else if (payloadIn.at(0) == 2)
        {
            m = meterdBuEMF;
        }
        else if (payloadIn.at(0) == 3)
        {
            m = meterdBm;
        }
        else
        {
            qWarning(logRadio()).noquote() << "Unknown meterType received!";
            m = meterNone;
        }
        value.setValue(m);
        return ReplyParseResult::Parsed;
    }
    default:
        return ReplyParseResult::NotHandled;
    }
}

Commander::ReplyParseResult Commander::parseFeatureReply(Funcs func, QVariant& value, uchar receiver)
{
    constexpr uchar vfo = 0;
    switch (func)
    {
    // Single-byte CI-V boolean flags.
    case funcMainSubTracking:
    case funcSatelliteMode:
    case funcNoiseBlanker:
    case funcAudioPeakFilter:
    case funcNoiseReduction:
    case funcAutoNotch:
    case funcRepeaterTone:
    case funcRepeaterTSQL:
    case funcRepeaterDTCS:
    case funcRepeaterCSQL:
    case funcCompressor:
    case funcMonitor:
    case funcVox:
    case funcManualNotch:
    case funcDigiSel:
    case funcTwinPeakFilter:
    case funcDialLock:
    case funcOverflowStatus:
    case funcSMeterSqlStatus:
    case funcVariousSql:
    case funcRXAntenna:
    case funcIPPlus:
    case funcBeepLevelLimit:
    case funcBeepConfirmation:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    case funcToneSquelchType:
    {
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        RptrAccessData r;
        r.accessMode = static_cast<rptAccessTxRx_t>(bcdHexToUChar(payloadIn.at(0)));
        r.useSecondaryVFO = static_cast<bool>(vfo);
        value.setValue(r);
        return ReplyParseResult::Parsed;
    }
    case funcTransceiverId:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        if (!radioCaps.modelID)
        {
            radioCaps.modelID = static_cast<quint16>(payloadIn.at(0)) & 0xff;
            if (radioCaps.modelID == kRadioModelId)
            {
                this->model = kRadioModelId;
            }
            qInfo(logRadio()).noquote()
                << QStringLiteral("Directed CI-V identity confirmed radioId=0x%1").arg(radioCaps.modelID, 2, 16);
            determineRadioCaps();
        }
        value.setValue(radioCaps.modelID);
        return ReplyParseResult::Parsed;
    case funcFilterWidth:
    {
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        quint16 calc;
        quint8 pass = bcdHexToUChar((quint8)payloadIn.at(0));
        VfoCommandType t = queue->getVfoCommand(vfoA, receiver, false);
        ModeInfo m = queue->peekCache(t.modeFunc, t.receiver).value.value<ModeInfo>();

        if (m.mk == modeAM)
        {
            calc = 200 + (pass * 200);
        }
        else if (pass <= 10)
        {
            calc = 50 + (pass * 50);
        }
        else
        {
            calc = 600 + ((pass - 10) * 100);
        }
        value.setValue(calc);
        return ReplyParseResult::Parsed;
    }
    case funcAFMute:
        qWarning(logRadio()).noquote() << "AF mute response parsing is not implemented";
        return ReplyParseResult::Parsed;
    // CI-V 1A05 two-byte level registers.
    case funcREFAdjust:
    case funcREFAdjustFine:
    case funcACCAModLevel:
    case funcACCBModLevel:
    case funcUSBModLevel:
    case funcLANModLevel:
    case funcSPDIFModLevel:
    case funcNBWidth:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0), payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    // Single byte returned as uchar (0-99).
    case funcDATAOffMod:
    case funcDATA1Mod:
    case funcDATA2Mod:
    case funcDATA3Mod:
    {
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        const auto inputIt =
            std::find_if(radioCaps.inputs.cbegin(), radioCaps.inputs.cend(),
                         [this](const radioInput& input) { return input.reg == bcdHexToUChar(payloadIn.at(0)); });
        if (inputIt != radioCaps.inputs.cend())
        {
            value.setValue(*inputIt);
        }
        return ReplyParseResult::Parsed;
    }
    case funcDashRatio:
    case funcNBDepth:
    case funcVOXDelay:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    case funcUTCOffset:
    case funcDate:
    case funcTime:
        return ReplyParseResult::Parsed;
    // Fixed-frequency scope edge payloads.
    case funcScopeEdge1a:
    case funcScopeEdge2a:
    case funcScopeEdge3a:
    case funcScopeEdge4a:
    case funcScopeEdge1b:
    case funcScopeEdge2b:
    case funcScopeEdge3b:
    case funcScopeEdge4b:
    case funcScopeEdge1c:
    case funcScopeEdge2c:
    case funcScopeEdge3c:
    case funcScopeEdge4c:
    case funcScopeEdge1d:
    case funcScopeEdge2d:
    case funcScopeEdge3d:
    case funcScopeEdge4d:
    case funcScopeEdge1e:
    case funcScopeEdge2e:
    case funcScopeEdge3e:
    case funcScopeEdge4e:
    case funcScopeEdge1f:
    case funcScopeEdge2f:
    case funcScopeEdge3f:
    case funcScopeEdge4f:
    case funcScopeEdge1g:
    case funcScopeEdge2g:
    case funcScopeEdge3g:
    case funcScopeEdge4g:
    case funcScopeEdge1h:
    case funcScopeEdge2h:
    case funcScopeEdge3h:
    case funcScopeEdge4h:
    case funcScopeEdge1i:
    case funcScopeEdge2i:
    case funcScopeEdge3i:
    case funcScopeEdge4i:
    case funcScopeEdge1j:
    case funcScopeEdge2j:
    case funcScopeEdge3j:
    case funcScopeEdge4j:
    case funcScopeEdge1k:
    case funcScopeEdge2k:
    case funcScopeEdge3k:
    case funcScopeEdge4k:
    case funcScopeEdge1l:
    case funcScopeEdge2l:
    case funcScopeEdge3l:
    case funcScopeEdge4l:
    case funcScopeEdge1m:
    case funcScopeEdge2m:
    case funcScopeEdge3m:
    case funcScopeEdge4m:
    case funcScopeEdge1n:
    case funcScopeEdge2n:
    case funcScopeEdge3n:
    case funcScopeEdge4n:
    case funcScopeEdge1o:
    case funcScopeEdge2o:
    case funcScopeEdge3o:
    case funcScopeEdge4o:
    case funcScopeEdge1p:
    case funcScopeEdge2p:
    case funcScopeEdge3p:
    case funcScopeEdge4p:
    case funcScopeEdge1q:
    case funcScopeEdge2q:
    case funcScopeEdge3q:
    case funcScopeEdge4q:
    case funcScopeEdge1r:
    case funcScopeEdge2r:
    case funcScopeEdge3r:
    case funcScopeEdge4r:
    case funcScopeEdge1s:
    case funcScopeEdge2s:
    case funcScopeEdge3s:
    case funcScopeEdge4s:
        return ReplyParseResult::Parsed;
    // CI-V 1B: tone and squelch code registers.
    case funcToneFreq:
    case funcTSQLFreq:
    case funcDTCSCode:
    case funcCSQLCode:
        value.setValue(decodeTone(payloadIn));
        return ReplyParseResult::Parsed;
    // CI-V 1C: boolean status registers.
    case funcRitStatus:
    case funcTransceiverStatus:
    case funcXFCStatus:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    case funcTunerStatus:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    // CI-V 21: RIT offset register.
    // IC-9700 format: 2 BCD bytes (offset Hz) + 1 sign byte (0=+, 1=-).
    case funcRitFreq:
    {
        if (replyPayloadTooShort(func, 3))
        {
            return ReplyParseResult::Malformed;
        }
        Frequency f;
        QByteArray longfreq = payloadIn.mid(0, 2);
        longfreq.append(QByteArray(3, '\x00'));
        f = parseFrequency(longfreq, 3);
        const short ritHz = static_cast<short>(f.Hz) * ((payloadIn.at(2) == '\x01') ? -1 : 1);
        value.setValue(ritHz);
        return ReplyParseResult::Parsed;
    }
    case funcRitTXStatus:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    case funcTXFreqMon:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    default:
        return ReplyParseResult::NotHandled;
    }
}

Commander::ReplyParseResult Commander::parseScopeReply(Funcs func, QVariant& value, uchar& receiver)
{
    switch (func)
    {
    case funcScopeWaveData:
    {
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        payloadIn.remove(0, 1);
        ScopeData d;
        if (parseSpectrum(d, receiver))
        {
            value.setValue(d);
        }
        return ReplyParseResult::Parsed;
    }
    case funcScopeOnOff:
    case funcScopeDataOutput:
    case funcScopeMainSub:
    case funcScopeSingleDual:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        return ReplyParseResult::Parsed;
    case funcScopeMode:
        // Scope mode: 0x00=center, 0x01=fixed, 0x02=scroll-C, 0x03=scroll-F.
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    case funcScopeSpan:
    {
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        payloadIn.remove(0, 1);
        Frequency f = parseFrequency(payloadIn, 3);
        for (auto& s : radioCaps.scopeCenterSpans)
        {
            if (s.freq == f.Hz)
            {
                value.setValue(s);
            }
        }
        return ReplyParseResult::Parsed;
    }
    case funcScopeEdge:
        // Fixed edge selection: 0x01, 0x02, or 0x03.
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        value.setValue(bcdHexToUChar(payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    case funcBandEdgeFreq:
        // Band-edge payload is currently not surfaced by SDR9700.
        return ReplyParseResult::Parsed;
    case funcScopeHold:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        value.setValue(static_cast<bool>(payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    case funcScopeRef:
    {
        if (replyPayloadTooShort(func, 4))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        // Scope reference level: BCD dB value plus sign byte.
        quint8 negative = payloadIn.at(3);
        short ref = bcdHexToUInt(payloadIn.at(1), payloadIn.at(2));
        ref = ref / 10;
        if (negative)
        {
            ref = -ref;
        }
        value.setValue(ref);
        return ReplyParseResult::Parsed;
    }
    case funcScopeSpeed:
        if (replyPayloadTooShort(func, 2))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        return ReplyParseResult::Parsed;
    case funcScopeVBW:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        return ReplyParseResult::Parsed;
    case funcScopeRBW:
        if (replyPayloadTooShort(func, 1))
        {
            return ReplyParseResult::Malformed;
        }
        receiver = payloadIn.at(0);
        return ReplyParseResult::Parsed;
    case funcScopeFixedEdgeFreq:
    case funcScopeDuringTX:
    case funcScopeCenterType:
        return ReplyParseResult::Parsed;
    case funcVoiceTX:
        return ReplyParseResult::Parsed;
    default:
        return ReplyParseResult::NotHandled;
    }
}

void Commander::parseCommand(FrameOrigin origin)
{
#ifdef DEBUG_PARSE
    QElapsedTimer performanceTimer;
    performanceTimer.start();
#endif

    Funcs func = funcNone;
    uchar receiver = 0;
    if (payloadIn.endsWith((char)0xfd))
    {
        payloadIn.chop(1);
    }

    if (radioCaps.hasCommand29 && payloadIn.size() >= 2 && payloadIn.at(0) == '\x29')
    {
        receiver = static_cast<uchar>(payloadIn.at(1));
        payloadIn.remove(0, 2);
    }

    // Some CI-V commands have both single-byte and multi-byte forms. Match the
    // longest command first so multi-byte commands are not split early.
    int count = 0;
    for (int i = 4; i > 0; i--)
    {
        auto it = radioCaps.commandsReverse.find(payloadIn.left(i));
        if (it != radioCaps.commandsReverse.end())
        {
            func = it.value();
            count = i;
            break;
        }
    }

    // Remove the matched command prefix; only payload bytes remain.
    payloadIn.remove(0, count);

#ifdef DEBUG_PARSE
    int currentParse = performanceTimer.nsecsElapsed();
#endif

    if (!radioCaps.commands.contains(func))
    {
        // Capability detection may still be in progress.
        if (haveRadioCaps)
        {
            qInfo(logRadio()).noquote() << "Unsupported command received from radio" << payloadIn.toHex().mid(0, 10)
                                        << "Check radio file";
        }
        return;
    }

    const Funcs correlationFunc = func;
    const bool explicitlyIdentifiesReceiver = radioCaps.hasCommand29 || replyDoesNotRequireReceiverCorrelation(func);
    bool pendingCorrelationFound = false;

    // When CI-V 29h is unavailable, most IC-9700 replies do not identify MAIN
    // or SUB. Prefer the receiver from the matching outstanding request; fall
    // back to the current selected receiver for unsolicited updates.
    if (!explicitlyIdentifiesReceiver)
    {
        uchar pendingReceiver = 0;
        pendingCorrelationFound =
            origin == FrameOrigin::SolicitedReply && pendingReplyReceiver(correlationFunc, &pendingReceiver);
        receiver = pendingCorrelationFound ? pendingReceiver : queue->getState().receiver;
    }

    QVector<MemParserFormat> memParser;
    QVariant value;
    auto payloadTooShort = [&](int requiredBytes) -> bool
    {
        if (payloadIn.size() >= requiredBytes)
        {
            return false;
        }

        qWarning(logRadio()).noquote() << "Ignoring short CI-V payload for" << funcString[func] << "required"
                                       << requiredBytes << "got" << payloadIn.size() << "data:" << payloadIn.toHex(' ');
        return true;
    };

    switch (func)
    {
    case funcVFODualWatch:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(bool(payloadIn.at(0))));
        break;
    case funcFreq:
    case funcSelectedFreq:
    case funcUnselectedFreq:
    case funcFreqGet:
    case funcFreqTR:
    case funcTXFreq:
        if (parseFrequencyReply(func, value, receiver) == ReplyParseResult::Malformed)
        {
            return;
        }
        break;
    case funcMode:
    case funcModeGet:
    case funcModeTR:
    case funcSelectedMode:
    case funcUnselectedMode:
    case funcDataModeWithFilter:
        if (parseModeReply(func, value, receiver) == ReplyParseResult::Malformed)
        {
            return;
        }
        break;

    case funcVFOBandMS:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcMemoryMode:
        qInfo(logRadio()).noquote() << "Memory Mode command!";
        break;
    case funcSatelliteMemory:
        memParser = radioCaps.satParser;
        [[fallthrough]];
    case funcMemoryContents:
    {
        qDebug(logRadio()).noquote() << "Received mem:" << payloadIn.toHex(' ');
        MemoryType mem;
        if (memParser.isEmpty())
        {
            memParser = radioCaps.memParser;
            mem.sat = false;
        }
        else
        {
            mem.sat = true;
        }

        parseMemory(&memParser, &mem);
        value.setValue(mem);
        break;
    }
    case funcMemoryClear:
    case funcBandStackReg:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        value.setValue(parseFrequencyRptOffset(payloadIn));
        break;
    // Single-byte BCD values converted to uchar.
    case funcTuningStep:
    case funcAttenuator:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcSplitStatus:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(static_cast<duplexMode_t>(uchar(payloadIn.at(0))));
        break;
    case funcQuickSplit:
        if (payloadTooShort(1))
        {
            return;
        }
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcAntenna:
    {
        if (payloadTooShort(1))
        {
            return;
        }
        AntennaInfo ant;
        ant.rx = false;
        ant.antenna = bcdHexToUChar(payloadIn.at(0));
        if (payloadIn.size() > 1)
        {
            ant.rx = static_cast<bool>(payloadIn.at(1));
        }
        value.setValue(ant);
        break;
    }
    case funcAfGain:
    case funcKeySpeed:
    case funcCwPitch:
    case funcSMeter:
    case funcCenterMeter:
    case funcPowerMeter:
    case funcSWRMeter:
    case funcALCMeter:
    case funcCompMeter:
    case funcVdMeter:
    case funcIdMeter:
    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
    case funcBackLightLevel:
    case funcBeepLevel:
    case funcBeepMain:
    case funcBeepSub:
    case funcRFSQLControl:
    case funcTXDelayHF:
    case funcTXDelay50m:
    case funcTimeOutCIV:
    case funcAGC:
    case funcAGCTimeConstant:
    case funcBreakIn:
    case funcPreamp:
    case funcManualNotchWidth:
    case funcSSBTXBandwidth:
    case funcRoofingFilter:
    case funcFilterShape:
    case funcSSBRXBass:
    case funcSSBRXTreble:
    case funcAMRXBass:
    case funcAMRXTreble:
    case funcFMRXBass:
    case funcFMRXTreble:
    case funcSSBTXBass:
    case funcSSBTXTreble:
    case funcAMTXBass:
    case funcAMTXTreble:
    case funcFMTXBass:
    case funcFMTXTreble:
    case funcTimeOutTimer:
    case funcBandEdgeBeep:
    case funcSSBRXHPFLPF:
    case funcAMRXHPFLPF:
    case funcFMRXHPFLPF:
    case funcCWRXHPFLPF:
    case funcRTTYRXHPFLPF:
    case funcAbsoluteMeter:
    case funcMeterType:
        if (parseLevelMeterReply(func, value) == ReplyParseResult::Malformed)
        {
            return;
        }
        break;
    case funcMainSubTracking:
    case funcSatelliteMode:
    case funcNoiseBlanker:
    case funcAudioPeakFilter:
    case funcNoiseReduction:
    case funcAutoNotch:
    case funcRepeaterTone:
    case funcRepeaterTSQL:
    case funcRepeaterDTCS:
    case funcRepeaterCSQL:
    case funcCompressor:
    case funcMonitor:
    case funcVox:
    case funcManualNotch:
    case funcDigiSel:
    case funcTwinPeakFilter:
    case funcDialLock:
    case funcOverflowStatus:
    case funcSMeterSqlStatus:
    case funcVariousSql:
    case funcRXAntenna:
    case funcIPPlus:
    case funcBeepLevelLimit:
    case funcBeepConfirmation:
    case funcToneSquelchType:
    case funcTransceiverId:
    case funcFilterWidth:
    case funcAFMute:
    case funcREFAdjust:
    case funcREFAdjustFine:
    case funcACCAModLevel:
    case funcACCBModLevel:
    case funcUSBModLevel:
    case funcLANModLevel:
    case funcSPDIFModLevel:
    case funcNBWidth:
    case funcDATAOffMod:
    case funcDATA1Mod:
    case funcDATA2Mod:
    case funcDATA3Mod:
    case funcDashRatio:
    case funcNBDepth:
    case funcVOXDelay:
    case funcUTCOffset:
    case funcDate:
    case funcTime:
    case funcScopeEdge1a:
    case funcScopeEdge2a:
    case funcScopeEdge3a:
    case funcScopeEdge4a:
    case funcScopeEdge1b:
    case funcScopeEdge2b:
    case funcScopeEdge3b:
    case funcScopeEdge4b:
    case funcScopeEdge1c:
    case funcScopeEdge2c:
    case funcScopeEdge3c:
    case funcScopeEdge4c:
    case funcScopeEdge1d:
    case funcScopeEdge2d:
    case funcScopeEdge3d:
    case funcScopeEdge4d:
    case funcScopeEdge1e:
    case funcScopeEdge2e:
    case funcScopeEdge3e:
    case funcScopeEdge4e:
    case funcScopeEdge1f:
    case funcScopeEdge2f:
    case funcScopeEdge3f:
    case funcScopeEdge4f:
    case funcScopeEdge1g:
    case funcScopeEdge2g:
    case funcScopeEdge3g:
    case funcScopeEdge4g:
    case funcScopeEdge1h:
    case funcScopeEdge2h:
    case funcScopeEdge3h:
    case funcScopeEdge4h:
    case funcScopeEdge1i:
    case funcScopeEdge2i:
    case funcScopeEdge3i:
    case funcScopeEdge4i:
    case funcScopeEdge1j:
    case funcScopeEdge2j:
    case funcScopeEdge3j:
    case funcScopeEdge4j:
    case funcScopeEdge1k:
    case funcScopeEdge2k:
    case funcScopeEdge3k:
    case funcScopeEdge4k:
    case funcScopeEdge1l:
    case funcScopeEdge2l:
    case funcScopeEdge3l:
    case funcScopeEdge4l:
    case funcScopeEdge1m:
    case funcScopeEdge2m:
    case funcScopeEdge3m:
    case funcScopeEdge4m:
    case funcScopeEdge1n:
    case funcScopeEdge2n:
    case funcScopeEdge3n:
    case funcScopeEdge4n:
    case funcScopeEdge1o:
    case funcScopeEdge2o:
    case funcScopeEdge3o:
    case funcScopeEdge4o:
    case funcScopeEdge1p:
    case funcScopeEdge2p:
    case funcScopeEdge3p:
    case funcScopeEdge4p:
    case funcScopeEdge1q:
    case funcScopeEdge2q:
    case funcScopeEdge3q:
    case funcScopeEdge4q:
    case funcScopeEdge1r:
    case funcScopeEdge2r:
    case funcScopeEdge3r:
    case funcScopeEdge4r:
    case funcScopeEdge1s:
    case funcScopeEdge2s:
    case funcScopeEdge3s:
    case funcScopeEdge4s:
    case funcToneFreq:
    case funcTSQLFreq:
    case funcDTCSCode:
    case funcCSQLCode:
    case funcRitStatus:
    case funcTransceiverStatus:
    case funcXFCStatus:
    case funcTunerStatus:
    case funcRitFreq:
    case funcRitTXStatus:
    case funcTXFreqMon:
        if (parseFeatureReply(func, value, receiver) == ReplyParseResult::Malformed)
        {
            return;
        }
        break;
    case funcScopeWaveData:
    case funcScopeOnOff:
    case funcScopeDataOutput:
    case funcScopeMainSub:
    case funcScopeSingleDual:
    case funcScopeMode:
    case funcScopeSpan:
    case funcScopeEdge:
    case funcBandEdgeFreq:
    case funcScopeHold:
    case funcScopeRef:
    case funcScopeSpeed:
    case funcScopeVBW:
    case funcScopeRBW:
    case funcScopeFixedEdgeFreq:
    case funcScopeDuringTX:
    case funcScopeCenterType:
    case funcVoiceTX:
        if (parseScopeReply(func, value, receiver) == ReplyParseResult::Malformed)
        {
            return;
        }
        break;
    // CI-V command 29h prefixes receiver-scoped commands.
    case funcMainSubPrefix:
        break;
    case funcPowerControl:
        qWarning(logRadio()).noquote() << "Received power control command from radio" << payloadIn;
        break;
    case funcFB:
    {
        ++m_correlationDiagnostics.acceptedAcknowledgements;
        break;
    }
    case funcFA:
    {
        ++m_correlationDiagnostics.rejectedAcknowledgements;
        qWarning(logRadio()).noquote() << "Radio rejected a CI-V command (FA)";
        break;
    }
    default:
        qWarning(logRadio()).noquote() << "Unhandled command received from radio:" << funcString[func]
                                       << "value:" << payloadIn.toHex().mid(0, 10);
        break;
    }
    if (logRadioTraffic().isDebugEnabled() && func != funcScopeWaveData && func != funcSMeter &&
        func != funcAbsoluteMeter && func != funcCenterMeter && func != funcPowerMeter && func != funcSWRMeter &&
        func != funcALCMeter && func != funcCompMeter && func != funcVdMeter && func != funcIdMeter)
    {
        // Spectrum and meter replies are high-volume and obscure useful traffic.
        QString commandName = logToken(funcString[func]);
        QByteArray decodedData = payloadIn;
        if (func == funcFB)
        {
            commandName = QStringLiteral("CommandOK");
            decodedData.clear();
        }
        else if (func == funcFA)
        {
            commandName = QStringLiteral("CommandError");
            decodedData.clear();
        }

        QString message = QStringLiteral("CivData::Decoded command=%1").arg(commandName);
        if (!decodedData.isEmpty())
        {
            message.append(QStringLiteral(" data=%1").arg(QString::fromLatin1(decodedData.toHex(' '))));
        }
        qDebug(logRadioTraffic()).noquote() << message;
    }

#ifdef DEBUG_PARSE
    averageParseTime += currentParse;
    if (lowParse > currentParse)
    {
        lowParse = currentParse;
    }
    else if (highParse < currentParse)
    {
        highParse = currentParse;
    }

    numParseSamples++;
    if (lastParseReport.msecsTo(QTime::currentTime()) >= 10000)
    {
        qInfo(logRadio()).noquote() << QString("10 second average command parse time %1 ns (low=%2, high=%3, num=%4:")
                                           .arg(averageParseTime / numParseSamples)
                                           .arg(lowParse)
                                           .arg(highParse)
                                           .arg(numParseSamples);
        averageParseTime = 0;
        numParseSamples = 0;
        lowParse = 9999;
        highParse = 0;
        lastParseReport = QTime::currentTime();
    }
#endif

    if (origin == FrameOrigin::SolicitedReply && pendingCorrelationFound)
    {
        uchar consumedReceiver = 0;
        if (!takePendingReplyReceiver(correlationFunc, &consumedReceiver) || consumedReceiver != receiver)
        {
            ++m_correlationDiagnostics.correlationLostDuringParse;
            qCritical(logRadio()).noquote()
                << "CI-V pending reply correlation changed while parsing" << funcString[correlationFunc];
            return;
        }
    }

    if (correlationFunc == funcSMeter && pendingCorrelationFound)
    {
        if (receiver == 1)
        {
            finishSMeterRead();
            ++m_correlationDiagnostics.subSMeterReplies;
        }
        else
        {
            ++m_correlationDiagnostics.mainSMeterReplies;
        }
    }

    const bool ambiguousFrequencyOrModeBroadcast =
        origin == FrameOrigin::UnsolicitedBroadcast && !explicitlyIdentifiesReceiver &&
        (correlationFunc == funcFreq || correlationFunc == funcFreqTR || correlationFunc == funcFreqGet ||
         correlationFunc == funcMode || correlationFunc == funcModeTR || correlationFunc == funcModeGet ||
         correlationFunc == funcDataModeWithFilter);
    if (ambiguousFrequencyOrModeBroadcast)
    {
        ++m_correlationDiagnostics.ambiguousUnsolicitedFrames;
        qWarning(logRadio()).noquote() << "Ignoring receiver-ambiguous unsolicited CI-V value"
                                       << funcString[correlationFunc];
        return;
    }
    if (origin == FrameOrigin::SolicitedReply && !explicitlyIdentifiesReceiver && !pendingCorrelationFound &&
        value.isValid())
    {
        ++m_correlationDiagnostics.unattributedDirectedValues;
        if (replyFamilyDraining(correlationFunc))
        {
            ++m_correlationDiagnostics.drainedReplyFrames;
            qWarning(logRadio()).noquote().nospace()
                << "Discarding unattributed receiver-less CI-V reply func=" << funcString[correlationFunc];
            return;
        }
    }

    if (value.isValid() && queue != nullptr)
    {
        emit radioReplyReceived(func, value, receiver);
        queue->receiveValue(func, value, receiver);
    }
}

void Commander::determineRadioCaps()
{
    // Reset capability state before loading the built-in IC-9700 table.
    radioCaps.preamps.clear();
    radioCaps.attenuators.clear();
    radioCaps.inputs.clear();
    radioCaps.scopeCenterSpans.clear();
    radioCaps.bands.clear();
    radioCaps.modes.clear();
    radioCaps.commands.clear();
    radioCaps.commandsReverse.clear();
    radioCaps.antennas.clear();
    radioCaps.filters.clear();
    radioCaps.steps.clear();
    radioCaps.memParser.clear();
    radioCaps.satParser.clear();
    radioCaps.periodic.clear();
    radioCaps.roofing.clear();
    radioCaps.scopeModes.clear();

    for (int i = meterNone; i < meterUnknown; i++)
    {
        radioCaps.meters[i].clear();
        radioCaps.meterLines[i] = 0.0;
    }

    // The IC-9700 transceiver ID response normally sets modelID before this point.
    if (radioCaps.modelID != kRadioModelId)
    {
        qWarning(logRadio()).noquote() << QString("Unsupported CI-V radio ID: 0x%1. SDR9700 only supports the IC-9700.")
                                              .arg(radioCaps.modelID, 2, 16);
        return;
    }

    sdr9700::populateRadioCapabilities(radioCaps);

    qInfo(logRadio()).noquote()
        << QString("Loading Radio: %1 from built-in IC-9700 capabilities").arg(radioCaps.modelName);

    // Compile memory parser formats from the built-in IC-9700 table.
    static QRegularExpression memFmtEx("%(?<flags>[-+#0])?(?<pos>\\d+|\\*)?(?:\\.(?<width>\\d+|\\*))?(?<spec>["
                                       "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+])");
    QRegularExpressionMatchIterator i = memFmtEx.globalMatch(radioCaps.memFormat);
    while (i.hasNext())
    {
        QRegularExpressionMatch qmatch = i.next();

        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width"))
        {
            radioCaps.memParser.append(MemParserFormat(qmatch.captured("spec").at(0).toLatin1(),
                                                       qmatch.captured("pos").toInt(),
                                                       qmatch.captured("width").toInt()));
        }
    }

    QRegularExpressionMatchIterator i2 = memFmtEx.globalMatch(radioCaps.satFormat);

    while (i2.hasNext())
    {
        QRegularExpressionMatch qmatch = i2.next();
        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width"))
        {
            radioCaps.satParser.append(MemParserFormat(qmatch.captured("spec").at(0).toLatin1(),
                                                       qmatch.captured("pos").toInt(),
                                                       qmatch.captured("width").toInt()));
        }
    }

    // Preserve the GUID reported by the selected radio.
    memcpy(radioCaps.guid, this->guid, GUIDLEN);

    haveRadioCaps = true;
    queue->setRadioCaps(&radioCaps);

    if (lookingForRadio)
    {
        lookingForRadio = false;

        qDebug(logRadio()).noquote() << "---Radio FOUND from broadcast query:";
        this->civAddr = incomingCIVAddr & 0xff; // Override and use immediately.
        payloadPrefix = QByteArray("\xFE\xFE");
        payloadPrefix.append((char)civAddr);
        payloadPrefix.append((char)compCivAddr);
        qInfo(logRadio()).noquote() << "Using incomingCIVAddr: (int): " << this->civAddr
                                    << " hex: " << QString("0x%1").arg(this->civAddr, 0, 16);
    }
}

bool Commander::decodeSpectrumSequence(quint8& sequence, quint8& sequenceMax) const
{
    if (payloadIn.size() < 2)
    {
        qWarning(logSpectrumScope()).noquote() << "Ignoring short scope payload:" << payloadIn.toHex(' ');
        return false;
    }
    sequence = bcdHexToUChar(payloadIn.at(0));
    sequenceMax = bcdHexToUChar(payloadIn.at(1));
    return true;
}

bool Commander::parseSpectrum(ScopeData& d, uchar receiver)
{
    bool ret = false;

    if (!haveRadioCaps)
    {
        qDebug(logSpectrumScope()).noquote() << "Spectrum received in Commander, but radioID is incomplete.";
        return ret;
    }
    if (radioCaps.spectSeqMax == 0)
    {
        qInfo(logSpectrumScope()).noquote() << "Spectrum received before IC-9700 scope capabilities were ready.";
        return ret;
    }

    if (receiver)
    {
        d = subScopeData;
    }
    else
    {
        d = mainScopeData;
    }

    // Scope data may arrive as one LAN frame or as a sequence of wave-info plus
    // waveform chunks. The first bytes describe the sequence layout.

    Frequency fStart;
    Frequency fEnd;

    d.receiver = receiver;
    quint8 sequence = 0;
    quint8 sequenceMax = 0;
    if (!decodeSpectrumSequence(sequence, sequenceMax))
    {
        return false;
    }
    const int assemblyIndex = receiver ? 1 : 0;

    if (sequenceMax > radioCaps.spectSeqMax)
    {
        qWarning(logSpectrumScope()).noquote().nospace()
            << "Ignoring scope sequence outside radio capabilities receiver=" << receiver << " sequence=" << sequence
            << " sequenceMax=" << sequenceMax << " supportedMax=" << radioCaps.spectSeqMax;
        m_expectedScopeSequences[assemblyIndex] = 0;
        d.data.clear();
        d.valid = false;
        return false;
    }

    // Append only pixels that fit the IC-9700's advertised completed-frame
    // size. Limiting the slice before QByteArray creates it avoids a transient
    // allocation proportional to an untrusted UDP payload, not merely an
    // oversized retained ScopeData buffer.
    const auto appendScopePixels = [this, &d](int payloadOffset)
    {
        const int remaining = qMax(0, static_cast<int>(radioCaps.spectLenMax) - d.data.size());
        if (remaining > 0 && payloadOffset < payloadIn.size())
        {
            d.data.append(payloadIn.mid(payloadOffset, remaining));
        }
    };

    constexpr int freqLen = 5;
    constexpr int sequenceHeaderBytes = 2;
    constexpr int waveInfoBytes = sequenceHeaderBytes + 2 + (freqLen * 2);
    auto logCompletedFrame = [&](int sequenceCount)
    {
        QElapsedTimer& diagnosticClock = m_scopeDiagnosticClocks[assemblyIndex];
        if (!logSpectrumScope().isDebugEnabled() || (diagnosticClock.isValid() && diagnosticClock.elapsed() < 1000))
        {
            return;
        }
        diagnosticClock.restart();
        qDebug(logSpectrumScope()).noquote()
            << "Spectrum frame sequences:" << sequenceCount << "start:" << d.startFreq << "end:" << d.endFreq
            << "mode:" << d.mode << "oor:" << d.oor << "dataLen:" << d.data.size();
    };

    if (sequenceMax <= 1)
    {
        m_expectedScopeSequences[assemblyIndex] = 0;
        if (payloadIn.size() < waveInfoBytes)
        {
            qWarning(logSpectrumScope()).noquote()
                << "Ignoring short single-frame scope payload. required" << waveInfoBytes << "got" << payloadIn.size()
                << "data:" << payloadIn.toHex(' ');
            return false;
        }

        d.mode = static_cast<uchar>(payloadIn.at(sequenceHeaderBytes));
        d.oor = static_cast<bool>(payloadIn.at(sequenceHeaderBytes + 1 + (freqLen * 2)));
        d.data.clear();

        fStart = parseFreqData(payloadIn.mid(sequenceHeaderBytes + 1, freqLen), receiver);
        fEnd = parseFreqData(payloadIn.mid(sequenceHeaderBytes + 1 + freqLen, freqLen), receiver);
        if (d.mode == 0)
        {
            // IC-9700 center-scope wave-info reports the center frequency and
            // the selected half-span (for example, the "+/-500 kHz" span is
            // reported as 0.500 MHz). Treating this as total width and halving
            // it makes every displayed signal appear halfway between the VFO
            // center and its real frequency, which in turn makes click-to-tune
            // walk halfway toward a signal on every click.
            const double halfSpanMhz = fEnd.MHzDouble;
            d.startFreq = fStart.MHzDouble - halfSpanMhz;
            d.endFreq = fStart.MHzDouble + halfSpanMhz;
        }
        else
        {
            d.startFreq = fStart.MHzDouble;
            d.endFreq = fEnd.MHzDouble;
        }

        if (d.oor)
        {
            d.data = QByteArray(radioCaps.spectLenMax, '\0');
            d.valid = true;
            return true;
        }

        d.data = payloadIn.mid(waveInfoBytes);
        if (d.data.size() > radioCaps.spectLenMax)
        {
            d.data.truncate(radioCaps.spectLenMax);
        }
        ret = !d.data.isEmpty();
        d.valid = ret;
        logCompletedFrame(1);
        return ret;
    }

    // Sequence 2, index 05 starts waveform data. Sequence 11 carries the final
    // waveform pixels seen from IC-9700 LAN scope data.
    if (sequence == 1)
    {
        const int multiFrameWaveInfoBytes = 4 + (freqLen * 2);
        if (payloadIn.size() < multiFrameWaveInfoBytes)
        {
            qWarning(logSpectrumScope()).noquote()
                << "Ignoring short scope wave-info payload. required" << multiFrameWaveInfoBytes << "got"
                << payloadIn.size() << "data:" << payloadIn.toHex(' ');
            return false;
        }

        d.mode = static_cast<uchar>(payloadIn.at(2));

        if (d.mode != oldScopeMode)
        {
            // Modes:
            // 0x00 Center
            // 0x01 Fixed
            // 0x02 Scroll-C
            // 0x03 Scroll-F
            oldScopeMode = d.mode;
        }

        d.oor = (bool)payloadIn[3 + (freqLen * 2)];
        if (d.oor)
        {
            d.data = QByteArray(radioCaps.spectLenMax, '\0');
            d.valid = true;
            return true;
        }

        d.data.clear();
        m_scopeAssemblyClocks[assemblyIndex].restart();
        m_expectedScopeSequences[assemblyIndex] = 2;

        // The first two frequency fields are mode-dependent. In fixed/scroll
        // modes they are explicit start/end frequencies; in center mode they
        // are center frequency and half-span, matching the single-frame parser.
        fStart = parseFreqData(payloadIn.mid(3, freqLen), receiver);
        d.startFreq = fStart.MHzDouble;
        fEnd = parseFreqData(payloadIn.mid(3 + freqLen, freqLen), receiver);
        d.endFreq = fEnd.MHzDouble;

        if (d.mode == 0)
        {
            // In center mode the second frequency field is the half-span, not
            // the right edge or total span. See the single-frame path above.
            const double halfSpanMhz = d.endFreq;
            d.startFreq -= halfSpanMhz;
            d.endFreq = d.startFreq + (2 * halfSpanMhz);
        }
    }
    else if ((sequence > 1) && (sequence < sequenceMax))
    {
        if (!m_scopeAssemblyClocks[assemblyIndex].isValid() ||
            m_scopeAssemblyClocks[assemblyIndex].elapsed() > kScopeAssemblyLifetimeMs ||
            sequence != m_expectedScopeSequences[assemblyIndex])
        {
            qWarning(logSpectrumScope()).noquote().nospace()
                << "Discarding incomplete scope assembly receiver=" << receiver
                << " expected=" << m_expectedScopeSequences[assemblyIndex] << " received=" << sequence;
            m_expectedScopeSequences[assemblyIndex] = 0;
            d.data.clear();
            d.valid = false;
            return false;
        }
        // Intermediate scope chunks carry 50 pixels each.
        appendScopePixels(2);
        m_expectedScopeSequences[assemblyIndex] = sequence + 1;
        ret = false;
    }
    else if (sequence == sequenceMax)
    {
        if (!m_scopeAssemblyClocks[assemblyIndex].isValid() ||
            m_scopeAssemblyClocks[assemblyIndex].elapsed() > kScopeAssemblyLifetimeMs ||
            sequence != m_expectedScopeSequences[assemblyIndex])
        {
            qWarning(logSpectrumScope()).noquote().nospace()
                << "Discarding incomplete scope assembly receiver=" << receiver
                << " expected=" << m_expectedScopeSequences[assemblyIndex] << " received=" << sequence;
            m_expectedScopeSequences[assemblyIndex] = 0;
            d.data.clear();
            d.valid = false;
            return false;
        }
        // Final IC-9700 scope chunk carries the remaining waveform pixels.
        appendScopePixels(2);
        m_expectedScopeSequences[assemblyIndex] = 0;
        ret = true;
        logCompletedFrame(sequenceMax);
    }
    d.valid = ret;

    if (!ret)
    {
        // Store partial multi-packet scope data until the final sequence arrives.
        if (receiver)
        {
            subScopeData = d;
        }
        else
        {
            mainScopeData = d;
        }
    }
    return ret;
}

quint8 Commander::bcdHexToUChar(quint8 in)
{
    quint8 out = 0;
    out = in & 0x0f;
    out += ((in & 0xf0) >> 4) * 10;
    return out;
}

unsigned int Commander::bcdHexToUInt(quint8 hundreds, quint8 tensunits)
{
    // Packed BCD: 0x41 0x23 -> 4123.
    quint8 thousands = ((hundreds & 0xf0) >> 4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f) * 100;
    rtnVal += ((tensunits & 0xf0) >> 4) * 10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;

    return rtnVal;
}

unsigned int Commander::bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits)
{
    // Packed BCD: 0x65 0x43 0x21 -> 654321.
    quint8 thousands = ((hundreds & 0xf0) >> 4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f) * 100;
    rtnVal += ((tensunits & 0xf0) >> 4) * 10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;
    rtnVal += (tenthou & 0x0f) * 10000;
    rtnVal += ((tenthou & 0xf0) >> 4) * 100000;
    return rtnVal;
}
quint8 Commander::bcdHexToUChar(quint8 hundreds, quint8 tensunits)
{
    // Packed BCD: 0x01 0x23 -> 123. Use int to avoid truncating the
    // intermediate (hundreds & 0x0f) * 100 before the full sum is known.
    const int rtnVal = (hundreds & 0x0f) * 100 + ((tensunits & 0xf0) >> 4) * 10 + (tensunits & 0x0f);
    if (rtnVal > 255)
    {
        qWarning(logRadio()).noquote() << "bcdHexToUChar: decoded value" << rtnVal << "exceeds quint8 range; clamping";
    }
    return static_cast<quint8>(qMin(rtnVal, 255));
}

QByteArray Commander::bcdEncodeInt(quint16 num)
{
    if (num > 9999)
    {
        qInfo(logRadio()).noquote() << __FUNCTION__ << "Error, number is too big for four-digit conversion: " << num;
        return QByteArray();
    }

    char thousands = num / 1000;
    char hundreds = (num - (1000 * thousands)) / 100;
    char tens = (num - (1000 * thousands) - (100 * hundreds)) / 10;
    char units = (num - (1000 * thousands) - (100 * hundreds) - (10 * tens));

    char b0 = hundreds | (thousands << 4);
    char b1 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1);
    return result;
}

QByteArray Commander::bcdEncodeInt(unsigned int num)
{
    if (num > 999999)
    {
        qInfo(logRadio()).noquote() << __FUNCTION__ << "Error, number is too big for six-digit conversion: " << num;
        return QByteArray();
    }

    char tenthou = num / 10000;
    char thousands = (num - (10000 * tenthou)) / 1000;
    char hundreds = (num - (10000 * tenthou) - (1000 * thousands)) / 100;
    char tens = (num - (10000 * tenthou) - (1000 * thousands) - (100 * hundreds)) / 10;
    char units = (num - (10000 * tenthou) - (1000 * thousands) - (100 * hundreds) - (10 * tens));

    char b0 = static_cast<char>(((tenthou / 10) << 4) | (tenthou % 10));
    char b1 = hundreds | (thousands << 4);
    char b2 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1).append(b2);
    qInfo(logRadio()).noquote() << __FUNCTION__ << " encoding value " << num << " as hex:" << result.toHex(' ');

    return result;
}
QByteArray Commander::bcdEncodeChar(quint8 num)
{
    if (num > 99)
    {
        qInfo(logRadio()).noquote() << __FUNCTION__ << "Error, number is too big for two-digit conversion: " << num;
        return QByteArray();
    }

    uchar tens = num / 10;
    uchar units = num - (10 * tens);

    uchar b0 = units | (tens << 4);

    QByteArray result;
    result.append(b0);
    return result;
}

Frequency Commander::parseFrequency()
{
    Frequency freq;
    freq.Hz = 0;
    freq.MHzDouble = 0;

    // Minimum meaningful payload is 5 bytes (indices 1-4, octal literals).
    if (payloadIn.length() < 5)
    {
        qWarning(logRadio()).noquote() << "parseFrequency(): payload too short:" << payloadIn.length();
        return freq;
    }

    frequencyMhz = 0.0;
    if (payloadIn.length() == 7)
    {
        // IC-9700 can report 100 MHz and 1 GHz digits.
        frequencyMhz += 100 * (payloadIn[05] & 0x0f);
        frequencyMhz += (1000 * ((payloadIn[05] & 0xf0) >> 4));

        freq.Hz += (payloadIn[05] & 0x0f) * 1E6 * 100;
        freq.Hz += ((payloadIn[05] & 0xf0) >> 4) * 1E6 * 1000;
    }

    freq.Hz += (payloadIn[04] & 0x0f) * 1E6;
    freq.Hz += ((payloadIn[04] & 0xf0) >> 4) * 1E6 * 10;

    frequencyMhz += payloadIn[04] & 0x0f;
    frequencyMhz += 10 * ((payloadIn[04] & 0xf0) >> 4);

    frequencyMhz += ((payloadIn[03] & 0xf0) >> 4) / 10.0;
    frequencyMhz += (payloadIn[03] & 0x0f) / 100.0;

    frequencyMhz += ((payloadIn[02] & 0xf0) >> 4) / 1000.0;
    frequencyMhz += (payloadIn[02] & 0x0f) / 10000.0;

    frequencyMhz += ((payloadIn[01] & 0xf0) >> 4) / 100000.0;
    frequencyMhz += (payloadIn[01] & 0x0f) / 1000000.0;

    freq.Hz += payloadIn[01] & 0x0f;
    freq.Hz += ((payloadIn[01] & 0xf0) >> 4) * 10;

    freq.Hz += (payloadIn[02] & 0x0f) * 100;
    freq.Hz += ((payloadIn[02] & 0xf0) >> 4) * 1000;

    freq.Hz += (payloadIn[03] & 0x0f) * 10000;
    freq.Hz += ((payloadIn[03] & 0xf0) >> 4) * 100000;

    freq.MHzDouble = frequencyMhz;

    return freq;
}

Frequency Commander::parseFrequencyRptOffset(QByteArray data)
{
    Frequency f;
    f.Hz = 0;
    f.MHzDouble = 0.0;
    f.VFO = activeVFO;

    if (data.size() < 3)
    {
        qWarning(logRadio()).noquote() << "Repeater offset response too short:" << data.toHex(' ');
        return f;
    }

    f.Hz += (data[2] & 0x0f) * 1E6;             // 1 MHz
    f.Hz += ((data[2] & 0xf0) >> 4) * 1E6 * 10; // 10 MHz
    f.Hz += (data[1] & 0x0f) * 10E3;            // 10 KHz
    f.Hz += ((data[1] & 0xf0) >> 4) * 100E3;    // 100 KHz
    f.Hz += (data[0] & 0x0f) * 100;             // 100 Hz
    f.Hz += ((data[0] & 0xf0) >> 4) * 1000;     // 1 KHz

    f.MHzDouble = f.Hz / 1E6;
    return f;
}

Frequency Commander::parseFrequency(QByteArray data, quint8 lastPosition)
{
    // IC-9700 frequencies can reach 1240 MHz, so 100 MHz and 1 GHz digits occupy position +1.

    Frequency freqs;
    freqs.MHzDouble = 0;
    freqs.Hz = 0;

    if (data.length() <= lastPosition)
    {
        qWarning(logRadio()).noquote() << "parseFrequency() given last position:" << lastPosition << "but data is only"
                                       << data.length() << "bytes";
        return freqs;
    }
    // Optional high-frequency bytes carry 100 MHz and GHz digits.
    if (data.length() > lastPosition + 2)
    {
        freqs.Hz += (data[lastPosition + 2] & 0x0f) * 1E9;             //  1 GHz
        freqs.Hz += ((data[lastPosition + 2] & 0xf0) >> 4) * 1E9 * 10; // 10 GHz
    }
    if (data.length() > lastPosition + 1)
    {
        freqs.Hz += (data[lastPosition + 1] & 0x0f) * 1E6 * 100;         //  100 MHz
        freqs.Hz += ((data[lastPosition + 1] & 0xf0) >> 4) * 1E6 * 1000; // 1000 MHz
    }

    // CI-V command 25h prepends a VFO byte before the frequency payload.
    if (lastPosition - 4 >= 0 && (quint8)data[lastPosition - 4] < 0x02)
    {
        freqs.VFO = (selVFO_t)(quint8)data[lastPosition - 4];
    }

    freqs.Hz += (data[lastPosition] & 0x0f) * 1E6;
    freqs.Hz += ((data[lastPosition] & 0xf0) >> 4) * 1E6 * 10;  //   10 MHz

    freqs.Hz += (data[lastPosition - 1] & 0x0f) * 10E3;         // 10 KHz
    freqs.Hz += ((data[lastPosition - 1] & 0xf0) >> 4) * 100E3; // 100 KHz

    freqs.Hz += (data[lastPosition - 2] & 0x0f) * 100;          // 100 Hz
    freqs.Hz += ((data[lastPosition - 2] & 0xf0) >> 4) * 1000;  // 1 KHz

    freqs.Hz += (data[lastPosition - 3] & 0x0f) * 1;            // 1 Hz
    freqs.Hz += ((data[lastPosition - 3] & 0xf0) >> 4) * 10;    // 10 Hz

    freqs.MHzDouble = (double)(freqs.Hz / 1000000.0);
    return freqs;
}

Frequency Commander::parseFreqData(const QByteArray& data, uchar receiver)
{
    Frequency freq;
    freq.Hz = parseFreqDataToInt(data);
    freq.MHzDouble = freq.Hz / 1000000.0;
    freq.VFO = selVFO_t(receiver);
    return freq;
}

quint64 Commander::parseFreqDataToInt(QByteArray data)
{
    // Parse packed BCD frequency bytes with a lookup table.
    quint64 val = 0;

    Q_ASSERT(data.size() * 2 < static_cast<int>(std::size(kPow10)));
    for (int i = 0; i < data.size() * 2; i = i + 2)
    {
        val += (data[i / 2] & 0x0f) * kPow10[i];
        val += ((data[i / 2] & 0xf0) >> 4) * kPow10[i + 1];
    }

    return val;
}

ModeInfo Commander::parseMode(uchar mode, uchar data, uchar filter, uchar receiver, uchar vfo)
{
    ModeInfo mi;
    bool found = false;
    if (mode == 0xff)
    {
        mi.reg = mode;
        mi.mk = modeUnknown;
        mi.filter = filter;
        mi.data = data;
        found = true;
    }
    else
    {
        const auto modeIt = std::find_if(radioCaps.modes.cbegin(), radioCaps.modes.cend(),
                                         [mode](const ModeInfo& candidate) { return candidate.reg == mode; });
        if (modeIt != radioCaps.modes.cend())
        {
            mi = *modeIt;
            mi.filter = filter;
            mi.data = data;
            found = true;
        }
    }

    if (!found)
    {
        qInfo(logRadio()).noquote() << QString("parseMode() No such mode %1 with filter %2").arg(mode).arg(filter)
                                    << payloadIn.toHex(' ');
    }

    // When CI-V 29h is unavailable, only the active receiver's filter width is queryable.
    if (!radioCaps.hasCommand29)
    {
        receiver = 0;
    }

    CacheItem item;

    // Use cached filter width when the current IC-9700 mode exposes one.
    if (vfo == 0 && mi.bwMin > 0 && mi.bwMax > 0)
    {
        item = queue->peekCache(funcFilterWidth, receiver);
    }

    if (item.value.isValid())
    {
        mi.pass = item.value.toInt();
    }
    else
    {
        // Use IC-9700 default filter widths until the radio reports a value.
        if (mi.mk == modeCW || mi.mk == modeCW_R || mi.mk == modePSK || mi.mk == modePSK_R)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 1200;
                break;
            case 2:
                mi.pass = 500;
                break;
            case 3:
                mi.pass = 250;
                break;
            }
        }
        else if (mi.mk == modeRTTY || mi.mk == modeRTTY_R)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 2400;
                break;
            case 2:
                mi.pass = 500;
                break;
            case 3:
                mi.pass = 250;
                break;
            }
        }
        else if (mi.mk == modeAM)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 9000;
                break;
            case 2:
                mi.pass = 6000;
                break;
            case 3:
                mi.pass = 3000;
                break;
            }
        }
        else if (mi.mk == modeFM)
        {
            switch (filter)
            {
            case 1:
                mi.pass = 15000;
                break;
            case 2:
                mi.pass = 10000;
                break;
            case 3:
                mi.pass = 7000;
                break;
            }
        }
        else if (mi.mk == modeWFM)
        {
            mi.pass = 200000;
        }
        else
        {
            switch (filter)
            {
            case 1:
                mi.pass = 3000;
                break;
            case 2:
                mi.pass = 2400;
                break;
            case 3:
                mi.pass = 1800;
                break;
            }
        }
    }

    return mi;
}

void Commander::parseMemoryField(const MemParserFormat& format, const QByteArray& data, MemoryType& memory)
{
    switch (format.spec)
    {
    case 'a':
        if (format.len == 1)
        {
            memory.group = bcdHexToUChar(data[0]);
        }
        else
        {
            memory.group = bcdHexToUChar(data[0], data[1]);
        }
        break;
    case 'b':
        memory.channel = bcdHexToUChar(data[0], data[1]);
        break;
    case 'c':
        memory.scan = data[0];
        break;
    case 'C':
        memory.skip = data[0] >> 4 & 0xf;
        memory.scan = data[0] & 0xf;
        break;
    case 'd': // combined split and scan
        memory.split = quint8(data[0] >> 4 & 0x0f);
        memory.scan = quint8(data[0] & 0x0f);
        break;
    case 'D': // duplex only
        memory.duplex = quint8(data[0] & 0x0f);
        break;
    case 'e':
        memory.vfo = data[0];
        break;
    case 'E':
        memory.vfoB = data[0];
        break;
    case 'f':
        memory.frequency.Hz = parseFreqDataToInt(data);
        break;
    case 'F':
        memory.frequencyB.Hz = parseFreqDataToInt(data);
        break;
    case 'g':
        memory.mode = bcdHexToUChar(data[0]);
        break;
    case 'G':
        memory.modeB = bcdHexToUChar(data[0]);
        break;
    case 'h':
        memory.filter = bcdHexToUChar(data[0]);
        break;
    case 'H':
        memory.filterB = bcdHexToUChar(data[0]);
        break;
    case 'i': // single datamode
        memory.datamode = bcdHexToUChar(data[0]);
        break;
    case 'I': // single datamode
        memory.datamodeB = bcdHexToUChar(data[0]);
        break;
    case 'j': // combined duplex and tonemode
        memory.duplex = duplexMode_t(quint8(data[0] >> 4 & 0x0f));
        memory.tonemode = quint8(quint8(data[0] & 0x0f));
        break;
    case 'J': // combined duplex and tonemodeB
        memory.duplexB = duplexMode_t((data[0] >> 4 & 0x0f));
        memory.tonemodeB = data[0] & 0x0f;
        break;
    case 'k': // combined datamode and tonemode
        memory.datamode = (quint8(data[0] >> 4 & 0x0f));
        memory.tonemode = data[0] & 0x0f;
        break;
    case 'K': // combined datamode and tonemode
        memory.datamodeB = (quint8(data[0] >> 4 & 0x0f));
        memory.tonemodeB = data[0] & 0x0f;
        break;
    case 'l': // tonemode
        memory.tonemode = data[0] & 0x0f;
        break;
    case 'L': // tonemode
        memory.tonemodeB = data[0] & 0x0f;
        break;
    case 'm':
        memory.dsql = (quint8(data[0] >> 4 & 0x0f));
        break;
    case 'M':
        memory.dsqlB = (quint8(data[0] >> 4 & 0x0f));
        break;
    case 'n':
        for (const auto& tn : radioCaps.ctcss)
        {
            if (tn.tone == bcdHexToUInt(data[1], data[2]))
            {
                memory.tone = tn.name;
            }
        }
        break;
    case 'N':
        for (const auto& tn : radioCaps.ctcss)
        {
            if (tn.tone == bcdHexToUInt(data[1], data[2]))
            {
                memory.toneB = tn.name;
            }
        }
        break;
    case 'o':
        for (const auto& tn : radioCaps.ctcss)
        {
            if (tn.tone == bcdHexToUInt(data[1], data[2]))
            {
                memory.tsql = tn.name;
            }
        }
        break;
    case 'O':
        for (const auto& tn : radioCaps.ctcss)
        {
            if (tn.tone == bcdHexToUInt(data[1], data[2]))
            {
                memory.tsqlB = tn.name;
            }
        }
        break;
    case 'p':
        memory.dtcsp = (quint8(data[0] >> 3 & 0x02) | quint8(data[0] & 0x01));
        break;
    case 'P':
        memory.dtcspB = (quint8(data[0] >> 3 & 0x02) | quint8(data[0] & 0x01));
        break;
    case 'q':
        memory.dtcs = bcdHexToUInt(data[0], data[1]);
        break;
    case 'Q':
        memory.dtcsB = bcdHexToUInt(data[0], data[1]);
        break;
    case 'r':
        memory.dvsql = bcdHexToUChar(data[0]);
        break;
    case 'R':
        memory.dvsqlB = bcdHexToUChar(data[0]);
        break;
    case 's':
        memory.duplexOffset.Hz = parseFreqDataToInt(data);
        break;
    case 'S':
        memory.duplexOffsetB.Hz = parseFreqDataToInt(data);
        break;
    case 't':
        memcpy(memory.UR, data.data(), qMin(int(sizeof memory.UR), data.size()));
        break;
    case 'T':
        memcpy(memory.URB, data.data(), qMin(int(sizeof memory.URB), data.size()));
        break;
    case 'u':
        memcpy(memory.R1, data.data(), qMin(int(sizeof memory.R1), data.size()));
        break;
    case 'U':
        memcpy(memory.R1B, data.data(), qMin(int(sizeof memory.R1B), data.size()));
        break;
    case 'v':
        memcpy(memory.R2, data.data(), qMin(int(sizeof memory.R2), data.size()));
        break;
    case 'V':
        memcpy(memory.R2B, data.data(), qMin(int(sizeof memory.R2B), data.size()));
        break;
    case 'w': // Tuning step
        if (bool(data[0]))
        {
            memory.tuningStep = bcdHexToUChar(data[1]);
            memory.progTs = bcdHexToUInt(data[2], data[3]);
        }
        else
        {
            memory.tuningStep = 0;
            memory.progTs = 5;
        }
        break;
    case 'x': // Attenuator & Preamp
        memory.atten = bcdHexToUChar(data[0]);
        memory.preamp = bcdHexToUChar(data[1]);
        break;
    case 'y': // Antenna
        memory.antenna = bcdHexToUChar(data[0]);
        break;
    case '+': // IP Plus
        memory.ipplus = bool(data[0] & 0x0f);
        break;
    case 'z':
        if (memory.scan == 0xfe)
        {
            memory.scan = 0;
        }
        memcpy(memory.name, data.data(), qMin(int(sizeof memory.name), data.size()));
        break;
    case 'Z': // Mode-dependent extension block.
        for (const auto& m : radioCaps.modes)
        {
            if (m.reg == memory.mode)
            {
                switch (m.mk)
                {
                case modeFM:
                    if (data.size() < 7)
                    {
                        return;
                    }
                    memory.tonemode = data[0] & 0x0f;
                    for (const auto& tn : radioCaps.ctcss)
                    {
                        if (tn.tone == bcdHexToUInt(data[2], data[3]))
                        {
                            memory.tsql = tn.name;
                        }
                    }
                    memory.dtcsp = quint8(data[4] & 0x0f);
                    memory.dtcs = bcdHexToUInt(data[5], data[6]);
                    break;
                case modeDV:
                    if (data.size() < 2)
                    {
                        return;
                    }
                    memory.dsql = (quint8(data[0] & 0x0f));
                    memory.dvsql = bcdHexToUChar(data[1]);
                    break;
                default:
                    break;
                }
                break;
            }
        }

        break;
    default:
        qInfo(logRadio()).noquote() << "Parser didn't match!" << "spec:" << format.spec << "pos:" << format.pos << "len"
                                    << format.len;
        break;
    }
}

void Commander::initializeMemoryForParsing(MemoryType& memory) const
{
    // Initialize optional fields before applying the parsed memory payload.
    memory.frequency.Hz = 0;
    memory.frequency.VFO = activeVFO;
    memory.frequency.MHzDouble = 0.0;
    memory.frequencyB = memory.frequency;
    memory.duplexOffset = memory.frequency;
    memory.duplexOffsetB = memory.frequency;
    memory.scan = 0xfe;
    memset(memory.UR, 0x0, sizeof(memory.UR));
    memset(memory.URB, 0x0, sizeof(memory.URB));
    memset(memory.R1, 0x0, sizeof(memory.R1));
    memset(memory.R1B, 0x0, sizeof(memory.R1B));
    memset(memory.R2, 0x0, sizeof(memory.R2));
    memset(memory.R2B, 0x0, sizeof(memory.R2B));
    memset(memory.name, 0x0, sizeof(memory.name));
}

bool Commander::parseMemory(QVector<MemParserFormat>* memParser, MemoryType* mem)
{
    initializeMemoryForParsing(*mem);
    // Memory parser positions are one-based and include the command prefix.
    const QByteArray memoryPayload = QByteArrayLiteral("**") + payloadIn;
    for (auto& parse : *memParser)
    {
        // Empty radio memory records are short; return the fields parsed so far.
        if (memoryPayload.size() < (parse.pos + 1 + parse.len) && parse.spec != 'Z')
        {
            return true;
        }
        const QByteArray data = memoryPayload.mid(parse.pos + 1, parse.len);
        parseMemoryField(parse, data, *mem);
    }

    return true;
}

void Commander::setRadioID(quint16 radioID)
{
    // Used by tests and manual diagnostics to force the IC-9700 capability path.

    qInfo(logRadio()).noquote() << QString("Setting radio ID to: 0x%1").arg(radioID & 0xff, 1, 16);

    lookingForRadio = true;

    // A forced ID has no incoming frame; use the configured CI-V address.
    this->incomingCIVAddr = this->civAddr & 0xff;

    if (radioID == kRadioModelId)
    {
        this->model = radioID & 0xff;
    }
    radioCaps.modelID = radioID & 0xff;
    radioCaps.model = this->model;
    determineRadioCaps();
}

uchar Commander::makeFilterWidth(ushort width, uchar receiver)
{
    quint8 calc;
    VfoCommandType t = queue->getVfoCommand(vfoA, receiver, false);
    ModeInfo mi = queue->getCache(t.modeFunc, receiver).value.value<ModeInfo>();

    if (mi.mk == modeAM)
    {
        calc = width < 200 ? 0 : quint16((width / 200) - 1);
        if (calc > 49)
        {
            calc = 49;
        }
    }
    else if (width >= 600) // SSB/CW/PSK 10-40 (10-31 for RTTY)
    {
        calc = quint16((width / 100) + 4);
        if (((calc > 31) && (mi.mk == modeRTTY || mi.mk == modeRTTY_R)))
        {
            calc = 31;
        }
        else if (calc > 40)
        {
            calc = 40;
        }
    }
    else // SSB etc 0-9
    {
        calc = quint16((width / 50) - 1);
    }

    char tens = (calc / 10);
    char units = (calc - (10 * tens));

    char b1 = (units) | (tens << 4);

    return b1;
}

unsigned char Commander::convertNumberToHex(unsigned char num)
{
    // BCD-encode a two-digit decimal value.
    if (num > 99)
    {
        qInfo(logRadio()).noquote() << "Invalid numeric conversion from num " << num << " to hex.";
        return 0xFA;
    }
    unsigned char result = 0;
    result = (num / 10) << 4;
    result |= (num - 10 * (num / 10));
    return result;
}

void Commander::enableAudio()
{
    emit requestEnableAudio();
}

void Commander::setRxAudioDevice(const QAudioDevice& device)
{
    if (udp == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(udp, [udp = udp, device]() { udp->setRxAudioDevice(device); }, Qt::QueuedConnection);
}

void Commander::setTxAudioDevice(const QAudioDevice& device)
{
    if (udp == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(udp, [udp = udp, device]() { udp->setTxAudioDevice(device); }, Qt::QueuedConnection);
}

bool Commander::stopLocalAudio()
{
    if (udp == nullptr)
    {
        return true;
    }

    auto stopDone = std::make_shared<QSemaphore>();
    UdpHandler* udpSession = udp;
    const bool queued = QMetaObject::invokeMethod(
        udpSession,
        [udpSession, stopDone]()
        {
            udpSession->stopLocalAudio();
            stopDone->release();
        },
        Qt::QueuedConnection);
    if (!queued || !stopDone->tryAcquire(1, 2500))
    {
        qWarning(logAudio()).noquote() << "Timed out stopping local audio worker";
        return false;
    }
    return true;
}

void Commander::setPttActive(bool active)
{
    if (udp == nullptr)
    {
        return;
    }

    QMetaObject::invokeMethod(
        udp, [udpSession = udp, active]() { udpSession->setPttActive(active); }, Qt::QueuedConnection);
}

void Commander::sendDtmfPcm(const QByteArray& pcm)
{
    if (udp == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(udp, [udpSession = udp, pcm]() { udpSession->queueDtmfPcm(pcm); }, Qt::QueuedConnection);
}

void Commander::readCurrentFrequencyAndMode()
{
    // Startup readiness needs the simplest IC-9700 current-VFO readback. The
    // normal routing layer may prefer selected-VFO commands 25/26, but field
    // logs showed the radio streaming scope data while not replying to those
    // selected-VFO probes during LAN startup. Raw 03/04 replies still parse
    // through the same cache/router path, so this bypass stays limited to
    // connection sync and can be backed out cleanly if later captures differ.
    QByteArray frequencyCommand;
    frequencyCommand.append(char(0x03));
    rememberPendingReply(funcFreqGet, 0);
    prepDataAndSend(frequencyCommand);

    QByteArray modeCommand;
    modeCommand.append(char(0x04));
    rememberPendingReply(funcModeGet, 0);
    prepDataAndSend(modeCommand);
}

bool Commander::appendSetCommandValue(Funcs func, const QVariant& value, uchar receiver, const FuncType& command,
                                      QByteArray& payload)
{
    const FuncType& cmd = command;

    if (!cmd.setCmd)
    {
        qDebug(logRadio()).noquote() << "Removing unsupported set command from queue" << funcString[func] << "VFO"
                                     << receiver;
        queue->del(func, receiver);
        return false;
    }

    if (m_suppressReadbackForCurrentCommand)
    {
        // The caller is building a receiver-scoped command sequence and
        // will issue any required readback before restoring MAIN/SUB.
    }
    else if (func == funcFreqSet)
    {
        queue->addUnique(kPriorityImmediate, funcFreqGet, false, receiver);
    }
    else if (func == funcModeSet)
    {
        queue->addUnique(kPriorityImmediate, funcModeGet, false, receiver);
    }
    else if (cmd.getCmd && func != funcScopeFixedEdgeFreq && func != funcSpeech && func != funcMemoryContents &&
             func != funcSatelliteMemory && func != funcBandStackReg && func != funcSendCW)
    {
        // Confirm radio-backed state by querying after the set command.
        queue->addUnique(kPriorityImmediate, func, false, receiver);
    }

    const int ValueType = value.userType();
    const auto valueHolds = [ValueType](int type) { return ValueType == type; };

    if (valueHolds(qMetaTypeId<bool>()))
    {
        payload.append(value.value<bool>());
    }
    else if (valueHolds(qMetaTypeId<QString>()))
    {
        QString text = value.value<QString>();
        if (func == funcSendCW)
        {
            QByteArray textData = text.toLatin1();
            qDebug(logRadio()).noquote() << "CW input:" << textData;
            for (int c = 0; c < textData.length(); c++)
            {
                const quint8 p = textData.at(c);
                if (((p >= 0x30) && (p <= 0x39)) || ((p >= 0x41) && (p <= 0x5A)) || ((p >= 0x61) && (p <= 0x7A)) ||
                    (p == 0x2F) || (p == 0x3F) || (p == 0x2E) || (p == 0x2D) || (p == 0x2C) || (p == 0x3A) ||
                    (p == 0x27) || (p == 0x28) || (p == 0x29) || (p == 0x3D) || (p == 0x2B) || (p == 0x22) ||
                    (p == 0x40) || (p == 0x20))
                {
                    // Allowed CW character.
                }
                else
                {
                    qWarning(logRadio()).noquote() << "Invalid character detected in CW message at position " << c
                                                   << ", the character is " << text.at(c);
                    textData[c] = 0x3F; // "?"
                }
            }
            if (textData.isEmpty())
            {
                payload.append(uchar(0xff));
            }
            else
            {
                payload.append(textData);
                qDebug(logRadio()).noquote() << "CW output::" << textData;
            }
            qDebug(logRadio()).noquote() << "Sending CW: payload:" << payload.toHex(' ');
        }
    }
    else if (valueHolds(qMetaTypeId<uchar>()))
    {
        if (func == funcRoofingFilter || func == funcFilterShape)
        {
            // The IC-9700 only sets shape for the active filter.
            payload.append(bcdEncodeChar(value.value<uchar>() % 10));
        }
        else
        {
            payload.append(bcdEncodeChar(value.value<uchar>()));
        }
    }
    else if (valueHolds(qMetaTypeId<ushort>()))
    {
        if (func == funcFilterWidth)
        {
            payload.append(makeFilterWidth(value.value<ushort>(), receiver));
        }
        else if (func == funcKeySpeed)
        {
            ushort wpm = round((value.value<ushort>() - 6) * (6.071));
            qDebug(logRadio()).noquote() << "Sending key speed orig:" << value.value<ushort>() << "sent:" << wpm;
            payload.append(bcdEncodeInt(wpm));
        }
        else if (func == funcCwPitch)
        {
            ushort pitch = 0;
            pitch = ceil((value.value<ushort>() - 300) * (255.0 / 600.0));
            payload.append(bcdEncodeInt(pitch));
        }
        else
        {
            payload.append(bcdEncodeInt(value.value<ushort>()));
        }
    }
    else if (valueHolds(qMetaTypeId<short>()) && func == funcRitFreq)
    {
        // RIT/XIT offset payload.
        bool isNegative = false;
        short ritValue = value.value<short>();
        qDebug(logRadio()).noquote() << "Setting RIT to " << ritValue;
        if (ritValue < 0)
        {
            isNegative = true;
            ritValue *= -1;
        }
        Frequency f;
        QByteArray freqBytes;
        f.Hz = ritValue;
        freqBytes = makeFreqPayload(f);
        freqBytes.truncate(2);
        payload.append(freqBytes);
        payload.append(QByteArray(1, (char)isNegative));
    }
    else if (valueHolds(qMetaTypeId<uint>()) &&
             (func == funcMemoryContents || func == funcMemoryMode || func == funcSatelliteMemory))
    {
        qDebug(logRadio()).noquote() << "Get Memory Contents" << (value.value<uint>() & 0xffff);
        qDebug(logRadio()).noquote() << "Get Memory Group (if exists)" << (value.value<uint>() >> 16 & 0xffff);
        if (func == funcMemoryContents)
        {
            const auto groupFormat = std::find_if(radioCaps.memParser.cbegin(), radioCaps.memParser.cend(),
                                                  [](const MemParserFormat& parse) { return parse.spec == 'a'; });
            if (groupFormat != radioCaps.memParser.cend())
            {
                // Include memory group when the IC-9700 memory format uses it.
                if (groupFormat->len == 1)
                {
                    payload.append(bcdEncodeChar(quint16(value.value<uint>() >> 16 & 0xff)));
                }
                else if (groupFormat->len == 2)
                {
                    payload.append(bcdEncodeInt(quint16(value.value<uint>() >> 16 & 0xffff)));
                }
            }
        }
        payload.append(bcdEncodeInt(quint16(value.value<uint>() & 0xffff)));
    }
    else if (valueHolds(qMetaTypeId<MemoryType>()))
    {
        // Build the memory payload from the compiled IC-9700 memory format.
        bool finished = false;
        char nul = 0x0;
        uchar ffchar = 0xff;
        QVector<MemParserFormat> parser;
        MemoryType mem = value.value<MemoryType>();
        if (mem.sat)
        {
            parser = radioCaps.satParser;
        }
        else
        {
            parser = radioCaps.memParser;
        }
        const auto appendToneByName = [this, &payload](const QString& name)
        {
            const auto tone = std::find_if(radioCaps.ctcss.cbegin(), radioCaps.ctcss.cend(),
                                           [&name](const ToneInfo& info) { return info.name == name; });
            if (tone != radioCaps.ctcss.cend())
            {
                payload.append(bcdEncodeInt(tone->tone));
            }
        };

        // Memory format legend for the compiled IC-9700 definition:
        // a/b identify group/channel, f/F carry RX/TX frequency, g/G
        // carry mode, h/H filter, i/I data mode, j/k/l tone access,
        // n/o tone frequencies, p/q DTCS polarity/code, s/S repeater
        // offset, t/u/v D-STAR callsigns, z name, and Z is an
        // IC-9700 mode-specific extension block. Deletion records are
        // intentionally short: once a delete marker is written, the
        // loop stops so the radio receives an empty memory record for
        // that slot instead of a partially populated one.
        for (auto& parse : parser)
        {
            switch (parse.spec)
            {
            case 'a':
                if (parse.len == 1)
                {
                    payload.append(mem.group);
                }
                else if (parse.len == 2)
                {
                    payload.append(bcdEncodeInt(mem.group));
                }
                break;
            case 'b':
                payload.append(bcdEncodeInt(mem.channel));
                break;
            case 'c':
                // Empty record marker for memory deletion.
                if (mem.del)
                {
                    payload.append(ffchar);
                    finished = true;
                    break;
                }
                else
                {
                    payload.append(mem.scan);
                }
                break;
            case 'C':
                // Empty record marker for memory deletion.
                if (mem.del)
                {
                    payload.append(ffchar);
                    finished = true;
                    break;
                }
                else
                {
                    payload.append(mem.scan);
                }
                break;
            case 'd': // combined split and scan
                if (mem.del)
                {
                    payload.append(ffchar);
                    finished = true;
                    break;
                }
                else
                {
                    payload.append(quint8((mem.split << 4 & 0xf0) | (mem.scan & 0x0f)));
                }
                break;
            case 'D': // Duplex only
                payload.append(mem.duplex);
                break;
            case 'e':
                payload.append(mem.vfo);
                break;
            case 'E':
                payload.append(mem.vfoB);
                break;
            case 'f':
                if (mem.del)
                {
                    qDebug(logRadio()).noquote() << "Pre deleting f" << payload.toHex(' ');
                    payload.append(ffchar);
                    qDebug(logRadio()).noquote() << "Deleting f" << payload.toHex(' ');
                    finished = true;
                    break;
                }
                else
                {
                    // Pad to the IC-9700 memory field width.
                    QByteArray f = makeFreqPayload(mem.frequency);
                    for (int i = f.size(); i < parse.len; i++)
                    {
                        f.append(nul);
                    }
                    payload.append(f);
                }
                break;
            case 'F':
            {
                QByteArray f = makeFreqPayload(mem.frequencyB);
                for (int i = f.size(); i < parse.len; i++)
                {
                    f.append(nul);
                }
                payload.append(f);
                break;
            }
            case 'g':
                payload.append(bcdEncodeChar(mem.mode));
                break;
            case 'G':
                payload.append(bcdEncodeChar(mem.modeB));
                break;
            case 'h':
                payload.append(bcdEncodeChar(mem.filter));
                break;
            case 'H':
                payload.append(bcdEncodeChar(mem.filterB));
                break;
            case 'i': // single datamode
                payload.append(bcdEncodeChar(mem.datamode));
                break;
            case 'I':
                payload.append(bcdEncodeChar(mem.datamodeB));
                break;
            case 'j': // combined duplex and tonemode
                payload.append((mem.duplex << 4) | mem.tonemode);
                break;
            case 'J': // combined duplex and tonemode
                payload.append((mem.duplexB << 4) | mem.tonemodeB);
                break;
            case 'k': // combined datamode and tonemode
                payload.append((mem.datamode << 4 & 0xf0) | (mem.tonemode & 0x0f));
                break;
            case 'K': // combined datamode and tonemode
                payload.append((mem.datamodeB << 4 & 0xf0) | (mem.tonemodeB & 0x0f));
                break;
            case 'l': // tonemode
                payload.append(mem.tonemode);
                break;
            case 'L':
                payload.append(mem.tonemodeB);
                break;
            case 'm':
                payload.append(mem.dsql << 4);
                break;
            case 'M':
                payload.append(mem.dsqlB << 4);
                break;
            case 'n':
                payload.append(nul);
                appendToneByName(mem.tone);
                break;
            case 'N':
                payload.append(nul);
                appendToneByName(mem.toneB);
                break;
            case 'o':
                payload.append(nul);
                appendToneByName(mem.tsql);
                break;
            case 'O':
                payload.append(nul);
                appendToneByName(mem.tsqlB);
                break;
            case 'p':
                payload.append((mem.dtcsp << 3 & 0x10) | (mem.dtcsp & 0x01));
                break;
            case 'P':
                payload.append((mem.dtcspB << 3 & 0x10) | (mem.dtcspB & 0x01));
                break;
            case 'q':
                payload.append(bcdEncodeInt(mem.dtcs));
                break;
            case 'Q':
                payload.append(bcdEncodeInt(mem.dtcsB));
                break;
            case 'r':
                payload.append(bcdEncodeChar(mem.dvsql));
                break;
            case 'R':
                payload.append(bcdEncodeChar(mem.dvsqlB));
                break;
            case 's':
                payload.append(makeFreqPayload(mem.duplexOffset).mid(1, parse.len));
                break;
            case 'S':
                payload.append(makeFreqPayload(mem.duplexOffsetB).mid(1, parse.len));
                break;
            case 't':
                payload.append(QByteArray(mem.UR).leftJustified(parse.len, ' ', true));
                break;
            case 'T':
                payload.append(QByteArray(mem.URB).leftJustified(parse.len, ' ', true));
                break;
            case 'u':
                payload.append(QByteArray(mem.R1).leftJustified(parse.len, ' ', true));
                break;
            case 'U':
                payload.append(QByteArray(mem.R1B).leftJustified(parse.len, ' ', true));
                break;
            case 'v':
                payload.append(QByteArray(mem.R2).leftJustified(parse.len, ' ', true));
                break;
            case 'V':
                payload.append(QByteArray(mem.R2B).leftJustified(parse.len, ' ', true));
                break;
            case 'w':                                                          // Tuning step
                payload.append(quint8(mem.tuningStep != 0 ? 1 : 0));
                payload.append(bcdEncodeChar(qMax(uchar(1), mem.tuningStep))); // 0 is invalid.
                payload.append(bcdEncodeInt(mem.progTs));
                break;
            case 'x': // Attenuator & Preamp
                payload.append(bcdEncodeChar(mem.atten));
                payload.append(bcdEncodeChar(mem.preamp));
                break;
            case 'y': // Antenna
                payload.append(bcdEncodeChar(mem.antenna));
                break;
            case '+': // IP Plus
                payload.append(bcdEncodeChar(mem.ipplus));
                break;
            case 'z':
                payload.append(QByteArray(mem.name).leftJustified(parse.len, ' ', true));
                break;
            case 'Z': // Mode-dependent extension block.
            {
                const auto modeIt = std::find_if(radioCaps.modes.cbegin(), radioCaps.modes.cend(),
                                                 [&mem](const ModeInfo& mode) { return mode.reg == mem.mode; });
                if (modeIt != radioCaps.modes.cend())
                {
                    switch (modeIt->mk)
                    {
                    case modeFM:
                        if (mem.tonemode)
                        {
                            payload.append(bcdEncodeChar(mem.tonemode));
                            payload.append(nul);
                            appendToneByName(mem.tsql);
                            payload.append(bcdEncodeChar(mem.dtcsp));
                            payload.append(bcdEncodeInt(mem.dtcs));
                        }
                        break;
                    case modeDV:
                        if (mem.dsql)
                        {
                            payload.append(bcdEncodeChar(2)); // IC-9700 expects 2 when enabling DV SQL.
                            payload.append(bcdEncodeChar(mem.dvsql));
                        }
                        break;
                    default:
                        break;
                    }
                }
                break;
            }
            default:
                break;
            }
            if (finished)
            {
                break;
            }
        }
        qDebug(logRadio()).noquote() << "Writing memory location:" << payload.toHex(' ');
    }
    else if (valueHolds(qMetaTypeId<int>()) && (func == funcScopeRef))
    {
        bool isNegative = false;
        int level = value.value<int>();
        if (level < 0)
        {
            isNegative = true;
            level *= -1;
        }
        payload.append(bcdEncodeInt(quint16(level * 10)));
        payload.append(static_cast<quint8>(isNegative));
    }
    else if (valueHolds(qMetaTypeId<ModeInfo>()))
    {
        {
            ModeInfo m = value.value<ModeInfo>();
            if (func == funcDataModeWithFilter)
            {
                payload.append(bcdEncodeChar(m.data));
                if (m.data != 0)
                {
                    payload.append(m.filter);
                }
            }
            else
            {
                payload.append(bcdEncodeChar(m.reg));
                if (func == funcMode || func == funcSelectedMode || func == funcUnselectedMode)
                {
                    payload.append(m.data);
                }
                if (!radioCaps.filters.empty() && m.mk != modeWFM)
                {
                    payload.append(m.filter);
                }
                qDebug(logRadio()).noquote() << "Sending mode command" << funcString[func] << " mode:" << m.name
                                             << "data:" << m.data << "filter" << m.filter;
            }
        }
    }
    else if (valueHolds(qMetaTypeId<Frequency>()))
    {
        if (func == funcSendFreqOffset)
        {
            payload.append(makeFreqPayload(value.value<Frequency>()).mid(1, 3));
        }
        else
        {
            payload.append(makeFreqPayload(value.value<Frequency>()));
        }
    }
    else if (valueHolds(qMetaTypeId<AntennaInfo>()))
    {
        payload.append(bcdEncodeChar(value.value<AntennaInfo>().antenna));
        if (radioCaps.commands.contains(funcRXAntenna))
        {
            payload.append(value.value<AntennaInfo>().rx);
        }
    }
    else if (valueHolds(qMetaTypeId<radioInput>()))
    {
        payload.append(bcdEncodeChar(value.value<radioInput>().reg));
    }
    else if (valueHolds(qMetaTypeId<SpectrumBounds>()))
    {
        SpectrumBounds s = value.value<SpectrumBounds>();
        uchar range = 1;
        double lastRange = -1.0;
        auto band = radioCaps.bands.cend();
        while (band != radioCaps.bands.cbegin())
        {
            band--;
            if (band->range > s.start)
            {
                break;
            }
            else if (lastRange != band->range && band->range != 0.0 && band->range <= s.start)
            {
                range++;
                lastRange = band->range;
            }
        }
        payload.append(bcdEncodeChar(range));
        payload.append(bcdEncodeChar(s.edge));
        payload.append(makeFreqPayload(s.start));
        payload.append(makeFreqPayload(s.end));
    }
    else if (valueHolds(qMetaTypeId<duplexMode_t>()))
    {
        payload.append(static_cast<uchar>(value.value<duplexMode_t>()));
    }
    else if (valueHolds(qMetaTypeId<centerSpanData>()))
    {
        centerSpanData span = value.value<centerSpanData>();
        double freq = double(span.freq / 1000000.0);
        payload.append(makeFreqPayload(freq));
    }
    else if (valueHolds(qMetaTypeId<ToneInfo>()))
    {
        ToneInfo t = value.value<ToneInfo>();
        payload.append(encodeTone(t.tone, t.tinv, t.rinv));
    }
    else if (valueHolds(qMetaTypeId<DateKind>()))
    {
        DateKind d = value.value<DateKind>();
        qInfo(logRadio()).noquote()
            << QString("Sending new date: (MM-DD-YYYY) %1-%2-%3").arg(d.month).arg(d.day).arg(d.year);
        payload.append(convertNumberToHex(d.year / 100));                  // 20
        payload.append(convertNumberToHex(d.year - 100 * (d.year / 100))); // 21
        payload.append(convertNumberToHex(d.month));
        payload.append(convertNumberToHex(d.day));
    }
    else if (valueHolds(qMetaTypeId<TimeKind>()))
    {
        TimeKind t = value.value<TimeKind>();
        if (cmd.cmd == funcTime)
        {
            qInfo(logRadio()).noquote() << QString("Sending new time: (HH:MM) %1:%2").arg(t.hours).arg(t.minutes);
            payload.append(convertNumberToHex(t.hours));
            payload.append(convertNumberToHex(t.minutes));
        }
        else if (cmd.cmd == funcUTCOffset)
        {
            qInfo(logRadio()).noquote()
                << QString("Sending new UTC offset: %1%2:%3").arg(t.isMinus ? "-" : "+").arg(t.hours).arg(t.minutes);
            payload.append(convertNumberToHex(t.hours));
            payload.append(convertNumberToHex(t.minutes));
            payload.append((uchar)t.isMinus);
        }
    }
    else if (valueHolds(qMetaTypeId<RptrAccessData>()))
    {
        RptrAccessData r = value.value<RptrAccessData>();
        qDebug(logRadio()).noquote() << "Sending RptrAccessData Mode" << r.accessMode;
        payload.append(bcdEncodeChar(static_cast<uchar>(r.accessMode)));
    }
    else
    {
        qInfo(logRadio()).noquote() << funcString[func] << "Got unknown value type" << QString(value.typeName());
        return false;
    }

    return true;
}

void Commander::receiveCommand(Funcs func, QVariant value, uchar receiver)
{
    int val = INT_MIN;
    if (value.isValid() && value.canConvert<int>() && func != funcSendCW)
    {
        // Integer values can be range-checked against the capability table.
        val = value.value<int>();
        if (func == funcMemoryContents || func == funcMemoryClear || func == funcMemoryWrite || func == funcMemoryMode)
        {
            // Strip memory group bits before range-checking the channel number.
            qDebug(logRadio()).noquote() << "Memory Command" << funcString[func] << "with valuetype "
                                         << QString(value.typeName());
            val = val & 0xffff;
        }
    }

    if (func == funcAfGain && value.isValid() && udp != nullptr && receiver == 0xff)
    {
        // AF gain maps to local Qt audio volume while connected over LAN.
        emit haveSetVolume(static_cast<uchar>(value.toInt()));
        queue->receiveValue(func, value, false);
        return;
    }

    if (func == funcSelectVFO)
    {
        vfo_t v = value.value<vfo_t>();
        queue->recordLocalRoutingState(func, value, receiver);
        func = (v == vfoA)      ? funcVFOASelect
               : (v == vfoB)    ? funcVFOBSelect
               : (v == vfoMain) ? funcVFOMainSelect
               : (v == vfoSub)  ? funcVFOSubSelect
               : (v == vfoMem)  ? funcMemoryMode
                                : funcNone;
        value.clear();
        val = INT_MIN;
    }

    QByteArray payload;
    FuncType cmd;
    cmd = getCommand(func, payload, val, receiver);
    if (cmd.cmd != funcNone)
    {
        if (!value.isValid() && cmd.getCmd && commandExpectsCorrelatedReply(func) && !radioCaps.hasCommand29 &&
            !replyDoesNotRequireReceiverCorrelation(func))
        {
            discardExpiredPendingReplies();
            if (deferReplyReadIfBlocked(func, receiver))
            {
                return;
            }
        }
        // Receiver-scoped commands carry the receiver byte before command data.
        switch (cmd.cmd)
        {
        case funcFreq:
        case funcMode:
        case funcScopeMode:
        case funcScopeSpan:
        case funcScopeRef:
        case funcScopeHold:
        case funcScopeSpeed:
        case funcScopeRBW:
        case funcScopeVBW:
        case funcScopeCenterType:
        case funcScopeEdge:
            payload.append(receiver);
            break;
        default:
            break;
        }

        if (value.isValid())
        {
            if (!appendSetCommandValue(func, value, receiver, cmd, payload))
            {
                return;
            }
        }
        else
        {
            if (!cmd.getCmd)
            {
                qDebug(logRadio()).noquote()
                    << "Removing unsupported get command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func, receiver);
                return;
            }
        }
        if (!value.isValid() && cmd.getCmd && commandExpectsCorrelatedReply(func) && !radioCaps.hasCommand29 &&
            !replyDoesNotRequireReceiverCorrelation(func))
        {
            rememberPendingReply(func, receiver);
        }
        if (func == funcSMeter && !value.isValid())
        {
            if (receiver == 1)
            {
                ++m_correlationDiagnostics.subSMeterRequests;
            }
            else
            {
                ++m_correlationDiagnostics.mainSMeterRequests;
            }
        }
        // Register command correlation before emitting dataForComm. Most
        // production connections are queued across threads, but this ordering
        // also remains correct for direct test or diagnostic connections that
        // can deliver an immediate reply synchronously.
        emit commandTransmitted(func, receiver, m_rttEstimator.replyTimeoutMs());
        prepDataAndSend(payload);
    }
    else
    {
        qDebug(logRadio()).noquote() << "CachingQueue(): unimplemented command" << funcString[func];
        queue->del(func, receiver);
    }
}

void Commander::receiveCommandNoReadback(Funcs func, QVariant value, uchar receiver)
{
    const bool previous = m_suppressReadbackForCurrentCommand;
    m_suppressReadbackForCurrentCommand = true;
    receiveCommand(func, value, receiver);
    m_suppressReadbackForCurrentCommand = previous;
}
