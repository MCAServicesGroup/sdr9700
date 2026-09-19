#include "ConnectionRetryPolicy.h"
#include "AudioDeviceSelection.h"
#include "DualWatchTransitionPolicy.h"
#include "MemorySyncPolicy.h"
#include "MainSubExchangePolicy.h"
#include "PttConfirmationPolicy.h"
#include "ReceiverReadinessPolicy.h"
#include "RadioSessionOwnership.h"
#include "RadioSessionCorrelation.h"
#include "RadioSessionRecoveryStore.h"
#include "RetainedSessionRemovalPolicy.h"
#include "RxAudioStartPolicy.h"
#include "SpectrumTuningPolicy.h"
#include "StandbyWakePolicy.h"
#include "TransmitSafetyPolicy.h"
#include "TransmitFrequencyPolicy.h"
#include "TransmitConfigurationPolicy.h"
#include "TxAudioMeterPolicy.h"
#include "VfoReceiverCommandRoute.h"

#include <utility>
#include <QCoreApplication>
#include <QTest>
#include <array>
#include <cmath>

class OfflinePoliciesTest : public QObject
{
    Q_OBJECT

  private slots:
    void selectsAudioDevicesAcrossDefaultChangesAndHotplug();
    void latchesRxAudioInitializationFailureUntilDeviceRefresh();
    void clampsMemoryPollingInterval();
    void tracksMemorySynchronization();
    void retriesIncompleteOperationSynchronization();
    void advancesMemorySynchronizationSlots();
    void identifiesExpectedMemoryWriteReadback();
    void retriesRecoverableRadioConnectionFailures();
    void roundsTuningFrequency();
    void clampsFrequencyAndScopeCenterToBand();
    void requiresConsecutiveHighSwrReadings();
    void resetsTransmitSafetyWhenNotTransmitting();
    void convertsMagnitudeToBoundedDbfs();
    void aggregatesTransmitMeterBlocksByEnergy();
    void holdsTransmitMeterActivityAcrossSpeechGaps();
    void holdsAndDecaysTransmitMeterPeak();
    void holdsFullScaleIndicationForThreeSeconds();
    void keepsTransmitMeterValidAcrossSilence();
    void keepsPttActiveUntilRadioConfirmsUnkey();
    void republishesContradictoryKeyedReadbackAfterUnkey();
    void validatesDuplexTransmitFrequency();
    void blocksPttUntilTransmitConfigurationIsConfirmed();
    void serializesRepeatedMainSubExchanges();
    void requiresDualWatchStateAndSubIdentity();
    void permitsRadioTeardownOnlyAfterStreamOwnership();
    void correlatesRadioSessionResponses();
    void retainedTokenResetPolicyIsSingleShot();
    void boundsStandbyWakeBootstrap();
    void requiresCompleteTransportRecoveryIdentity();
    void waitsForRetainedTokenRemovalBeforeReplacementLogin();
    void refusesRecoveryWhileJournalOwnerIsAlive();
    void expiresMeterPollDeadlinesConservatively();
    void waitsForBothReceiverIdentitiesBeforeReadiness();
};

void OfflinePoliciesTest::selectsAudioDevicesAcrossDefaultChangesAndHotplug()
{
    struct Device
    {
        QByteArray deviceID;
        const QByteArray& id() const { return deviceID; }
    };
    const Device speakers{"speakers"};
    const Device headphones{"headphones"};
    const Device none{};
    const QList<Device> both{headphones, speakers};
    using sdr9700::selectAudioDevice;

    // Enumeration order must not become a preference. Default changes should
    // move an unpinned route, but never override an available saved device.
    QCOMPARE(selectAudioDevice(both, {}, speakers).id(), speakers.id());
    QCOMPARE(selectAudioDevice(both, {}, headphones).id(), headphones.id());
    QCOMPARE(selectAudioDevice(both, speakers.id(), headphones).id(), speakers.id());
    QCOMPARE(selectAudioDevice(both, headphones.id(), speakers).id(), headphones.id());
    QCOMPARE(selectAudioDevice(QList<Device>{speakers}, headphones.id(), speakers).id(), speakers.id());
    QCOMPARE(selectAudioDevice(both, headphones.id(), speakers).id(), headphones.id());
    QVERIFY(selectAudioDevice(QList<Device>{}, headphones.id(), none).id().isEmpty());
    QCOMPARE(selectAudioDevice(both, headphones.id(), speakers).id(), headphones.id());
}

