#include "SpectrumScopeCanvas.h"
#include "WaterfallCanvas.h"

#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QSizeF>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <limits>

#ifndef SDR9700_GPU_PANADAPTER
#error "GpuPanadapterRenderTest requires SDR9700_GPU_PANADAPTER"
#endif

namespace
{
template <typename Canvas> void configureRequestedBackend(Canvas* canvas)
{
    if (qEnvironmentVariable("SDR9700_TEST_RHI_BACKEND") == QStringLiteral("vulkan"))
    {
        canvas->setApi(QRhiWidget::Api::Vulkan);
    }
}

int deviceCoordinate(double logicalCoordinate, qreal devicePixelRatio, int limit)
{
    return std::clamp(int(std::lround(logicalCoordinate * devicePixelRatio)), 0, std::max(0, limit - 1));
}

QSize expectedFramebufferSize(const QWidget& widget)
{
    return (QSizeF(widget.size()) * widget.devicePixelRatioF()).toSize();
}

double meanRgbDifference(const QImage& first, const QImage& second)
{
    if (first.size() != second.size() || first.isNull())
    {
        return std::numeric_limits<double>::infinity();
    }
    quint64 totalDifference = 0;
    quint64 sampleCount = 0;
    for (int y = 0; y < first.height(); y += 2)
    {
        for (int x = 0; x < first.width(); x += 2)
        {
            const QColor firstColor = first.pixelColor(x, y);
            const QColor secondColor = second.pixelColor(x, y);
            totalDifference += quint64(std::abs(firstColor.red() - secondColor.red()) +
                                       std::abs(firstColor.green() - secondColor.green()) +
                                       std::abs(firstColor.blue() - secondColor.blue()));
            sampleCount += 3;
        }
    }
    return sampleCount > 0 ? double(totalDifference) / double(sampleCount) : 0.0;
}

void verifyGpuBackend(const QString& backend)
{
    QVERIFY2(!backend.isEmpty(), "The canvas did not report an initialized QRhi backend");
#ifdef Q_OS_MAC
    QCOMPARE(backend, QStringLiteral("Metal"));
#else
    QVERIFY(backend != QStringLiteral("Null"));
    if (qEnvironmentVariable("SDR9700_TEST_RHI_BACKEND") == QStringLiteral("vulkan"))
    {
        QCOMPARE(backend, QStringLiteral("Vulkan"));
    }
#endif
}
} // namespace

class GpuPanadapterRenderTest : public QObject
{
    Q_OBJECT

  private slots:
    void rendersWaterfallRowsInRingOrder();
    void rendersSpectrumLayersWithoutTextureCollapse();
    void preservesNarrowSpectrumPeakGeometry();
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
    configureRequestedBackend(&canvas);
    canvas.resize(kWidth, kHeight);
    canvas.setWaterfallImageSource(&waterfall);
    canvas.setWaterfallRow(-1, kFirstVisibleRow);
    QSignalSpy failureSpy(&canvas, &QRhiWidget::renderFailed);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QImage rendered = canvas.grabFramebuffer();
    QVERIFY2(!rendered.isNull(), "QRhi failed to return a waterfall framebuffer");
    QCOMPARE(failureSpy.count(), 0);
    verifyGpuBackend(canvas.gpuBackendNameForTest());
    const qreal devicePixelRatio = canvas.devicePixelRatioF();
    for (const int displayedRow : {20, 60, 100})
    {
        const int sourceRow = (kFirstVisibleRow + displayedRow) % kHeight;
        const QColor actual = rendered.pixelColor(deviceCoordinate(kWidth / 2.0, devicePixelRatio, rendered.width()),
                                                  deviceCoordinate(displayedRow, devicePixelRatio, rendered.height()));
        const QColor expected = waterfall.pixelColor(kWidth / 2, sourceRow);
        QVERIFY(std::abs(actual.red() - expected.red()) <= 2);
        QVERIFY(std::abs(actual.green() - expected.green()) <= 2);
        QVERIFY(std::abs(actual.blue() - expected.blue()) <= 2);
    }
}

