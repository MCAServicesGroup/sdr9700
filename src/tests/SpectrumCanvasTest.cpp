#include "SpectrumScopeCanvas.h"
#include "SpectrumScopeDisplay.h"
#include "UiTheme.h"
#include "WaterfallCanvas.h"

#include <QSignalSpy>
#include <QSet>
#include <QTest>
#include <QWheelEvent>
#include <cmath>

class SpectrumCanvasTest : public QObject
{
    Q_OBJECT

  private slots:
    void mapsFrequencyAcrossClosedPixelRange();
    void alignsWaterfallWithSpectrumFrequencyPlot();
    void keepsSpectrumAndWaterfallHeightsEqual();
    void normalizesReversedRange();
    void emitsFrequencyForClickWithoutDrag();
    void ignoresClicksOutsidePlotAndWhileLocked();
    void emitsWheelStepsAndHonorsInversion();
    void paintsEmptyAndPopulatedData();
    void keepsMaximumScopeLevelBelowTopEdge();
    void mapsObservedS8ScopePeakToMeterFraction();
    void keepsHorizontalGridDivisionsEven();
    void smoothsSuccessiveFramesAndResetsAcrossRanges();
    void interpolatesSparseBinsIntoContinuousTrace();
    void colorsTraceBySignalIntensity();
    void paintsNeutralShelfEdges();
};

void SpectrumCanvasTest::mapsFrequencyAcrossClosedPixelRange()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(144.0, 148.0);
    QCOMPARE(canvas.freqToX(144.0), 0);
    QCOMPARE(canvas.freqToX(148.0), 429);
    QCOMPARE(canvas.freqToX(146.0), 429 / 2);
}

void SpectrumCanvasTest::alignsWaterfallWithSpectrumFrequencyPlot()
{
    SpectrumScopeDisplay display;
    display.resize(700, 500);
    display.show();

    auto* spectrum = display.findChild<SpectrumScopeCanvas*>();
    auto* waterfall = display.findChild<WaterfallCanvas*>();
    QVERIFY(spectrum != nullptr);
    QVERIFY(waterfall != nullptr);
    QCOMPARE(waterfall->geometry().left(), spectrum->freqToX(144.0));
    QCOMPARE(waterfall->geometry().right(), spectrum->freqToX(146.0));

    display.resize(900, 600);
    QCoreApplication::processEvents();
    QCOMPARE(waterfall->geometry().left(), spectrum->freqToX(144.0));
    QCOMPARE(waterfall->geometry().right(), spectrum->freqToX(146.0));
}

void SpectrumCanvasTest::keepsSpectrumAndWaterfallHeightsEqual()
{
    SpectrumScopeDisplay display;
    display.resize(700, 500);
    display.show();

    auto* spectrum = display.findChild<SpectrumScopeCanvas*>();
    auto* waterfall = display.findChild<WaterfallCanvas*>();
    QVERIFY(spectrum != nullptr);
    QVERIFY(waterfall != nullptr);
    QCOMPARE(spectrum->height() - SpectrumScopeCanvas::scaleHeight(), waterfall->height());

    display.resize(900, 600);
    QCoreApplication::processEvents();
    QCOMPARE(spectrum->height() - SpectrumScopeCanvas::scaleHeight(), waterfall->height());
}

void SpectrumCanvasTest::normalizesReversedRange()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(148.0, 144.0);
    QVERIFY(canvas.freqToX(144.0) < canvas.freqToX(148.0));
}

void SpectrumCanvasTest::emitsFrequencyForClickWithoutDrag()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(144.0, 148.0);
    canvas.show();
    QSignalSpy clickSpy(&canvas, &SpectrumScopeCanvas::frequencyClicked);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(canvas.freqToX(146.0), 80));
    QCOMPARE(clickSpy.count(), 1);
    QVERIFY(qAbs(clickSpy.takeFirst().at(0).toDouble() - 146.0) < 0.02);
}

void SpectrumCanvasTest::ignoresClicksOutsidePlotAndWhileLocked()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    QSignalSpy clickSpy(&canvas, &SpectrumScopeCanvas::frequencyClicked);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(5, canvas.height() - 1));
    canvas.setInteractionLocked(true);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(230, 80));
    QCOMPARE(clickSpy.count(), 0);
}

