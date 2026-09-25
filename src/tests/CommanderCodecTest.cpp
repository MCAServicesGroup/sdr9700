#include "Commander.h"

#include "CivSequenceGate.h"
#include "RadioCapabilities.h"

#include <QSignalSpy>
#include <QTest>
#include <limits>

class CommanderCodecTest : public QObject
{
    Q_OBJECT

  private slots:
    void init();
    void encodesAndDecodesPackedBcd();
    void frequencyPayloadRoundTrips_data();
    void frequencyPayloadRoundTrips();
    void tonePayloadRoundTrips();
    void parsesModesAndUnknownMode();
    void parsesFrequencyReplyFamily();
    void parsesModeReplyFamily();
    void rejectsMalformedFrequencyAndModeReplies();
    void parsesLevelAndMeterReplyFamily();
    void rejectsMalformedLevelAndMeterReplies();
    void parsesFeatureAndScopeReplyFamilies();
    void rejectsMalformedFeatureAndScopeReplies();
    void parsesMemoryFields();
    void serializesOutboundCommandValues();
    void serializesStandbyWakeFrame();
    void serializesDialLockSetAndRead();
    void rejectsUnknownOutboundValueTypes();
    void acknowledgementsAreDiagnosticOnly();
    void tracksPendingReplyPressure();
    void unsolicitedUpdateDoesNotConsumePendingReply();
    void acceptsOneMainFrequencyBroadcastAtEachPttTransition();
    void malformedReplyDoesNotConsumePendingReply();
    void selectedRepliesUsePendingReceiverIdentity();
    void correlatesEquivalentFrequencyAndModeReplyCommands();
    void discardsPendingRepliesByCanonicalFamily();
    void serializesReceiverlessReadsByCanonicalFamily();
    void doesNotDeferMainSubSwapActions();
    void defersWholeMainSubExchangeUntilReplyFamiliesAreIdle();
    void defersMainSubExchangeUntilInteractiveSetDispatches();
    void receiverScopedRetrySelectsOnlyWhenReadCanDispatch();
    void exchangeGateAllowsOnlyConfirmationRetries();
    void repeatedExchangesDrainScheduledPressure();
    void discardsLateReplyDuringFamilyDrain();
    void adaptsReplyDrainWindowsToMeasuredRtt();
    void rejectsShortSpectrumFrames();
    void assemblesMultiPacketSpectrum();
    void boundsMultiPacketSpectrum();
    void rejectsBrokenAndExpiredSpectrumAssemblies();
    void parserToleratesDeterministicArbitraryInput();
    void schedulerCoalescesAndBoundsReads();
    void schedulerMakesStartupProgressUnderMeterPressure();
    void schedulerInterleavesBackgroundAndMeters();
    void schedulerEscapesPermanentMeterOverdueState();
    void schedulerCoalescesInteractiveActionsAndPreservesReadProgress();
    void pacesInteractiveConfirmationAfterSet();
    void reportsMeterTransmissionAtWireDispatch();
    void sMeterMainReadAvoidsContextCommands();
    void sMeterSubReadWaitsForReplyFamilyDrainBeforeSelectingContext();
    void sMeterSubReadRestoresContextAfterReply();
    void staleSMeterTimerDoesNotAffectLaterRead();
    void sMeterSubReadPreservesOperatorSelectedSub();
    void receiverScopedReadPreservesOperatorSelectedSub();
    void survivesCombinedTransportAndSchedulerFaultSoak();
    void sessionResetCancelsTransactionalAndScopeState();

  private:
    Commander m_commander;
};

void CommanderCodecTest::init()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_deferredReplyReads.clear();
    m_commander.m_replyFamilyDrains.clear();
    m_commander.m_replyDrainTimer->stop();
    m_commander.m_receiverScopedReadActive = false;
    m_commander.m_smeterScopedReadActive = false;
    m_commander.m_rttEstimator.reset();
    m_commander.m_correlationDiagnostics = {};
    m_commander.resetScheduledCommands();
    m_commander.m_schedulerDiagnostics = {};
    m_commander.m_lastLoggedSchedulerDiagnostics = {};
    m_commander.m_shutdownComplete = false;
    m_commander.m_mainSubExchangeQueued = false;
    m_commander.m_mainSubExchangeConfirmationPending = false;
    sdr9700::populateRadioCapabilities(m_commander.radioCaps);
    m_commander.haveRadioCaps = true;
    m_commander.setCIVAddr(0xA2);
}

void CommanderCodecTest::serializesStandbyWakeFrame()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    m_commander.sendStandbyWake();

    QCOMPARE(wireSpy.count(), 1);
    const QByteArray frame = wireSpy.takeFirst().at(0).toByteArray();
    QCOMPARE(frame.size(), 157);
    QCOMPARE(frame.left(152), QByteArray(152, '\xFE'));
    QCOMPARE(frame.right(5), QByteArray::fromHex("a2e11801fd"));
}

void CommanderCodecTest::doesNotDeferMainSubSwapActions()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    m_commander.receiveCommand(funcVFOSwapMS, QVariant(), 0);
    m_commander.receiveCommand(funcVFOSwapMS, QVariant(), 0);

    QCOMPARE(wireSpy.count(), 2);
    for (const QList<QVariant>& emission : wireSpy)
    {
        QVERIFY(emission.at(0).toByteArray().contains(QByteArray::fromHex("07b0")));
    }
    QCOMPARE(m_commander.m_pendingReplies.size(), 0);
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 0);
}

void CommanderCodecTest::defersWholeMainSubExchangeUntilReplyFamiliesAreIdle()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    QSignalSpy dispatchedSpy(&m_commander, &Commander::mainSubExchangeDispatched);

    m_commander.rememberPendingReply(funcModeGet, 0);
    m_commander.requestMainSubExchange();

    QCOMPARE(wireSpy.count(), 0);
    QCOMPARE(dispatchedSpy.count(), 0);
    QVERIFY(m_commander.m_mainSubExchangeQueued);

    m_commander.m_pendingReplies.clear();
    m_commander.dispatchDeferredReplyReads();

    QVERIFY(!m_commander.m_mainSubExchangeQueued);
    QCOMPARE(dispatchedSpy.count(), 1);
    QVERIFY(wireSpy.count() > 0);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("07b0")));

    int interactiveDispatches = 0;
    m_commander.scheduleInteractiveAction(funcRfGain, 0, [&interactiveDispatches]() { ++interactiveDispatches; });
    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(interactiveDispatches, 0);
    m_commander.finishMainSubExchangeConfirmation();
    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(interactiveDispatches, 1);
}

void CommanderCodecTest::defersMainSubExchangeUntilInteractiveSetDispatches()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    QSignalSpy dispatchedSpy(&m_commander, &Commander::mainSubExchangeDispatched);
    int interactiveDispatches = 0;
    m_commander.scheduleInteractiveAction(funcFreqSet, 1, [&interactiveDispatches]() { ++interactiveDispatches; });

    m_commander.requestMainSubExchange();
    QCOMPARE(dispatchedSpy.count(), 0);
    QVERIFY(m_commander.m_mainSubExchangeQueued);

    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(interactiveDispatches, 1);
    QCOMPARE(dispatchedSpy.count(), 1);
    QVERIFY(!m_commander.m_mainSubExchangeQueued);
    QVERIFY(wireSpy.count() > 0);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("07b0")));
}

