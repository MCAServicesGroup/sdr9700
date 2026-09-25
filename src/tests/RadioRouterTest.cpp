#include "CachingQueue.h"
#include "RadioRouter.h"
#include "SMeterScale.h"

#include <QSignalSpy>
#include <QTest>
#include <thread>

class RadioRouterTest : public QObject
{
    Q_OBJECT

  private slots:
    void routesOnlyMainReceiverFrequencyAndMode();
    void clampsMeterAndLevelValues();
    void mapsAgcAndPreampValues();
    void keepsSubReceiverControlsOutOfLegacyMainSignals();
    void routesToneRegisterForActiveToneMode();
    void keepsToneModesIndependentByReceiver();
    void keepsMainTransmitConfigurationSignalsIsolated();
    void routesProtocolPayloadTypes();
    void routesConfirmedVfoSelectionState();
    void routesAllSimpleControlBranches();
    void routesBatchInOrder();
    void coalescesReplaceableBacklogAcrossOneQueuedDrain();
    void boundsReplaceableTrafficWhileConsumerIsStalled();
    void preservesOrderingAcrossLosslessBarriers();
    void routesNormalAndSatelliteMemoryReplies();
    void rejectsBatchesFromCancelledSessions();
    void boundsLosslessBacklogWithProducerBackpressure();
    void routesOnlyConfirmedScopeReceiver();
    void ignoresUnknownCommands();
};

void RadioRouterTest::coalescesReplaceableBacklogAcrossOneQueuedDrain()
{
    RadioRouter router;
    QSignalSpy meterSpy(&router, &RadioRouter::smeterChanged);

    for (int value = 0; value < 100; ++value)
    {
        router.enqueueBatch({CacheItem(funcSMeter, value, 0)});
    }

    QCOMPARE(router.queueDiagnostics().pendingItems, qsizetype(1));
    QCOMPARE(router.queueDiagnostics().coalescedItems, quint64(99));
    QTRY_COMPARE(meterSpy.size(), 1);
    QCOMPARE(meterSpy.at(0).at(0).toInt(), 99);
    QCOMPARE(router.queueDiagnostics().drainEvents, quint64(1));
}

void RadioRouterTest::boundsReplaceableTrafficWhileConsumerIsStalled()
{
    constexpr int kBatchCount = 5000;
    constexpr int kItemsPerBatch = 4;
    // Twenty thousand replaceable items intentionally arrive while the
    // consumer event loop is stalled.
    RadioRouter router;
    QSignalSpy meterSpy(&router, &RadioRouter::smeterChanged);
    QSignalSpy scopeSpy(&router, &RadioRouter::scopeDataReady);
    ScopeData mainFrame;
    mainFrame.valid = true;
    mainFrame.receiver = 0;
    ScopeData subFrame = mainFrame;
    subFrame.receiver = 1;

    std::thread producer(
        [&router, &mainFrame, &subFrame]()
        {
            for (int value = 0; value < kBatchCount; ++value)
            {
                mainFrame.data = QByteArray::number(value);
                subFrame.data = QByteArray::number(value);
                router.enqueueBatch({CacheItem(funcSMeter, value, 0), CacheItem(funcSMeter, value, 1),
                                     CacheItem(funcScopeWaveData, QVariant::fromValue(mainFrame), 0),
                                     CacheItem(funcScopeWaveData, QVariant::fromValue(subFrame), 1)});
            }
        });

    // Intentionally do not process this thread's Qt event loop. Replaceable
    // traffic must collapse at the producer boundary instead of blocking or
    // filling the queued-delivery path while the consumer is stalled.
    producer.join();

    const RadioRouterQueueDiagnostics stalled = router.queueDiagnostics();
    QCOMPARE(stalled.pendingItems, qsizetype(4));
    QCOMPARE(stalled.highWaterMark, qsizetype(4));
    QCOMPARE(stalled.coalescedItems, quint64(kBatchCount * kItemsPerBatch - kItemsPerBatch));
    QCOMPARE(stalled.backpressureWaits, quint64(0));

    QTRY_COMPARE(router.queueDiagnostics().pendingItems, qsizetype(0));
    QCOMPARE(meterSpy.size(), 1);
    QCOMPARE(meterSpy.at(0).at(0).toInt(), 255);
    QCOMPARE(scopeSpy.size(), 1);
    QCOMPARE(scopeSpy.at(0).at(0).value<ScopeData>().data, QByteArray::number(kBatchCount - 1));
}