void SpectrumCanvasTest::emitsWheelStepsAndHonorsInversion()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    QSignalSpy wheelSpy(&canvas, &SpectrumScopeCanvas::wheelStepRequested);
    const QPointF position(230, 80);
    QWheelEvent forward(position, canvas.mapToGlobal(position.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &forward);
    QCOMPARE(wheelSpy.takeFirst().at(0).toInt(), 1);

    canvas.setInvertMouseWheel(true);
    QWheelEvent inverted(position, canvas.mapToGlobal(position.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton,
                         Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &inverted);
    QCOMPARE(wheelSpy.takeFirst().at(0).toInt(), -1);
}

void SpectrumCanvasTest::paintsEmptyAndPopulatedData()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    canvas.clearDisplay();
    QVERIFY(!canvas.grab().isNull());
    canvas.updateSpectrum({0.0f, 40.0f, 80.0f, 120.0f, 160.0f}, false);
    QVERIFY(!canvas.grab().isNull());
    canvas.updateSpectrum({}, true);
    QVERIFY(!canvas.grab().isNull());
}

void SpectrumCanvasTest::keepsMaximumScopeLevelBelowTopEdge()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    canvas.updateSpectrum(QVector<float>(64, 160.0f), false);
    QCoreApplication::processEvents();

    // A saturated frame draws one continuous trace. It must retain visible
    // headroom rather than touching the top inset, which made strong signals
    // appear clipped even though their radio values had been bounded safely.
    // Verify the projection directly; palette changes must not make a brighter
    // grid row look like the trace to this geometry regression.
    const int plotHeight = canvas.height() - SpectrumScopeCanvas::scaleHeight();
    const int firstTraceRow = int(std::lround(canvas.levelToY(160.0f, 0, plotHeight)));

    QVERIFY(firstTraceRow >= 8);
    QVERIFY(firstTraceRow < 24);
}

void SpectrumCanvasTest::mapsObservedS8ScopePeakToMeterFraction()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    canvas.updateSpectrum(QVector<float>(64, 35.0f), false);
    QCoreApplication::processEvents();

    const int plotHeight = canvas.height() - SpectrumScopeCanvas::scaleHeight();
    const double traceRow = canvas.levelToY(35.0f, 0, plotHeight);
    const double displayedFraction = 1.0 - traceRow / double(plotHeight - 1);
    QVERIFY(displayedFraction >= 0.39);
    QVERIFY(displayedFraction <= 0.43);
}

void SpectrumCanvasTest::keepsHorizontalGridDivisionsEven()
{
    SpectrumScopeCanvas canvas;
    constexpr int kPlotHeight = 400;
    QVector<double> gridRows;
    for (int level = 0; level <= 160; level += 20)
    {
        gridRows.append(canvas.gridLevelToY(float(level), 0, kPlotHeight));
    }

    const double firstGap = qAbs(gridRows[1] - gridRows[0]);
    for (int index = 2; index < gridRows.size(); ++index)
    {
        QVERIFY(qAbs(qAbs(gridRows[index] - gridRows[index - 1]) - firstGap) < 0.001);
    }

    // Signal samples retain their independent calibrated transfer function.
    QVERIFY(canvas.levelToY(35.0f, 0, kPlotHeight) < canvas.gridLevelToY(35.0f, 0, kPlotHeight));
}

void SpectrumCanvasTest::smoothsSuccessiveFramesAndResetsAcrossRanges()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();

    canvas.updateSpectrum(QVector<float>(64, 0.0f), false);
    canvas.updateSpectrum(QVector<float>(64, 160.0f), false);
    QCoreApplication::processEvents();
    QCOMPARE(canvas.m_spectrumBins.constFirst(), 56.0f);
    const int plotHeight = canvas.height() - SpectrumScopeCanvas::scaleHeight();
    const int smoothedRow = int(std::lround(canvas.levelToY(canvas.m_spectrumBins.constFirst(), 0, plotHeight)));
    QVERIFY(smoothedRow >= 85);
    QVERIFY(smoothedRow < 115);

    canvas.setDataFrequencyRange(144.0, 145.0);
    canvas.updateSpectrum(QVector<float>(64, 160.0f), false);
    QCoreApplication::processEvents();
    QCOMPARE(canvas.m_spectrumBins.constFirst(), 160.0f);
    const int resetRow = int(std::lround(canvas.levelToY(canvas.m_spectrumBins.constFirst(), 0, plotHeight)));
    QVERIFY(resetRow >= 8);
    QVERIFY(resetRow < 24);
}