void CommanderCodecTest::receiverScopedRetrySelectsOnlyWhenReadCanDispatch()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    m_commander.rememberPendingReply(funcModeGet, 1);
    m_commander.requestReceiverScopedRead(funcModeGet, 1);

    QCOMPARE(wireSpy.count(), 0);
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 1);

    m_commander.m_pendingReplies.clear();
    m_commander.dispatchDeferredReplyReads();

    QTRY_COMPARE(wireSpy.count(), 3);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("07d1")));
    QVERIFY(wireSpy.at(1).at(0).toByteArray().contains(QByteArray::fromHex("04")));
    QVERIFY(wireSpy.at(2).at(0).toByteArray().contains(QByteArray::fromHex("07d0")));

    wireSpy.clear();
    m_commander.m_pendingReplies.clear();
    m_commander.m_replyFamilyDrains.clear();
    m_commander.m_deferredReplyReads.append({funcFreqGet, 1});
    m_commander.m_deferredReplyReads.append({funcModeGet, 1});
    m_commander.m_deferredReplyReads.append({funcRfGain, 1});
    m_commander.dispatchDeferredReplyReads();
    QTRY_COMPARE_WITH_TIMEOUT(wireSpy.count(), 9, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_commander.m_receiverScopedReadActive, 1000);
}

void CommanderCodecTest::exchangeGateAllowsOnlyConfirmationRetries()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    m_commander.m_mainSubExchangeConfirmationPending = true;
    m_commander.m_deferredReplyReads.append({funcFreqGet, 1});
    m_commander.m_deferredReplyReads.append({funcRfGain, 0});

    m_commander.dispatchDeferredReplyReads();

    // Receiver-scoped operations intentionally serialize select, read, and
    // restore across event-loop turns so another routed action cannot steal
    // the physical context between frames.
    QTRY_COMPARE_WITH_TIMEOUT(wireSpy.count(), 3, 1000);
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 1);
    QCOMPARE(m_commander.m_deferredReplyReads.first().func, funcRfGain);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("07d1")));
    QVERIFY(wireSpy.at(1).at(0).toByteArray().contains(QByteArray::fromHex("03")));
    QVERIFY(wireSpy.at(2).at(0).toByteArray().contains(QByteArray::fromHex("07d0")));

    m_commander.scheduleStartupRead(funcAttenuator, 0);
    m_commander.scheduleInteractiveAction(funcSquelch, 0, []() {});
    m_commander.m_scheduledCommandTimer->stop();
    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(m_commander.m_scheduledCommands.size(), 2);
    QVERIFY(m_commander.m_scheduledCommandTimer->isActive());
}

void CommanderCodecTest::repeatedExchangesDrainScheduledPressure()
{
    constexpr int kExchangeCount = 500;
    constexpr int kUpdatesPerExchange = 20;
    // Exercise ten thousand replaceable updates across hundreds of complete
    // exchange state-machine cycles.
    int dispatchedExchanges = 0;
    int deliveredInteractiveValue = -1;
    connect(&m_commander, &Commander::mainSubExchangeDispatched, this,
            [&dispatchedExchanges]() { ++dispatchedExchanges; });

    for (int exchange = 0; exchange < kExchangeCount; ++exchange)
    {
        m_commander.requestMainSubExchange();
        QVERIFY(m_commander.m_mainSubExchangeConfirmationPending);

        for (int update = 0; update < kUpdatesPerExchange; ++update)
        {
            const int value = exchange * kUpdatesPerExchange + update;
            m_commander.scheduleInteractiveAction(funcRfGain, 0, [&deliveredInteractiveValue, value]()
                                                  { deliveredInteractiveValue = value; });
        }
        m_commander.scheduleMeterRead(funcSMeter, 0);
        QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(2));

        m_commander.finishMainSubExchangeConfirmation();
        while (!m_commander.m_scheduledCommands.isEmpty())
        {
            m_commander.dispatchNextScheduledCommand();
        }
        QCOMPARE(deliveredInteractiveValue, (exchange + 1) * kUpdatesPerExchange - 1);

        // The scripted transport considers the two ambiguous confirmation
        // reads resolved before beginning the next exchange. Wire loss and
        // late-reply behavior are covered independently by the correlation
        // and sequence-gate tests.
        m_commander.m_pendingReplies.clear();
        m_commander.m_replyFamilyDrains.clear();
        m_commander.m_deferredReplyReads.clear();
    }

    QCOMPARE(dispatchedExchanges, kExchangeCount);
    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(0));
    QCOMPARE(m_commander.m_pendingReplies.size(), qsizetype(0));
    QCOMPARE(m_commander.m_deferredReplyReads.size(), qsizetype(0));
    QCOMPARE(m_commander.schedulerDiagnostics().droppedCommands, quint64(0));
    QCOMPARE(m_commander.schedulerDiagnostics().coalescedCommands, quint64(kExchangeCount * (kUpdatesPerExchange - 1)));
}

void CommanderCodecTest::schedulerCoalescesAndBoundsReads()
{
    constexpr int kRepeatedMeterReads = 10000;
    // A five-digit burst protects against queue growth that only appears after
    // sustained, same-key meter pressure.
    for (int i = 0; i < kRepeatedMeterReads; ++i)
    {
        m_commander.scheduleMeterRead(funcSMeter, 0);
    }
    QCOMPARE(m_commander.m_scheduledCommands.size(), 1);
    QCOMPARE(m_commander.schedulerDiagnostics().coalescedCommands, quint64(kRepeatedMeterReads - 1));

    for (int i = 0; i < 100; ++i)
    {
        m_commander.scheduleStartupRead(funcRfGain, static_cast<uchar>(i));
    }
    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(64));
    QVERIFY(m_commander.schedulerDiagnostics().droppedCommands > 0);
    QCOMPARE(m_commander.schedulerDiagnostics().highWaterMark, qsizetype(64));
}

void CommanderCodecTest::schedulerMakesStartupProgressUnderMeterPressure()
{
    m_commander.scheduleMeterRead(funcSMeter, 0);
    m_commander.scheduleMeterRead(funcSWRMeter, 0);
    m_commander.scheduleMeterRead(funcPowerMeter, 0);
    m_commander.scheduleMeterRead(funcALCMeter, 0);
    m_commander.scheduleStartupRead(funcRfGain, 0);

    m_commander.m_consecutiveMeterDispatches = 3;
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(m_commander.m_scheduledCommands.size(), 4);
    QVERIFY(std::none_of(m_commander.m_scheduledCommands.cbegin(), m_commander.m_scheduledCommands.cend(),
                         [](const Commander::ScheduledCommand& command) { return command.func == funcRfGain; }));
    QCOMPARE(m_commander.m_consecutiveMeterDispatches, 0);
}

void CommanderCodecTest::schedulerInterleavesBackgroundAndMeters()
{
    const QVector<Funcs> backgroundFunctions = {funcRfGain, funcSquelch, funcNRLevel};
    const QVector<Funcs> meterFunctions = {funcSMeter, funcPowerMeter, funcSWRMeter};
    for (const Funcs func : backgroundFunctions)
    {
        m_commander.scheduleStartupRead(func, 0);
    }
    for (const Funcs func : meterFunctions)
    {
        m_commander.scheduleMeterRead(func, 0);
    }

    for (Commander::ScheduledCommand& command : m_commander.m_scheduledCommands)
    {
        if (command.commandClass != Commander::ScheduledCommandClass::Meter)
        {
            command.enqueuedAtMs -= 2000;
        }
    }

    QSignalSpy transmittedSpy(&m_commander, &Commander::commandTransmitted);
    int meterCount = 0;
    int backgroundCount = 0;
    bool previousWasBackground = false;
    while (!m_commander.m_scheduledCommands.isEmpty())
    {
        m_commander.dispatchNextScheduledCommand();
        QVERIFY(!transmittedSpy.isEmpty());
        const Funcs dispatched = static_cast<Funcs>(transmittedSpy.constLast().at(0).toInt());
        const bool isMeter = dispatched == funcSMeter || dispatched == funcPowerMeter || dispatched == funcSWRMeter;
        if (isMeter)
        {
            ++meterCount;
            previousWasBackground = false;
        }
        else
        {
            ++backgroundCount;
            QVERIFY(!previousWasBackground);
            previousWasBackground = true;
        }
    }
    QCOMPARE(meterCount, meterFunctions.size());
    QCOMPARE(backgroundCount, backgroundFunctions.size());
}