void OfflinePoliciesTest::latchesRxAudioInitializationFailureUntilDeviceRefresh()
{
    sdr9700::audio::RxAudioStartPolicy policy;
    QVERIFY(!policy.shouldStart(false, false, true));
    QVERIFY(!policy.shouldStart(true, true, true));
    QVERIFY(!policy.shouldStart(true, false, false));
    QVERIFY(policy.shouldStart(true, false, true));

    policy.initializationFailed();
    QVERIFY(policy.initializationBlocked());
    QVERIFY(!policy.shouldStart(true, false, true));
    policy.deviceRefreshed();
    QVERIFY(!policy.initializationBlocked());
    QVERIFY(policy.shouldStart(true, false, true));
}

void OfflinePoliciesTest::expiresMeterPollDeadlinesConservatively()
{
    using sdr9700::backend::meterPollDeadlineExpired;
    QVERIFY(meterPollDeadlineExpired(false, -1, 300));
    QVERIFY(!meterPollDeadlineExpired(true, 299, 300));
    QVERIFY(meterPollDeadlineExpired(true, 300, 300));
    QVERIFY(meterPollDeadlineExpired(true, 301, 300));
}

void OfflinePoliciesTest::waitsForBothReceiverIdentitiesBeforeReadiness()
{
    using sdr9700::backend::receiverStateReady;
    QVERIFY(!receiverStateReady(false, false, false, false));
    QVERIFY(!receiverStateReady(true, true, false, false));
    QVERIFY(!receiverStateReady(true, true, true, false));
    QVERIFY(receiverStateReady(true, true, true, true));
}

void OfflinePoliciesTest::refusesRecoveryWhileJournalOwnerIsAlive()
{
    sdr9700::RadioSessionRecoveryRecord record;
    record.ownerProcessId = QCoreApplication::applicationPid();
    QVERIFY(sdr9700::RadioSessionRecoveryStore::ownerProcessIsRunning(record));
}

void OfflinePoliciesTest::waitsForRetainedTokenRemovalBeforeReplacementLogin()
{
    sdr9700::RetainedSessionRemovalPolicy policy;
    QVERIFY(!policy.pending());
    QVERIFY(!policy.acknowledge());

    policy.begin();
    QVERIFY(policy.pending());
    for (int attempt = 1; attempt <= sdr9700::RetainedSessionRemovalPolicy::kMaxAttempts; ++attempt)
    {
        QVERIFY(policy.takeAttempt());
        QCOMPARE(policy.attempts(), attempt);
        QVERIFY(policy.pending());
    }
    QVERIFY(policy.exhausted());
    QVERIFY(!policy.takeAttempt());

    policy.begin();
    QVERIFY(policy.takeAttempt());
    QVERIFY(policy.acknowledge());
    QVERIFY(!policy.pending());
    QVERIFY(!policy.exhausted());
    QVERIFY(!policy.acknowledge());
}

void OfflinePoliciesTest::boundsStandbyWakeBootstrap()
{
    using Action = sdr9700::StandbyWakePolicy::Action;
    sdr9700::StandbyWakePolicy policy;

    QCOMPARE(policy.commandPlaneUnavailable(), Action::RetrySession);
    QCOMPARE(policy.commandPlaneUnavailable(), Action::Wake);
    QCOMPARE(policy.wakeAttempts(), 1);
    QCOMPARE(policy.commandPlaneUnavailable(), Action::Wake);
    QCOMPARE(policy.wakeAttempts(), 2);
    QCOMPARE(policy.commandPlaneUnavailable(), Action::Fail);

    policy.reset();
    QCOMPARE(policy.commandPlaneReady(), Action::Continue);
    QVERIFY(policy.complete());
    QCOMPARE(policy.commandPlaneUnavailable(), Action::Continue);
    QCOMPARE(policy.wakeAttempts(), 0);
}

void OfflinePoliciesTest::requiresCompleteTransportRecoveryIdentity()
{
    sdr9700::RadioSessionRecoveryRecord record;
    QVERIFY(!record.hasTransportIdentities());
    record.control = {50001, 50001, 0x11111111, 0x22222222};
    record.civ = {50002, 50002, 0x33333333, 0x44444444};
    record.audio = {50003, 50003, 0x55555555, 0x66666666};
    QVERIFY(record.hasTransportIdentities());
    record.audio.remotePort = 0;
    QVERIFY(!record.hasTransportIdentities());
}