void SpectrumCanvasTest::interpolatesSparseBinsIntoContinuousTrace()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(144.0, 145.0);
    canvas.setDataFrequencyRange(144.0, 145.0);
    canvas.setVfoFrequency(999.0);
    canvas.show();
    canvas.updateSpectrum({0.0f, 0.0f, 160.0f, 160.0f}, false);
    QCoreApplication::processEvents();

    const QImage rendered = canvas.grab().toImage();
    QSet<int> traceRows;
    for (int x = 120; x <= 310; ++x)
    {
        int brightestRow = -1;
        int brightestValue = -1;
        for (int y = 0; y < rendered.height() - SpectrumScopeCanvas::scaleHeight(); ++y)
        {
            const int value = qGray(rendered.pixel(x, y));
            if (value > brightestValue)
            {
                brightestValue = value;
                brightestRow = y;
            }
        }
        traceRows.insert(brightestRow);
    }

    // Nearest-bin plotting produces only two long plateaus with a near-vertical
    // join. Subpixel Catmull-Rom sampling must populate many intermediate rows.
    QVERIFY(traceRows.size() > 20);
}

void SpectrumCanvasTest::colorsTraceBySignalIntensity()
{
    QCOMPARE(UiTheme::spectrumSignalColor(0.00), QColor::fromRgbF(0.0, 0.0, 1.0));
    QCOMPARE(UiTheme::spectrumSignalColor(0.50), QColor::fromRgbF(0.0, 1.0, 0.0));
    QCOMPARE(UiTheme::spectrumSignalColor(1.00), QColor::fromRgbF(1.0, 0.0, 0.0));
    QCOMPARE(UiTheme::sMeterSignalColor(0.00), QColor(0x00, 0x90, 0x30));
    QCOMPARE(UiTheme::sMeterSignalColor(0.50), QColor(0xd4, 0xc0, 0x00));
    QCOMPARE(UiTheme::sMeterSignalColor(0.85), QColor(0xff, 0x00, 0x00));

    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();

    auto strongestColorNearRow = [&canvas](int expectedRow)
    {
        const QImage rendered = canvas.grab().toImage();
        QColor strongest;
        int strongestChannel = -1;
        const int x = rendered.width() / 3;
        for (int y = qMax(0, expectedRow - 3); y <= qMin(rendered.height() - 1, expectedRow + 3); ++y)
        {
            const QColor candidate = rendered.pixelColor(x, y);
            const int channel = qMax(candidate.red(), qMax(candidate.green(), candidate.blue()));
            if (channel > strongestChannel)
            {
                strongest = candidate;
                strongestChannel = channel;
            }
        }
        return strongest;
    };
    auto frameWithPeak = [](float floor, float peak)
    {
        QVector<float> levels(64, floor);
        for (int index = 19; index <= 24; ++index)
        {
            levels[index] = peak;
        }
        return levels;
    };

    canvas.updateSpectrum(frameWithPeak(0.0f, 20.0f), false);
    QCoreApplication::processEvents();
    const int weakRow =
        int(std::lround(canvas.levelToY(20.0f, 0, canvas.height() - SpectrumScopeCanvas::scaleHeight())));
    const QColor weakColor = strongestColorNearRow(weakRow);
    QVERIFY(weakColor.blue() > weakColor.red() + 80);

    canvas.clearDisplay();
    canvas.updateSpectrum(frameWithPeak(0.0f, 35.0f), false);
    QCoreApplication::processEvents();
    const int s8Row = int(std::lround(canvas.levelToY(35.0f, 0, canvas.height() - SpectrumScopeCanvas::scaleHeight())));
    const QColor s8Color = strongestColorNearRow(s8Row);
    QVERIFY(s8Color.green() > s8Color.red() + 80);
    QVERIFY(s8Color.green() > s8Color.blue() + 50);

    canvas.clearDisplay();
    canvas.updateSpectrum(frameWithPeak(0.0f, 160.0f), false);
    QCoreApplication::processEvents();
    const QColor saturatedColor = strongestColorNearRow(10);
    QVERIFY(saturatedColor.red() > saturatedColor.green() + 80);
    QVERIFY(saturatedColor.red() > saturatedColor.blue() + 80);
}

void SpectrumCanvasTest::paintsNeutralShelfEdges()
{
    SpectrumScopeCanvas spectrum;
    spectrum.resize(320, 180);
    QPixmap spectrumImage(spectrum.size());
    spectrum.render(&spectrumImage);
    const int spectrumEdgeY = spectrum.height() - SpectrumScopeCanvas::scaleHeight() - 1;
    QCOMPARE(spectrumImage.toImage().pixelColor(20, spectrumEdgeY), UiTheme::Color::ScopeShelfEdge);

    WaterfallCanvas waterfall;
    waterfall.resize(320, 120);
    QPixmap waterfallImage(waterfall.size());
    waterfall.render(&waterfallImage);
    QCOMPARE(waterfallImage.toImage().pixelColor(20, 0), UiTheme::Color::ScopeShelfEdge);
}

QTEST_MAIN(SpectrumCanvasTest)

#include "SpectrumCanvasTest.moc"