void RadioRouterTest::preservesOrderingAcrossLosslessBarriers()
{
    RadioRouter router;
    QVector<QString> order;
    connect(&router, &RadioRouter::smeterChanged, this,
            [&order](int value) { order.append(QStringLiteral("meter:%1").arg(value)); });
    connect(&router, &RadioRouter::radioMemoryReceived, this,
            [&order](const MemoryType&) { order.append(QStringLiteral("memory")); });

    MemoryType memory;
    router.enqueueBatch({CacheItem(funcSMeter, 10, 0), CacheItem(funcSMeter, 20, 0),
                         CacheItem(funcMemoryContents, QVariant::fromValue(memory), 0), CacheItem(funcSMeter, 30, 0),
                         CacheItem(funcSMeter, 40, 0)});

    QTRY_COMPARE(order.size(), 3);
    QCOMPARE(order,
             QVector<QString>({QStringLiteral("meter:20"), QStringLiteral("memory"), QStringLiteral("meter:40")}));
}

void RadioRouterTest::routesNormalAndSatelliteMemoryReplies()
{
    RadioRouter router;
    QSignalSpy memorySpy(&router, &RadioRouter::radioMemoryReceived);

    MemoryType normal;
    normal.group = 1;
    normal.channel = 107;
    MemoryType satellite;
    satellite.group = 0;
    satellite.channel = 99;
    satellite.sat = true;

    router.route(CacheItem(funcMemoryContents, QVariant::fromValue(normal), 0));
    router.route(CacheItem(funcSatelliteMemory, QVariant::fromValue(satellite), 0));

    QCOMPARE(memorySpy.size(), 2);
    QCOMPARE(memorySpy.at(0).at(0).value<MemoryType>().channel, quint16(107));
    const MemoryType routedSatellite = memorySpy.at(1).at(0).value<MemoryType>();
    QCOMPARE(routedSatellite.channel, quint16(99));
    QVERIFY(routedSatellite.sat);
}

void RadioRouterTest::rejectsBatchesFromCancelledSessions()
{
    RadioRouter router;
    QSignalSpy meterSpy(&router, &RadioRouter::smeterChanged);
    const quint64 oldSession = router.beginQueueSession();
    router.enqueueBatch({CacheItem(funcSMeter, 10, 0)}, oldSession);
    router.cancelQueueSession(oldSession);

    const quint64 newSession = router.beginQueueSession();
    router.enqueueBatch({CacheItem(funcSMeter, 20, 0)}, oldSession);
    router.enqueueBatch({CacheItem(funcSMeter, 30, 0)}, newSession);

    QTRY_COMPARE(meterSpy.size(), 1);
    QCOMPARE(meterSpy.at(0).at(0).toInt(), 30);
}

