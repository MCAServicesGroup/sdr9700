#include "SpectrumScopeCanvas.h"
#include "UiTheme.h"
#include "LogCategories.h"

#include <QFontMetrics>
#include <QFile>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSizeF>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

#ifdef SDR9700_GPU_PANADAPTER
#include <rhi/qrhi.h>
#endif

namespace
{
constexpr int kClickMoveTolerancePx = 6;
constexpr int kLevelScaleTopInsetPx = 6;
// Map the raw scope minimum to the clipped bottom edge. This suppresses the
// artificial full-width baseline while leaving signal peaks visible.
constexpr int kLevelScaleBottomInsetPx = 0;
constexpr int kFrequencyLabelHorizontalPaddingPx = 6;
constexpr int kGridDensityFewer = 0;
constexpr int kGridDensityMore = 2;
constexpr float kSpectrumSmoothingAlpha = 0.35f;
constexpr float kMaximumSpatialSmoothBlend = 0.75f;
constexpr int kTraceSamplesPerPixel = 1;
constexpr int kMinimumRasterTraceSamples = 64;
constexpr int kRasterTraceColorSegmentPoints = 24;
constexpr int kToolbarShadowHeightPx = 8;
constexpr int kScaleShadowHeightPx = 8;
constexpr double kScopeDisplayExponent = 0.58;
constexpr double kScopeDisplayCeilingFraction = 0.98;
constexpr double kWheelStepAngleDelta = 120.0;
constexpr double kMinFrequencyRangeMhz = 0.001;

bool normalizeFrequencyRange(double* startMhz, double* endMhz)
{
    if (!startMhz || !endMhz || !std::isfinite(*startMhz) || !std::isfinite(*endMhz))
    {
        return false;
    }
    if (*endMhz < *startMhz)
    {
        std::swap(*startMhz, *endMhz);
    }
    return (*endMhz - *startMhz) >= kMinFrequencyRangeMhz;
}

double lowFrequencyMhz(double startMhz, double endMhz)
{
    return qMin(startMhz, endMhz);
}

double highFrequencyMhz(double startMhz, double endMhz)
{
    return qMax(startMhz, endMhz);
}

QColor colorWithAlpha(const QColor& color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

QColor spectrumHeatColor(float level)
{
    const double linearFraction = std::clamp(double(level) / 160.0, 0.0, 1.0);
    return UiTheme::spectrumSignalColor(std::pow(linearFraction, kScopeDisplayExponent));
}

int normalizedGridDensity(int density)
{
    return qBound(kGridDensityFewer, density, kGridDensityMore);
}

#ifdef SDR9700_GPU_PANADAPTER
using ColorVertex = std::array<float, 6>;
using TextureVertex = std::array<float, 4>;

struct alignas(16) TextureUniforms
{
    float matrix[16]{};
    float rowOffset{0.0f};
    float padding[3]{0.0f, 0.0f, 0.0f};
};

QShader loadShader(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

QRhiGraphicsPipeline::TargetBlend alphaBlend()
{
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
}

const char* rhiBackendName(QRhi::Implementation backend)
{
    switch (backend)
    {
    case QRhi::Vulkan:
        return "Vulkan";
    case QRhi::OpenGLES2:
        return "OpenGL";
    case QRhi::D3D11:
        return "Direct3D11";
    case QRhi::Metal:
        return "Metal";
    case QRhi::D3D12:
        return "Direct3D12";
    case QRhi::Null:
        return "Null";
    }
    return "Unknown";
}
#endif
} // namespace

#ifdef SDR9700_GPU_PANADAPTER
struct SpectrumScopeCanvas::GpuState
{
    QRhi* rhi{nullptr};
    QRhiRenderPassDescriptor* renderPassDescriptor{nullptr};
    QSize outputSize;
    qsizetype traceBufferCapacity{0};
    std::unique_ptr<QRhiBuffer> quadBuffer;
    std::unique_ptr<QRhiBuffer> uniformBuffer;
    std::unique_ptr<QRhiBuffer> traceBuffer;
    std::unique_ptr<QRhiTexture> backgroundTexture;
    std::unique_ptr<QRhiTexture> overlayTexture;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiShaderResourceBindings> colorBindings;
    std::unique_ptr<QRhiShaderResourceBindings> backgroundBindings;
    std::unique_ptr<QRhiShaderResourceBindings> overlayBindings;
    std::unique_ptr<QRhiGraphicsPipeline> backgroundPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> overlayPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> fillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> featherPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> linePipeline;

    void release()
    {
        linePipeline.reset();
        featherPipeline.reset();
        fillPipeline.reset();
        overlayPipeline.reset();
        backgroundPipeline.reset();
        overlayBindings.reset();
        backgroundBindings.reset();
        colorBindings.reset();
        sampler.reset();
        overlayTexture.reset();
        backgroundTexture.reset();
        traceBuffer.reset();
        uniformBuffer.reset();
        quadBuffer.reset();
        traceBufferCapacity = 0;
        outputSize = {};
        renderPassDescriptor = nullptr;
        rhi = nullptr;
    }
};
#endif

SpectrumScopeCanvas::SpectrumScopeCanvas(QWidget* parent) : SpectrumScopeCanvasBase(parent)
{
#ifdef SDR9700_GPU_PANADAPTER
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
    {
        setApi(QRhiWidget::Api::Null);
    }
#ifdef Q_OS_MAC
    else
    {
        setApi(QRhiWidget::Api::Metal);
    }
#endif
    m_gpuState = std::make_unique<GpuState>();
#endif
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    m_repaintTimer.setSingleShot(true);
    m_repaintTimer.setInterval(16);
    connect(&m_repaintTimer, &QTimer::timeout, this, qOverload<>(&SpectrumScopeCanvas::update));
}

SpectrumScopeCanvas::~SpectrumScopeCanvas() = default;

int SpectrumScopeCanvas::plotHeight() const
{
    return qMax(1, height() - scaleHeight());
}

int SpectrumScopeCanvas::plotRightX() const
{
    return qMax(plotLeftX(), width() - 1);
}

int SpectrumScopeCanvas::plotWidthPx() const
{
    return plotRightX() - plotLeftX();
}

double SpectrumScopeCanvas::xToFreq(int x) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int plotLeft = plotLeftX();
    const int plotRight = plotRightX();
    const int plotW = plotWidthPx();
    if (plotW <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    // Map the right edge to the last drawable pixel, not one pixel past the
    // widget. Click-to-tune and trace drawing must share the same closed pixel
    // range or signals near the edge appear slightly displaced after tuning.
    const int plotX = qBound(plotLeft, x, plotRight);
    return startMhz + (double(plotX - plotLeft) / plotW) * (endMhz - startMhz);
}

int SpectrumScopeCanvas::freqToX(double mhz) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int plotLeft = plotLeftX();
    const int plotW = plotWidthPx();
    if (plotW <= 0 || endMhz <= startMhz)
    {
        return plotLeft;
    }
    return plotLeft + int((mhz - startMhz) / (endMhz - startMhz) * plotW);
}

double SpectrumScopeCanvas::levelToY(float level, int topY, int h) const
{
    const double linearFraction = std::clamp(double(level - m_minLevel) / double(m_maxLevel - m_minLevel), 0.0, 1.0);
    // IC-9700 scope bytes are vertical raster intensities, not S-meter units.
    // A linear 0..160 projection substantially understates ordinary received
    // signals: a simultaneously observed S8 signal produced a scope peak near
    // 35 while CI-V 15 02 reported 103..105. The exponent maps that observation
    // to about 41% of the full S0..S9+60 meter range, while the ceiling fraction
    // reserves two percent of headroom for a maximum 160-byte scope sample.
    const double norm = std::pow(linearFraction, kScopeDisplayExponent) * kScopeDisplayCeilingFraction;
    const int topInset = qMin(kLevelScaleTopInsetPx, qMax(0, h - 1));
    const int bottomInset = qMin(kLevelScaleBottomInsetPx, qMax(0, h - 1 - topInset));
    return topY + topInset + (1.0 - norm) * qMax(1, h - 1 - topInset - bottomInset);
}

double SpectrumScopeCanvas::gridLevelToY(float level, int topY, int h) const
{
    const double norm = std::clamp(double(level - m_minLevel) / double(m_maxLevel - m_minLevel), 0.0, 1.0);
    const int topInset = qMin(kLevelScaleTopInsetPx, qMax(0, h - 1));
    const int bottomInset = qMin(kLevelScaleBottomInsetPx, qMax(0, h - 1 - topInset));
    return topY + topInset + (1.0 - norm) * qMax(1, h - 1 - topInset - bottomInset);
}

double SpectrumScopeCanvas::sourcePositionForDisplayX(double x, int binCount) const
{
    const double displayStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double displayEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const double dataStartMhz = lowFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const double dataEndMhz = highFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const int plotW = plotWidthPx();
    if (binCount <= 0 || plotW <= 0 || displayEndMhz <= displayStartMhz || dataEndMhz <= dataStartMhz)
    {
        return -1.0;
    }
    const double boundedX = qBound(double(plotLeftX()), x, double(plotRightX()));
    const double mhz = displayStartMhz + ((boundedX - plotLeftX()) / plotW) * (displayEndMhz - displayStartMhz);
    if (mhz < dataStartMhz || mhz > dataEndMhz)
    {
        return -1.0;
    }
    if (binCount == 1)
    {
        return 0.0;
    }

    const double normalized = (mhz - dataStartMhz) / (dataEndMhz - dataStartMhz);
    return qBound(0.0, normalized * double(binCount - 1), double(binCount - 1));
}

float SpectrumScopeCanvas::interpolatedLevel(const QVector<float>& levels, double sourcePosition)
{
    if (levels.isEmpty() || sourcePosition < 0.0)
    {
        return 0.0f;
    }
    if (levels.size() == 1 || sourcePosition >= levels.size() - 1)
    {
        return levels.constLast();
    }

    const int i1 = qBound(0, int(std::floor(sourcePosition)), levels.size() - 1);
    const int i2 = qMin(i1 + 1, levels.size() - 1);
    const float t = float(sourcePosition - i1);
    const float p0 = levels[qMax(0, i1 - 1)];
    const float p1 = levels[i1];
    const float p2 = levels[i2];
    const float p3 = levels[qMin(levels.size() - 1, i2 + 1)];
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float interpolated = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);

