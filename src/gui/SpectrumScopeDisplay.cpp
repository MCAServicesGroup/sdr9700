#include "SpectrumScopeDisplay.h"
#include "SpectrumScopeCanvas.h"
#include "UiTheme.h"
#include "WaterfallCanvas.h"
#include "WaterfallController.h"

#include <QComboBox>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int kSpanComboWidth = 94;
constexpr int kSpanComboHeight = 24;
constexpr int kSpanComboMargin = 8;
constexpr qint64 kPanScrollUnitHz = 100;
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

int scrollUnitForMhz(double mhz)
{
    const qint64 unit = static_cast<qint64>(std::llround(mhz * 1e6 / kPanScrollUnitHz));
    return static_cast<int>(std::clamp<qint64>(unit, 0, std::numeric_limits<int>::max()));
}

int scrollUnitsForMhzDelta(double mhz)
{
    if (!std::isfinite(mhz) || mhz <= 0.0)
    {
        return 1;
    }

    const qint64 units = static_cast<qint64>(std::llround(mhz * 1e6 / kPanScrollUnitHz));
    return static_cast<int>(std::clamp<qint64>(units, 1, std::numeric_limits<int>::max()));
}

double mhzForScrollUnit(int unit)
{
    return (static_cast<double>(unit) * kPanScrollUnitHz) / 1e6;
}
} // namespace

SpectrumScopeDisplay::SpectrumScopeDisplay(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 320);

    m_spectrumScopeCanvas = new SpectrumScopeCanvas(this);
    m_panScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_waterfallCanvas = new WaterfallCanvas(this);
    m_waterfallController = new WaterfallController(this);
    m_spanCombo = new QComboBox(this);

    m_panScrollBar->setFixedHeight(panScrollBarHeight());
    m_panScrollBar->setTracking(true);
    m_panScrollBar->setEnabled(false);
    m_panScrollBar->setToolTip(QStringLiteral("Pan Spectrum Scope"));
    m_panScrollBar->setAccessibleName(QStringLiteral("Spectrum Scope pan"));
    m_panScrollBar->setStyleSheet(
        QStringLiteral(
            "QScrollBar:horizontal { background: #061116; border-top: 1px solid #0d2630; border-bottom: 0; "
            "height: 16px; margin: 0px; }"
            "QScrollBar::groove:horizontal { background: #061116; border: 1px solid #173542; border-radius: 4px; }"
            "QScrollBar::handle:horizontal { background: %1; border: 1px solid %2; border-radius: 4px; "
            "min-width: 34px; margin: 2px 1px; }"
            "QScrollBar::handle:horizontal:hover { background: %3; border-color: %4; }"
            "QScrollBar::handle:horizontal:pressed { background: %5; border-color: %6; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { background: transparent; border: none; "
            "width: 0px; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
            "QScrollBar:disabled { background: #061116; }"
            "QScrollBar::groove:horizontal:disabled { background: #061116; border-color: #173542; }"
            "QScrollBar::handle:horizontal:disabled { background: transparent; border: none; min-width: 0px; }"
            "QScrollBar::add-page:horizontal:disabled, QScrollBar::sub-page:horizontal:disabled { background: "
            "transparent; }")
            .arg(UiTheme::Color::ScrollHandle, UiTheme::Color::ControlActiveBorder, UiTheme::Color::ScrollHandleHover,
                 UiTheme::Color::ScrollBorderHover, UiTheme::Color::ScrollHandlePressed,
                 UiTheme::Color::ScrollBorderPressed));
    m_spanCombo->setFixedSize(kSpanComboWidth, kSpanComboHeight);
    m_spanCombo->setFocusPolicy(Qt::NoFocus);
    m_spanCombo->setToolTip(QStringLiteral("Spectrum Scope span"));
    m_spanCombo->setStyleSheet(
        QStringLiteral("QComboBox { background: rgba(16, 22, 30, 220); "
                       "border: 1px solid #566576; border-radius: 3px; "
                       "color: #e8f2f8; font-size: 10px; font-weight: bold; "
                       "padding: 1px 22px 1px 10px; }"
                       "QComboBox:hover { background: rgba(32, 42, 55, 235); "
                       "border-color: #7f96ad; }"
                       "QComboBox::drop-down { border: none; width: 16px; }"
                       "QComboBox::down-arrow { image: none; width: 0px; height: 0px; "
                       "border-left: 4px solid transparent; border-right: 4px solid transparent; "
                       "border-top: 5px solid #e8f2f8; margin-right: 5px; }"
                       "QComboBox QAbstractItemView { background: #10161e; "
                       "border: 1px solid #566576; color: #e8f2f8; "
                       "selection-background-color: #2a82da; }"
                       "QComboBox QAbstractItemView::item { padding: 4px 10px; }"));
    m_spanCombo->raise();

    connect(m_spectrumScopeCanvas, &SpectrumScopeCanvas::frequencyClicked, this,
            &SpectrumScopeDisplay::frequencyClicked);
    connect(m_spectrumScopeCanvas, &SpectrumScopeCanvas::wheelStepRequested, this,
            &SpectrumScopeDisplay::wheelStepRequested);
    m_waterfallCanvas->setWaterfallImageSource(&m_waterfallController->image());
    connect(m_waterfallController, &WaterfallController::imageChanged, m_waterfallCanvas,
            [this]() { m_waterfallCanvas->setWaterfallImageSource(&m_waterfallController->image()); });
    connect(m_waterfallController, &WaterfallController::rowRendered, m_waterfallCanvas,
            &WaterfallCanvas::setWaterfallRow);
    connect(m_panScrollBar, &QScrollBar::sliderPressed, m_waterfallController,
            [this]() { m_waterfallController->setPaused(true); });
    connect(m_panScrollBar, &QScrollBar::sliderReleased, m_waterfallController,
            [this]() { m_waterfallController->setPaused(false); });
    connect(m_panScrollBar, &QScrollBar::valueChanged, this,
            [this](int value)
            {
                if (!m_hasPanRange || m_interactionLocked || !m_panScrollBar->isEnabled())
                {
                    return;
                }
                Q_EMIT panCenterRequested(mhzForScrollUnit(value));
            });
    connect(m_spanCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (index < 0)
                {
                    return;
                }
                Q_EMIT spanSelected(m_spanCombo->itemData(index).toULongLong());
            });
}