void CommanderCodecTest::schedulerEscapesPermanentMeterOverdueState()
{
    m_commander.scheduleStartupRead(funcRfGain, 0);
    m_commander.scheduleMeterRead(funcSMeter, 0);
    for (Commander::ScheduledCommand& command : m_commander.m_scheduledCommands)
    {
        command.enqueuedAtMs -= 2000;
    }
    m_commander.m_lastBackgroundDispatchMs = -2000;

    QSignalSpy transmittedSpy(&m_commander, &Commander::commandTransmitted);
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(transmittedSpy.size(), 1);
    QCOMPARE(static_cast<Funcs>(transmittedSpy.constFirst().at(0).toInt()), funcRfGain);
}

void CommanderCodecTest::schedulerCoalescesInteractiveActionsAndPreservesReadProgress()
{
    int deliveredValue = -1;
    for (int value = 0; value < 100; ++value)
    {
        m_commander.scheduleInteractiveAction(funcRfGain, 0, [&deliveredValue, value]() { deliveredValue = value; });
    }
    QCOMPARE(m_commander.m_scheduledCommands.size(), 1);
    QCOMPARE(m_commander.schedulerDiagnostics().coalescedCommands, quint64(99));
    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(deliveredValue, 99);

    int interactiveCount = 0;
    m_commander.scheduleInteractiveAction(funcRfGain, 0, [&interactiveCount]() { ++interactiveCount; });
    m_commander.scheduleStartupRead(funcRfGain, 0);
    QCOMPARE(m_commander.m_scheduledCommands.size(), 2);
    m_commander.m_scheduledCommands.removeLast();
    m_commander.scheduleInteractiveAction(funcSquelch, 0, [&interactiveCount]() { ++interactiveCount; });
    m_commander.scheduleInteractiveAction(funcNRLevel, 0, [&interactiveCount]() { ++interactiveCount; });
    m_commander.scheduleInteractiveAction(funcNBLevel, 0, [&interactiveCount]() { ++interactiveCount; });
    m_commander.scheduleStartupRead(funcAttenuator, 0);
    m_commander.m_consecutiveInteractiveDispatches = 3;
    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(interactiveCount, 0);
    QCOMPARE(m_commander.m_scheduledCommands.size(), 4);
}

void CommanderCodecTest::pacesInteractiveConfirmationAfterSet()
{
    int setDispatches = 0;
    int confirmationDispatches = 0;
    m_commander.scheduleInteractiveAction(funcFreqSet, 0,
                                          [this, &setDispatches, &confirmationDispatches]()
                                          {
                                              ++setDispatches;
                                              m_commander.scheduleConfirmatoryAction(funcFreqGet, 0,
                                                                                     [&confirmationDispatches]()
                                                                                     { ++confirmationDispatches; });
                                          });

    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(setDispatches, 1);
    QCOMPARE(confirmationDispatches, 0);
    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(1));
    QCOMPARE(m_commander.m_scheduledCommands.first().commandClass, Commander::ScheduledCommandClass::ConfirmatoryRead);

    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(confirmationDispatches, 1);
    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(0));
}

void CommanderCodecTest::reportsMeterTransmissionAtWireDispatch()
{
    QSignalSpy transmittedSpy(&m_commander, &Commander::commandTransmitted);
    m_commander.scheduleMeterRead(funcSMeter, 1);

    QCOMPARE(transmittedSpy.size(), 0);
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(transmittedSpy.size(), 1);
    QCOMPARE(static_cast<Funcs>(transmittedSpy.at(0).at(0).toInt()), funcSMeter);
    QCOMPARE(transmittedSpy.at(0).at(1).toUInt(), uint(1));
    QCOMPARE(transmittedSpy.at(0).at(2).toLongLong(), m_commander.correlationDiagnostics().replyTimeoutMs);
    QCOMPARE(m_commander.schedulerDiagnostics().transmittedFrames, quint64(1));
    QCOMPARE(m_commander.schedulerDiagnostics().scheduledFrames, quint64(1));
    QCOMPARE(m_commander.schedulerDiagnostics().directFrames, quint64(0));

    m_commander.receiveCommandNoReadback(funcRfGain, QVariant::fromValue<ushort>(128), 0);
    QCOMPARE(m_commander.schedulerDiagnostics().transmittedFrames, quint64(2));
    QCOMPARE(m_commander.schedulerDiagnostics().scheduledFrames, quint64(1));
    QCOMPARE(m_commander.schedulerDiagnostics().directFrames, quint64(1));
}

void CommanderCodecTest::sMeterMainReadAvoidsContextCommands()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    m_commander.scheduleSMeterRead(0);
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(wireSpy.count(), 1);
    QCOMPARE(wireSpy.at(0).at(0).toByteArray(), QByteArray::fromHex("fefea2e11502"));
    QVERIFY(!m_commander.m_receiverScopedReadActive);
    QCOMPARE(m_commander.correlationDiagnostics().mainSMeterRequests, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().mainSMeterReplies, quint64(0));
}

void CommanderCodecTest::sMeterSubReadWaitsForReplyFamilyDrainBeforeSelectingContext()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    m_commander.beginReplyFamilyDrain(funcSMeter, 500);

    m_commander.scheduleSMeterRead(1);
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(wireSpy.count(), 0);
    QVERIFY(!m_commander.m_receiverScopedReadActive);
    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(1));

    m_commander.m_replyFamilyDrains.first().untilMs = m_commander.m_pendingCommandClock.elapsed() - 1;
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(wireSpy.count(), 1);
    QCOMPARE(wireSpy.at(0).at(0).toByteArray(), QByteArray::fromHex("fefea2e107d1"));
    QVERIFY(m_commander.m_receiverScopedReadActive);
    m_commander.abortSMeterRead(1);
    QTRY_VERIFY_WITH_TIMEOUT(!m_commander.m_receiverScopedReadActive, 250);
    QCOMPARE(wireSpy.count(), 3);
    QCOMPARE(wireSpy.at(1).at(0).toByteArray(), QByteArray::fromHex("fefea2e107d0"));
    QCOMPARE(wireSpy.at(2).at(0).toByteArray(), QByteArray::fromHex("fefea2e1271200"));
    QCOMPARE(m_commander.correlationDiagnostics().subSMeterTimeouts, quint64(1));
    QCOMPARE(m_commander.schedulerDiagnostics().scheduledFrames, quint64(3));
    QCOMPARE(m_commander.schedulerDiagnostics().directFrames, quint64(0));
}

void CommanderCodecTest::sMeterSubReadRestoresContextAfterReply()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    m_commander.scheduleSMeterRead(1);
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(wireSpy.count(), 1);
    QCOMPARE(wireSpy.at(0).at(0).toByteArray(), QByteArray::fromHex("fefea2e107d1"));
    QVERIFY(m_commander.m_receiverScopedReadActive);
    QTRY_COMPARE_WITH_TIMEOUT(wireSpy.count(), 2, 250);
    QCOMPARE(wireSpy.at(1).at(0).toByteArray(), QByteArray::fromHex("fefea2e11502"));

    m_commander.handleNewData(QByteArray::fromHex("fefee1a215020000fd"));

    QCOMPARE(wireSpy.count(), 4);
    QCOMPARE(wireSpy.at(2).at(0).toByteArray(), QByteArray::fromHex("fefea2e107d0"));
    QCOMPARE(wireSpy.at(3).at(0).toByteArray(), QByteArray::fromHex("fefea2e1271200"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_commander.m_receiverScopedReadActive, 250);
    QCOMPARE(m_commander.correlationDiagnostics().subSMeterRequests, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().subSMeterReplies, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().subSMeterTimeouts, quint64(0));
    QCOMPARE(m_commander.schedulerDiagnostics().scheduledFrames, quint64(3));
    QCOMPARE(m_commander.schedulerDiagnostics().directFrames, quint64(1));
}

