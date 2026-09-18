#include "VfoSMeter.h"

#include "SMeterScale.h"
#include "UiTheme.h"

#include <QEvent>
#include <QPainter>
#include <cmath>

namespace
{
constexpr int kMeterHeight = 31;
constexpr int kReadoutWidth = 48;
constexpr int kReadoutGap = 8;
constexpr int kMeterEndInset = 20;
constexpr int kSegmentGap = 2;
constexpr int kSegmentWidth = 6;
constexpr int kSegmentHeight = 14;
constexpr int kScaleFontSize = 7;
constexpr int kReadoutFontSize = 12;
constexpr int kLegendRightInset = 4;
constexpr int kSignalAnimationIntervalMs = 16;
constexpr double kSignalAttackSeconds = 0.045;
constexpr double kSignalReleaseSeconds = 0.180;
constexpr double kSignalSnapRaw = 0.25;

struct MeterMark
{
    const char* label;
    int raw;
    bool overS9;
};

constexpr MeterMark kMarks[] = {{"1", 13, false},  {"3", 40, false},   {"5", 67, false},   {"7", 93, false},
                                {"9", 120, false}, {"+20", 160, true}, {"+40", 201, true}, {"+60", 241, true}};

struct PowerMark
{
    const char* label;
    double watts;
    double fraction;
};

constexpr PowerMark kPowerMarks[] = {{"1", 1.0, 0.060},    {"5", 5.0, 0.180},   {"10", 10.0, 0.235},
                                     {"25", 25.0, 0.400},  {"50", 50.0, 0.620}, {"75", 75.0, 0.820},
                                     {"100", 100.0, 1.000}};

double powerFractionOn100WScale(double watts)
{
    const double bounded = qBound(0.0, watts, 100.0);
    double previousWatts = 0.0;
    double previousFraction = 0.0;
    for (const PowerMark& mark : kPowerMarks)
    {
        if (bounded <= mark.watts)
        {
            return previousFraction +
                   ((bounded - previousWatts) / (mark.watts - previousWatts)) * (mark.fraction - previousFraction);
        }
        previousWatts = mark.watts;
        previousFraction = mark.fraction;
    }
    return 1.0;
}

double powerFraction(double watts, double maxWatts)
{
    const double boundedMax = qBound(0.1, maxWatts, 100.0);
    return qBound(0.0, powerFractionOn100WScale(qBound(0.0, watts, boundedMax)) / powerFractionOn100WScale(boundedMax),
                  1.0);
}

QString signalText(int rawValue)
{
    const int bounded = qBound(0, rawValue, 255);
    if (bounded <= 120)
    {
        return QStringLiteral("S%1").arg(qBound(0, qRound(bounded * 9.0 / 120.0), 9));
    }

    // Icom defines raw 0241 as S9+60. Preserve values through 0255 in
    // the model, but do not extrapolate the presentation beyond full scale.
    const int plusDb = qRound(qMin(bounded - 120, 121) * 60.0 / 121.0);
    return QStringLiteral("S9+%1").arg(plusDb, 2, 10, QLatin1Char('0'));
}
} // namespace

VfoSMeter::VfoSMeter(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kMeterHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("Signal strength meter"));
    refreshPaintFonts();

    m_signalAnimationTimer.setTimerType(Qt::PreciseTimer);
    m_signalAnimationTimer.setInterval(kSignalAnimationIntervalMs);
    connect(&m_signalAnimationTimer, &QTimer::timeout, this, &VfoSMeter::advanceSignalDisplay);
}

void VfoSMeter::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange)
    {
        refreshPaintFonts();
    }
    QWidget::changeEvent(event);
}

void VfoSMeter::refreshPaintFonts()
{
    m_scaleFont = font();
    m_scaleFont.setPixelSize(kScaleFontSize);
    m_scaleFont.setBold(true);
    m_scaleMetrics = QFontMetrics(m_scaleFont);
    m_readoutFont = font();
    m_readoutFont.setPixelSize(kReadoutFontSize);
    m_readoutFont.setBold(true);
}

void VfoSMeter::setRawValue(int value)
{
    const int bounded = qBound(0, value, 255);
    if (m_rawValue == bounded)
    {
        return;
    }
    m_rawValue = bounded;
    setAccessibleDescription(QStringLiteral("Signal strength %1").arg(signalText(m_rawValue)));
    if (!m_signalAnimationTimer.isActive())
    {
        m_signalAnimationElapsed.restart();
        m_signalAnimationTimer.start();
    }
}

void VfoSMeter::advanceSignalDisplay()
{
    const qint64 elapsedMs = m_signalAnimationElapsed.restart();
    if (elapsedMs <= 0)
    {
        return;
    }

    const double delta = double(m_rawValue) - m_displayRawValue;
    if (qAbs(delta) <= kSignalSnapRaw)
    {
        m_displayRawValue = m_rawValue;
        m_signalAnimationTimer.stop();
        update();
        return;
    }

    const double timeConstant = delta >= 0.0 ? kSignalAttackSeconds : kSignalReleaseSeconds;
    const double elapsedSeconds = double(elapsedMs) / 1000.0;
    const double alpha = 1.0 - std::exp(-elapsedSeconds / timeConstant);
    m_displayRawValue += delta * alpha;
    update();
}