void SpectrumScopeDisplay::setSpanChoices(const QVector<SpanChoice>& choices)
{
    const quint64 previousHz = m_spanCombo->currentData().toULongLong();
    {
        const QSignalBlocker blocker(m_spanCombo);
        m_spanCombo->clear();
        for (const SpanChoice& choice : choices)
        {
            m_spanCombo->addItem(choice.label, QVariant::fromValue<qulonglong>(choice.hz));
        }
    }
    setCurrentSpanHz(previousHz);
    m_spanCombo->setVisible(!choices.isEmpty());
    updateSpanComboGeometry();
}

void SpectrumScopeDisplay::setCurrentSpanHz(quint64 hz)
{
    const QSignalBlocker blocker(m_spanCombo);
    const int index = m_spanCombo->findData(QVariant::fromValue<qulonglong>(hz));
    if (index >= 0)
    {
        m_spanCombo->setCurrentIndex(index);
    }
}

void SpectrumScopeDisplay::updateSpanComboGeometry()
{
    if (!m_spanCombo || m_spanCombo->parentWidget() != this)
    {
        return;
    }
    const int x = qMax(kSpanComboMargin, width() - kSpanComboMargin - m_spanCombo->width());
    m_spanCombo->move(x, kSpanComboMargin);
    m_spanCombo->raise();
}

void SpectrumScopeDisplay::updatePanScrollBar()
{
    if (!m_panScrollBar)
    {
        return;
    }

    double visibleStartMhz = m_visibleStartMhz;
    double visibleEndMhz = m_visibleEndMhz;
    double panStartMhz = m_panStartMhz;
    double panEndMhz = m_panEndMhz;
    const bool validRange = normalizeFrequencyRange(&visibleStartMhz, &visibleEndMhz) && m_hasPanRange &&
                            normalizeFrequencyRange(&panStartMhz, &panEndMhz);
    if (!validRange)
    {
        const QSignalBlocker blocker(m_panScrollBar);
        m_panScrollBar->setRange(0, 100);
        m_panScrollBar->setPageStep(20);
        m_panScrollBar->setSingleStep(1);
        m_panScrollBar->setValue(50);
        m_panScrollBar->setEnabled(false);
        return;
    }

    const double bandwidthMhz = visibleEndMhz - visibleStartMhz;
    const double halfBandwidthMhz = bandwidthMhz / 2.0;
    const double minCenterMhz = panStartMhz + halfBandwidthMhz;
    const double maxCenterMhz = panEndMhz - halfBandwidthMhz;
    const bool canPan = maxCenterMhz > minCenterMhz;
    const double centerMhz = qBound(qMin(minCenterMhz, maxCenterMhz), (visibleStartMhz + visibleEndMhz) / 2.0,
                                    qMax(minCenterMhz, maxCenterMhz));

    const QSignalBlocker blocker(m_panScrollBar);
    m_panScrollBar->setRange(scrollUnitForMhz(qMin(minCenterMhz, maxCenterMhz)),
                             scrollUnitForMhz(qMax(minCenterMhz, maxCenterMhz)));
    const int pageStep = scrollUnitsForMhzDelta(bandwidthMhz);
    m_panScrollBar->setPageStep(pageStep);
    m_panScrollBar->setSingleStep(qMax(1, pageStep / 100));
    m_panScrollBar->setValue(scrollUnitForMhz(centerMhz));
    m_panScrollBar->setEnabled(!m_interactionLocked && canPan);
}