    // Catmull-Rom can overshoot around a sharp transition. The scope trace is
    // measured data, so interpolation may round the path between adjacent bins
    // but must never fabricate a value outside those bins' actual range.
    return qBound(qMin(p1, p2), interpolated, qMax(p1, p2));
}

QVector<float> SpectrumScopeCanvas::spatiallySmoothedBins(const QVector<float>& bins)
{
    if (bins.size() < 3)
    {
        return bins;
    }

    int plateauPairs = 0;
    for (int i = 1; i < bins.size(); ++i)
    {
        if (qAbs(bins[i] - bins[i - 1]) < 0.01f)
        {
            ++plateauPairs;
        }
    }
    const float plateauFraction = float(plateauPairs) / float(bins.size() - 1);
    const float blend = kMaximumSpatialSmoothBlend * qBound(0.0f, (plateauFraction - 0.35f) / 0.30f, 1.0f);
    if (blend <= 0.0f)
    {
        return bins;
    }

    QVector<float> smoothedBins(bins.size());
    smoothedBins[0] = bins[0];
    smoothedBins.last() = bins.constLast();
    for (int i = 1; i < bins.size() - 1; ++i)
    {
        const float smoothed =
            (i >= 2 && i + 2 < bins.size())
                ? (bins[i - 2] + 4.0f * bins[i - 1] + 6.0f * bins[i] + 4.0f * bins[i + 1] + bins[i + 2]) / 16.0f
                : (bins[i - 1] + 2.0f * bins[i] + bins[i + 1]) / 4.0f;
        smoothedBins[i] = bins[i] * (1.0f - blend) + smoothed * blend;
    }
    return smoothedBins;
}