void CommanderCodecTest::staleSMeterTimerDoesNotAffectLaterRead()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    const quint64 initialGeneration = m_commander.m_smeterScopedReadGeneration;

    m_commander.scheduleSMeterRead(1);
    m_commander.dispatchNextScheduledCommand();
    QCOMPARE(wireSpy.count(), 1);
    m_commander.abortSMeterRead(1);
    QCOMPARE(wireSpy.count(), 3);

    // The first transaction still owns a delayed 50 ms callback. Queue a new
    // transaction while its 25 ms context-restoration guard is active so the
    // scheduler starts the replacement before that stale callback fires.
    m_commander.scheduleSMeterRead(1);
    QTRY_COMPARE_WITH_TIMEOUT(m_commander.m_smeterScopedReadGeneration, initialGeneration + 2, 100);
    QTRY_COMPARE_WITH_TIMEOUT(wireSpy.count(), 5, 250);
    QCOMPARE(wireSpy.at(3).at(0).toByteArray(), QByteArray::fromHex("fefea2e107d1"));
    QCOMPARE(wireSpy.at(4).at(0).toByteArray(), QByteArray::fromHex("fefea2e11502"));
    QCOMPARE(m_commander.correlationDiagnostics().subSMeterRequests, quint64(1));

    m_commander.handleNewData(QByteArray::fromHex("fefee1a215020000fd"));
    QCOMPARE(m_commander.correlationDiagnostics().subSMeterReplies, quint64(1));
    QTRY_VERIFY_WITH_TIMEOUT(!m_commander.m_receiverScopedReadActive, 250);
}

void CommanderCodecTest::sMeterSubReadPreservesOperatorSelectedSub()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    m_commander.setOperatorSelectedVfo(vfoSub);
    m_commander.scheduleSMeterRead(1);
    m_commander.dispatchNextScheduledCommand();

    QCOMPARE(wireSpy.count(), 1);
    QCOMPARE(wireSpy.at(0).at(0).toByteArray(), QByteArray::fromHex("fefea2e11502"));
    m_commander.handleNewData(QByteArray::fromHex("fefee1a215020000fd"));

    QCOMPARE(wireSpy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!m_commander.m_receiverScopedReadActive, 250);
}

void CommanderCodecTest::receiverScopedReadPreservesOperatorSelectedSub()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);
    m_commander.setOperatorSelectedVfo(vfoSub);
    m_commander.requestReceiverScopedRead(funcModeGet, 1);

    QTRY_COMPARE_WITH_TIMEOUT(wireSpy.count(), 2, 250);
    QCOMPARE(wireSpy.at(0).at(0).toByteArray(), QByteArray::fromHex("fefea2e107d1"));
    QCOMPARE(wireSpy.at(1).at(0).toByteArray(), QByteArray::fromHex("fefea2e104"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_commander.m_receiverScopedReadActive, 250);
    QCOMPARE(wireSpy.count(), 2);
}

void CommanderCodecTest::survivesCombinedTransportAndSchedulerFaultSoak()
{
    CivSequenceGate gate;
    quint16 sequence = 100;
    int latestInteractiveValue = -1;
    constexpr int kCycles = 500;
    constexpr int kUpdatesPerCycle = 40;
    // Twenty thousand interactive updates run alongside duplicated, lost, and
    // reordered CI-V replies.

    const auto deliver = [this, &gate](quint16 datagramSequence, const QByteArray& frame)
    {
        const CivSequenceGateResult result = gate.accept(datagramSequence, frame);
        if (result)
        {
            m_commander.handleNewData(*result);
        }
    };

    for (int cycle = 0; cycle < kCycles; ++cycle)
    {
        // Simulate a stalled consumer accumulating high-rate replaceable work.
        // Hold the same scheduler gate used by an active MAIN/SUB exchange;
        // every logical key must remain bounded to one latest queued value.
        m_commander.m_mainSubExchangeConfirmationPending = true;
        for (int update = 0; update < kUpdatesPerCycle; ++update)
        {
            const int value = cycle * kUpdatesPerCycle + update;
            m_commander.scheduleInteractiveAction(funcRfGain, cycle % 2, [&latestInteractiveValue, value]()
                                                  { latestInteractiveValue = value; });
            m_commander.scheduleMeterRead(funcSMeter, 0);
            m_commander.scheduleMeterRead(funcSMeter, 1);
        }
        QVERIFY(m_commander.m_scheduledCommands.size() <= qsizetype(3));

        // Frequency and mode are distinct canonical families and may be live
        // together. Deliver them in reverse transport order, duplicate both,
        // and periodically lose frequency before injecting a late response.
        m_commander.rememberPendingReply(funcFreqGet, static_cast<uchar>(cycle % 2));
        m_commander.rememberPendingReply(funcModeGet, static_cast<uchar>((cycle + 1) % 2));

        const quint16 frequencySequence = sequence++;
        const quint16 modeSequence = sequence++;
        const QByteArray frequencyReply = QByteArray::fromHex("fefee1a2030052140600fd");
        const QByteArray modeReply = QByteArray::fromHex("fefee1a2040501fd");

        deliver(modeSequence, modeReply);
        deliver(modeSequence, modeReply);
        if (cycle % 17 == 0)
        {
            m_commander.discardPendingReplies(funcFreqGet);
            QTest::ignoreMessage(
                QtWarningMsg, QRegularExpression(QStringLiteral("Discarding unattributed receiver-less CI-V reply.*")));
            deliver(sequence++, frequencyReply);
        }
        else
        {
            deliver(frequencySequence, frequencyReply);
            deliver(frequencySequence, frequencyReply);
        }

        m_commander.finishMainSubExchangeConfirmation();
        while (!m_commander.m_scheduledCommands.isEmpty())
        {
            m_commander.dispatchNextScheduledCommand();
        }
        QCOMPARE(latestInteractiveValue, (cycle + 1) * kUpdatesPerCycle - 1);

        // Advance the deterministic soak beyond any real-time drain without
        // sleeping; drain timing behavior has dedicated timer-based tests.
        m_commander.m_pendingReplies.clear();
        m_commander.m_replyFamilyDrains.clear();
        m_commander.m_deferredReplyReads.clear();
    }

    QCOMPARE(m_commander.m_scheduledCommands.size(), qsizetype(0));
    QCOMPARE(m_commander.m_pendingReplies.size(), qsizetype(0));
    QCOMPARE(m_commander.m_deferredReplyReads.size(), qsizetype(0));
    QCOMPARE(m_commander.schedulerDiagnostics().droppedCommands, quint64(0));
    QVERIFY(m_commander.schedulerDiagnostics().highWaterMark <= qsizetype(3));
    QVERIFY(m_commander.correlationDiagnostics().drainedReplyFrames > 0);
    QVERIFY(gate.diagnostics().duplicatesSuppressed > 0);
    QVERIFY(gate.diagnostics().reordered > 0);
    QVERIFY(gate.diagnostics().highWaterMark <= CivSequenceGate::kRecentSequenceWindow);
}

