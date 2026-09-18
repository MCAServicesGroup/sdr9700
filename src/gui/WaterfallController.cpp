#include "WaterfallController.h"
#include "LogCategories.h"

#include <QElapsedTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace
{
constexpr double kMinFrequencyRangeMhz = 0.001;
constexpr int kWaterfallPaletteResolution = 4096;
const QRgb kWaterfallIdleColor = qRgb(0x02, 0x0c, 0x14);

QRgb interpolatedWaterfallColor(float normalizedLevel)
{
    static constexpr struct
    {
        float pos;
        int r, g, b;
    } kStops[] = {
        {0.00f, 0, 20, 120},  {0.18f, 0, 58, 205},  {0.34f, 0, 150, 255}, {0.50f, 0, 220, 105},
        {0.66f, 165, 245, 0}, {0.78f, 255, 230, 0}, {0.90f, 255, 92, 0},  {1.00f, 255, 255, 210},
    };
    for (std::size_t index = 1; index < std::size(kStops); ++index)
    {
        if (normalizedLevel <= kStops[index].pos)
        {
            const float fraction =
                (normalizedLevel - kStops[index - 1].pos) / (kStops[index].pos - kStops[index - 1].pos);
            const int red = int(kStops[index - 1].r + fraction * (kStops[index].r - kStops[index - 1].r));
            const int green = int(kStops[index - 1].g + fraction * (kStops[index].g - kStops[index - 1].g));
            const int blue = int(kStops[index - 1].b + fraction * (kStops[index].b - kStops[index - 1].b));
            return qRgb(red, green, blue);
        }
    }
    return qRgb(kStops[std::size(kStops) - 1].r, kStops[std::size(kStops) - 1].g, kStops[std::size(kStops) - 1].b);
}

const std::array<QRgb, kWaterfallPaletteResolution + 1>& waterfallPalette()
{
    static const auto kPalette = []()
    {
        std::array<QRgb, kWaterfallPaletteResolution + 1> palette{};
        for (int index = 0; index <= kWaterfallPaletteResolution; ++index)
        {
            palette[std::size_t(index)] = interpolatedWaterfallColor(float(index) / float(kWaterfallPaletteResolution));
        }
        return palette;
    }();
    return kPalette;
}

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
} // namespace

WaterfallController::WaterfallController(QObject* parent) : QObject(parent) {}

void WaterfallController::setCanvasSize(const QSize& size)
{
    if (!size.isValid() || size == m_canvasSize)
    {
        return;
    }
    m_canvasSize = size;
    if (m_waterfall.isNull())
    {
        rebuildImage();
    }
    else
    {
        remapHistory(size, m_startMhz, m_endMhz);
    }
}

double WaterfallController::xToFreq(int x) const
{
    const double startMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    const double endMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    const int right = qMax(0, m_canvasSize.width() - 1);
    if (right <= 0 || endMhz <= startMhz)
    {
        return startMhz;
    }
    // Keep the waterfall bin map aligned with the Spectrum Scope canvas by
    // treating the last drawable pixel as the end frequency.
    return startMhz + (double(qBound(0, x, right)) / right) * (endMhz - startMhz);
}

int WaterfallController::binForFrequency(double mhz, int binCount) const
{
    const double dataStartMhz = lowFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    const double dataEndMhz = highFrequencyMhz(m_dataStartMhz, m_dataEndMhz);
    if (binCount <= 0 || dataEndMhz <= dataStartMhz)
    {
        return -1;
    }
    if (mhz < dataStartMhz || mhz > dataEndMhz)
    {
        return -1;
    }
    if (binCount == 1)
    {
        return 0;
    }

    const double normalized = (mhz - dataStartMhz) / (dataEndMhz - dataStartMhz);
    return qBound(0, int(std::llround(normalized * double(binCount - 1))), binCount - 1);
}

int WaterfallController::binForDisplayX(int x, int binCount) const
{
    return binForFrequency(xToFreq(x), binCount);
}

QRgb WaterfallController::levelToColor(float level) const
{
    const float normalized = std::clamp((level - m_minLevel) / (m_maxLevel - m_minLevel), 0.0f, 1.0f);
    const int index =
        qBound(0, int(std::lround(normalized * kWaterfallPaletteResolution)), kWaterfallPaletteResolution);
    return waterfallPalette()[std::size_t(index)];
}