bool SpectrumScopeCanvas::isSpectrumClickArea(const QPoint& pos) const
{
    const QRect plotRect(plotLeftX(), 0, qMax(0, width() - plotLeftX()), qMax(0, plotHeight() - 1));
    return plotRect.contains(pos);
}

void SpectrumScopeCanvas::invalidateStaticLayer()
{
    m_staticLayerDirty = true;
#ifdef SDR9700_GPU_PANADAPTER
    m_gpuBackgroundDirty = true;
    m_gpuTraceDirty = true;
    invalidateGpuOverlay();
#endif
}

#ifdef SDR9700_GPU_PANADAPTER
void SpectrumScopeCanvas::invalidateGpuOverlay()
{
    m_gpuOverlayDirty = true;
}
#endif

void SpectrumScopeCanvas::ensureStaticLayer()
{
    const QSize currentSize = size();
    if (!currentSize.isValid())
    {
        return;
    }

    const qreal devicePixelRatio = devicePixelRatioF();
    if (!m_staticLayerDirty && !m_staticLayer.isNull() && m_staticLayerSize == currentSize &&
        qFuzzyCompare(m_staticLayerDevicePixelRatio, devicePixelRatio))
    {
        return;
    }

    m_staticLayer = QPixmap((QSizeF(currentSize) * devicePixelRatio).toSize());
    m_staticLayer.setDevicePixelRatio(devicePixelRatio);
    m_staticLayerSize = currentSize;
    m_staticLayerDevicePixelRatio = devicePixelRatio;
    m_staticLayer.fill(Qt::transparent);

    QPainter painter(&m_staticLayer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    renderStaticLayer(&painter);

    m_staticLayerDirty = false;
}

void SpectrumScopeCanvas::renderStaticLayer(QPainter* painter) const
{
    if (!painter)
    {
        return;
    }

    static const QColor kBgScale(0x06, 0x11, 0x16);
    static const QColor kGridText(0xc6, 0xe0, 0xe8);

    const int specH = plotHeight();
    const int w = width();
    const int specTop = 0;
    const int specDrawH = specH;

    QLinearGradient specBg(0, 0, 0, specH);
    specBg.setColorAt(0.00, m_backgroundColor);
    specBg.setColorAt(0.52, m_backgroundColor.lighter(145));
    specBg.setColorAt(1.00, m_backgroundColor.darker(135));
    painter->fillRect(0, 0, w, specH, specBg);

    {
        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);

        const float range = m_maxLevel - m_minLevel;
        // Keep the level grid open enough that the spectrum trace is not boxed in by
        // closely spaced horizontal rules. Density preferences still scale this base.
        float majorLevelStep = range > 100.0f ? 20.0f : 10.0f;
        if (m_gridDensity == kGridDensityFewer)
        {
            majorLevelStep *= 2.0f;
        }
        else if (m_gridDensity == kGridDensityMore)
        {
            majorLevelStep /= 2.0f;
        }
        auto drawLevelLines = [&](float step)
        {
            const int firstStep = int(std::ceil(m_minLevel / step));
            const int lastStep = int(std::floor(m_maxLevel / step));
            for (int i = firstStep; i <= lastStep; ++i)
            {
                const float level = float(i) * step;
                if (qFuzzyIsNull(level - m_minLevel))
                {
                    // The opaque red scope boundary owns the minimum-level
                    // row. Drawing the blue grid floor there leaves a second
                    // device-pixel row beside it and makes the pair appear
                    // purple on high-DPI displays.
                    continue;
                }
                // Grid geometry is a visual ruler, not an amplitude transfer
                // curve. Keep its divisions linear even though received trace
                // samples use the calibrated non-linear projection.
                const int y = gridLevelToY(level, specTop, specDrawH);
                painter->drawLine(plotLeftX(), y, w, y);
            }
        };

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 86), 1));
        drawLevelLines(majorLevelStep);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const int plotW = qMax(1, plotWidthPx());
        const double mhzPerPx = (scaleEndMhz > scaleStartMhz) ? (scaleEndMhz - scaleStartMhz) / plotW : 1.0;
        // The reference presentation uses closely spaced major divisions. Start
        // at 100 kHz and still coarsen the step for wider spans or small canvases.
        double tickStep = 0.1;
        double minMajorGridPx = 60.0;
        if (m_gridDensity == kGridDensityFewer)
        {
            minMajorGridPx = 105.0;
        }
        else if (m_gridDensity == kGridDensityMore)
        {
            minMajorGridPx = 35.0;
        }
        while (tickStep / mhzPerPx < minMajorGridPx && tickStep < 100)
        {
            tickStep *= 2;
        }
        auto drawMhzLines = [&](double step)
        {
            const qint64 firstStep = qint64(std::ceil(scaleStartMhz / step - 1e-9));
            const qint64 lastStep = qint64(std::floor(scaleEndMhz / step + 1e-9));
            for (qint64 i = firstStep; i <= lastStep; ++i)
            {
                const double mhz = double(i) * step;
                const int x = freqToX(mhz);
                painter->drawLine(x, specTop, x, specH);
            }
        };

        painter->setPen(QPen(colorWithAlpha(m_gridLineColor, 86), 1));
        drawMhzLines(tickStep);
    }

    {
        const int scaleY = specH - 1;
        painter->fillRect(0, scaleY, w, scaleHeight(), kBgScale);
        painter->setPen(kGridText);

        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);
        const QFontMetrics fontMetrics(f);

        const double scaleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
        const double scaleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
        const int plotW = qMax(1, plotWidthPx());
        const double mhzPerPx = (scaleEndMhz > scaleStartMhz) ? (scaleEndMhz - scaleStartMhz) / plotW : 1.0;
        static constexpr double kNiceSteps[] = {100.0, 50.0, 25.0, 10.0, 5.0,   2.5,  1.0,
                                                0.5,   0.25, 0.1,  0.05, 0.025, 0.01, 0.005};
        double tickStep = kNiceSteps[0];
        for (double s : kNiceSteps)
        {
            if (s / mhzPerPx >= 70.0)
            {
                tickStep = s;
            }
            else
            {
                break;
            }
        }

        const int decimals = (tickStep >= 1.0) ? 1 : (tickStep >= 0.1) ? 2 : 3;
        const int tickTop = scaleY + 2;
        const int tickH = 6;
        const int textY = scaleY + 21;
        const qint64 firstStep = qint64(std::ceil(scaleStartMhz / tickStep - 1e-9));
        const qint64 lastStep = qint64(std::floor(scaleEndMhz / tickStep + 1e-9));
        for (qint64 i = firstStep; i <= lastStep; ++i)
        {
            const double mhz = double(i) * tickStep;
            const int x = freqToX(mhz);
            const QString label = QString::number(mhz, 'f', decimals);
            const int labelW = fontMetrics.horizontalAdvance(label);
            const int labelX = qBound(plotLeftX() + kFrequencyLabelHorizontalPaddingPx, x - labelW / 2,
                                      qMax(plotLeftX() + kFrequencyLabelHorizontalPaddingPx,
                                           w - labelW - kFrequencyLabelHorizontalPaddingPx));
            painter->setPen(QPen(kGridText, 1));
            painter->drawLine(x, tickTop, x, tickTop + tickH);
            painter->setPen(kGridText);
            painter->drawText(labelX, textY, label);
        }
    }
}