void CommanderCodecTest::sessionResetCancelsTransactionalAndScopeState()
{
    m_commander.rememberPendingReply(funcFreqGet, 1);
    m_commander.m_deferredReplyReads.append({funcModeGet, 1});
    m_commander.m_replyFamilyDrains.append({funcFreqGet, 5000});
    m_commander.m_mainSubExchangeQueued = true;
    m_commander.m_mainSubExchangeConfirmationPending = true;
    m_commander.scheduleInteractiveAction(funcRfGain, 1, []() {});
    m_commander.mainScopeData.valid = true;
    m_commander.mainScopeData.data = QByteArrayLiteral("main-old-session");
    m_commander.subScopeData.valid = true;
    m_commander.subScopeData.data = QByteArrayLiteral("sub-old-session");
    m_commander.m_scopeAssemblyClocks[0].start();
    m_commander.m_scopeAssemblyClocks[1].start();
    m_commander.m_expectedScopeSequences[0] = 2;
    m_commander.m_expectedScopeSequences[1] = 3;

    m_commander.shutdownComm();

    QVERIFY(m_commander.m_pendingReplies.isEmpty());
    QVERIFY(m_commander.m_deferredReplyReads.isEmpty());
    QVERIFY(m_commander.m_replyFamilyDrains.isEmpty());
    QVERIFY(m_commander.m_scheduledCommands.isEmpty());
    QVERIFY(!m_commander.m_mainSubExchangeQueued);
    QVERIFY(!m_commander.m_mainSubExchangeConfirmationPending);
    QVERIFY(!m_commander.mainScopeData.valid);
    QVERIFY(m_commander.mainScopeData.data.isEmpty());
    QVERIFY(!m_commander.subScopeData.valid);
    QVERIFY(m_commander.subScopeData.data.isEmpty());
    QVERIFY(!m_commander.m_scopeAssemblyClocks[0].isValid());
    QVERIFY(!m_commander.m_scopeAssemblyClocks[1].isValid());
    QCOMPARE(m_commander.m_expectedScopeSequences[0], quint8(0));
    QCOMPARE(m_commander.m_expectedScopeSequences[1], quint8(0));
    QCOMPARE(m_commander.queue->diagnostics().depth, qsizetype(0));

    // A fresh session starts with independent diagnostics and no state from
    // the cancelled transaction generation.
    m_commander.m_shutdownComplete = false;
    m_commander.commonSetup();
    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(0));
    QCOMPARE(m_commander.schedulerDiagnostics().queuedCommands, qsizetype(0));
    QCOMPARE(m_commander.schedulerDiagnostics().highWaterMark, qsizetype(0));
    QVERIFY(!m_commander.mainScopeData.valid);
    QVERIFY(!m_commander.subScopeData.valid);
}

void CommanderCodecTest::correlatesEquivalentFrequencyAndModeReplyCommands()
{
    m_commander.rememberPendingReply(funcFreqGet, 1);
    m_commander.rememberPendingReply(funcModeGet, 1);

    uchar receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedFreq, &receiver));
    QCOMPARE(receiver, uchar(1));
    receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedMode, &receiver));
    QCOMPARE(receiver, uchar(1));
}

void CommanderCodecTest::discardsPendingRepliesByCanonicalFamily()
{
    m_commander.rememberPendingReply(funcFreqGet, 1);
    m_commander.rememberPendingReply(funcModeGet, 0);

    m_commander.discardPendingReplies(funcSelectedFreq);

    uchar receiver = 0;
    QVERIFY(!m_commander.takePendingReplyReceiver(funcFreqTR, &receiver));
    QVERIFY(m_commander.replyFamilyDraining(funcFreqGet));
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedMode, &receiver));
    QCOMPARE(receiver, uchar(0));
}

void CommanderCodecTest::serializesReceiverlessReadsByCanonicalFamily()
{
    m_commander.m_pendingCommandClock.restart();
    m_commander.rememberPendingReply(funcFreqGet, 0);
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    QVERIFY(m_commander.deferReplyReadIfBlocked(funcFreqGet, 1));
    QVERIFY(m_commander.deferReplyReadIfBlocked(funcFreqGet, 1));
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 1);
    QCOMPARE(m_commander.correlationDiagnostics().deferredReplyReads, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().coalescedReplyReads, quint64(1));

    uchar receiver = 0xff;
    QVERIFY(m_commander.takePendingReplyReceiver(funcSelectedFreq, &receiver));
    QCOMPARE(receiver, uchar(0));
    QVERIFY(m_commander.replyFamilyBlocked(funcFreq));

    QTRY_COMPARE_WITH_TIMEOUT(m_commander.m_pendingReplies.size(), 1, 250);
    QCOMPARE(m_commander.m_pendingReplies.constFirst().receiver, uchar(1));
    QCOMPARE(m_commander.m_deferredReplyReads.size(), 0);
    QCOMPARE(wireSpy.count(), 3);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("07d1")));
    QVERIFY(wireSpy.at(1).at(0).toByteArray().contains(QByteArray::fromHex("03")));
    QVERIFY(wireSpy.at(2).at(0).toByteArray().contains(QByteArray::fromHex("07d0")));

    m_commander.m_pendingReplies.clear();
    m_commander.m_replyFamilyDrains.clear();
    m_commander.rememberPendingReply(funcModeGet, 0);
    for (int receiverIndex = 0; receiverIndex < 70; ++receiverIndex)
    {
        if (receiverIndex >= 64)
        {
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression(QStringLiteral("CI-V deferred reply-read queue full.*")));
        }
        m_commander.deferReplyReadIfBlocked(funcModeGet, static_cast<uchar>(receiverIndex));
    }
    QCOMPARE(m_commander.m_deferredReplyReads.size(), qsizetype(64));
    QCOMPARE(m_commander.correlationDiagnostics().droppedReplyReads, quint64(6));
}

void CommanderCodecTest::discardsLateReplyDuringFamilyDrain()
{
    m_commander.m_pendingCommandClock.restart();
    m_commander.queue->resetSessionState();
    m_commander.rememberPendingReply(funcFreqGet, 0);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a2030052140600fd"));
    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(0));

    QSignalSpy cacheSpy(m_commander.queue, &CachingQueue::cacheUpdated);
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("Discarding unattributed receiver-less CI-V reply.*")));
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2030052140600fd"));
    QCOMPARE(cacheSpy.count(), 0);
    QCOMPARE(m_commander.correlationDiagnostics().drainedReplyFrames, quint64(1));
}

void CommanderCodecTest::adaptsReplyDrainWindowsToMeasuredRtt()
{
    CivRttEstimator estimator;
    QCOMPARE(estimator.resolvedDrainMs(), qint64(50));
    QCOMPARE(estimator.abandonedDrainMs(), qint64(500));
    QCOMPARE(estimator.replyTimeoutMs(), qint64(1000));

    estimator.observe(40);
    QCOMPARE(estimator.sampleCount(), quint64(1));
    QCOMPARE(estimator.resolvedDrainMs(), qint64(60));
    QCOMPARE(estimator.abandonedDrainMs(), qint64(120));
    QCOMPARE(estimator.replyTimeoutMs(), qint64(300));

    for (int sample = 0; sample < 20; ++sample)
    {
        estimator.observe(40);
    }
    const qint64 settledResolved = estimator.resolvedDrainMs();
    const qint64 settledAbandoned = estimator.abandonedDrainMs();
    QVERIFY(settledResolved < 60);
    QCOMPARE(settledAbandoned, qint64(100));

    estimator.observe(400);
    QVERIFY(estimator.resolvedDrainMs() > settledResolved);
    QVERIFY(estimator.abandonedDrainMs() > settledAbandoned);
    QVERIFY(estimator.resolvedDrainMs() <= 250);
    QVERIFY(estimator.abandonedDrainMs() <= 2000);
    QVERIFY(estimator.replyTimeoutMs() > 300);
    QVERIFY(estimator.replyTimeoutMs() <= 3000);

    m_commander.m_rttEstimator = estimator;
    const CommanderCorrelationDiagnostics diagnostics = m_commander.correlationDiagnostics();
    QCOMPARE(diagnostics.rttSampleCount, estimator.sampleCount());
    QCOMPARE(diagnostics.resolvedReplyDrainMs, estimator.resolvedDrainMs());
    QCOMPARE(diagnostics.abandonedReplyDrainMs, estimator.abandonedDrainMs());
    QCOMPARE(diagnostics.replyTimeoutMs, estimator.replyTimeoutMs());
}