void OfflinePoliciesTest::correlatesRadioSessionResponses()
{
    sdr9700::RadioSessionRequest request;
    request.begin(0x1234, 0x5678, 0x9abcdef0);

    QVERIFY(request.matches(0x1234, 0x5678, 0x9abcdef0));
    QVERIFY(request.matchesIdentity(0x1234, 0x5678, 0x9abcdef0));
    QVERIFY(!request.matches(0x1235, 0x5678, 0x9abcdef0));
    QVERIFY(!request.matches(0x1234, 0x5679, 0x9abcdef0));
    QVERIFY(!request.matches(0x1234, 0x5678, 0x9abcdef1));
    QVERIFY(request.matchesLogin(0x1234, 0x5678));
    QVERIFY(request.matchesAuthenticationResponse(0x1234));
    // A token-reissue response is correlated by request identity, not by its
    // newly returned six-byte authentication identifier.
    QVERIFY(request.matchesAuthenticationResponse(0x1234));
    QVERIFY(!request.matchesAuthenticationResponse(0x1235));
    request.clear();
    QVERIFY(!request.matches(0x1234, 0x5678, 0x9abcdef0));
    QVERIFY(request.matchesIdentity(0x1234, 0x5678, 0x9abcdef0));

    QVERIFY(sdr9700::matchesRadioSessionEnvelope(0x11111111, 0x22222222, 0x11111111, 0x22222222));
    QVERIFY(!sdr9700::matchesRadioSessionEnvelope(0x33333333, 0x22222222, 0x11111111, 0x22222222));
    QVERIFY(!sdr9700::matchesRadioSessionEnvelope(0x11111111, 0x44444444, 0x11111111, 0x22222222));
}

void OfflinePoliciesTest::retainedTokenResetPolicyIsSingleShot()
{
    QVERIFY(sdr9700::shouldResetReissuedTokenAfterStreamRejection(true, false, 0xffffffff));
    QVERIFY(!sdr9700::shouldResetReissuedTokenAfterStreamRejection(false, false, 0xffffffff));
    QVERIFY(!sdr9700::shouldResetReissuedTokenAfterStreamRejection(true, true, 0xffffffff));
    QVERIFY(!sdr9700::shouldResetReissuedTokenAfterStreamRejection(true, false, 0));
}

void OfflinePoliciesTest::permitsRadioTeardownOnlyAfterStreamOwnership()
{
    constexpr int kRejectedConnectionCount = 10000;
    sdr9700::RadioSessionOwnership ownership;

    // Authentication and rejected stream negotiations never call acquire().
    // Repeating that lifecycle must not eventually authorize a token removal,
    // stream close, or departure packet against the actual session owner.
    for (int attempt = 0; attempt < kRejectedConnectionCount; ++attempt)
    {
        QVERIFY(!ownership.permitsRadioTeardown());
        ownership.release();
    }

    ownership.acquire();
    QVERIFY(ownership.permitsRadioTeardown());
    ownership.release();
    QVERIFY(!ownership.permitsRadioTeardown());
}

void OfflinePoliciesTest::requiresDualWatchStateAndSubIdentity()
{
    // Alternate ten thousand complete transitions so every enable must wait
    // for fresh SUB identity and every disable must remain state-confirmed.
    constexpr int kTransitionCount = 10000;
    sdr9700::DualWatchTransitionPolicy policy;

    for (int transition = 0; transition < kTransitionCount; ++transition)
    {
        const bool enable = transition % 2 == 0;
        QVERIFY(policy.request(enable));
        QVERIFY(!policy.request(!enable));
        QVERIFY(!policy.observeState(!enable));
        QVERIFY(!policy.complete());
        QVERIFY(policy.observeState(enable));

        if (enable)
        {
            QCOMPARE(policy.missingConfirmations(), quint8(sdr9700::DualWatchTransitionPolicy::kSubFrequencyConfirmed |
                                                           sdr9700::DualWatchTransitionPolicy::kSubModeConfirmed));
            policy.observeSubMode();
            QVERIFY(!policy.complete());
            policy.observeSubFrequency();
        }

        QVERIFY(policy.complete());
        policy.reset();
        QVERIFY(!policy.pending());
    }
}