void SpectrumScopeCanvas::setFrequencyRange(double startMhz, double endMhz)
{
    if (!normalizeFrequencyRange(&startMhz, &endMhz))
    {
        return;
    }
    if (m_startMhz == startMhz && m_endMhz == endMhz)
    {
        return;
    }
    m_startMhz = startMhz;
    m_endMhz = endMhz;
    m_resetSpectrumSmoothing = true;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setDataFrequencyRange(double startMhz, double endMhz)
{
    if (!normalizeFrequencyRange(&startMhz, &endMhz))
    {
        return;
    }
    if (m_dataStartMhz == startMhz && m_dataEndMhz == endMhz)
    {
        return;
    }
    m_dataStartMhz = startMhz;
    m_dataEndMhz = endMhz;
    m_resetSpectrumSmoothing = true;
#ifdef SDR9700_GPU_PANADAPTER
    m_gpuTraceDirty = true;
#endif
}

void SpectrumScopeCanvas::setVfoFrequency(double freqMhz)
{
    m_vfoMhz = freqMhz;
#ifdef SDR9700_GPU_PANADAPTER
    invalidateGpuOverlay();
#endif
    scheduleRepaint();
}

void SpectrumScopeCanvas::setVfoMarkerColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    QColor markerColor = color;
    markerColor.setAlpha(230);
    if (m_vfoMarkerColor == markerColor)
    {
        return;
    }

    m_vfoMarkerColor = markerColor;
#ifdef SDR9700_GPU_PANADAPTER
    invalidateGpuOverlay();
#endif
    scheduleRepaint();
}

void SpectrumScopeCanvas::setBackgroundColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_backgroundColor == normalized)
    {
        return;
    }

    m_backgroundColor = normalized;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setGridLineColor(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_gridLineColor == normalized)
    {
        return;
    }

    m_gridLineColor = normalized;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setGridDensity(int density)
{
    const int normalized = normalizedGridDensity(density);
    if (m_gridDensity == normalized)
    {
        return;
    }

    m_gridDensity = normalized;
    invalidateStaticLayer();
    scheduleRepaint();
}

void SpectrumScopeCanvas::setFilterWidth(int lowHz, int highHz)
{
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
#ifdef SDR9700_GPU_PANADAPTER
    invalidateGpuOverlay();
#endif
    scheduleRepaint();
}

void SpectrumScopeCanvas::setInteractionLocked(bool locked)
{
    if (m_interactionLocked == locked)
    {
        return;
    }

    m_interactionLocked = locked;
    m_clickPressed = false;
    scheduleRepaint();
}

void SpectrumScopeCanvas::setInvertMouseWheel(bool invert)
{
    m_invertMouseWheel = invert;
}

void SpectrumScopeCanvas::updateSpectrum(const QVector<float>& levels, bool outOfRange)
{
    // Keep an owned copy for painting because the incoming QVector belongs to
    // the model signal delivery path and may be superseded before paintEvent().
    // Backout/optimization point: a future double-buffered SpectrumScopeModel
    // could own this storage and let the canvas paint a shared immutable frame.
    // Reuse the existing backing store where possible. Spectrum frames arrive
    // continuously, so avoiding a fresh QVector allocation per repaint keeps
    // click tuning and waterfall painting from competing with allocator churn.
    if (m_resetSpectrumSmoothing || m_spectrumBins.size() != levels.size())
    {
        m_spectrumBins = levels;
        m_resetSpectrumSmoothing = false;
    }
    else
    {
        for (int i = 0; i < levels.size(); ++i)
        {
            m_spectrumBins[i] =
                kSpectrumSmoothingAlpha * levels[i] + (1.0f - kSpectrumSmoothingAlpha) * m_spectrumBins[i];
        }
    }
    const bool outOfRangeChanged = m_scopeOutOfRange != outOfRange;
    m_scopeOutOfRange = outOfRange;
#ifdef SDR9700_GPU_PANADAPTER
    if (outOfRangeChanged)
    {
        invalidateGpuOverlay();
    }
#else
    Q_UNUSED(outOfRangeChanged)
#endif

    m_displaySpectrumBins = spatiallySmoothedBins(m_spectrumBins);
#ifdef SDR9700_GPU_PANADAPTER
    m_gpuTraceDirty = true;
#endif

    scheduleRepaint();
}

void SpectrumScopeCanvas::clearDisplay()
{
    m_spectrumBins.clear();
    m_displaySpectrumBins.clear();
    m_scopeOutOfRange = false;
    m_resetSpectrumSmoothing = true;
#ifdef SDR9700_GPU_PANADAPTER
    m_gpuTraceDirty = true;
    invalidateGpuOverlay();
#endif
    scheduleRepaint();
}

void SpectrumScopeCanvas::scheduleRepaint()
{
    if (!m_repaintTimer.isActive())
    {
        m_repaintTimer.start();
    }
}