void CommanderCodecTest::encodesAndDecodesPackedBcd()
{
    QCOMPARE(m_commander.bcdEncodeInt(quint16(4123)), QByteArray::fromHex("4123"));
    QCOMPARE(Commander::bcdHexToUInt(0x41, 0x23), 4123U);
    QCOMPARE(m_commander.bcdEncodeInt(654321U), QByteArray::fromHex("654321"));
    QCOMPARE(Commander::bcdHexToUInt(0x65, 0x43, 0x21), 654321U);
    QVERIFY(m_commander.bcdEncodeInt(quint16(10000)).isEmpty());
    QVERIFY(m_commander.bcdEncodeInt(1000000U).isEmpty());
}

void CommanderCodecTest::frequencyPayloadRoundTrips_data()
{
    QTest::addColumn<quint64>("hz");
    QTest::newRow("2m") << quint64(146520000);
    QTest::newRow("70cm") << quint64(440000000);
    QTest::newRow("23cm") << quint64(1296000000);
    QTest::newRow("six-byte") << quint64(10000000000ULL);
}

void CommanderCodecTest::frequencyPayloadRoundTrips()
{
    QFETCH(quint64, hz);
    Frequency frequency;
    frequency.Hz = hz;
    const QByteArray encoded = m_commander.makeFreqPayload(frequency);
    QCOMPARE(encoded.size(), hz >= 10000000000ULL ? 6 : 5);
    QCOMPARE(m_commander.parseFreqDataToInt(encoded), hz);
}

void CommanderCodecTest::tonePayloadRoundTrips()
{
    for (const quint16 tone : {quint16(670), quint16(1273), quint16(2541)})
    {
        const ToneInfo decoded = m_commander.decodeTone(m_commander.encodeTone(tone, true, true));
        QCOMPARE(decoded.tone, tone);
        QVERIFY(decoded.tinv);
        QVERIFY(decoded.rinv);
    }
    const ToneInfo shortTone = m_commander.decodeTone(QByteArray::fromHex("01"));
    QCOMPARE(shortTone.tone, quint16(670));
}

void CommanderCodecTest::parsesModesAndUnknownMode()
{
    QCOMPARE(m_commander.parseMode(5, 0, 2).mk, modeFM);
    QCOMPARE(m_commander.parseMode(17, 1, 1).mk, modeDV);
    const ModeInfo unknown = m_commander.parseMode(0x7f, 0, 1);
    QCOMPARE(unknown.mk, modeUnknown);
}

void CommanderCodecTest::parsesFrequencyReplyFamily()
{
    Frequency expected;
    expected.Hz = 145825000;
    m_commander.payloadIn = QByteArray(1, '\x01') + m_commander.makeFreqPayload(expected);
    Funcs func = funcFreq;
    QVariant value;
    uchar receiver = 0;

    QCOMPARE(m_commander.parseFrequencyReply(func, value, receiver), Commander::ReplyParseResult::Parsed);
    QCOMPARE(func, funcFreq);
    QCOMPARE(receiver, uchar(1));
    QCOMPARE(value.value<Frequency>().Hz, expected.Hz);
}

void CommanderCodecTest::parsesModeReplyFamily()
{
    m_commander.payloadIn = QByteArray::fromHex("01050102");
    Funcs func = funcMode;
    QVariant value;
    uchar receiver = 0;

    QCOMPARE(m_commander.parseModeReply(func, value, receiver), Commander::ReplyParseResult::Parsed);
    QCOMPARE(func, funcMode);
    QCOMPARE(receiver, uchar(1));
    const ModeInfo mode = value.value<ModeInfo>();
    QCOMPARE(mode.mk, modeFM);
    QCOMPARE(mode.data, uchar(1));
    QCOMPARE(mode.filter, uchar(2));
    QCOMPARE(mode.VFO, selVFO_t(1));
}