void GpuPanadapterRenderTest::rendersSpectrumLayersWithoutTextureCollapse()
{
    SpectrumScopeCanvas canvas;
    configureRequestedBackend(&canvas);
    canvas.resize(320, 200);
    canvas.setFrequencyRange(144.0, 145.0);
    canvas.setDataFrequencyRange(144.0, 145.0);
    canvas.setVfoFrequency(999.0);
    QVector<float> bins(160, 10.0f);
    for (int index = 20; index < bins.size(); index += 30)
    {
        bins[index] = float(std::min(160, index + 50));
    }
    canvas.updateSpectrum(bins, false);
    QSignalSpy failureSpy(&canvas, &QRhiWidget::renderFailed);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QImage rendered = canvas.grabFramebuffer();
    QVERIFY2(!rendered.isNull(), "QRhi failed to return a spectrum framebuffer");
    QCOMPARE(failureSpy.count(), 0);
    verifyGpuBackend(canvas.gpuBackendNameForTest());
    QSet<QRgb> verticalColors;
    for (int row = 0; row < rendered.height(); ++row)
    {
        verticalColors.insert(rendered.pixel(rendered.width() / 3, row));
    }
    QVERIFY2(verticalColors.size() > 12, "Spectrum texture collapsed to a single sampled row");
    QVERIFY(rendered.pixelColor(rendered.width() / 2, rendered.height() - 1).alpha() > 0);

    QImage raster(rendered.size(), QImage::Format_ARGB32_Premultiplied);
    raster.setDevicePixelRatio(canvas.devicePixelRatioF());
    raster.fill(Qt::transparent);
    QPainter rasterPainter(&raster);
    canvas.paintRaster(&rasterPainter);
    rasterPainter.end();
    QVERIFY2(meanRgbDifference(rendered, raster) < 18.0, "GPU and raster spectrum rendering diverged visibly");
}

void GpuPanadapterRenderTest::preservesNarrowSpectrumPeakGeometry()
{
    SpectrumScopeCanvas canvas;
    configureRequestedBackend(&canvas);
    canvas.resize(320, 200);
    canvas.setFrequencyRange(144.0, 145.0);
    canvas.setDataFrequencyRange(144.0, 145.0);
    canvas.setVfoFrequency(999.0);
    QVector<float> bins(161, 10.0f);
    bins[bins.size() / 2] = 150.0f;
    canvas.updateSpectrum(bins, false);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QVERIFY(!canvas.grabFramebuffer().isNull());

    QVector<QPointF> points;
    QVector<float> levels;
    canvas.buildTraceSamples(&points, &levels);
    QVERIFY(points.size() >= 3);
    QCOMPARE(*std::max_element(levels.cbegin(), levels.cend()), 150.0f);
    const qsizetype expectedVertexCount = 6 * (points.size() - 1) + 6 * (points.size() - 2);
    QCOMPARE(canvas.m_gpuLineVertices.size() / qsizetype(6 * sizeof(float)), expectedVertexCount);
}

void GpuPanadapterRenderTest::reusesSpectrumLayersAcrossUnchangedStateAndResize()
{
    SpectrumScopeCanvas canvas;
    configureRequestedBackend(&canvas);
    canvas.resize(320, 200);
    QVector<float> bins(160, 20.0f);
    bins[bins.size() / 2] = 150.0f;
    canvas.updateSpectrum(bins, false);
    QSignalSpy failureSpy(&canvas, &QRhiWidget::renderFailed);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QVERIFY(!canvas.grabFramebuffer().isNull());
    QVERIFY(!canvas.m_gpuOverlayDirty);
    QVERIFY(!canvas.m_gpuOverlayTextureDirty);
    QVERIFY(!canvas.m_gpuVertexUploadPending);
    const quintptr pipelineIdentity = canvas.gpuPipelineIdentityForTest();
    QVERIFY(pipelineIdentity != 0);

    canvas.setVfoFrequency(144.5);
    canvas.setFilterWidth(-1800, 2200);
    QVERIFY(!canvas.m_gpuOverlayDirty);
    QVERIFY(!canvas.m_gpuOverlayTextureDirty);
    QVERIFY(canvas.m_gpuTuningDirty);
    QVERIFY(!canvas.grabFramebuffer().isNull());
    QVERIFY(!canvas.m_gpuTuningDirty);
    QVERIFY(!canvas.m_gpuVertexUploadPending);

    canvas.resize(400, 240);
    QTRY_COMPARE(canvas.size(), QSize(400, 240));
    const QImage resized = canvas.grabFramebuffer();
    QVERIFY2(!resized.isNull(), "QRhi failed to render the resized spectrum framebuffer");
    QCOMPARE(resized.size(), expectedFramebufferSize(canvas));
    QCOMPARE(canvas.gpuPipelineIdentityForTest(), pipelineIdentity);
    QCOMPARE(failureSpy.count(), 0);
}

void GpuPanadapterRenderTest::fallsBackToRasterOverlaysAfterGpuFailure()
{
    SpectrumScopeCanvas spectrum;
    configureRequestedBackend(&spectrum);
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
    spectrum.resize(400, 240);
    QCoreApplication::processEvents();
    QVERIFY(!spectrum.gpuResourcesActiveForTest());

    QImage waterfallImage(96, 120, QImage::Format_RGB32);
    waterfallImage.fill(Qt::blue);
    WaterfallCanvas waterfall;
    configureRequestedBackend(&waterfall);
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
    waterfall.resize(120, 140);
    QCoreApplication::processEvents();
    QVERIFY(!waterfall.gpuResourcesActiveForTest());
}

QTEST_MAIN(GpuPanadapterRenderTest)

#include "GpuPanadapterRenderTest.moc"