void SpectrumScopeCanvas::renderDynamicLayer(QPainter* painter) const
{
    if (!painter)
    {
        return;
    }

    const int specH = plotHeight();
    const int w = width();
    const int specTop = 0;
    const int specDrawH = specH;
    if (m_scopeOutOfRange)
    {
        painter->setPen(QColor(0xff, 0x7a, 0x7a));
        QFont font = painter->font();
        font.setPointSize(10);
        font.setBold(true);
        painter->setFont(font);
        painter->drawText(QRect(0, specTop, w, specDrawH), Qt::AlignCenter, QStringLiteral("OUT OF RANGE"));
    }

    const double visibleStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double visibleEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    if (m_vfoMhz >= visibleStartMhz && m_vfoMhz <= visibleEndMhz && m_filterHighHz > m_filterLowHz)
    {
        const double loMhz = m_vfoMhz + m_filterLowHz / 1e6;
        const double hiMhz = m_vfoMhz + m_filterHighHz / 1e6;
        const int fx1 = freqToX(loMhz);
        const int fx2 = freqToX(hiMhz);
        painter->fillRect(fx1, 0, fx2 - fx1, specH, QColor(0x00, 0xb4, 0xd8, 22));
    }

    if (m_vfoMhz >= visibleStartMhz && m_vfoMhz <= visibleEndMhz)
    {
        const int vx = freqToX(m_vfoMhz);
        const int scaleY = specH - 1;
        painter->setPen(QPen(m_vfoMarkerColor, 1, Qt::SolidLine));
        painter->drawLine(vx, 0, vx, scaleY - 1);
    }

    const int shadowTop = qMax(0, specH - kScaleShadowHeightPx);
    QLinearGradient scaleShadow(0, shadowTop, 0, specH);
    scaleShadow.setColorAt(0.0, QColor(0x00, 0x08, 0x0f, 0));
    scaleShadow.setColorAt(1.0, QColor(0x00, 0x04, 0x08, 220));
    painter->fillRect(0, shadowTop, w, specH - shadowTop, scaleShadow);
    painter->fillRect(0, specH - 1, w, 1, UiTheme::Color::ScopeShelfEdge);

    const int toolbarShadowHeight = qMin(specH, kToolbarShadowHeightPx);
    QLinearGradient toolbarShadow(0, 0, 0, toolbarShadowHeight);
    toolbarShadow.setColorAt(0.0, QColor(0x00, 0x04, 0x08, 180));
    toolbarShadow.setColorAt(1.0, QColor(0x00, 0x08, 0x0f, 0));
    painter->fillRect(0, 0, w, toolbarShadowHeight, toolbarShadow);
    painter->fillRect(0, 0, w, 1, UiTheme::Color::ScopeShelfEdge);
}

void SpectrumScopeCanvas::paintEvent(QPaintEvent* event)
{
#ifdef SDR9700_GPU_PANADAPTER
    if (api() != QRhiWidget::Api::Null)
    {
        QRhiWidget::paintEvent(event);
        return;
    }
#endif
    Q_UNUSED(event)

    ensureStaticLayer();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int specH = plotHeight();
    const int w = width();
    const int specTop = 0;
    const int specDrawH = specH;
    const QRect spectrumPlotRect(plotLeftX(), specTop, qMax(0, w - plotLeftX()), qMax(0, specDrawH));
    p.drawPixmap(0, 0, m_staticLayer);

    if (!m_displaySpectrumBins.isEmpty())
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.save();
        p.setClipRect(spectrumPlotRect);
        QVector<QPointF> tracePoints;
        QVector<float> traceLevels;

        const int sampleCount =
            qMax(1, qMin(plotWidthPx(), qMax(kMinimumRasterTraceSamples, m_displaySpectrumBins.size())));
        tracePoints.reserve(sampleCount + 1);
        traceLevels.reserve(sampleCount + 1);
        for (int sample = 0; sample <= sampleCount; ++sample)
        {
            const double x = plotLeftX() + (double(sample) / sampleCount) * plotWidthPx();
            const double sourcePosition = sourcePositionForDisplayX(x, m_displaySpectrumBins.size());
            const float level =
                sourcePosition >= 0.0 ? interpolatedLevel(m_displaySpectrumBins, sourcePosition) : m_minLevel;

            const double sy = levelToY(level, specTop, specDrawH);
            tracePoints.append(QPointF(x, sy));
            traceLevels.append(level);
        }

        QPolygonF tracePolygon(tracePoints);
        QPolygonF fillPolygon(tracePolygon);
        fillPolygon.append(QPointF(tracePoints.constLast().x(), specH));
        fillPolygon.append(QPointF(tracePoints.constFirst().x(), specH));
        QColor fillTopColor = UiTheme::spectrumSignalColor(0.5);
        fillTopColor.setAlpha(54);
        QLinearGradient fillGradient(0.0, 0.0, 0.0, specH);
        fillGradient.setColorAt(0.0, fillTopColor);
        fillGradient.setColorAt(1.0, QColor(0x00, 0x00, 0x4d, 178));
        p.setPen(Qt::NoPen);
        p.setBrush(fillGradient);
        p.drawPolygon(fillPolygon);

        auto drawColoredTrace = [&](qreal width, int alpha)
        {
            for (int first = 0; first + 1 < tracePoints.size(); first += kRasterTraceColorSegmentPoints)
            {
                const int last = qMin(first + kRasterTraceColorSegmentPoints, tracePoints.size() - 1);
                float segmentLevel = traceLevels[first];
                QPolygonF segment;
                segment.reserve(last - first + 1);
                for (int index = first; index <= last; ++index)
                {
                    segmentLevel = qMax(segmentLevel, traceLevels[index]);
                    segment.append(tracePoints[index]);
                }
                QColor color = spectrumHeatColor(segmentLevel);
                color.setAlpha(alpha);
                p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPolyline(segment);
            }
        };
        drawColoredTrace(3.0, 56);
        drawColoredTrace(1.0, 255);

        p.restore();
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    renderDynamicLayer(&p);
}