void OfflinePoliciesTest::serializesRepeatedMainSubExchanges()
{
    constexpr int kExchangeCount = 500;
    constexpr int kDuplicateClicksPerExchange = 10;
    // Five thousand rejected duplicate clicks intentionally pressure hundreds
    // of complete exchange cycles.
    sdr9700::MainSubExchangePolicy policy;
    bool mainContainsVhf = true;

    for (int exchange = 0; exchange < kExchangeCount; ++exchange)
    {
        QVERIFY(policy.request());
        QCOMPARE(policy.state(), sdr9700::MainSubExchangePolicy::State::AwaitingRadio);
        for (int duplicateClick = 0; duplicateClick < kDuplicateClicksPerExchange; ++duplicateClick)
        {
            QVERIFY(!policy.request());
        }
        QVERIFY(policy.confirmRadio());
        QCOMPARE(policy.state(), sdr9700::MainSubExchangePolicy::State::AwaitingScope);
        QVERIFY(!policy.request());
        QVERIFY(!policy.confirmRadio());
        QVERIFY(policy.confirmScope());
        mainContainsVhf = !mainContainsVhf;
        QVERIFY(!policy.pending());
    }

    QVERIFY(mainContainsVhf);
}

void OfflinePoliciesTest::clampsMemoryPollingInterval()
{
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(0), 0);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(300), 300);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(600), 600);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(900), 900);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(1800), 1800);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(3600), 3600);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(-1), sdr9700::kDefaultMemoryPollIntervalSeconds);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(30), sdr9700::kDefaultMemoryPollIntervalSeconds);
    QCOMPARE(sdr9700::normalizeMemoryPollIntervalSeconds(9999), sdr9700::kDefaultMemoryPollIntervalSeconds);
}

void OfflinePoliciesTest::tracksMemorySynchronization()
{
    const QSet<quint32> expected = {0x00010001, 0x00010002, 0x00020001};
    QVERIFY(!sdr9700::memorySyncComplete(expected, {}));
    QVERIFY(!sdr9700::memorySyncComplete(expected, {0x00010001, 0x00010002, 0x00090009}));
    QVERIFY(sdr9700::memorySyncComplete(expected, {0x00010001, 0x00010002, 0x00020001}));
    QVERIFY(sdr9700::memorySyncComplete(expected, {0x00010001, 0x00010002, 0x00020001, 0x00090009}));
    QVERIFY(!sdr9700::memorySyncComplete({}, {}));
}

void OfflinePoliciesTest::retriesIncompleteOperationSynchronization()
{
    QVERIFY(sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, false, 1, 3));
    QVERIFY(sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, false, 2, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, false, 3, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(false, true, false, 1, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(true, false, false, 1, 3));
    QVERIFY(!sdr9700::shouldRetryIncompleteMemoryOperationSync(true, true, true, 1, 3));
}

void OfflinePoliciesTest::advancesMemorySynchronizationSlots()
{
    quint16 group = 1;
    quint16 channel = 1;
    QCOMPARE(sdr9700::memorySyncProgressIndex(group, channel, 1, 1, 99), 1);

    channel = 99;
    QCOMPARE(sdr9700::memorySyncProgressIndex(group, channel, 1, 1, 99), 99);
    sdr9700::advanceMemorySyncSlot(group, channel, 1, 99);
    QCOMPARE(group, quint16(2));
    QCOMPARE(channel, quint16(1));
    QCOMPARE(sdr9700::memorySyncProgressIndex(group, channel, 1, 1, 99), 100);

    group = 3;
    channel = 99;
    sdr9700::advanceMemorySyncSlot(group, channel, 1, 99);
    QCOMPARE(group, quint16(4));
    QCOMPARE(channel, quint16(1));
}

void OfflinePoliciesTest::identifiesExpectedMemoryWriteReadback()
{
    QVERIFY(sdr9700::memoryReadbackExpected(true, 0x00010002, 0x00010002));
    QVERIFY(!sdr9700::memoryReadbackExpected(false, 0x00010002, 0x00010002));
    QVERIFY(!sdr9700::memoryReadbackExpected(true, 0x00010002, 0x00010003));
    QVERIFY(!sdr9700::memoryReadbackExpected(true, 0, 0));
}