void WaterfallController::invalidateBinMap()
{
    m_binMap.clear();
    m_binMapBinCount = 0;
}

void WaterfallController::ensureBinMap(int binCount)
{
    if (binCount == m_binMapBinCount && m_binMap.size() == m_waterfall.width())
    {
        return;
    }

    m_binMap.resize(m_waterfall.width());
    for (int x = 0; x < m_binMap.size(); ++x)
    {
        m_binMap[x] = binForDisplayX(x, binCount);
    }
    m_binMapBinCount = binCount;
}

void WaterfallController::setFrequencyRange(double startMhz, double endMhz)
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
    invalidateBinMap();
    if (historyNeedsRemap(startMhz, endMhz))
    {
        remapHistory(m_canvasSize, startMhz, endMhz);
    }
}

void WaterfallController::setDataFrequencyRange(double startMhz, double endMhz)
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
    invalidateBinMap();
}

void WaterfallController::setPaused(bool paused)
{
    m_paused = paused;
}

void WaterfallController::updateSpectrum(const QVector<float>& levels)
{
    if (m_paused || m_waterfall.isNull() || m_waterfall.height() == 0)
    {
        return;
    }

    renderRow(levels);
}

void WaterfallController::clearDisplay()
{
    if (!m_waterfall.isNull())
    {
        m_waterfall.fill(kWaterfallIdleColor);
    }
    m_firstVisibleRow = 0;
    emit imageChanged();
}

void WaterfallController::rebuildImage()
{
    if (m_canvasSize.height() <= 0 || m_canvasSize.width() <= 0)
    {
        return;
    }
    m_waterfall = QImage(m_canvasSize, QImage::Format_RGB32);
    m_waterfall.fill(kWaterfallIdleColor);
    m_firstVisibleRow = 0;
    m_imageStartMhz = lowFrequencyMhz(m_startMhz, m_endMhz);
    m_imageEndMhz = highFrequencyMhz(m_startMhz, m_endMhz);
    invalidateBinMap();
    emit imageChanged();
}

bool WaterfallController::historyNeedsRemap(double newStartMhz, double newEndMhz) const
{
    if (m_waterfall.isNull() || m_waterfall.width() <= 1)
    {
        return true;
    }
    const double oldSpanMhz = m_imageEndMhz - m_imageStartMhz;
    if (oldSpanMhz < kMinFrequencyRangeMhz)
    {
        return true;
    }
    const double pixelsPerMhz = double(m_waterfall.width() - 1) / oldSpanMhz;
    const double startDisplacement = qAbs(newStartMhz - m_imageStartMhz) * pixelsPerMhz;
    const double endDisplacement = qAbs(newEndMhz - m_imageEndMhz) * pixelsPerMhz;
    // Preserve subpixel pans without touching the complete history image. Once
    // either edge has moved by a visible pixel, realign all retained rows in a
    // single remap instead of blanking the waterfall.
    return qMax(startDisplacement, endDisplacement) >= 1.0;
}

