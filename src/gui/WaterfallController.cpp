#include "WaterfallController.h"
#include "LogCategories.h"

#include <QElapsedTimer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace
{
constexpr int kWaterfallRenderIntervalMs = 33;
constexpr double kMinFrequencyRangeMhz = 0.001;
const QRgb kWaterfallIdleColor = qRgb(0x02, 0x0c, 0x14);

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

WaterfallController::WaterfallController(QObject* parent) : QObject(parent)
{
    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kWaterfallRenderIntervalMs);
    connect(m_renderTimer, &QTimer::timeout, this, &WaterfallController::renderPendingRow);
}

void WaterfallController::setCanvasSize(const QSize& size)
{
    if (!size.isValid() || size == m_canvasSize)
    {
        return;
    }
    m_canvasSize = size;
    rebuildImage();
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
    static const struct
    {
        float pos;
        int r, g, b;
    } stops[] = {
        {0.00f, 0, 20, 120},  {0.18f, 0, 58, 205},  {0.34f, 0, 150, 255}, {0.50f, 0, 220, 105},
        {0.66f, 165, 245, 0}, {0.78f, 255, 230, 0}, {0.90f, 255, 92, 0},  {1.00f, 255, 255, 210},
    };
    static constexpr int N = static_cast<int>(std::size(stops));

    float t = (level - m_minLevel) / (m_maxLevel - m_minLevel);
    t = std::max(0.0f, std::min(1.0f, t));

    for (int i = 1; i < N; ++i)
    {
        if (t <= stops[i].pos)
        {
            const float f = (t - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos);
            const int r = int(stops[i - 1].r + f * (stops[i].r - stops[i - 1].r));
            const int g = int(stops[i - 1].g + f * (stops[i].g - stops[i - 1].g));
            const int b = int(stops[i - 1].b + f * (stops[i].b - stops[i - 1].b));
            return qRgb(r, g, b);
        }
    }
    Q_UNREACHABLE();
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
    rebuildImage();
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

    // The waterfall intentionally stores only the newest frame between render
    // ticks. Reuse the backing vector so the handoff from the model's
    // signal-owned frame does not allocate on every scope update.
    m_pendingLevels.resize(levels.size());
    std::copy(levels.cbegin(), levels.cend(), m_pendingLevels.begin());
    m_hasPendingLevels = true;
    scheduleRender();
}

void WaterfallController::clearDisplay()
{
    m_pendingLevels.clear();
    m_hasPendingLevels = false;
    if (m_renderTimer)
    {
        m_renderTimer->stop();
    }
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
    emit imageChanged();
}

void WaterfallController::scheduleRender()
{
    if (m_renderTimer && !m_renderTimer->isActive())
    {
        m_renderTimer->start();
    }
}

void WaterfallController::renderPendingRow()
{
    if (!m_hasPendingLevels || m_paused || m_waterfall.isNull() || m_waterfall.height() == 0)
    {
        return;
    }

    m_hasPendingLevels = false;
    const QVector<float> levels = std::move(m_pendingLevels);

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

    for (int x = 0; x < w; ++x)
    {
        const int bin = binForDisplayX(x, levels.size());
        row[x] = bin >= 0 ? levelToColor(levels[bin]) : kWaterfallIdleColor;
    }
    emit rowRendered(m_firstVisibleRow, m_firstVisibleRow);
}