void OfflinePoliciesTest::retriesRecoverableRadioConnectionFailures()
{
    QVERIFY(sdr9700::isAutomaticReconnectError(ErrorCode::ConnectionFailed));
    QVERIFY(sdr9700::isAutomaticReconnectError(ErrorCode::Disconnected));
    QVERIFY(sdr9700::isAutomaticReconnectError(ErrorCode::PortReservationFailed));
    QVERIFY(!sdr9700::isAutomaticReconnectError(ErrorCode::RadioBusy));
    QVERIFY(!sdr9700::isAutomaticReconnectError(ErrorCode::AuthFailure));

    QVERIFY(sdr9700::shouldRetryRadioConnection(true, false, false, false, true));
    QVERIFY(sdr9700::shouldRetryRadioConnection(false, true, false, false, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, false, false, false, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, true, true, false, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, true, false, true, true));
    QVERIFY(!sdr9700::shouldRetryRadioConnection(false, true, false, false, false));
}

void OfflinePoliciesTest::roundsTuningFrequency()
{
    QCOMPARE(sdr9700::roundFrequencyToStep(146520049, 100), quint64(146520000));
    QCOMPARE(sdr9700::roundFrequencyToStep(146520050, 100), quint64(146520100));
    QCOMPARE(sdr9700::roundFrequencyToStep(146520049, 1), quint64(146520049));
}

void OfflinePoliciesTest::clampsFrequencyAndScopeCenterToBand()
{
    QCOMPARE(sdr9700::clampFrequencyToBand(100000000, 146520000), quint64(144000000));
    QCOMPARE(sdr9700::clampFrequencyToBand(200000000, 146520000), quint64(148000000));
    QCOMPARE(sdr9700::clampScopeCenterToBand(144000000, 146520000, 1.0), quint64(144500000));
    QCOMPARE(sdr9700::clampScopeCenterToBand(148000000, 146520000, 1.0), quint64(147500000));
    QCOMPARE(sdr9700::clampScopeCenterToBand(146000000, 146520000, 10.0), quint64(146000000));
}

void OfflinePoliciesTest::requiresConsecutiveHighSwrReadings()
{
    sdr9700::TransmitSafetyPolicy policy;
    QVERIFY(!policy.observe(true, 3.0));
    QVERIFY(!policy.observe(true, 4.0));
    QVERIFY(policy.observe(true, 3.1));
    QCOMPARE(policy.highReadingCount(), 3);
    QVERIFY(!policy.observe(true, 2.99));
    QCOMPARE(policy.highReadingCount(), 0);
}

void OfflinePoliciesTest::resetsTransmitSafetyWhenNotTransmitting()
{
    sdr9700::TransmitSafetyPolicy policy;
    QVERIFY(!policy.observe(true, 5.0));
    QVERIFY(!policy.observe(false, 5.0));
    QCOMPARE(policy.highReadingCount(), 0);
    QVERIFY(!policy.observe(true, std::numeric_limits<double>::quiet_NaN()));
}

void OfflinePoliciesTest::keepsPttActiveUntilRadioConfirmsUnkey()
{
    sdr9700::PttConfirmationPolicy policy;
    QVERIFY(policy.requestOn());
    QVERIFY(policy.desiredActive());
    QVERIFY(policy.safetyActive());
    QVERIFY(policy.transmitMetersActive());

    policy.requestOff();
    QVERIFY(policy.offPending());
    QVERIFY(policy.safetyActive());
    QVERIFY(!policy.desiredActive());
    QVERIFY(!policy.transmitMetersActive());

    policy.reset();
    QVERIFY(policy.requestOn());
    policy.confirm(true);
    QVERIFY(policy.transmitMetersActive());
    policy.requestOff();
    QVERIFY(policy.confirmedActive());
    QVERIFY(policy.offPending());
    QVERIFY(policy.safetyActive());
    QVERIFY(!policy.transmitMetersActive());
    QVERIFY(policy.requestOn());
    QVERIFY(!policy.offPending());
    policy.requestOff();

    // A radio report that it is still keyed must remain authoritative.
    policy.confirm(true);
    QVERIFY(policy.confirmedActive());
    QVERIFY(policy.offPending());
    QVERIFY(policy.safetyActive());

    policy.confirm(false);
    QVERIFY(!policy.confirmedActive());
    QVERIFY(!policy.offPending());
    QVERIFY(!policy.safetyActive());
}