void SpectrumScopeDisplay::updateChildGeometry()
{
    if (!m_spectrumScopeCanvas || !m_panScrollBar || !m_waterfallCanvas)
    {
        return;
    }

    const int availablePlotHeight = qMax(0, height() - SpectrumScopeCanvas::scaleHeight() - panScrollBarHeight());
    const int spectrumHeight = availablePlotHeight / 2;
    const int spectrumScopeHeight = spectrumHeight + SpectrumScopeCanvas::scaleHeight();
    const int splitTop = spectrumScopeHeight;
    const int waterfallTop = splitTop + panScrollBarHeight();
    const int waterfallHeight = qMax(0, height() - waterfallTop);
    constexpr int plotLeft = 0;

    m_spectrumScopeCanvas->setGeometry(0, 0, width(), spectrumScopeHeight);
    m_panScrollBar->setGeometry(plotLeft, splitTop, qMax(0, width() - plotLeft), panScrollBarHeight());
    m_waterfallCanvas->setGeometry(plotLeft, waterfallTop, qMax(0, width() - plotLeft), waterfallHeight);
    m_waterfallController->setCanvasSize(m_waterfallCanvas->size());
    updateSpanComboGeometry();
}

void SpectrumScopeDisplay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

void SpectrumScopeDisplay::setFrequencyRange(double startMhz, double endMhz)
{
    m_visibleStartMhz = startMhz;
    m_visibleEndMhz = endMhz;
    m_spectrumScopeCanvas->setFrequencyRange(startMhz, endMhz);
    m_waterfallController->setFrequencyRange(startMhz, endMhz);
    updatePanScrollBar();
}

void SpectrumScopeDisplay::setFrequencyPanRange(double startMhz, double endMhz)
{
    if (!normalizeFrequencyRange(&startMhz, &endMhz))
    {
        clearFrequencyPanRange();
        return;
    }

    if (m_hasPanRange && m_panStartMhz == startMhz && m_panEndMhz == endMhz)
    {
        return;
    }

    m_panStartMhz = startMhz;
    m_panEndMhz = endMhz;
    m_hasPanRange = true;
    updatePanScrollBar();
}

void SpectrumScopeDisplay::clearFrequencyPanRange()
{
    if (!m_hasPanRange)
    {
        return;
    }

    m_hasPanRange = false;
    m_panStartMhz = 0.0;
    m_panEndMhz = 0.0;
    updatePanScrollBar();
}

void SpectrumScopeDisplay::setDataFrequencyRange(double startMhz, double endMhz)
{
    m_spectrumScopeCanvas->setDataFrequencyRange(startMhz, endMhz);
    m_waterfallController->setDataFrequencyRange(startMhz, endMhz);
}

void SpectrumScopeDisplay::setVfoFrequency(double freqMhz)
{
    m_spectrumScopeCanvas->setVfoFrequency(freqMhz);
}

void SpectrumScopeDisplay::setVfoMarkerColor(const QColor& color)
{
    m_spectrumScopeCanvas->setVfoMarkerColor(color);
}

void SpectrumScopeDisplay::setBackgroundColor(const QColor& color)
{
    m_spectrumScopeCanvas->setBackgroundColor(color);
}

void SpectrumScopeDisplay::setGridLineColor(const QColor& color)
{
    m_spectrumScopeCanvas->setGridLineColor(color);
}

void SpectrumScopeDisplay::setGridDensity(int density)
{
    m_spectrumScopeCanvas->setGridDensity(density);
}

void SpectrumScopeDisplay::setInteractionLocked(bool locked)
{
    m_interactionLocked = locked;
    m_spectrumScopeCanvas->setInteractionLocked(locked);
    updatePanScrollBar();
}

void SpectrumScopeDisplay::setInvertMouseWheel(bool invert)
{
    m_spectrumScopeCanvas->setInvertMouseWheel(invert);
}

void SpectrumScopeDisplay::updateSpectrum(const QVector<float>& levels, bool outOfRange)
{
    m_spectrumScopeCanvas->updateSpectrum(levels, outOfRange);
    m_waterfallController->updateSpectrum(levels);
}

void SpectrumScopeDisplay::clearDisplay()
{
    m_spectrumScopeCanvas->clearDisplay();
    m_waterfallController->clearDisplay();
}

void SpectrumScopeDisplay::setFilterWidth(int lowHz, int highHz)
{
    m_spectrumScopeCanvas->setFilterWidth(lowHz, highHz);
}

int SpectrumScopeDisplay::freqToX(double mhz) const
{
    return m_spectrumScopeCanvas->freqToX(mhz);
}

void SpectrumScopeDisplay::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    updateChildGeometry();
}