void WaterfallController::remapHistory(const QSize& newSize, double newStartMhz, double newEndMhz)
{
    if (!newSize.isValid())
    {
        return;
    }
    const double normalizedStartMhz = lowFrequencyMhz(newStartMhz, newEndMhz);
    const double normalizedEndMhz = highFrequencyMhz(newStartMhz, newEndMhz);
    newStartMhz = normalizedStartMhz;
    newEndMhz = normalizedEndMhz;
    if (m_waterfall.isNull())
    {
        rebuildImage();
        return;
    }

    const int oldWidth = m_waterfall.width();
    const int oldHeight = m_waterfall.height();
    const double oldSpanMhz = m_imageEndMhz - m_imageStartMhz;
    const double newSpanMhz = newEndMhz - newStartMhz;
    if (newSize == m_waterfall.size() && qFuzzyCompare(oldSpanMhz, newSpanMhz) && oldWidth > 1)
    {
        const int pixelShift = int(std::llround(((newStartMhz - m_imageStartMhz) / oldSpanMhz) * (oldWidth - 1)));
        if (pixelShift != 0 && qAbs(pixelShift) < oldWidth)
        {
            for (int row = 0; row < oldHeight; ++row)
            {
                auto* pixels = reinterpret_cast<QRgb*>(m_waterfall.scanLine(row));
                if (pixelShift > 0)
                {
                    std::memmove(pixels, pixels + pixelShift, size_t(oldWidth - pixelShift) * sizeof(QRgb));
                    std::fill(pixels + oldWidth - pixelShift, pixels + oldWidth, kWaterfallIdleColor);
                }
                else
                {
                    const int rightShift = -pixelShift;
                    std::memmove(pixels + rightShift, pixels, size_t(oldWidth - rightShift) * sizeof(QRgb));
                    std::fill(pixels, pixels + rightShift, kWaterfallIdleColor);
                }
            }
            m_imageStartMhz = newStartMhz;
            m_imageEndMhz = newEndMhz;
            emit imageChanged();
            return;
        }
    }
    if (m_remapScratch.size() != newSize || m_remapScratch.format() != QImage::Format_RGB32)
    {
        m_remapScratch = QImage(newSize, QImage::Format_RGB32);
    }
    m_remapScratch.fill(kWaterfallIdleColor);

    const int retainedRows = qMin(oldHeight, newSize.height());
    const int newRight = qMax(1, newSize.width() - 1);
    for (int displayRow = 0; displayRow < retainedRows; ++displayRow)
    {
        const int oldPhysicalRow = (m_firstVisibleRow + displayRow) % oldHeight;
        const auto* oldPixels = reinterpret_cast<const QRgb*>(m_waterfall.constScanLine(oldPhysicalRow));
        auto* newPixels = reinterpret_cast<QRgb*>(m_remapScratch.scanLine(displayRow));
        for (int x = 0; x < newSize.width(); ++x)
        {
            const double frequencyMhz = newStartMhz + (double(x) / newRight) * (newEndMhz - newStartMhz);
            if (frequencyMhz < m_imageStartMhz || frequencyMhz > m_imageEndMhz || oldSpanMhz <= 0.0)
            {
                continue;
            }
            const int oldX = qBound(
                0, int(std::llround(((frequencyMhz - m_imageStartMhz) / oldSpanMhz) * (oldWidth - 1))), oldWidth - 1);
            newPixels[x] = oldPixels[oldX];
        }
    }

    m_waterfall.swap(m_remapScratch);
    m_firstVisibleRow = 0;
    m_imageStartMhz = newStartMhz;
    m_imageEndMhz = newEndMhz;
    emit imageChanged();
}

void WaterfallController::renderRow(const QVector<float>& levels)
{
    if (m_paused || m_waterfall.isNull() || m_waterfall.height() == 0)
    {
        return;
    }

    const int w = m_waterfall.width();
    const int h = m_waterfall.height();
    Q_ASSERT(m_waterfall.format() == QImage::Format_RGB32);
    m_firstVisibleRow = (m_firstVisibleRow + h - 1) % h;
    QRgb* row = reinterpret_cast<QRgb*>(m_waterfall.scanLine(m_firstVisibleRow));
    if (levels.isEmpty())
    {
        for (int x = 0; x < w; ++x)
        {
            row[x] = kWaterfallIdleColor;
        }
        emit rowRendered(m_firstVisibleRow, m_firstVisibleRow);
        return;
    }

    if (logWaterfall().isDebugEnabled())
    {
        static QElapsedTimer waterfallLogTimer;
        if (!waterfallLogTimer.isValid() || waterfallLogTimer.elapsed() >= 1000)
        {
            float minLevel = std::numeric_limits<float>::max();
            float maxLevel = std::numeric_limits<float>::lowest();
            double totalLevel = 0.0;
            int zeroCount = 0;
            for (const float level : levels)
            {
                minLevel = std::min(minLevel, level);
                maxLevel = std::max(maxLevel, level);
                totalLevel += level;
                if (level <= m_minLevel)
                {
                    ++zeroCount;
                }
            }
            qDebug(logWaterfall()).noquote().nospace()
                << "Waterfall row: image=" << w << "x" << h << " bins=" << levels.size() << " levels[min=" << minLevel
                << " max=" << maxLevel << " avg=" << (totalLevel / double(levels.size())) << " floor=" << zeroCount
                << "/" << levels.size() << "]";
            waterfallLogTimer.restart();
        }
    }

    ensureBinMap(levels.size());
    for (int x = 0; x < w; ++x)
    {
        const int bin = m_binMap[x];
        row[x] = bin >= 0 ? levelToColor(levels[bin]) : kWaterfallIdleColor;
    }
    emit rowRendered(m_firstVisibleRow, m_firstVisibleRow);
}
