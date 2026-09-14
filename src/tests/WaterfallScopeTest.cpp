#include "ScopeAdapter.h"
#include "ScopeController.h"
#include "WaterfallController.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class WaterfallScopeTest : public QObject
{
    Q_OBJECT

  private slots:
    void convertsAndClampsRawScopeBytes();
    void rejectsInvalidScopeFrames();
    void coalescesScopeFrames();
    void limitsScopeFrameRate();
    void sustainsSelectedFrameRate();
    void resetDropsPendingScopeFrame();
    void rebuildsAndClearsWaterfall();
    void rendersAndScrollsWaterfallRows();
    void mapsPartialDataRangeToIdlePixels();
    void pausePreventsRendering();
};

void WaterfallScopeTest::convertsAndClampsRawScopeBytes()
{
    const QByteArray raw = QByteArray::fromHex("00017fa0a1ff");
    QCOMPARE(ScopeAdapter::toLevels(raw), QVector<float>({0.0f, 1.0f, 127.0f, 160.0f, 160.0f, 160.0f}));

    ScopeAdapter::toLevels(raw, nullptr);
}

void WaterfallScopeTest::rejectsInvalidScopeFrames()
{
    ScopeController controller;
    QSignalSpy dataSpy(&controller, &ScopeController::spectrumDataReady);

    ScopeData invalid;
    invalid.valid = false;
    invalid.data = QByteArray::fromHex("01");
    controller.acceptScopeData(invalid);

    ScopeData empty;
    empty.valid = true;
    controller.acceptScopeData(empty);
    QTest::qWait(25);
    QCOMPARE(dataSpy.count(), 0);
}

void WaterfallScopeTest::coalescesScopeFrames()
{
    ScopeController controller;
    QSignalSpy dataSpy(&controller, &ScopeController::spectrumDataReady);
    QSignalSpy receivedSpy(&controller, &ScopeController::scopeDataReceived);

    ScopeData first;
    first.valid = true;
    first.startFreq = 144.0;
    first.endFreq = 145.0;
    first.data = QByteArray::fromHex("0102");
    controller.acceptScopeData(first);

    ScopeData latest = first;
    latest.startFreq = 145.0;
    latest.endFreq = 146.0;
    latest.oor = true;
    latest.data = QByteArray::fromHex("03a1");
    controller.acceptScopeData(latest);

    QTRY_COMPARE(dataSpy.count(), 1);
    QCOMPARE(receivedSpy.count(), 1);
    const QList<QVariant> arguments = dataSpy.takeFirst();
    QCOMPARE(arguments.at(0).value<QVector<float>>(), QVector<float>({3.0f, 160.0f}));
    QCOMPARE(arguments.at(1).toDouble(), 145.0);
    QCOMPARE(arguments.at(2).toDouble(), 146.0);
    QCOMPARE(arguments.at(3).toBool(), true);
}

void WaterfallScopeTest::limitsScopeFrameRate()
{
    ScopeController controller;
    QCOMPARE(controller.framesPerSecond(), 30);
    QCOMPARE(sdr9700::spectrumFrameIntervalMs(controller.framesPerSecond()), 34);

    controller.setFramesPerSecond(10);
    QCOMPARE(controller.framesPerSecond(), 10);
    QCOMPARE(sdr9700::spectrumFrameIntervalMs(controller.framesPerSecond()), 100);

    QSignalSpy dataSpy(&controller, &ScopeController::spectrumDataReady);
    ScopeData frame;
    frame.valid = true;
    frame.data = QByteArray::fromHex("01");
    controller.acceptScopeData(frame);
    QTRY_COMPARE(dataSpy.count(), 1);

    frame.data = QByteArray::fromHex("02");
    controller.acceptScopeData(frame);
    QTest::qWait(50);
    QCOMPARE(dataSpy.count(), 1);
    QTRY_COMPARE(dataSpy.count(), 2);
    QCOMPARE(dataSpy.constLast().constFirst().value<QVector<float>>(), QVector<float>({2.0f}));

    controller.setFramesPerSecond(12);
    QCOMPARE(controller.framesPerSecond(), 30);
    QCOMPARE(sdr9700::spectrumFrameIntervalMs(30), 34);
}