void RadioRouterTest::boundsLosslessBacklogWithProducerBackpressure()
{
    constexpr int kRecordCount = 600;
    // This is a capacity-boundary test: exceed the 512-item limit enough to
    // prove producer backpressure without adding unrelated runtime.
    RadioRouter router;
    QSignalSpy memorySpy(&router, &RadioRouter::radioMemoryReceived);
    QVector<CacheItem> records;
    records.reserve(kRecordCount);
    for (int index = 0; index < kRecordCount; ++index)
    {
        MemoryType memory;
        memory.channel = static_cast<quint16>(index);
        records.append(CacheItem(funcMemoryContents, QVariant::fromValue(memory), 0));
    }

    std::atomic_bool producerFinished{false};
    std::thread producer(
        [&router, &records, &producerFinished]()
        {
            router.enqueueBatch(records);
            producerFinished.store(true, std::memory_order_release);
        });

    // Freeze the router's event loop long enough for the worker to hit the
    // lossless capacity limit. It must block rather than discard records or
    // grow the container beyond its documented bound.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    QVERIFY(!producerFinished.load(std::memory_order_acquire));
    QCOMPARE(router.queueDiagnostics().pendingItems, qsizetype(512));
    QCOMPARE(router.queueDiagnostics().highWaterMark, qsizetype(512));
    QTRY_VERIFY_WITH_TIMEOUT(producerFinished.load(std::memory_order_acquire), 2000);
    producer.join();
    QTRY_COMPARE_WITH_TIMEOUT(memorySpy.size(), kRecordCount, 2000);

    const RadioRouterQueueDiagnostics diagnostics = router.queueDiagnostics();
    QCOMPARE(diagnostics.highWaterMark, qsizetype(512));
    QVERIFY(diagnostics.backpressureWaits > 0);
    QCOMPARE(diagnostics.coalescedItems, quint64(0));
}

void RadioRouterTest::routesOnlyConfirmedScopeReceiver()
{
    RadioRouter router;
    QSignalSpy scopeSpy(&router, &RadioRouter::scopeDataReady);
    ScopeData mainFrame;
    mainFrame.valid = true;
    mainFrame.receiver = 0;
    mainFrame.data = QByteArrayLiteral("main");
    ScopeData subFrame = mainFrame;
    subFrame.receiver = 1;
    subFrame.data = QByteArrayLiteral("sub");

    router.route(CacheItem(funcScopeWaveData, QVariant::fromValue(subFrame), 1));
    router.route(CacheItem(funcScopeWaveData, QVariant::fromValue(mainFrame), 0));
    QCOMPARE(scopeSpy.size(), 1);
    QCOMPARE(scopeSpy.at(0).at(0).value<ScopeData>().receiver, uchar(0));

    router.route(CacheItem(funcScopeMainSub, true, 0));
    router.route(CacheItem(funcScopeWaveData, QVariant::fromValue(mainFrame), 0));
    router.route(CacheItem(funcScopeWaveData, QVariant::fromValue(subFrame), 1));
    QCOMPARE(scopeSpy.size(), 2);
    QCOMPARE(scopeSpy.at(1).at(0).value<ScopeData>().receiver, uchar(1));

    RadioRouter queuedRouter;
    QSignalSpy queuedScopeSpy(&queuedRouter, &RadioRouter::scopeDataReady);
    queuedRouter.enqueueBatch({CacheItem(funcScopeWaveData, QVariant::fromValue(subFrame), 1)});
    queuedRouter.enqueueBatch({CacheItem(funcScopeWaveData, QVariant::fromValue(mainFrame), 0)});
    QCOMPARE(queuedRouter.queueDiagnostics().pendingItems, qsizetype(2));
    QCOMPARE(queuedRouter.queueDiagnostics().coalescedItems, quint64(0));
    QTRY_COMPARE(queuedScopeSpy.size(), 1);
    QCOMPARE(queuedScopeSpy.at(0).at(0).value<ScopeData>().receiver, uchar(0));
}

void RadioRouterTest::routesOnlyMainReceiverFrequencyAndMode()
{
    RadioRouter router;
    QSignalSpy frequencySpy(&router, &RadioRouter::frequencyReported);
    QSignalSpy modeSpy(&router, &RadioRouter::modeReported);

    Frequency frequency;
    frequency.Hz = 146520000;
    QSignalSpy valueSpy(&router, &RadioRouter::radioValueUpdated);
    router.route(CacheItem(funcFreqGet, QVariant::fromValue(frequency), 1));
    QCOMPARE(valueSpy.count(), 1);
    QCOMPARE(valueSpy.at(0).at(2).toUInt(), uint(1));
    router.route(CacheItem(funcUnselectedFreq, QVariant::fromValue(frequency), 0));
    QCOMPARE(valueSpy.count(), 1);
    QCOMPARE(frequencySpy.count(), 0);
    router.route(CacheItem(funcSelectedFreq, QVariant::fromValue(frequency), 0));
    QCOMPARE(frequencySpy.count(), 1);
    QCOMPARE(frequencySpy.takeFirst().at(0).toULongLong(), quint64(146520000));

    ModeInfo mode;
    mode.mk = modeFM;
    mode.filter = 2;
    router.route(CacheItem(funcUnselectedMode, QVariant::fromValue(mode), 0));
    QCOMPARE(valueSpy.count(), 2);
    QCOMPARE(modeSpy.count(), 0);
    router.route(CacheItem(funcSelectedMode, QVariant::fromValue(mode), 0));
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.at(0).at(0).toString(), QStringLiteral("FM"));
    QCOMPARE(modeSpy.at(0).at(1).toInt(), 2);
}

