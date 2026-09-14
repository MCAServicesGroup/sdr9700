#include "MetersDialog.h"
#include "SMeterScale.h"
#include "SettingsPanelStyle.h"
#include "UiTheme.h"

#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr int kMeterScale = 1000;
constexpr int kSMeterMax = 255;
constexpr int kS9MeterValue = 146;
constexpr double kPowerMeterMaxWatts = 120.0;
constexpr double kSwrMeterMin = 1.0;
constexpr double kSwrMeterMax = 6.0;
constexpr double kAlcMeterMax = 2.0;
constexpr double kCompressionMeterMaxDb = 25.5;
constexpr double kVoltageMeterMax = 16.0;
constexpr double kCurrentMeterMax = 20.0;
constexpr int kAudioMax = 255;

QString meterStyle(const QString& fill)
{
    return QStringLiteral("QProgressBar {"
                          "  background: %1; border: 1px solid %2; border-radius: 3px;"
                          "}"
                          "QProgressBar::chunk {"
                          "  background: %3;"
                          "  border-radius: 2px;"
                          "}")
        .arg(QLatin1String(UiTheme::Color::MeterTrough), QLatin1String(UiTheme::Color::BorderMedium), fill);
}

QString standardMeterFill()
{
    return QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                          " stop:0 %1, stop:1 %2)")
        .arg(QLatin1String(UiTheme::Color::ControlActive), QLatin1String(UiTheme::Color::ScrollHandleHover));
}

QGridLayout* createMeterSection(QVBoxLayout* parentLayout, const QString& title, const QString& objectName)
{
    auto* group = new QGroupBox(title);
    group->setObjectName(objectName);
    group->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* grid = new QGridLayout(group);
    grid->setContentsMargins(10, 8, 10, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(7);
    grid->setColumnStretch(1, 1);
    parentLayout->addWidget(group);
    return grid;
}

int scaledValue(double value, double maximum)
{
    if (maximum <= 0.0)
    {
        return 0;
    }
    return qBound(0, qRound(value / maximum * kMeterScale), kMeterScale);
}

QString sMeterText(int value)
{
    const int bounded = qBound(0, value, kSMeterMax);
    if (bounded <= kS9MeterValue)
    {
        const int sUnits = qBound(0, qRound(static_cast<double>(bounded) / kS9MeterValue * 9.0), 9);
        return QStringLiteral("S%1").arg(sUnits);
    }

    const int plusDb = qBound(
        0,
        qRound(static_cast<double>(bounded - kS9MeterValue) / static_cast<double>(kSMeterMax - kS9MeterValue) * 60.0),
        60);
    return QStringLiteral("S9+%1").arg(plusDb);
}

} // namespace

MetersDialog::MetersDialog(QWidget* parent) : sdr9700::ui::UtilityWindow(QStringLiteral("Meters"), parent)
{
    setMinimumWidth(500);
    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(QStringLiteral("Meters"), this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QWidget::close);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(UiTheme::Size::DialogContentMargin, 6, UiTheme::Size::DialogContentMargin,
                                      UiTheme::Size::DialogContentMargin);
    contentLayout->setSpacing(6);
    root->addWidget(content);

    auto* audioGrid = createMeterSection(contentLayout, QStringLiteral("Audio"), QStringLiteral("audioMeters"));
    m_txAudioAverageMeter =
        addMeterRow(audioGrid, 0, QStringLiteral("Microphone Average"),
                    QStringLiteral("Local microphone input average level, including before PTT; high at 60% or above"));
    m_txAudioPeakMeter = addMeterRow(
        audioGrid, 1, QStringLiteral("Microphone Peak"),
        QStringLiteral("Local microphone input peak level, including before PTT; high at 85%, clipping at 95%"));
    m_compressionMeter =
        addMeterRow(audioGrid, 2, QStringLiteral("Compression"), QStringLiteral("Transmit compression"));

    auto* radioGrid = createMeterSection(contentLayout, QStringLiteral("Radio"), QStringLiteral("radioMeters"));
    m_currentMeter =
        addMeterRow(radioGrid, 0, QStringLiteral("Current"), QStringLiteral("Final amplifier drain current (Id)"));
    m_voltageMeter =
        addMeterRow(radioGrid, 1, QStringLiteral("Voltage"), QStringLiteral("Final amplifier drain voltage (Vd)"));

    auto* receiveGrid = createMeterSection(contentLayout, QStringLiteral("Receive"), QStringLiteral("receiveMeters"));
    m_sMeter = addMeterRow(receiveGrid, 0, QStringLiteral("S-Meter"), QStringLiteral("Receive signal strength"));

    auto* transmitGrid =
        createMeterSection(contentLayout, QStringLiteral("Transmit"), QStringLiteral("transmitMeters"));
    m_alcMeter = addMeterRow(transmitGrid, 0, QStringLiteral("ALC"), QStringLiteral("Automatic level control"));
    m_powerMeter = addMeterRow(transmitGrid, 1, QStringLiteral("RF Power"), QStringLiteral("Transmit output power"));
    m_swrMeter = addMeterRow(transmitGrid, 2, QStringLiteral("SWR"), QStringLiteral("Standing wave ratio"));

    resetMeters();
    setMinimumHeight(sizeHint().height());
    resize(500, sizeHint().height());
}