#ifdef SDR9700_GPU_PANADAPTER
void SpectrumScopeCanvas::ensureGpuLayers()
{
    const bool staticLayerWasDirty = m_staticLayerDirty || m_staticLayerSize != size();
    ensureStaticLayer();
    if (staticLayerWasDirty)
    {
        m_gpuBackgroundDirty = true;
        m_gpuTraceDirty = true;
    }

    const qreal devicePixelRatio = devicePixelRatioF();
    const QSize pixelSize = (QSizeF(size()) * devicePixelRatio).toSize();
    if (!m_gpuOverlayDirty && m_gpuOverlayLayer.size() == pixelSize &&
        qFuzzyCompare(m_gpuOverlayLayer.devicePixelRatio(), devicePixelRatio))
    {
        return;
    }

    m_gpuOverlayLayer = QImage(pixelSize, QImage::Format_RGBA8888);
    m_gpuOverlayLayer.setDevicePixelRatio(devicePixelRatio);
    m_gpuOverlayLayer.fill(Qt::transparent);
    QPainter painter(&m_gpuOverlayLayer);
    painter.setRenderHint(QPainter::Antialiasing, false);
    renderDynamicLayer(&painter);
    m_gpuOverlayDirty = false;
    m_gpuOverlayTextureDirty = true;
}

void SpectrumScopeCanvas::rebuildGpuTrace()
{
    m_gpuFillVertices.clear();
    m_gpuFeatherVertices.clear();
    m_gpuLineVertices.clear();
    m_gpuTraceSize = size();
    m_gpuTraceDirty = false;
    if (m_displaySpectrumBins.isEmpty() || width() <= 1 || height() <= 1)
    {
        return;
    }

    const int sampleCount = qMax(1, plotWidthPx() * kTraceSamplesPerPixel);
    const int pointCount = sampleCount + 1;
    QVector<QPointF> points;
    QVector<QColor> colors;
    points.reserve(pointCount);
    colors.reserve(pointCount);
    for (int sample = 0; sample <= sampleCount; ++sample)
    {
        const double x = plotLeftX() + (double(sample) / sampleCount) * plotWidthPx();
        const double sourcePosition = sourcePositionForDisplayX(x, m_displaySpectrumBins.size());
        const float level =
            sourcePosition >= 0.0 ? interpolatedLevel(m_displaySpectrumBins, sourcePosition) : m_minLevel;
        points.append(QPointF(x, levelToY(level, 0, plotHeight())));
        colors.append(spectrumHeatColor(level));
    }

    auto normalizedX = [this](double x) { return float((2.0 * x / qMax(1, width() - 1)) - 1.0); };
    auto normalizedY = [this](double y) { return float(1.0 - (2.0 * y / qMax(1, height() - 1))); };
    auto colorVertex = [&](const QPointF& point, const QColor& color, float alpha)
    {
        return ColorVertex{normalizedX(point.x()), normalizedY(point.y()), color.redF(),
                           color.greenF(),         color.blueF(),          alpha};
    };

    m_gpuFillVertices.resize(pointCount * 2 * qsizetype(sizeof(ColorVertex)));
    auto* fillVertices = reinterpret_cast<ColorVertex*>(m_gpuFillVertices.data());
    const QColor bottomColor(0x00, 0x00, 0x4d);
    for (int index = 0; index < pointCount; ++index)
    {
        fillVertices[index * 2] = colorVertex(points[index], colors[index], 54.0f / 255.0f);
        fillVertices[index * 2 + 1] =
            colorVertex(QPointF(points[index].x(), plotHeight()), bottomColor, 178.0f / 255.0f);
    }

    m_gpuFeatherVertices.resize(pointCount * qsizetype(sizeof(ColorVertex)));
    m_gpuLineVertices.resize(pointCount * qsizetype(sizeof(ColorVertex)));
    auto* featherVertices = reinterpret_cast<ColorVertex*>(m_gpuFeatherVertices.data());
    auto* lineVertices = reinterpret_cast<ColorVertex*>(m_gpuLineVertices.data());
    for (int index = 0; index < pointCount; ++index)
    {
        featherVertices[index] = colorVertex(points[index], colors[index], 56.0f / 255.0f);
        lineVertices[index] = colorVertex(points[index], colors[index], 1.0f);
    }
}