void RadioRouterTest::routesConfirmedVfoSelectionState()
{
    RadioRouter router;
    QSignalSpy valueSpy(&router, &RadioRouter::radioValueUpdated);

    router.route(CacheItem(funcVFOBandMS, true));
    router.route(CacheItem(funcVFODualWatch, true));
    router.route(CacheItem(funcScopeMainSub, true));

    QCOMPARE(valueSpy.count(), 3);
    QCOMPARE(static_cast<Funcs>(valueSpy.at(0).at(0).toInt()), funcVFOBandMS);
    QCOMPARE(valueSpy.at(0).at(1).toBool(), true);
    QCOMPARE(static_cast<Funcs>(valueSpy.at(1).at(0).toInt()), funcVFODualWatch);
    QCOMPARE(valueSpy.at(1).at(1).toBool(), true);
    QCOMPARE(static_cast<Funcs>(valueSpy.at(2).at(0).toInt()), funcScopeMainSub);
    QCOMPARE(valueSpy.at(2).at(1).toBool(), true);
    QCOMPARE(valueSpy.at(2).at(2).toUInt(), uint(0));
}

void RadioRouterTest::clampsMeterAndLevelValues()
{
    RadioRouter router;
    QSignalSpy smeterSpy(&router, &RadioRouter::smeterChanged);
    QSignalSpy rfSpy(&router, &RadioRouter::rfGainChanged);
    QSignalSpy nrLevelSpy(&router, &RadioRouter::nrLevelChanged);
    QSignalSpy nbLevelSpy(&router, &RadioRouter::nbLevelChanged);
    QSignalSpy powerSpy(&router, &RadioRouter::txPowerChanged);
    QSignalSpy squelchSpy(&router, &RadioRouter::squelchChanged);

    router.route(CacheItem(funcSMeter, -1));
    router.route(CacheItem(funcSMeter, 0));
    router.route(CacheItem(funcSMeter, 120));
    router.route(CacheItem(funcSMeter, 201));
    router.route(CacheItem(funcSMeter, 240));
    router.route(CacheItem(funcSMeter, 241));
    router.route(CacheItem(funcSMeter, 255));
    QCOMPARE(smeterSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(smeterSpy.at(1).at(0).toInt(), 0);
    QCOMPARE(smeterSpy.at(2).at(0).toInt(), 120);
    QCOMPARE(smeterSpy.at(3).at(0).toInt(), 201);
    QCOMPARE(smeterSpy.at(4).at(0).toInt(), 240);
    QCOMPARE(smeterSpy.at(5).at(0).toInt(), 241);
    QCOMPARE(smeterSpy.at(6).at(0).toInt(), 255);
    QCOMPARE(sdr9700::sMeterDisplayValue(120), 146);
    QCOMPARE(sdr9700::sMeterDisplayPercent(240), 99);
    QCOMPARE(sdr9700::sMeterDisplayPercent(241), 100);
    QCOMPARE(sdr9700::sMeterDisplayPercent(255), 100);

    router.route(CacheItem(funcRfGain, -1));
    router.route(CacheItem(funcNRLevel, 999));
    router.route(CacheItem(funcNBLevel, -1));
    router.route(CacheItem(funcRFPower, 999));
    router.route(CacheItem(funcSquelch, 0));
    QCOMPARE(rfSpy.takeFirst().at(0).toInt(), 0);
    QCOMPARE(nrLevelSpy.takeFirst().at(0).toInt(), 15);
    QCOMPARE(nbLevelSpy.takeFirst().at(0).toInt(), 1);
    QCOMPARE(powerSpy.takeFirst().at(0).toInt(), 255);
    QCOMPARE(squelchSpy.at(0).at(0).toBool(), false);
    QCOMPARE(squelchSpy.at(0).at(1).toInt(), 0);
}

void RadioRouterTest::mapsAgcAndPreampValues()
{
    RadioRouter router;
    QSignalSpy agcSpy(&router, &RadioRouter::agcModeChanged);
    QSignalSpy preampEnabledSpy(&router, &RadioRouter::preampChanged);
    QSignalSpy preampLevelSpy(&router, &RadioRouter::preampLevelChanged);

    router.route(CacheItem(funcAGCTimeConstant, -5));
    router.route(CacheItem(funcAGCTimeConstant, 99));
    QCOMPARE(agcSpy.at(0).at(0).toString(), QStringLiteral("off"));
    QCOMPARE(agcSpy.at(1).at(0).toString(), QStringLiteral("slow"));

    router.route(CacheItem(funcPreamp, 9));
    QCOMPARE(preampLevelSpy.takeFirst().at(0).toInt(), 3);
    QCOMPARE(preampEnabledSpy.takeFirst().at(0).toBool(), true);
}

void RadioRouterTest::keepsSubReceiverControlsOutOfLegacyMainSignals()
{
    RadioRouter router;
    QSignalSpy valueSpy(&router, &RadioRouter::radioValueUpdated);
    QSignalSpy agcSpy(&router, &RadioRouter::agcModeChanged);
    QSignalSpy nrSpy(&router, &RadioRouter::nrChanged);
    QSignalSpy rfGainSpy(&router, &RadioRouter::rfGainChanged);
    QSignalSpy smeterSpy(&router, &RadioRouter::smeterChanged);

    router.route(CacheItem(funcAGCTimeConstant, 3, 1));
    router.route(CacheItem(funcNoiseReduction, true, 1));
    router.route(CacheItem(funcRfGain, 128, 1));
    router.route(CacheItem(funcSMeter, 120, 1));

    QCOMPARE(valueSpy.count(), 4);
    for (const QList<QVariant>& arguments : valueSpy)
    {
        QCOMPARE(arguments.at(2).toUInt(), uint(1));
    }
    QCOMPARE(agcSpy.count(), 0);
    QCOMPARE(nrSpy.count(), 0);
    QCOMPARE(rfGainSpy.count(), 0);
    QCOMPARE(smeterSpy.count(), 0);
}

void RadioRouterTest::routesToneRegisterForActiveToneMode()
{
    RadioRouter router;
    QSignalSpy toneSpy(&router, &RadioRouter::toneFrequencyChanged);
    QSignalSpy dtcsSpy(&router, &RadioRouter::dtcsCodeChanged);

    RptrAccessData access;
    ToneInfo txTone(885);
    ToneInfo rxTone(670);
    ToneInfo dtcs(245);

    access.accessMode = ratrTN;
    router.route(CacheItem(funcToneSquelchType, QVariant::fromValue(access)));
    router.route(CacheItem(funcToneFreq, QVariant::fromValue(txTone)));
    router.route(CacheItem(funcTSQLFreq, QVariant::fromValue(rxTone)));
    QCOMPARE(toneSpy.count(), 1);
    QCOMPARE(toneSpy.takeFirst().at(0).value<ushort>(), ushort(885));

    access.accessMode = ratrNT;
    router.route(CacheItem(funcToneSquelchType, QVariant::fromValue(access)));
    router.route(CacheItem(funcToneFreq, QVariant::fromValue(txTone)));
    router.route(CacheItem(funcTSQLFreq, QVariant::fromValue(rxTone)));
    QCOMPARE(toneSpy.count(), 1);
    QCOMPARE(toneSpy.takeFirst().at(0).value<ushort>(), ushort(670));

    access.accessMode = ratrDD;
    router.route(CacheItem(funcToneSquelchType, QVariant::fromValue(access)));
    router.route(CacheItem(funcToneFreq, QVariant::fromValue(txTone)));
    router.route(CacheItem(funcTSQLFreq, QVariant::fromValue(rxTone)));
    router.route(CacheItem(funcDTCSCode, QVariant::fromValue(dtcs)));
    QCOMPARE(toneSpy.count(), 0);
    QCOMPARE(dtcsSpy.count(), 1);
    QCOMPARE(dtcsSpy.takeFirst().at(0).value<ushort>(), ushort(245));
}

void RadioRouterTest::keepsToneModesIndependentByReceiver()
{
    RadioRouter router;
    QSignalSpy toneSpy(&router, &RadioRouter::toneFrequencyChanged);
    QSignalSpy accessSpy(&router, &RadioRouter::toneAccessModeChanged);

    RptrAccessData mainAccess;
    mainAccess.accessMode = ratrTN;
    RptrAccessData subAccess;
    subAccess.accessMode = ratrNT;
    router.route(CacheItem(funcToneSquelchType, QVariant::fromValue(mainAccess), 0));
    router.route(CacheItem(funcToneSquelchType, QVariant::fromValue(subAccess), 1));
    QCOMPARE(accessSpy.count(), 1);

    router.route(CacheItem(funcTSQLFreq, QVariant::fromValue(ToneInfo(670)), 1));
    router.route(CacheItem(funcToneFreq, QVariant::fromValue(ToneInfo(885)), 0));
    QCOMPARE(toneSpy.count(), 1);
    QCOMPARE(toneSpy.takeFirst().at(0).value<ushort>(), ushort(885));
}

void RadioRouterTest::keepsMainTransmitConfigurationSignalsIsolated()
{
    RadioRouter router;
    QSignalSpy valueSpy(&router, &RadioRouter::radioValueUpdated);
    QSignalSpy duplexSpy(&router, &RadioRouter::duplexModeChanged);
    QSignalSpy offsetSpy(&router, &RadioRouter::repeaterOffsetChanged);
    QSignalSpy dtcsSpy(&router, &RadioRouter::dtcsCodeChanged);

    Frequency mainOffset;
    mainOffset.Hz = 600000;
    router.route(CacheItem(funcSplitStatus, QVariant::fromValue(dmDupPlus), 0));
    router.route(CacheItem(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0));
    router.route(CacheItem(funcDTCSCode, QVariant::fromValue(ToneInfo(23)), 0));
    QCOMPARE(duplexSpy.takeFirst().at(0).value<duplexMode_t>(), dmDupPlus);
    QCOMPARE(offsetSpy.takeFirst().at(0).toULongLong(), quint64(600000));
    QCOMPARE(dtcsSpy.takeFirst().at(0).value<ushort>(), ushort(23));

    valueSpy.clear();
    Frequency subOffset;
    subOffset.Hz = 5000000;
    router.route(CacheItem(funcSplitStatus, QVariant::fromValue(dmDupMinus), 1));
    router.route(CacheItem(funcReadFreqOffset, QVariant::fromValue(subOffset), 1));
    router.route(CacheItem(funcDTCSCode, QVariant::fromValue(ToneInfo(245)), 1));

    // Receiver-tagged state still reaches RadioState for the SUB panel, but
    // legacy MAIN-only consumers and PTT validation must not be overwritten.
    QCOMPARE(valueSpy.count(), 3);
    QCOMPARE(valueSpy.at(0).at(2).value<uchar>(), uchar(1));
    QCOMPARE(valueSpy.at(1).at(2).value<uchar>(), uchar(1));
    QCOMPARE(valueSpy.at(2).at(2).value<uchar>(), uchar(1));
    QCOMPARE(duplexSpy.count(), 0);
    QCOMPARE(offsetSpy.count(), 0);
    QCOMPARE(dtcsSpy.count(), 0);
}

void RadioRouterTest::routesProtocolPayloadTypes()
{
    RadioRouter router;
    QSignalSpy offsetSpy(&router, &RadioRouter::repeaterOffsetChanged);
    QSignalSpy pttSpy(&router, &RadioRouter::pttChanged);
    QSignalSpy scopeSpy(&router, &RadioRouter::scopeDataReady);

    Frequency offset;
    offset.Hz = 600000;
    router.route(CacheItem(funcReadFreqOffset, QVariant::fromValue(offset)));
    router.route(CacheItem(funcTransceiverStatus, true));
    ScopeData scope;
    scope.valid = true;
    scope.data = QByteArray::fromHex("0102");
    router.route(CacheItem(funcScopeWaveData, QVariant::fromValue(scope)));

    QCOMPARE(offsetSpy.takeFirst().at(0).toULongLong(), quint64(600000));
    QCOMPARE(pttSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(scopeSpy.takeFirst().at(0).value<ScopeData>().data, scope.data);
}

void RadioRouterTest::routesAllSimpleControlBranches()
{
    RadioRouter router;
    QSignalSpy nrSpy(&router, &RadioRouter::nrChanged);
    QSignalSpy nbSpy(&router, &RadioRouter::nbChanged);
    QSignalSpy attenuatorSpy(&router, &RadioRouter::attenuatorChanged);
    QSignalSpy autoNotchSpy(&router, &RadioRouter::autoNotchChanged);
    QSignalSpy manualNotchSpy(&router, &RadioRouter::manualNotchChanged);
    QSignalSpy compressorSpy(&router, &RadioRouter::compressorChanged);
    QSignalSpy compressorLevelSpy(&router, &RadioRouter::compressorLevelChanged);
    QSignalSpy xfcSpy(&router, &RadioRouter::xfcChanged);
    QSignalSpy ritSpy(&router, &RadioRouter::ritEnabledChanged);
    QSignalSpy duplexSpy(&router, &RadioRouter::duplexModeChanged);

    router.routeBatch({
        CacheItem(funcNoiseReduction, true),
        CacheItem(funcNoiseBlanker, true),
        CacheItem(funcAttenuator, 1),
        CacheItem(funcAutoNotch, true),
        CacheItem(funcManualNotch, true),
        CacheItem(funcCompressor, true),
        CacheItem(funcCompressorLevel, 192),
        CacheItem(funcXFCStatus, true),
        CacheItem(funcRitStatus, true),
        CacheItem(funcSplitStatus, QVariant::fromValue(dmDupPlus)),
    });
    QCOMPARE(nrSpy.count(), 1);
    QCOMPARE(nbSpy.count(), 1);
    QCOMPARE(attenuatorSpy.count(), 1);
    QCOMPARE(autoNotchSpy.count(), 1);
    QCOMPARE(manualNotchSpy.count(), 1);
    QCOMPARE(compressorSpy.count(), 1);
    QCOMPARE(compressorLevelSpy.takeFirst().at(0).toInt(), 192);
    QCOMPARE(xfcSpy.count(), 1);
    QCOMPARE(ritSpy.count(), 1);
    QCOMPARE(duplexSpy.takeFirst().at(0).value<duplexMode_t>(), dmDupPlus);
}

void RadioRouterTest::routesBatchInOrder()
{
    RadioRouter router;
    QVector<int> values;
    connect(&router, &RadioRouter::rfGainChanged, this, [&values](int value) { values.append(value); });
    router.routeBatch({CacheItem(funcRfGain, 1), CacheItem(funcRfGain, 2), CacheItem(funcRfGain, 3)});
    QCOMPARE(values, QVector<int>({1, 2, 3}));
}

void RadioRouterTest::ignoresUnknownCommands()
{
    RadioRouter router;
    QSignalSpy valueSpy(&router, &RadioRouter::radioValueUpdated);
    router.route(CacheItem(funcNone, 42));
    QCOMPARE(valueSpy.count(), 0);
}

QTEST_GUILESS_MAIN(RadioRouterTest)

#include "RadioRouterTest.moc"
