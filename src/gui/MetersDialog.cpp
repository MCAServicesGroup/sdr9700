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

// Map a dBFS reading onto the meter bar. The scale is deliberately not extended
// above 0 dBFS: post-mix floats can exceed unity, and that overage is reported
// through the full-scale sample count instead of a longer bar.
int dbBarValue(double db)
{
    constexpr double kRangeDb = sdr9700::audio::kMeterDisplayCeilingDb - sdr9700::audio::kMeterDisplayFloorDb;
    const double bounded = qBound(sdr9700::audio::kMeterDisplayFloorDb, db, sdr9700::audio::kMeterDisplayCeilingDb);
    return qBound(0, qRound((bounded - sdr9700::audio::kMeterDisplayFloorDb) / kRangeDb * kMeterScale), kMeterScale);
}

const char* transmitAudioFill(sdr9700::audio::TxAudioMeterState state)
{
    switch (state)
    {
    case sdr9700::audio::TxAudioMeterState::FullScaleDetected:
        return UiTheme::Color::Danger;
    case sdr9700::audio::TxAudioMeterState::NearFullScale:
        return UiTheme::Color::Warning;
    case sdr9700::audio::TxAudioMeterState::RecommendedHeadroom:
        return UiTheme::Color::Success;
    case sdr9700::audio::TxAudioMeterState::SignalPresent:
        return UiTheme::Color::Accent;
    case sdr9700::audio::TxAudioMeterState::NoActivity:
    case sdr9700::audio::TxAudioMeterState::Invalid:
        break;
    }
    return UiTheme::Color::TextStatusLabel;
}

QString transmitAudioStateText(sdr9700::audio::TxAudioMeterState state)
{
    switch (state)
    {
    case sdr9700::audio::TxAudioMeterState::Invalid:
        return QStringLiteral("Unavailable");
    case sdr9700::audio::TxAudioMeterState::NoActivity:
        return QStringLiteral("No activity");
    case sdr9700::audio::TxAudioMeterState::SignalPresent:
        return QStringLiteral("Signal present");
    case sdr9700::audio::TxAudioMeterState::RecommendedHeadroom:
        return QStringLiteral("Recommended local headroom");
    case sdr9700::audio::TxAudioMeterState::NearFullScale:
        return QStringLiteral("Near full scale");
    case sdr9700::audio::TxAudioMeterState::FullScaleDetected:
        return QStringLiteral("Full-scale samples detected");
    }
    return QStringLiteral("Unavailable");
}

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
    auto* localInputLabel = new QLabel(QStringLiteral("Local processed input"), this);
    localInputLabel->setAccessibleName(QStringLiteral("Local processed input"));
    localInputLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(UiTheme::Color::TextBright));
    audioGrid->addWidget(localInputLabel, 0, 0, 1, 3);
    m_txAudioAverageMeter = addMeterRow(
        audioGrid, 1, QStringLiteral("Average"),
        QStringLiteral("Local processed input average level in dBFS, including before PTT. Recommended -24 to -12 "
                       "dBFS. This is a local recording level, not radio drive: use the ALC meter for transmit "
                       "drive on SSB."));
    m_txAudioPeakMeter = addMeterRow(
        audioGrid, 2, QStringLiteral("Peak"),
        QStringLiteral("Local processed input peak level in dBFS, including before PTT. Recommended -12 to -3 dBFS; "
                       "-1 dBFS and above is near full scale. This is a local recording level, not radio drive."));
    m_txAudioAverageMeter.bar->setAccessibleName(QStringLiteral("Local processed input average"));
    m_txAudioPeakMeter.bar->setAccessibleName(QStringLiteral("Local processed input peak"));
    m_txAudioStateLabel = new QLabel(this);
    m_txAudioStateLabel->setObjectName(QStringLiteral("txAudioState"));
    m_txAudioStateLabel->setAccessibleName(QStringLiteral("Local processed input state"));
    m_txAudioStateLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(UiTheme::Color::TextMuted));
    audioGrid->addWidget(m_txAudioStateLabel, 3, 1, 1, 2);
    m_compressionMeter =
        addMeterRow(audioGrid, 4, QStringLiteral("Compression"), QStringLiteral("Transmit compression"));

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
    setTransmitAudioMeter(sdr9700::audio::TxAudioMeterState::Invalid, sdr9700::audio::kMeterDisplayFloorDb,
                          sdr9700::audio::kMeterDisplayFloorDb, 0);
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

void MetersDialog::setTransmitAudioMeter(sdr9700::audio::TxAudioMeterState state, double rmsDb, double peakDb,
                                         quint32 fullScaleCount)
{
    const bool invalid = state == sdr9700::audio::TxAudioMeterState::Invalid;
    const bool silent = state == sdr9700::audio::TxAudioMeterState::NoActivity;

    // Invalid means no measurement exists yet: no sample, disconnect, input or
    // converter failure, or an audio-device restart. Digital silence is a real
    // measurement and reads at the floor instead.
    const QString averageText = invalid  ? QStringLiteral("--")
                                : silent ? QStringLiteral("No activity")
                                         : QStringLiteral("%1 dB").arg(rmsDb, 0, 'f', 1);
    QString peakText = invalid  ? QStringLiteral("--")
                       : silent ? QStringLiteral("No activity")
                                : QStringLiteral("%1 dB").arg(peakDb, 0, 'f', 1);
    if (fullScaleCount > 0)
    {
        peakText = QStringLiteral("CLIP %1").arg(fullScaleCount);
    }

    setMeterRow(m_txAudioAverageMeter, invalid ? 0 : dbBarValue(rmsDb), averageText);
    setMeterRow(m_txAudioPeakMeter, invalid ? 0 : dbBarValue(peakDb), peakText);
    if (m_txAudioStateLabel)
    {
        m_txAudioStateLabel->setText(transmitAudioStateText(state));
    }

    const char* const averageFill =
        (state == sdr9700::audio::TxAudioMeterState::RecommendedHeadroom &&
         rmsDb >= sdr9700::audio::kHeadroomRmsMinDb && rmsDb <= sdr9700::audio::kHeadroomRmsMaxDb)
            ? UiTheme::Color::Success
            : (invalid || silent ? UiTheme::Color::TextStatusLabel : UiTheme::Color::Accent);
    setMeterFillColor(m_txAudioAverageMeter, averageFill);
    setMeterFillColor(m_txAudioPeakMeter, transmitAudioFill(state));
}