void VfoSMeter::setTransmitPowerMode(bool enabled)
{
    if (m_transmitPowerMode == enabled)
    {
        return;
    }
    m_transmitPowerMode = enabled;
    setAccessibleName(enabled ? QStringLiteral("RF power meter") : QStringLiteral("Signal strength meter"));
    if (enabled)
    {
        // A power-meter reply belongs to the transmission during which it was
        // sampled. Never carry the final reading from the previous transmission
        // into a newly confirmed PTT-on interval while the first fresh sample is
        // still in flight.
        m_powerWatts = 0.0;
        setAccessibleDescription(QStringLiteral("RF power 0.0 watts"));
    }
    else
    {
        setAccessibleDescription(QStringLiteral("Signal strength %1").arg(signalText(m_rawValue)));
    }
    update();
}

void VfoSMeter::setPowerWatts(double watts)
{
    const double bounded = qBound(0.0, watts, m_maxPowerWatts);
    if (qFuzzyCompare(m_powerWatts + 1.0, bounded + 1.0))
    {
        return;
    }
    m_powerWatts = bounded;
    if (m_transmitPowerMode)
    {
        setAccessibleDescription(QStringLiteral("RF power %1 watts").arg(m_powerWatts, 0, 'f', 1));
        update();
    }
}

void VfoSMeter::setMaxPowerWatts(double watts)
{
    const double bounded = qBound(0.1, watts, 100.0);
    if (qFuzzyCompare(m_maxPowerWatts + 1.0, bounded + 1.0))
    {
        return;
    }
    m_maxPowerWatts = bounded;
    m_powerWatts = qMin(m_powerWatts, m_maxPowerWatts);
    update();
}

void VfoSMeter::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int meterRight = qMax(0, width() - kReadoutWidth - kReadoutGap - kMeterEndInset);
    const QRect meterRect(0, height() - kSegmentHeight - 2, meterRight, kSegmentHeight);
    painter.fillRect(meterRect, QColor(4, 9, 13));

    painter.setFont(m_scaleFont);
    if (m_transmitPowerMode)
    {
        for (const PowerMark& mark : kPowerMarks)
        {
            if (mark.watts > m_maxPowerWatts || (mark.watts == 10.0 && m_maxPowerWatts > 10.0))
            {
                continue;
            }
            const int x = qRound(powerFraction(mark.watts, m_maxPowerWatts) * qMax(0, meterRect.width() - 1));
            painter.setPen(UiTheme::Color::TextStatusSecondaryQColor);
            const QString label = qFuzzyCompare(mark.watts + 1.0, m_maxPowerWatts + 1.0)
                                      ? QStringLiteral("%1W").arg(m_maxPowerWatts, 0, 'f',
                                                                  m_maxPowerWatts == qRound(m_maxPowerWatts) ? 0 : 2)
                                      : QString::fromLatin1(mark.label);
            const int labelWidth = m_scaleMetrics.horizontalAdvance(label);
            painter.drawText(qBound(0, x - labelWidth / 2, qMax(0, meterRect.width() - labelWidth - kLegendRightInset)),
                             meterRect.top() - 6, label);
        }
    }
    else
    {
        for (const MeterMark& mark : kMarks)
        {
            const int x = qRound(mark.raw / 241.0 * qMax(0, meterRect.width() - 1));
            painter.setPen(mark.overS9 ? UiTheme::Color::DangerQColor : UiTheme::Color::TextStatusSecondaryQColor);
            const QString label = QString::fromLatin1(mark.label);
            const int labelWidth = m_scaleMetrics.horizontalAdvance(label);
            painter.drawText(qBound(0, x - labelWidth / 2, qMax(0, meterRect.width() - labelWidth - kLegendRightInset)),
                             meterRect.top() - 6, label);
        }
    }

    const double meterFraction =
        m_transmitPowerMode ? powerFraction(m_powerWatts, m_maxPowerWatts) : qMin(m_displayRawValue, 241.0) / 241.0;
    const int activeWidth = qRound(meterFraction * meterRect.width());
    for (int x = meterRect.left(); x + kSegmentWidth <= meterRect.right() + 1; x += kSegmentWidth + kSegmentGap)
    {
        const bool active = x - meterRect.left() < activeWidth;
        const bool highPower = m_transmitPowerMode && x - meterRect.left() >= meterRect.width() * 0.82;
        QColor segmentColor = UiTheme::Color::BorderLightQColor;
        if (active && m_transmitPowerMode)
        {
            segmentColor = highPower ? UiTheme::Color::DangerQColor : UiTheme::Color::AccentBrightQColor;
        }
        else if (active)
        {
            const double segmentFraction = double(x - meterRect.left()) / qMax(1, meterRect.width() - 1);
            segmentColor = UiTheme::sMeterSignalColor(segmentFraction);
        }
        painter.fillRect(QRect(x, meterRect.top(), kSegmentWidth, meterRect.height()), segmentColor);
    }

    const QRect readoutRect(width() - kReadoutWidth - 10, meterRect.top(), kReadoutWidth, kSegmentHeight);
    painter.setFont(m_readoutFont);
    painter.setPen(UiTheme::Color::TextPrimaryQColor);
    const int displayedRawValue = qRound(m_displayRawValue);
    const QString readout = m_transmitPowerMode     ? QStringLiteral("%1W").arg(qRound(m_powerWatts))
                            : displayedRawValue > 0 ? signalText(displayedRawValue)
                                                    : QString();
    painter.drawText(readoutRect, Qt::AlignLeft | Qt::AlignVCenter, readout);
}