void SpectrumScopeCanvas::initialize(QRhiCommandBuffer* commandBuffer)
{
    if (!m_gpuState)
    {
        m_gpuState = std::make_unique<GpuState>();
    }

    QRhi* currentRhi = rhi();
    QRhiRenderTarget* currentTarget = renderTarget();
    const QSize outputSize = currentTarget ? currentTarget->pixelSize() : QSize();
    if (!currentRhi || !currentTarget || outputSize.isEmpty())
    {
        return;
    }
    if (m_gpuState->rhi == currentRhi && m_gpuState->outputSize == outputSize &&
        m_gpuState->renderPassDescriptor == currentTarget->renderPassDescriptor())
    {
        return;
    }

    const bool rhiChanged = m_gpuState->rhi != currentRhi;
    m_gpuState->release();
    GpuState& state = *m_gpuState;
    state.rhi = currentRhi;
    state.outputSize = outputSize;
    state.renderPassDescriptor = currentTarget->renderPassDescriptor();

    static constexpr TextureVertex kQuadVertices[] = {
        {-1.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
    };
    state.quadBuffer.reset(
        state.rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadVertices)));
    state.uniformBuffer.reset(
        state.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(TextureUniforms)));
    state.backgroundTexture.reset(state.rhi->newTexture(QRhiTexture::RGBA8, outputSize));
    state.overlayTexture.reset(state.rhi->newTexture(QRhiTexture::RGBA8, outputSize));
    state.sampler.reset(state.rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                              QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));

    const bool resourcesCreated = state.quadBuffer->create() && state.uniformBuffer->create() &&
                                  state.backgroundTexture->create() && state.overlayTexture->create() &&
                                  state.sampler->create();
    if (!resourcesCreated)
    {
        qCritical(logSpectrumScope()) << "Could not create GPU panadapter resources";
        state.release();
        return;
    }

    state.colorBindings.reset(state.rhi->newShaderResourceBindings());
    state.colorBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, state.uniformBuffer.get())});
    state.colorBindings->create();
    auto createTextureBindings = [&](QRhiTexture* texture)
    {
        std::unique_ptr<QRhiShaderResourceBindings> bindings(state.rhi->newShaderResourceBindings());
        bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                                        state.uniformBuffer.get()),
                               QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                         texture, state.sampler.get())});
        bindings->create();
        return bindings;
    };
    state.backgroundBindings = createTextureBindings(state.backgroundTexture.get());
    state.overlayBindings = createTextureBindings(state.overlayTexture.get());

    const QShader colorVertexShader = loadShader(QStringLiteral(":/shaders/gui/shaders/panadapter_color.vert.qsb"));
    const QShader colorFragmentShader = loadShader(QStringLiteral(":/shaders/gui/shaders/panadapter_color.frag.qsb"));
    const QShader textureVertexShader = loadShader(QStringLiteral(":/shaders/gui/shaders/panadapter_texture.vert.qsb"));
    const QShader textureFragmentShader =
        loadShader(QStringLiteral(":/shaders/gui/shaders/panadapter_texture.frag.qsb"));
    if (!colorVertexShader.isValid() || !colorFragmentShader.isValid() || !textureVertexShader.isValid() ||
        !textureFragmentShader.isValid())
    {
        qCritical(logSpectrumScope()) << "Could not load GPU panadapter shaders";
        state.release();
        return;
    }

    QRhiVertexInputLayout textureLayout;
    textureLayout.setBindings({QRhiVertexInputBinding(4 * sizeof(float))});
    textureLayout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
                                 QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float))});
    auto createTexturePipeline = [&](QRhiShaderResourceBindings* bindings, bool blending)
    {
        std::unique_ptr<QRhiGraphicsPipeline> pipeline(state.rhi->newGraphicsPipeline());
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, textureVertexShader}, {QRhiShaderStage::Fragment, textureFragmentShader}});
        pipeline->setVertexInputLayout(textureLayout);
        pipeline->setShaderResourceBindings(bindings);
        pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pipeline->setSampleCount(currentTarget->sampleCount());
        pipeline->setRenderPassDescriptor(state.renderPassDescriptor);
        if (blending)
        {
            pipeline->setTargetBlends({alphaBlend()});
        }
        pipeline->create();
        return pipeline;
    };
    state.backgroundPipeline = createTexturePipeline(state.backgroundBindings.get(), false);
    state.overlayPipeline = createTexturePipeline(state.overlayBindings.get(), true);

    QRhiVertexInputLayout colorLayout;
    colorLayout.setBindings({QRhiVertexInputBinding(6 * sizeof(float))});
    colorLayout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
                               QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float))});
    auto createColorPipeline = [&](QRhiGraphicsPipeline::Topology topology, float lineWidth)
    {
        std::unique_ptr<QRhiGraphicsPipeline> pipeline(state.rhi->newGraphicsPipeline());
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, colorVertexShader}, {QRhiShaderStage::Fragment, colorFragmentShader}});
        pipeline->setVertexInputLayout(colorLayout);
        pipeline->setShaderResourceBindings(state.colorBindings.get());
        pipeline->setTopology(topology);
        pipeline->setLineWidth(lineWidth);
        pipeline->setSampleCount(currentTarget->sampleCount());
        pipeline->setRenderPassDescriptor(state.renderPassDescriptor);
        pipeline->setTargetBlends({alphaBlend()});
        pipeline->create();
        return pipeline;
    };
    state.fillPipeline = createColorPipeline(QRhiGraphicsPipeline::TriangleStrip, 1.0f);
    state.featherPipeline = createColorPipeline(QRhiGraphicsPipeline::LineStrip, 3.0f);
    state.linePipeline = createColorPipeline(QRhiGraphicsPipeline::LineStrip, 1.0f);

    if (rhiChanged)
    {
        const QRhiDriverInfo driver = state.rhi->driverInfo();
        qInfo(logSpectrumScope()).noquote().nospace()
            << "GPU panadapter initialized backend=" << rhiBackendName(state.rhi->backend())
            << " device=" << driver.deviceName;
    }

    QRhiResourceUpdateBatch* updates = state.rhi->nextResourceUpdateBatch();
    updates->uploadStaticBuffer(state.quadBuffer.get(), kQuadVertices);
    commandBuffer->resourceUpdate(updates);
    m_gpuBackgroundDirty = true;
    m_gpuOverlayDirty = true;
    m_gpuOverlayTextureDirty = true;
    m_gpuTraceDirty = true;
}

