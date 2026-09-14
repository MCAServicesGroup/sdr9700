#include "SpectrumScopeCanvas.h"
#include "WaterfallCanvas.h"

#include <QImage>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

#ifndef SDR9700_GPU_PANADAPTER
#error "GpuPanadapterRenderTest requires SDR9700_GPU_PANADAPTER"
#endif

class GpuPanadapterRenderTest : public QObject
{
    Q_OBJECT

  private slots:
    void rendersWaterfallRowsInRingOrder();
    void rendersSpectrumLayersWithoutTextureCollapse();
    void reusesSpectrumLayersAcrossUnchangedStateAndResize();
    void fallsBackToRasterOverlaysAfterGpuFailure();
};

void GpuPanadapterRenderTest::rendersWaterfallRowsInRingOrder()
{
    constexpr int kWidth = 96;
    constexpr int kHeight = 120;
    constexpr int kFirstVisibleRow = 17;
    QImage waterfall(kWidth, kHeight, QImage::Format_RGB32);
    waterfall.fill(Qt::black);
    for (int row = 0; row < kHeight; ++row)
    {
        QRgb* pixels = reinterpret_cast<QRgb*>(waterfall.scanLine(row));
        for (int x = 0; x < kWidth; ++x)
        {
            pixels[x] = qRgb(row * 2, 255 - row * 2, (row * 5) % 256);
        }
    }

    WaterfallCanvas canvas;
    canvas.resize(kWidth, kHeight);
    canvas.setWaterfallImageSource(&waterfall);
    canvas.setWaterfallRow(-1, kFirstVisibleRow);
    QSignalSpy failureSpy(&canvas, &QRhiWidget::renderFailed);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QImage rendered = canvas.grabFramebuffer();
    QVERIFY2(!rendered.isNull(), "QRhi failed to return a waterfall framebuffer");
    QCOMPARE(failureSpy.count(), 0);
    for (const int displayedRow : {20, 60, 100})
    {
        const int sourceRow = (kFirstVisibleRow + displayedRow) % kHeight;
        const QColor actual = rendered.pixelColor(kWidth / 2, displayedRow);
        const QColor expected = waterfall.pixelColor(kWidth / 2, sourceRow);
        QVERIFY(qAbs(actual.red() - expected.red()) <= 2);
        QVERIFY(qAbs(actual.green() - expected.green()) <= 2);
        QVERIFY(qAbs(actual.blue() - expected.blue()) <= 2);
    }
}

void GpuPanadapterRenderTest::rendersSpectrumLayersWithoutTextureCollapse()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(320, 200);
    canvas.setFrequencyRange(144.0, 145.0);
    canvas.setDataFrequencyRange(144.0, 145.0);
    QVector<float> bins(160, 10.0f);
    for (int index = 20; index < bins.size(); index += 30)
    {
        bins[index] = float(qMin(160, index + 50));
    }
    canvas.updateSpectrum(bins, false);
    QSignalSpy failureSpy(&canvas, &QRhiWidget::renderFailed);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QImage rendered = canvas.grabFramebuffer();
    QVERIFY2(!rendered.isNull(), "QRhi failed to return a spectrum framebuffer");
    QCOMPARE(failureSpy.count(), 0);
    QSet<QRgb> verticalColors;
    for (int row = 0; row < rendered.height(); ++row)
    {
        verticalColors.insert(rendered.pixel(rendered.width() / 3, row));
    }
    QVERIFY2(verticalColors.size() > 12, "Spectrum texture collapsed to a single sampled row");
    QVERIFY(rendered.pixelColor(rendered.width() / 2, rendered.height() - 1).alpha() > 0);
}

void GpuPanadapterRenderTest::reusesSpectrumLayersAcrossUnchangedStateAndResize()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(320, 200);
    QVector<float> bins(160, 20.0f);
    bins[bins.size() / 2] = 150.0f;
    canvas.updateSpectrum(bins, false);
    QSignalSpy failureSpy(&canvas, &QRhiWidget::renderFailed);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QVERIFY(!canvas.grabFramebuffer().isNull());
    QVERIFY(!canvas.m_gpuOverlayDirty);

    canvas.setVfoFrequency(145.0);
    canvas.setFilterWidth(-1400, 1400);
    QVERIFY(!canvas.m_gpuOverlayDirty);

    canvas.resize(400, 240);
    QTRY_COMPARE(canvas.size(), QSize(400, 240));
    const QImage resized = canvas.grabFramebuffer();
    QVERIFY2(!resized.isNull(), "QRhi failed to render the resized spectrum framebuffer");
    QCOMPARE(resized.size(), canvas.size() * canvas.devicePixelRatioF());
    QCOMPARE(failureSpy.count(), 0);
}

void GpuPanadapterRenderTest::fallsBackToRasterOverlaysAfterGpuFailure()
{
    SpectrumScopeCanvas spectrum;
    spectrum.resize(320, 200);
    QVector<float> bins(160, 10.0f);
    bins[bins.size() / 2] = 150.0f;
    spectrum.updateSpectrum(bins, false);
    spectrum.show();
    QVERIFY(QTest::qWaitForWindowExposed(&spectrum));
    QVERIFY(!spectrum.grabFramebuffer().isNull());
    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("Forced spectrum fallback.*raster rendering")));
    spectrum.requestRasterFallback(QStringLiteral("Forced spectrum fallback"));
    QTRY_VERIFY(spectrum.m_rasterFallbackOverlay != nullptr);
    QVERIFY(spectrum.m_rasterFallbackOverlay->isVisible());
    const QImage rasterSpectrum = spectrum.m_rasterFallbackOverlay->grab().toImage();
    QVERIFY(!rasterSpectrum.isNull());
    QSet<QRgb> rasterSpectrumColors;
    for (int row = 0; row < rasterSpectrum.height(); ++row)
    {
        rasterSpectrumColors.insert(rasterSpectrum.pixel(rasterSpectrum.width() / 3, row));
    }
    QVERIFY(rasterSpectrumColors.size() > 12);

    QImage waterfallImage(96, 120, QImage::Format_RGB32);
    waterfallImage.fill(Qt::blue);
    WaterfallCanvas waterfall;
    waterfall.resize(waterfallImage.size());
    waterfall.setWaterfallImageSource(&waterfallImage);
    waterfall.show();
    QVERIFY(QTest::qWaitForWindowExposed(&waterfall));
    QVERIFY(!waterfall.grabFramebuffer().isNull());
    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("Forced waterfall fallback.*raster rendering")));
    waterfall.requestRasterFallback(QStringLiteral("Forced waterfall fallback"));
    QTRY_VERIFY(waterfall.m_rasterFallbackOverlay != nullptr);
    QVERIFY(waterfall.m_rasterFallbackOverlay->isVisible());
    const QImage rasterWaterfall = waterfall.m_rasterFallbackOverlay->grab().toImage();
    QVERIFY(!rasterWaterfall.isNull());
    QCOMPARE(rasterWaterfall.pixelColor(rasterWaterfall.width() / 2, rasterWaterfall.height() / 2), QColor(Qt::blue));
}

QTEST_MAIN(GpuPanadapterRenderTest)

#include "GpuPanadapterRenderTest.moc"