void OfflinePoliciesTest::republishesContradictoryKeyedReadbackAfterUnkey()
{
    sdr9700::PttConfirmationPolicy policy;
    QVERIFY(policy.requestOn());
    policy.confirm(true);
    QVERIFY(!policy.shouldPublishReadback(true));

    policy.requestOff();
    QVERIFY(policy.shouldPublishReadback(true));
    policy.confirm(true);
    QVERIFY(policy.offPending());

    policy.confirm(false);
    QVERIFY(policy.shouldPublishReadback(true));
}

void OfflinePoliciesTest::validatesDuplexTransmitFrequency()
{
    QCOMPARE(sdr9700::duplexTransmitFrequency(446500000, dmSimplex, 5000000).value(), quint64(446500000));
    QCOMPARE(sdr9700::duplexTransmitFrequency(446500000, dmDupMinus, 5000000).value(), quint64(441500000));
    QCOMPARE(sdr9700::duplexTransmitFrequency(446500000, dmDupPlus, 5000000).value(), quint64(451500000));
    QVERIFY(!sdr9700::duplexTransmitFrequency(100, dmDupMinus, 200).has_value());

    // Every compiled IC-9700 band accepts its exact edges and rejects one hertz beyond them.
    const std::array<std::pair<quint64, quint64>, 3> bands = {
        std::pair<quint64, quint64>{144000000, 148000000},
        std::pair<quint64, quint64>{430000000, 450000000},
        std::pair<quint64, quint64>{1240000000, 1300000000},
    };
    for (const auto& [start, end] : bands)
    {
        const quint64 receiveHz = start + (end - start) / 2;
        QVERIFY(sdr9700::transmitFrequencyAllowed(receiveHz, start));
        QVERIFY(sdr9700::transmitFrequencyAllowed(receiveHz, end));
        QVERIFY(!sdr9700::transmitFrequencyAllowed(receiveHz, start - 1));
        QVERIFY(!sdr9700::transmitFrequencyAllowed(receiveHz, end + 1));
    }
}

void OfflinePoliciesTest::blocksPttUntilTransmitConfigurationIsConfirmed()
{
    sdr9700::TransmitConfigurationPolicy policy;
    QVERIFY(policy.confirmFrequency(443250000));
    policy.confirmDuplexMode(dmSimplex);
    policy.confirmOffset(0);
    QVERIFY(policy.transmitFrequencyAllowed());

    policy.requestOffset(5000000);
    policy.requestDuplexMode(dmDupPlus);
    QVERIFY(policy.confirmationPending());
    QVERIFY(!policy.transmitFrequencyAllowed());

    policy.confirmOffset(5000000);
    QVERIFY(policy.confirmationPending());
    policy.confirmDuplexMode(dmDupPlus);
    QVERIFY(!policy.confirmationPending());
    QVERIFY(policy.transmitFrequencyAllowed());

    struct RepeaterCase
    {
        quint64 receiveHz{0};
        duplexMode_t mode{dmSimplex};
        quint64 offsetHz{0};
        quint64 transmitHz{0};
    };
    const std::array<RepeaterCase, 4> repeaters = {
        RepeaterCase{145250000, dmDupPlus, 600000, 145850000},
        RepeaterCase{146940000, dmDupMinus, 600000, 146340000},
        RepeaterCase{443250000, dmDupPlus, 5000000, 448250000},
        RepeaterCase{1296100000, dmDupMinus, 12000000, 1284100000},
    };
    for (const RepeaterCase& repeater : repeaters)
    {
        sdr9700::TransmitConfigurationPolicy repeatedKeyUpPolicy;
        QVERIFY(repeatedKeyUpPolicy.confirmFrequency(repeater.receiveHz));
        repeatedKeyUpPolicy.confirmDuplexMode(repeater.mode);
        repeatedKeyUpPolicy.confirmOffset(repeater.offsetHz);
        QVERIFY(repeatedKeyUpPolicy.transmitFrequencyAllowed());

        // A keyed IC-9700 can report the shifted transmit frequency through
        // the ordinary selected-frequency route. Repeated key-ups must keep
        // calculating from the stable RX frequency instead of applying the
        // duplex offset again to the prior TX report.
        for (int keyUp = 0; keyUp < 1000; ++keyUp)
        {
            QVERIFY(!repeatedKeyUpPolicy.confirmFrequency(repeater.transmitHz, true));
            QVERIFY(repeatedKeyUpPolicy.transmitFrequencyAllowed());
        }
    }

    policy.requestDuplexMode(dmDupMinus);
    policy.confirmDuplexMode(dmSimplex);
    QVERIFY(policy.confirmationPending());
    policy.confirmDuplexMode(dmDupMinus);
    QVERIFY(policy.transmitFrequencyAllowed());
}