MetersDialog::MeterRow MetersDialog::addMeterRow(QGridLayout* layout, int row, const QString& label,
                                                 const QString& description)
{
    auto* labelWidget = new QLabel(label, this);
    labelWidget->setFixedWidth(108);
    labelWidget->setTextFormat(Qt::RichText);
    labelWidget->setToolTip(description);
    labelWidget->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(UiTheme::Color::TextMuted));

    auto* bar = new QProgressBar(this);
    bar->setAccessibleName(label);
    bar->setAccessibleDescription(description);
    bar->setRange(0, kMeterScale);
    bar->setTextVisible(false);
    bar->setFixedHeight(14);
    bar->setToolTip(description);
    const QString initialFill = standardMeterFill();
    bar->setStyleSheet(meterStyle(initialFill));

    auto* valueLabel = new QLabel(QStringLiteral("--"), this);
    valueLabel->setFixedWidth(68);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setToolTip(description);
    valueLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    valueLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(QLatin1String(UiTheme::Color::TextBright)));

    layout->addWidget(labelWidget, row, 0);
    layout->addWidget(bar, row, 1);
    layout->addWidget(valueLabel, row, 2);

    return MeterRow{bar, valueLabel, initialFill};
}

void MetersDialog::setMeterRow(const MeterRow& row, int value, const QString& text)
{
    if (row.bar)
    {
        row.bar->setValue(qBound(0, value, kMeterScale));
    }
    if (row.valueLabel)
    {
        row.valueLabel->setText(text);
    }
}

void MetersDialog::setMeterFillColor(MeterRow& row, const char* color)
{
    if (!row.bar)
    {
        return;
    }

    const QString fillColor = QString::fromLatin1(color);
    if (row.fillColor == fillColor)
    {
        return;
    }

    row.fillColor = fillColor;
    row.bar->setStyleSheet(meterStyle(fillColor));
}

void MetersDialog::resetMeters()
{
    setMeterRow(m_sMeter, 0, QStringLiteral("--"));
    setMeterRow(m_powerMeter, 0, QStringLiteral("-- W"));
    setMeterRow(m_swrMeter, 0, QStringLiteral("--"));
    setMeterRow(m_alcMeter, 0, QStringLiteral("--"));
    setMeterRow(m_compressionMeter, 0, QStringLiteral("-- dB"));
    setMeterRow(m_voltageMeter, 0, QStringLiteral("-- V"));
    setMeterRow(m_currentMeter, 0, QStringLiteral("-- A"));
    setMeterRow(m_txAudioAverageMeter, 0, QStringLiteral("--"));
    setMeterRow(m_txAudioPeakMeter, 0, QStringLiteral("--"));
    setMeterFillColor(m_txAudioAverageMeter, UiTheme::Color::TextStatusLabel);
    setMeterFillColor(m_txAudioPeakMeter, UiTheme::Color::TextStatusLabel);
}

void MetersDialog::setSMeter(int value)
{
    const int displayValue = sdr9700::sMeterDisplayValue(value);
    setMeterRow(m_sMeter, scaledValue(displayValue, kSMeterMax), sMeterText(displayValue));
}