void CommanderCodecTest::rejectsMalformedFrequencyAndModeReplies()
{
    QVariant value;
    uchar receiver = 0;

    Funcs frequencyFunc = funcFreq;
    m_commander.payloadIn.clear();
    QCOMPARE(m_commander.parseFrequencyReply(frequencyFunc, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    Funcs modeFunc = funcMode;
    m_commander.payloadIn.clear();
    QCOMPARE(m_commander.parseModeReply(modeFunc, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    modeFunc = funcDataModeWithFilter;
    m_commander.payloadIn = QByteArray(1, '\x01');
    QCOMPARE(m_commander.parseModeReply(modeFunc, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());
}

void CommanderCodecTest::parsesLevelAndMeterReplyFamily()
{
    QVariant value;

    m_commander.payloadIn = QByteArray::fromHex("0123");
    QCOMPARE(m_commander.parseLevelMeterReply(funcAfGain, value), Commander::ReplyParseResult::Parsed);
    QCOMPARE(value.toUInt(), 123U);

    m_commander.payloadIn = QByteArray::fromHex("0192");
    QCOMPARE(m_commander.parseLevelMeterReply(funcCompressorLevel, value), Commander::ReplyParseResult::Parsed);
    QCOMPARE(value.toUInt(), 192U);

    m_commander.payloadIn = QByteArray::fromHex("01230000");
    QCOMPARE(m_commander.parseLevelMeterReply(funcAbsoluteMeter, value), Commander::ReplyParseResult::Parsed);
    const MeterKind meter = value.value<MeterKind>();
    QCOMPARE(meter.value, 12.3);
    QCOMPARE(meter.type, meterdBu);

    QCOMPARE(m_commander.parseLevelMeterReply(funcToneFreq, value), Commander::ReplyParseResult::NotHandled);
}

void CommanderCodecTest::rejectsMalformedLevelAndMeterReplies()
{
    QVariant value;
    m_commander.payloadIn = QByteArray(1, '\0');
    QCOMPARE(m_commander.parseLevelMeterReply(funcAfGain, value), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    m_commander.payloadIn = QByteArray(3, '\0');
    QCOMPARE(m_commander.parseLevelMeterReply(funcAbsoluteMeter, value), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());
}

void CommanderCodecTest::parsesFeatureAndScopeReplyFamilies()
{
    QVariant value;

    m_commander.payloadIn = QByteArray(1, '\x01');
    QCOMPARE(m_commander.parseFeatureReply(funcNoiseBlanker, value, 0), Commander::ReplyParseResult::Parsed);
    QVERIFY(value.toBool());

    uchar receiver = 0;
    m_commander.payloadIn = QByteArray::fromHex("0102");
    QCOMPARE(m_commander.parseScopeReply(funcScopeMode, value, receiver), Commander::ReplyParseResult::Parsed);
    QCOMPARE(receiver, uchar(1));
    QCOMPARE(value.value<uchar>(), uchar(2));

    QCOMPARE(m_commander.parseScopeReply(funcToneFreq, value, receiver), Commander::ReplyParseResult::NotHandled);
}

void CommanderCodecTest::rejectsMalformedFeatureAndScopeReplies()
{
    QVariant value;
    m_commander.payloadIn.clear();
    QCOMPARE(m_commander.parseFeatureReply(funcNoiseBlanker, value, 0), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());

    uchar receiver = 0;
    m_commander.payloadIn = QByteArray(1, '\0');
    QCOMPARE(m_commander.parseScopeReply(funcScopeMode, value, receiver), Commander::ReplyParseResult::Malformed);
    QVERIFY(!value.isValid());
}

void CommanderCodecTest::parsesMemoryFields()
{
    MemoryType memory;
    m_commander.initializeMemoryForParsing(memory);

    m_commander.parseMemoryField(MemParserFormat('a', 0, 1), QByteArray::fromHex("02"), memory);
    m_commander.parseMemoryField(MemParserFormat('b', 0, 2), QByteArray::fromHex("0042"), memory);
    Frequency frequency;
    frequency.Hz = 145825000;
    m_commander.parseMemoryField(MemParserFormat('f', 0, 5), m_commander.makeFreqPayload(frequency), memory);
    m_commander.parseMemoryField(MemParserFormat('z', 0, 8), QByteArray("SAT TEST"), memory);

    QCOMPARE(memory.group, quint16(2));
    QCOMPARE(memory.channel, quint16(42));
    QCOMPARE(memory.frequency.Hz, quint64(145825000));
    QCOMPARE(QByteArray(memory.name, 8), QByteArray("SAT TEST"));

    for (quint8 polarity = 0; polarity <= 3; ++polarity)
    {
        const char encoded = static_cast<char>(((polarity << 3) & 0x10) | (polarity & 0x01));
        m_commander.parseMemoryField(MemParserFormat('p', 0, 1), QByteArray(1, encoded), memory);
        m_commander.parseMemoryField(MemParserFormat('P', 0, 1), QByteArray(1, encoded), memory);
        QCOMPARE(memory.dtcsp, polarity);
        QCOMPARE(memory.dtcspB, polarity);
    }
}

void CommanderCodecTest::serializesOutboundCommandValues()
{
    QByteArray payload;
    const FuncType boolCommand = m_commander.radioCaps.commands.value(funcNoiseBlanker);
    QVERIFY(m_commander.appendSetCommandValue(funcNoiseBlanker, QVariant::fromValue(true), 0, boolCommand, payload));
    QCOMPARE(payload, QByteArray(1, '\x01'));

    payload.clear();
    const FuncType agcCommand = m_commander.radioCaps.commands.value(funcAGCTimeConstant);
    QVERIFY(
        m_commander.appendSetCommandValue(funcAGCTimeConstant, QVariant::fromValue<uchar>(3), 0, agcCommand, payload));
    QCOMPARE(payload, QByteArray(1, '\x03'));

    payload.clear();
    const FuncType compressorLevelCommand = m_commander.radioCaps.commands.value(funcCompressorLevel);
    QVERIFY(m_commander.appendSetCommandValue(funcCompressorLevel, QVariant::fromValue<ushort>(192), 0,
                                              compressorLevelCommand, payload));
    QCOMPARE(payload, QByteArray::fromHex("0192"));

    payload.clear();
    const FuncType nbLevelCommand = m_commander.radioCaps.commands.value(funcNBLevel);
    QVERIFY(
        m_commander.appendSetCommandValue(funcNBLevel, QVariant::fromValue<ushort>(128), 0, nbLevelCommand, payload));
    QCOMPARE(payload, QByteArray::fromHex("0128"));

    payload.clear();
    const FuncType nrLevelCommand = m_commander.radioCaps.commands.value(funcNRLevel);
    QVERIFY(
        m_commander.appendSetCommandValue(funcNRLevel, QVariant::fromValue<ushort>(255), 1, nrLevelCommand, payload));
    QCOMPARE(payload, QByteArray::fromHex("0255"));

    Frequency frequency;
    frequency.Hz = 145825000;
    payload.clear();
    const FuncType frequencyCommand = m_commander.radioCaps.commands.value(funcFreqSet);
    QVERIFY(
        m_commander.appendSetCommandValue(funcFreqSet, QVariant::fromValue(frequency), 0, frequencyCommand, payload));
    QCOMPARE(m_commander.parseFreqDataToInt(payload), frequency.Hz);

    payload.clear();
    const FuncType satelliteMemoryCommand = m_commander.radioCaps.commands.value(funcSatelliteMemory);
    QVERIFY(m_commander.appendSetCommandValue(funcSatelliteMemory, QVariant::fromValue<uint>(42), 0,
                                              satelliteMemoryCommand, payload));
    QCOMPARE(payload, QByteArray::fromHex("0042"));
}

void CommanderCodecTest::serializesDialLockSetAndRead()
{
    QSignalSpy wireSpy(&m_commander, &Commander::dataForComm);

    m_commander.receiveCommandNoReadback(funcDialLock, QVariant::fromValue<bool>(true), 0);
    m_commander.receiveCommand(funcDialLock, QVariant(), 0);

    QCOMPARE(wireSpy.count(), 2);
    QVERIFY(wireSpy.at(0).at(0).toByteArray().contains(QByteArray::fromHex("165001")));
    QVERIFY(wireSpy.at(1).at(0).toByteArray().contains(QByteArray::fromHex("1650")));
}

void CommanderCodecTest::rejectsUnknownOutboundValueTypes()
{
    QByteArray payload;
    const FuncType command = m_commander.radioCaps.commands.value(funcNoiseBlanker);
    QVERIFY(!m_commander.appendSetCommandValue(funcNoiseBlanker, QVariant::fromValue(QRect(1, 2, 3, 4)), 0, command,
                                               payload));
    QVERIFY(payload.isEmpty());
}

void CommanderCodecTest::acknowledgementsAreDiagnosticOnly()
{
    m_commander.queue->resetSessionState();
    m_commander.radioPoweredOn = true;
    QSignalSpy cacheSpy(m_commander.queue, &CachingQueue::cacheUpdated);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a2fbfd"));
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2fafd"));
    QCOMPARE(cacheSpy.count(), 0);
    QCOMPARE(m_commander.correlationDiagnostics().acceptedAcknowledgements, quint64(1));
    QCOMPARE(m_commander.correlationDiagnostics().rejectedAcknowledgements, quint64(1));

    m_commander.handleNewData(QByteArray::fromHex("fefee1a21c0000fd"));
    cacheSpy.clear();
    m_commander.handleNewData(QByteArray::fromHex("fefee1a21c0001fd"));
    QTRY_COMPARE(cacheSpy.count(), 1);
    const CacheItem update = qvariant_cast<CacheItem>(cacheSpy.takeFirst().at(0));
    QCOMPARE(update.command, funcTransceiverStatus);
    QVERIFY(update.value.toBool());
}

void CommanderCodecTest::tracksPendingReplyPressure()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("CI-V pending reply overflow.*")));
    for (int i = 0; i < 65; ++i)
    {
        m_commander.rememberPendingReply(funcFreqGet, static_cast<uchar>(i % 2));
    }

    const CommanderCorrelationDiagnostics diagnostics = m_commander.correlationDiagnostics();
    QCOMPARE(diagnostics.pendingReplies, qsizetype(64));
    QCOMPARE(diagnostics.pendingReplyHighWaterMark, qsizetype(65));
    QCOMPARE(diagnostics.pendingReplyOverflows, quint64(1));
}

void CommanderCodecTest::unsolicitedUpdateDoesNotConsumePendingReply()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();
    m_commander.queue->resetSessionState();
    m_commander.rememberPendingReply(funcFreqGet, 1);

    m_commander.handleNewData(QByteArray::fromHex("fefe00a2030052140600fd"));

    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(1));
    QCOMPARE(m_commander.correlationDiagnostics().ambiguousUnsolicitedFrames, quint64(1));
    uchar receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcFreqGet, &receiver));
    QCOMPARE(receiver, uchar(1));
    QVERIFY(!m_commander.queue->peekCache(funcFreq, 0).value.isValid());
    QVERIFY(!m_commander.queue->peekCache(funcFreq, 1).value.isValid());
}