namespace
{
sdr9700::audio::TxAudioMeterBlock meterBlockAt(double peakDb, double rmsDb, quint32 sampleCount = 960,
                                               quint32 fullScaleCount = 0)
{
    sdr9700::audio::TxAudioMeterBlock block;
    block.peak = static_cast<float>(std::pow(10.0, peakDb / 20.0));
    const double rms = std::pow(10.0, rmsDb / 20.0);
    block.sumSquares = rms * rms * static_cast<double>(sampleCount);
    block.sampleCount = sampleCount;
    block.fullScaleCount = fullScaleCount;
    block.valid = true;
    return block;
}
} // namespace

void OfflinePoliciesTest::convertsMagnitudeToBoundedDbfs()
{
    using namespace sdr9700::audio;
    QVERIFY(std::abs(dbfsFromMagnitude(1.0) - 0.0) < 0.001);
    QVERIFY(std::abs(dbfsFromMagnitude(0.5) - (-6.0206)) < 0.01);
    QVERIFY(std::abs(dbfsFromMagnitude(0.1) - (-20.0)) < 0.01);

    // Digital silence has no logarithm and reports the floor; callers separate
    // true silence from "no measurement" through the block sample count.
    QCOMPARE(dbfsFromMagnitude(0.0), kMeterDisplayFloorDb);
    QCOMPARE(dbfsFromMagnitude(-1.0), kMeterDisplayFloorDb);

    // Anything quieter than the floor clamps rather than running away.
    QCOMPARE(dbfsFromMagnitude(1.0e-9), kMeterDisplayFloorDb);

    // Post-mix samples are not clamped to unity, but the nominal scale is not
    // extended above 0 dBFS.
    QCOMPARE(dbfsFromMagnitude(2.0), kMeterDisplayCeilingDb);
}

void OfflinePoliciesTest::aggregatesTransmitMeterBlocksByEnergy()
{
    using namespace sdr9700::audio;

    // Two blocks at very different levels. Correct combined RMS is computed on
    // summed energy; the previous implementation averaged the block RMS values
    // arithmetically, which under-reports whenever levels differ.
    const TxAudioMeterBlock loud = meterBlockAt(-3.0, -6.0, 480);
    const TxAudioMeterBlock quiet = meterBlockAt(-40.0, -46.0, 480);
    const TxAudioMeterBlock combined = aggregate(loud, quiet);

    QCOMPARE(combined.sampleCount, 960U);
    QVERIFY(std::abs(double(combined.peak) - double(loud.peak)) < 1.0e-6);

    const double expectedRms = std::sqrt((loud.sumSquares + quiet.sumSquares) / 960.0);
    QVERIFY(std::abs(blockRms(combined) - expectedRms) < 1.0e-9);

    const double arithmeticMeanOfRms = (blockRms(loud) + blockRms(quiet)) / 2.0;
    QVERIFY(blockRms(combined) > arithmeticMeanOfRms);

    // Counts sum, and an invalid block contributes nothing.
    const TxAudioMeterBlock clipped = meterBlockAt(0.0, -6.0, 480, 7);
    QCOMPARE(aggregate(combined, clipped).fullScaleCount, 7U);
    QCOMPARE(aggregate(TxAudioMeterBlock{}, loud).sampleCount, loud.sampleCount);
    QCOMPARE(aggregate(loud, TxAudioMeterBlock{}).sampleCount, loud.sampleCount);
}

void OfflinePoliciesTest::holdsTransmitMeterActivityAcrossSpeechGaps()
{
    using namespace sdr9700::audio;
    TxAudioMeterPresentation presentation;

    presentation.accept(meterBlockAt(-6.0, -18.0), 0);
    QVERIFY(presentation.active());

    // A 300 ms inter-word gap must not drop to "no activity".
    presentation.accept(meterBlockAt(-70.0, -70.0), 100);
    presentation.accept(meterBlockAt(-70.0, -70.0), 300);
    QVERIFY(presentation.active());

    // Still held just before the 1500 ms hold expires.
    presentation.accept(meterBlockAt(-70.0, -70.0), 1500);
    QVERIFY(presentation.active());

    // Released once sustained silence exceeds the hold.
    presentation.accept(meterBlockAt(-70.0, -70.0), 1700);
    QVERIFY(!presentation.active());

    // Hysteresis: a level between the off and on thresholds does not re-arm.
    presentation.accept(meterBlockAt(-52.0, -52.0), 1800);
    QVERIFY(!presentation.active());
    presentation.accept(meterBlockAt(-48.0, -48.0), 1900);
    QVERIFY(presentation.active());
}

