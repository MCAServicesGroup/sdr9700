// QtTest invokes private slots through the generated meta-object.
#include "MeterController.h"

#include <QtTest>
#include <cmath>

namespace
{
// Build a measurement block with an exact peak and RMS in dBFS.
sdr9700::audio::TxAudioMeterBlock blockAt(double peakDb, double rmsDb, quint32 sampleCount = 960,
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

class MeterControllerTest : public QObject
{
    Q_OBJECT

  private slots:
    void batchesUpdatesIntoOneSnapshot();
    void clampsMeterValues();
    void resetTransmitMetersPreservesReceiveMeter();
    void resetReceiveMeterPreservesTransmitMeters();
    void swrRequiresForwardPower();
    void resetPublishesDefaultSnapshotImmediately();
};

void MeterControllerTest::batchesUpdatesIntoOneSnapshot()
{
    MeterController controller;
    QSignalSpy snapshotSpy(&controller, &MeterController::snapshotChanged);

    controller.setSMeter(100);
    controller.setPowerMeter(50.0);
    controller.setTransmitAudioMeter(blockAt(-6.0, -18.0));

    QTRY_COMPARE(snapshotSpy.count(), 1);
    const MeterSnapshot snapshot = snapshotSpy.constFirst().constFirst().value<MeterSnapshot>();
    QCOMPARE(snapshot.sMeter, 100);
    QVERIFY(snapshot.sMeterValid);
    QCOMPARE(snapshot.powerWatts, 50.0);
    QVERIFY(snapshot.powerValid);
    QVERIFY(snapshot.txAudioState != sdr9700::audio::TxAudioMeterState::Invalid);
    QVERIFY(qAbs(snapshot.txAudioRmsDb - (-18.0)) < 0.1);
    QVERIFY(qAbs(snapshot.txAudioPeakDb - (-6.0)) < 0.1);
}

void MeterControllerTest::clampsMeterValues()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setSMeter(999);
    controller.setPowerMeter(-1.0);
    controller.setSwr(10.0);
    controller.setAlc(-2.0);
    controller.setCompressionMeter(99.0);
    controller.setVoltageMeter(99.0);
    controller.setCurrentMeter(-1.0);
    // Post-mix samples are not clamped to unity, so an over-unity peak must be
    // reported at the 0 dBFS ceiling rather than extending the scale.
    controller.setTransmitAudioMeter(blockAt(6.0, -80.0));

    QTRY_VERIFY(snapshot.sMeterValid);
    QCOMPARE(snapshot.sMeter, 255);
    QCOMPARE(snapshot.powerWatts, 0.0);
    QCOMPARE(snapshot.swr, 6.0);
    QCOMPARE(snapshot.alc, 0.0);
    QCOMPARE(snapshot.compressionDb, 25.5);
    QCOMPARE(snapshot.voltageVolts, 16.0);
    QCOMPARE(snapshot.currentAmps, 0.0);
    QCOMPARE(snapshot.txAudioPeakDb, sdr9700::audio::kMeterDisplayCeilingDb);
    QCOMPARE(snapshot.txAudioRmsDb, sdr9700::audio::kMeterDisplayFloorDb);
}

void MeterControllerTest::resetTransmitMetersPreservesReceiveMeter()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setSMeter(90);
    controller.setPowerMeter(25.0);
    controller.setSwr(2.0);
    QTRY_VERIFY(snapshot.powerValid);

    controller.resetTransmitMeters();
    QTRY_VERIFY(!snapshot.powerValid);
    QCOMPARE(snapshot.sMeter, 90);
    QVERIFY(snapshot.sMeterValid);
    QCOMPARE(snapshot.powerWatts, 0.0);
    QCOMPARE(snapshot.swr, 1.0);
    QVERIFY(!snapshot.swrValid);
}

void MeterControllerTest::resetReceiveMeterPreservesTransmitMeters()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setSMeter(90);
    controller.setPowerMeter(25.0);
    QTRY_VERIFY(snapshot.sMeterValid);

    controller.resetReceiveMeter();
    QTRY_VERIFY(!snapshot.sMeterValid);
    QCOMPARE(snapshot.sMeter, 0);
    QCOMPARE(snapshot.powerWatts, 25.0);
    QVERIFY(snapshot.powerValid);
}

void MeterControllerTest::swrRequiresForwardPower()
{
    MeterController controller;
    MeterSnapshot snapshot;
    connect(&controller, &MeterController::snapshotChanged, this,
            [&snapshot](const MeterSnapshot& value) { snapshot = value; });

    controller.setPowerMeter(0.0);
    controller.setSwr(1.5);
    QTRY_VERIFY(snapshot.powerValid);
    QVERIFY(!snapshot.swrValid);

    controller.setPowerMeter(10.0);
    controller.setSwr(1.5);
    QTRY_VERIFY(snapshot.swrValid);
    QCOMPARE(snapshot.swr, 1.5);

    controller.setPowerMeter(0.0);
    QTRY_VERIFY(!snapshot.swrValid);
}

void MeterControllerTest::resetPublishesDefaultSnapshotImmediately()
{
    MeterController controller;
    QSignalSpy snapshotSpy(&controller, &MeterController::snapshotChanged);

    controller.setSMeter(200);
    controller.reset();

    QCOMPARE(snapshotSpy.count(), 1);
    const MeterSnapshot snapshot = snapshotSpy.constFirst().constFirst().value<MeterSnapshot>();
    QCOMPARE(snapshot.sMeter, 0);
    QVERIFY(!snapshot.sMeterValid);
    QVERIFY(!snapshot.powerValid);
}

QTEST_GUILESS_MAIN(MeterControllerTest)
#include "MeterControllerTest.moc"