void CommanderCodecTest::acceptsOneMainFrequencyBroadcastAtEachPttTransition()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.queue->resetSessionState();
    QSignalSpy replySpy(&m_commander, &RadioCommander::radioReplyReceived);

    m_commander.setPttActive(true);
    m_commander.handleNewData(QByteArray::fromHex("fefe00a2000000954904fd"));

    QCOMPARE(replySpy.count(), 1);
    QList<QVariant> accepted = replySpy.takeFirst();
    QCOMPARE(accepted.at(0).value<Funcs>(), funcSelectedFreq);
    QCOMPARE(accepted.at(1).value<Frequency>().Hz, quint64(449950000));
    QCOMPARE(accepted.at(2).value<uchar>(), uchar(0));
    QCOMPARE(m_commander.correlationDiagnostics().ambiguousUnsolicitedFrames, quint64(0));

    // Each edge accepts only one broadcast.
    m_commander.handleNewData(QByteArray::fromHex("fefe00a2000000954904fd"));
    QCOMPARE(replySpy.count(), 0);
    QCOMPARE(m_commander.correlationDiagnostics().ambiguousUnsolicitedFrames, quint64(1));

    m_commander.setPttActive(false);
    m_commander.handleNewData(QByteArray::fromHex("fefe00a2000000264701fd"));

    QCOMPARE(replySpy.count(), 1);
    accepted = replySpy.takeFirst();
    QCOMPARE(accepted.at(0).value<Funcs>(), funcSelectedFreq);
    QCOMPARE(accepted.at(1).value<Frequency>().Hz, quint64(147260000));
    QCOMPARE(accepted.at(2).value<uchar>(), uchar(0));
    QCOMPARE(m_commander.correlationDiagnostics().ambiguousUnsolicitedFrames, quint64(1));

    // The exception is single-use. A later receiver-less broadcast remains
    // ambiguous and must not silently inherit MAIN identity.
    m_commander.handleNewData(QByteArray::fromHex("fefe00a2000000274701fd"));
    QCOMPARE(replySpy.count(), 0);
    QCOMPARE(m_commander.correlationDiagnostics().ambiguousUnsolicitedFrames, quint64(2));
}

void CommanderCodecTest::malformedReplyDoesNotConsumePendingReply()
{
    m_commander.m_pendingReplies.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();
    m_commander.rememberPendingReply(funcModeGet, 1);

    m_commander.handleNewData(QByteArray::fromHex("fefee1a204fd"));

    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(1));
    uchar receiver = 0;
    QVERIFY(m_commander.takePendingReplyReceiver(funcModeGet, &receiver));
    QCOMPARE(receiver, uchar(1));
}

void CommanderCodecTest::selectedRepliesUsePendingReceiverIdentity()
{
    // Commands 03h and 04h identify the currently selected side, not physical
    // MAIN. During a receiver-scoped SUB read SDR9700 selects SUB first, so the
    // outstanding request is the only reliable identity for the reply.
    m_commander.m_pendingReplies.clear();
    m_commander.m_replyFamilyDrains.clear();
    m_commander.m_correlationDiagnostics = {};
    m_commander.m_pendingCommandClock.restart();
    QSignalSpy replySpy(&m_commander, &RadioCommander::radioReplyReceived);

    m_commander.rememberPendingReply(funcFreqGet, 1);
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2030052140600fd"));
    QCOMPARE(replySpy.count(), 1);
    QCOMPARE(replySpy.takeFirst().at(2).value<uchar>(), uchar(1));
    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(0));

    m_commander.m_replyFamilyDrains.clear();
    m_commander.rememberPendingReply(funcModeGet, 1);
    m_commander.handleNewData(QByteArray::fromHex("fefee1a2040501fd"));
    QCOMPARE(replySpy.count(), 1);
    QCOMPARE(replySpy.takeFirst().at(2).value<uchar>(), uchar(1));
    QCOMPARE(m_commander.correlationDiagnostics().pendingReplies, qsizetype(0));
}

void CommanderCodecTest::rejectsShortSpectrumFrames()
{
    m_commander.radioCaps.spectSeqMax = 11;
    ScopeData scope;
    for (int length = 0; length < 14; ++length)
    {
        m_commander.payloadIn = QByteArray(length, '\0');
        QVERIFY(!m_commander.parseSpectrum(scope, 0));
    }
}

void CommanderCodecTest::assemblesMultiPacketSpectrum()
{
    m_commander.radioCaps.spectSeqMax = 3;
    m_commander.radioCaps.spectLenMax = 8;

    Frequency start;
    start.Hz = 144000000;
    Frequency end;
    end.Hz = 148000000;
    m_commander.payloadIn = QByteArray::fromHex("010301") + m_commander.makeFreqPayload(start) +
                            m_commander.makeFreqPayload(end) + QByteArray::fromHex("0011");
    ScopeData scope;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));

    m_commander.payloadIn = QByteArray::fromHex("0203aabb");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    m_commander.payloadIn = QByteArray::fromHex("0303ccdd");
    QVERIFY(m_commander.parseSpectrum(scope, 0));
    QCOMPARE(scope.startFreq, 144.0);
    QCOMPARE(scope.endFreq, 148.0);
    QCOMPARE(scope.data, QByteArray::fromHex("aabbccdd"));
    QVERIFY(scope.valid);
}

void CommanderCodecTest::boundsMultiPacketSpectrum()
{
    m_commander.radioCaps.spectSeqMax = 3;
    m_commander.radioCaps.spectLenMax = 4;

    Frequency start;
    start.Hz = 144000000;
    Frequency end;
    end.Hz = 148000000;
    const QByteArray first = QByteArray::fromHex("010301") + m_commander.makeFreqPayload(start) +
                             m_commander.makeFreqPayload(end) + QByteArray::fromHex("0000");

    ScopeData scope;
    m_commander.payloadIn = QByteArray::fromHex("010401") + m_commander.makeFreqPayload(start) +
                            m_commander.makeFreqPayload(end) + QByteArray::fromHex("0000");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Ignoring scope sequence.*")));
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QVERIFY(scope.data.isEmpty());
    QVERIFY(!scope.valid);

    m_commander.payloadIn = first;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    m_commander.payloadIn = QByteArray::fromHex("0203112233445566");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QCOMPARE(scope.data, QByteArray::fromHex("11223344"));
    m_commander.payloadIn = QByteArray::fromHex("0303778899aa");
    QVERIFY(m_commander.parseSpectrum(scope, 0));
    QCOMPARE(scope.data, QByteArray::fromHex("11223344"));
    QVERIFY(scope.valid);
}

void CommanderCodecTest::rejectsBrokenAndExpiredSpectrumAssemblies()
{
    m_commander.radioCaps.spectSeqMax = 3;
    m_commander.radioCaps.spectLenMax = 8;

    Frequency start;
    start.Hz = 144000000;
    Frequency end;
    end.Hz = 148000000;
    const QByteArray first = QByteArray::fromHex("010301") + m_commander.makeFreqPayload(start) +
                             m_commander.makeFreqPayload(end) + QByteArray::fromHex("0011");

    ScopeData scope;
    m_commander.payloadIn = first;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    m_commander.payloadIn = QByteArray::fromHex("0303ccdd");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QVERIFY(scope.data.isEmpty());

    m_commander.payloadIn = first;
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QTest::qWait(275);
    m_commander.payloadIn = QByteArray::fromHex("0203aabb");
    QVERIFY(!m_commander.parseSpectrum(scope, 0));
    QVERIFY(scope.data.isEmpty());
}

void CommanderCodecTest::parserToleratesDeterministicArbitraryInput()
{
    quint32 state = 0x9700;
    for (int iteration = 0; iteration < 500; ++iteration)
    {
        state = state * 1664525U + 1013904223U;
        const int length = int(state % 128U);
        QByteArray input(length, Qt::Uninitialized);
        for (int i = 0; i < length; ++i)
        {
            state = state * 1664525U + 1013904223U;
            input[i] = char(state >> 24);
        }
        m_commander.parseData(input);
    }
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(CommanderCodecTest)

#include "CommanderCodecTest.moc"