void MetersDialog::setPowerMeter(double watts)
{
    const double bounded = qBound(0.0, watts, kPowerMeterMaxWatts);
    setMeterRow(m_powerMeter, scaledValue(bounded, kPowerMeterMaxWatts),
                QStringLiteral("%1 W").arg(bounded, 0, 'f', 1));
}

void MetersDialog::clearPowerMeter()
{
    setMeterRow(m_powerMeter, 0, QStringLiteral("-- W"));
}

void MetersDialog::setSwr(double swr)
{
    const double bounded = qBound(kSwrMeterMin, swr, kSwrMeterMax);
    setMeterRow(m_swrMeter, scaledValue(bounded - kSwrMeterMin, kSwrMeterMax - kSwrMeterMin),
                QStringLiteral("%1").arg(bounded, 0, 'f', 2));
}

void MetersDialog::clearSwr()
{
    setMeterRow(m_swrMeter, 0, QStringLiteral("--"));
}

void MetersDialog::setAlc(double alc)
{
    const double bounded = qBound(0.0, alc, kAlcMeterMax);
    setMeterRow(m_alcMeter, scaledValue(bounded, kAlcMeterMax), QStringLiteral("%1").arg(bounded, 0, 'f', 2));
}

void MetersDialog::clearAlc()
{
    setMeterRow(m_alcMeter, 0, QStringLiteral("--"));
}

void MetersDialog::setCompressionMeter(double db)
{
    const double bounded = qBound(0.0, db, kCompressionMeterMaxDb);
    setMeterRow(m_compressionMeter, scaledValue(bounded, kCompressionMeterMaxDb),
                QStringLiteral("%1 dB").arg(bounded, 0, 'f', 1));
}

void MetersDialog::clearCompressionMeter()
{
    setMeterRow(m_compressionMeter, 0, QStringLiteral("-- dB"));
}

void MetersDialog::setVoltageMeter(double volts)
{
    const double bounded = qBound(0.0, volts, kVoltageMeterMax);
    setMeterRow(m_voltageMeter, scaledValue(bounded, kVoltageMeterMax), QStringLiteral("%1 V").arg(bounded, 0, 'f', 1));
}

void MetersDialog::clearVoltageMeter()
{
    setMeterRow(m_voltageMeter, 0, QStringLiteral("-- V"));
}

void MetersDialog::setCurrentMeter(double amps)
{
    const double bounded = qBound(0.0, amps, kCurrentMeterMax);
    setMeterRow(m_currentMeter, scaledValue(bounded, kCurrentMeterMax), QStringLiteral("%1 A").arg(bounded, 0, 'f', 1));
}

void MetersDialog::clearCurrentMeter()
{
    setMeterRow(m_currentMeter, 0, QStringLiteral("-- A"));
}

void MetersDialog::setTransmitAudioLevel(int peak, int rms)
{
    const int peakPct = qBound(0, qRound(qBound(0, peak, kAudioMax) * 100.0 / kAudioMax), 100);
    const int averagePct = qBound(0, qRound(qBound(0, rms, kAudioMax) * 100.0 / kAudioMax), 100);
    const bool inactive = peakPct == 0 && averagePct == 0;

    setMeterRow(m_txAudioAverageMeter, scaledValue(averagePct, 100),
                inactive ? QStringLiteral("--") : QStringLiteral("%1%").arg(averagePct));
    setMeterRow(m_txAudioPeakMeter, scaledValue(peakPct, 100),
                inactive ? QStringLiteral("--") : QStringLiteral("%1%").arg(peakPct));

    const char* averageColor = UiTheme::Color::TextStatusLabel;
    if (averagePct >= 60)
    {
        averageColor = UiTheme::Color::Warning;
    }
    else if (averagePct >= 5)
    {
        averageColor = UiTheme::Color::Success;
    }

    const char* peakColor = UiTheme::Color::TextStatusLabel;
    if (peakPct >= 95)
    {
        peakColor = UiTheme::Color::Danger;
    }
    else if (peakPct >= 85)
    {
        peakColor = UiTheme::Color::Warning;
    }
    else if (peakPct > 0)
    {
        peakColor = UiTheme::Color::Success;
    }

    setMeterFillColor(m_txAudioAverageMeter, averageColor);
    setMeterFillColor(m_txAudioPeakMeter, peakColor);
}