void SpectrumScopeCanvas::render(QRhiCommandBuffer* commandBuffer)
{
    if (!m_gpuState || !m_gpuState->rhi || !renderTarget())
    {
        return;
    }

    ensureGpuLayers();
    if (m_gpuTraceDirty || m_gpuTraceSize != size())
    {
        rebuildGpuTrace();
    }

    GpuState& state = *m_gpuState;
    const qsizetype requiredTraceBytes =
        m_gpuFillVertices.size() + m_gpuFeatherVertices.size() + m_gpuLineVertices.size();
    if (requiredTraceBytes > state.traceBufferCapacity)
    {
        state.traceBufferCapacity = qMax<qsizetype>(requiredTraceBytes, 4096);
        state.traceBuffer.reset(
            state.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, quint32(state.traceBufferCapacity)));
        if (!state.traceBuffer->create())
        {
            qCritical(logSpectrumScope()) << "Could not create GPU spectrum vertex buffer";
            state.traceBuffer.reset();
            state.traceBufferCapacity = 0;
        }
    }

    TextureUniforms uniforms;
    const QMatrix4x4 matrix = state.rhi->clipSpaceCorrMatrix();
    std::copy(matrix.constData(), matrix.constData() + 16, uniforms.matrix);
    QRhiResourceUpdateBatch* updates = state.rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(state.uniformBuffer.get(), 0, sizeof(uniforms), &uniforms);

    if (m_gpuBackgroundDirty)
    {
        const QImage background = m_staticLayer.toImage().convertToFormat(QImage::Format_RGBA8888);
        updates->uploadTexture(state.backgroundTexture.get(), background);
        m_gpuBackgroundDirty = false;
    }
    if (m_gpuOverlayTextureDirty)
    {
        updates->uploadTexture(state.overlayTexture.get(), m_gpuOverlayLayer);
        m_gpuOverlayTextureDirty = false;
    }
    if (state.traceBuffer && requiredTraceBytes > 0)
    {
        qsizetype offset = 0;
        updates->updateDynamicBuffer(state.traceBuffer.get(), quint32(offset), quint32(m_gpuFillVertices.size()),
                                     m_gpuFillVertices.constData());
        offset += m_gpuFillVertices.size();
        updates->updateDynamicBuffer(state.traceBuffer.get(), quint32(offset), quint32(m_gpuFeatherVertices.size()),
                                     m_gpuFeatherVertices.constData());
        offset += m_gpuFeatherVertices.size();
        updates->updateDynamicBuffer(state.traceBuffer.get(), quint32(offset), quint32(m_gpuLineVertices.size()),
                                     m_gpuLineVertices.constData());
    }

    commandBuffer->beginPass(renderTarget(), Qt::black, {1.0f, 0}, updates);
    commandBuffer->setViewport(QRhiViewport(0, 0, state.outputSize.width(), state.outputSize.height()));
    QRhiCommandBuffer::VertexInput binding(state.quadBuffer.get(), 0);
    commandBuffer->setGraphicsPipeline(state.backgroundPipeline.get());
    commandBuffer->setShaderResources(state.backgroundBindings.get());
    commandBuffer->setVertexInput(0, 1, &binding);
    commandBuffer->draw(4);

    if (state.traceBuffer && requiredTraceBytes > 0)
    {
        quint32 offset = 0;
        binding = QRhiCommandBuffer::VertexInput(state.traceBuffer.get(), offset);
        commandBuffer->setGraphicsPipeline(state.fillPipeline.get());
        commandBuffer->setShaderResources(state.colorBindings.get());
        commandBuffer->setVertexInput(0, 1, &binding);
        commandBuffer->draw(quint32(m_gpuFillVertices.size() / sizeof(ColorVertex)));

        offset += quint32(m_gpuFillVertices.size());
        binding = QRhiCommandBuffer::VertexInput(state.traceBuffer.get(), offset);
        commandBuffer->setGraphicsPipeline(state.featherPipeline.get());
        commandBuffer->setShaderResources(state.colorBindings.get());
        commandBuffer->setVertexInput(0, 1, &binding);
        commandBuffer->draw(quint32(m_gpuFeatherVertices.size() / sizeof(ColorVertex)));

        offset += quint32(m_gpuFeatherVertices.size());
        binding = QRhiCommandBuffer::VertexInput(state.traceBuffer.get(), offset);
        commandBuffer->setGraphicsPipeline(state.linePipeline.get());
        commandBuffer->setShaderResources(state.colorBindings.get());
        commandBuffer->setVertexInput(0, 1, &binding);
        commandBuffer->draw(quint32(m_gpuLineVertices.size() / sizeof(ColorVertex)));
    }

    binding = QRhiCommandBuffer::VertexInput(state.quadBuffer.get(), 0);
    commandBuffer->setGraphicsPipeline(state.overlayPipeline.get());
    commandBuffer->setShaderResources(state.overlayBindings.get());
    commandBuffer->setVertexInput(0, 1, &binding);
    commandBuffer->draw(4);
    commandBuffer->endPass();
}

void SpectrumScopeCanvas::releaseResources()
{
    if (m_gpuState)
    {
        m_gpuState->release();
    }
}
#endif

void SpectrumScopeCanvas::mousePressEvent(QMouseEvent* ev)
{
    if (m_interactionLocked)
    {
        return;
    }
    if (ev->button() == Qt::LeftButton)
    {
        m_clickPressed = isSpectrumClickArea(ev->pos());
        m_clickPressPos = ev->pos();
        ev->accept();
    }
}

void SpectrumScopeCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        const bool isClick = m_clickPressed && isSpectrumClickArea(ev->pos()) &&
                             (ev->pos() - m_clickPressPos).manhattanLength() <= kClickMoveTolerancePx;
        m_clickPressed = false;
        if (!m_interactionLocked && isClick)
        {
            Q_EMIT frequencyClicked(xToFreq(ev->pos().x()));
        }
        ev->accept();
    }
}

void SpectrumScopeCanvas::wheelEvent(QWheelEvent* ev)
{
    if (m_interactionLocked)
    {
        ev->ignore();
        return;
    }

    const QPoint angle = ev->angleDelta();
    const int rawDelta = angle.y() != 0 ? angle.y() : angle.x();
    if (rawDelta == 0 || m_endMhz <= m_startMhz)
    {
        ev->ignore();
        return;
    }

    double physicalSteps = rawDelta / kWheelStepAngleDelta;
    if (ev->inverted())
    {
        physicalSteps = -physicalSteps;
    }
    if (m_invertMouseWheel)
    {
        physicalSteps = -physicalSteps;
    }

    if ((m_wheelStepAccumulator > 0.0 && physicalSteps > 0.0) || (m_wheelStepAccumulator < 0.0 && physicalSteps < 0.0))
    {
        m_wheelStepAccumulator += physicalSteps;
    }
    else
    {
        m_wheelStepAccumulator = physicalSteps;
    }

    const int acceptedSteps = static_cast<int>(m_wheelStepAccumulator);
    if (acceptedSteps == 0)
    {
        ev->accept();
        return;
    }

    m_wheelStepAccumulator -= acceptedSteps;
    qDebug(logSpectrumScope()).noquote().nospace()
        << "Spectrum scope wheel angle=" << angle << " pixel=" << ev->pixelDelta() << " qtInverted=" << ev->inverted()
        << " physicalSteps=" << physicalSteps << " reversePreference=" << m_invertMouseWheel
        << " acceptedSteps=" << acceptedSteps << " accumulator=" << m_wheelStepAccumulator;

    Q_EMIT wheelStepRequested(acceptedSteps);
    ev->accept();
}