void OfflinePoliciesTest::holdsAndDecaysTransmitMeterPeak()
{
    using namespace sdr9700::audio;
    TxAudioMeterPresentation presentation;

    presentation.accept(meterBlockAt(-6.0, -18.0), 0);
    QVERIFY(std::abs(presentation.heldPeakDb() - (-6.0)) < 0.1);

    // Held flat for kPeakHoldMs even though the signal dropped.
    presentation.accept(meterBlockAt(-40.0, -40.0), 500);
    QVERIFY(std::abs(presentation.heldPeakDb() - (-6.0)) < 0.1);
    presentation.accept(meterBlockAt(-40.0, -40.0), 1000);
    QVERIFY(std::abs(presentation.heldPeakDb() - (-6.0)) < 0.1);

    // Then decays at kPeakDecayDbPerSec: 500 ms beyond the hold is 10 dB.
    presentation.accept(meterBlockAt(-40.0, -40.0), 1500);
    QVERIFY(std::abs(presentation.heldPeakDb() - (-16.0)) < 0.5);

    // A louder block re-arms the hold immediately.
    presentation.accept(meterBlockAt(-2.0, -12.0), 1600);
    QVERIFY(std::abs(presentation.heldPeakDb() - (-2.0)) < 0.1);
}

void OfflinePoliciesTest::holdsFullScaleIndicationForThreeSeconds()
{
    using namespace sdr9700::audio;
    TxAudioMeterPresentation presentation;

    presentation.accept(meterBlockAt(0.0, -10.0, 960, 4), 0);
    QCOMPARE(presentation.fullScaleCount(), 4U);
    QCOMPARE(presentation.state(), TxAudioMeterState::FullScaleDetected);

    // Counts accumulate while events continue, and the hold restarts.
    presentation.accept(meterBlockAt(0.0, -10.0, 960, 3), 1000);
    QCOMPARE(presentation.fullScaleCount(), 7U);

    // Still shown just before the hold expires, measured from the latest event.
    presentation.accept(meterBlockAt(-20.0, -20.0), 3900);
    QCOMPARE(presentation.fullScaleCount(), 7U);

    // Cleared once kFullScaleCountHoldMs passes with no new full-scale sample.
    presentation.accept(meterBlockAt(-20.0, -20.0), 4100);
    QCOMPARE(presentation.fullScaleCount(), 0U);
    QVERIFY(presentation.state() != TxAudioMeterState::FullScaleDetected);
}

void OfflinePoliciesTest::keepsTransmitMeterValidAcrossSilence()
{
    using namespace sdr9700::audio;
    TxAudioMeterPresentation presentation;

    // No measurement yet.
    QCOMPARE(presentation.state(), TxAudioMeterState::Invalid);

    // Valid digital silence is a measurement, not an absence of one.
    TxAudioMeterBlock silence;
    silence.sampleCount = 960;
    silence.valid = true;
    presentation.accept(silence, 0);
    QVERIFY(presentation.valid());
    QCOMPARE(presentation.state(), TxAudioMeterState::NoActivity);
    QCOMPARE(presentation.rmsDb(), kMeterDisplayFloorDb);

    // Speech inside the recommended window classifies as such.
    presentation.accept(meterBlockAt(-6.0, -18.0), 100);
    QCOMPARE(presentation.state(), TxAudioMeterState::RecommendedHeadroom);

    // Near full scale outranks the headroom window.
    presentation.accept(meterBlockAt(-0.5, -18.0), 200);
    QCOMPARE(presentation.state(), TxAudioMeterState::NearFullScale);

    // Only an explicit reset invalidates the meter. Encoding authorization
    // changes must never reach this state.
    presentation.reset();
    QCOMPARE(presentation.state(), TxAudioMeterState::Invalid);
    QVERIFY(!presentation.valid());
}

QTEST_GUILESS_MAIN(OfflinePoliciesTest)

#include "OfflinePoliciesTest.moc"