void WaterfallScopeTest::sustainsSelectedFrameRate()
{
    ScopeController controller;
    controller.setFramesPerSecond(30);
    QSignalSpy dataSpy(&controller, &ScopeController::spectrumDataReady);

    ScopeData frame;
    frame.valid = true;
    frame.data = QByteArray::fromHex("01");
    QTimer sourceTimer;
    sourceTimer.setTimerType(Qt::PreciseTimer);
    sourceTimer.setInterval(33);
    connect(&sourceTimer, &QTimer::timeout, &controller,
            [&controller, &frame]()
            {
                frame.data[0] = char(uchar(frame.data[0]) + 1);
                controller.acceptScopeData(frame);
            });

    sourceTimer.start();
    QTest::qWait(2000);
    sourceTimer.stop();

    // A 30 Hz source must not collapse toward the old ~18 Hz behavior. Leave
    // scheduling headroom for shared CI runners while still rejecting a timer
    // that waits a complete extra source period after every emission.
    QVERIFY2(dataSpy.count() >= 50, qPrintable(QStringLiteral("emitted %1 frames").arg(dataSpy.count())));
    QVERIFY(dataSpy.count() <= 62);
}

void WaterfallScopeTest::resetDropsPendingScopeFrame()
{
    ScopeController controller;
    QSignalSpy dataSpy(&controller, &ScopeController::spectrumDataReady);
    ScopeData frame;
    frame.valid = true;
    frame.data = QByteArray::fromHex("0102");
    controller.acceptScopeData(frame);
    controller.reset();
    QTest::qWait(25);
    QCOMPARE(dataSpy.count(), 0);
}

void WaterfallScopeTest::rebuildsAndClearsWaterfall()
{
    WaterfallController controller;
    QSignalSpy imageSpy(&controller, &WaterfallController::imageChanged);
    controller.setCanvasSize(QSize(4, 3));
    QCOMPARE(controller.image().size(), QSize(4, 3));
    QCOMPARE(imageSpy.count(), 1);

    controller.clearDisplay();
    QCOMPARE(imageSpy.count(), 2);
    const QRgb idle = controller.image().pixel(0, 0);
    for (int y = 0; y < controller.image().height(); ++y)
    {
        for (int x = 0; x < controller.image().width(); ++x)
        {
            QCOMPARE(controller.image().pixel(x, y), idle);
        }
    }
}

void WaterfallScopeTest::rendersAndScrollsWaterfallRows()
{
    WaterfallController controller;
    controller.setCanvasSize(QSize(3, 2));
    controller.setFrequencyRange(144.0, 146.0);
    controller.setDataFrequencyRange(144.0, 146.0);

    auto visiblePixel = [&controller](int x, int y)
    {
        const int physicalRow = (controller.firstVisibleRow() + y) % controller.image().height();
        return controller.image().pixel(x, physicalRow);
    };

    controller.updateSpectrum({0.0f, 80.0f, 160.0f});
    QTRY_VERIFY(visiblePixel(0, 0) != visiblePixel(2, 0));
    const QVector<QRgb> firstRow = {visiblePixel(0, 0), visiblePixel(1, 0), visiblePixel(2, 0)};

    controller.updateSpectrum({160.0f, 80.0f, 0.0f});
    QTRY_COMPARE(visiblePixel(0, 1), firstRow.at(0));
    QCOMPARE(visiblePixel(1, 1), firstRow.at(1));
    QCOMPARE(visiblePixel(2, 1), firstRow.at(2));
}

void WaterfallScopeTest::mapsPartialDataRangeToIdlePixels()
{
    WaterfallController controller;
    controller.setCanvasSize(QSize(5, 1));
    controller.setFrequencyRange(144.0, 148.0);
    const QRgb idle = controller.image().pixel(0, 0);
    controller.setDataFrequencyRange(145.0, 147.0);
    controller.updateSpectrum({160.0f, 160.0f, 160.0f});
    QTRY_VERIFY(controller.image().pixel(2, 0) != idle);
    QCOMPARE(controller.image().pixel(0, 0), idle);
    QCOMPARE(controller.image().pixel(4, 0), idle);
}

void WaterfallScopeTest::pausePreventsRendering()
{
    WaterfallController controller;
    controller.setCanvasSize(QSize(2, 1));
    const QImage initial = controller.image();
    controller.setPaused(true);
    controller.updateSpectrum({160.0f, 160.0f});
    QTest::qWait(45);
    QCOMPARE(controller.image(), initial);
}

QTEST_GUILESS_MAIN(WaterfallScopeTest)

#include "WaterfallScopeTest.moc"
