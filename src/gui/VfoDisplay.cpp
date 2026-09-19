#include "VfoDisplay.h"

#include "MainWindowHelpers.h"
#include "UiTheme.h"
#include "VfoSMeter.h"

#include <algorithm>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kDisplayHeight = 218;
constexpr int kDisplayHorizontalInset = 12;
constexpr int kDisplayVerticalInset = 12;
constexpr int kHeaderMeterSpacing = 20;
constexpr int kMeterFrequencySpacing = 0;
constexpr int kTitleFontPixelSize = 10;
constexpr int kFrequencyFontPixelSize = 32;
constexpr int kTransmitFrequencyFontPixelSize = 13;
constexpr int kTransmitFrequencyHeight = 16;
constexpr int kTransmitFrequencyRightInset = 6;
constexpr int kTxBadgeWidth = 40;
constexpr int kTxBadgeHeight = 18;
constexpr int kHeaderButtonHeight = 18;
constexpr int kIdentityButtonWidth = 38;
constexpr int kBandButtonWidth = 52;
constexpr int kModeButtonWidth = 52;
constexpr int kTxPowerButtonWidth = 68;
constexpr int kLanModButtonWidth = 68;
constexpr int kFrequencyEditTimeoutMs = 10000;
constexpr int kHeaderControlSpacing = 6;
constexpr int kReceiverControlHeight = 18;
constexpr int kReceiverControlSpacing = 6;
constexpr int kSecondaryControlWidth = 80;
constexpr int kXfcControlWidth = 52;
constexpr int kFrequencyGroupVerticalOffset = 7;
constexpr int kSecondaryToPrimaryControlSpacing = 40 - kFrequencyGroupVerticalOffset;

QString vfoTitle(Vfo vfo)
{
    return vfo == Vfo::Main ? QStringLiteral("MAIN VFO") : QStringLiteral("SUB VFO");
}

QString vfoName(Vfo vfo)
{
    return vfo == Vfo::Main ? QStringLiteral("MAIN") : QStringLiteral("SUB");
}

QString receiverControlStyle()
{
    return QStringLiteral("QPushButton { background: #080b0f; border: 1px solid %1; border-radius: 1px; color: %2; "
                          "font-size: 9px; font-weight: bold; padding: 0px 5px; } "
                          "QPushButton:hover { border-color: %3; color: %4; } "
                          "QPushButton[active=\"true\"] { background: %5; border-color: %3; color: %4; } "
                          "QPushButton[active=\"true\"]:hover { background: %6; border-color: %3; color: %4; } "
                          "QPushButton:disabled { background: #080b0f; border-color: %1; color: %2; }")
        .arg(UiTheme::Color::Border, UiTheme::Color::TextStatusSecondary, UiTheme::Color::ControlActiveBorder,
             UiTheme::Color::TextBright, UiTheme::Color::ControlActive, UiTheme::Color::ControlActiveHover);
}
} // namespace

VfoDisplay::VfoDisplay(Vfo vfo, QWidget* parent) : QWidget(parent), m_vfo(vfo)
{
    const QString title = vfoTitle(vfo);
    setObjectName(vfo == Vfo::Main ? QStringLiteral("mainVfoDisplay") : QStringLiteral("subVfoDisplay"));
    setAccessibleName(title);
    setFixedHeight(kDisplayHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kDisplayHorizontalInset, kDisplayVerticalInset, kDisplayHorizontalInset,
                               kDisplayVerticalInset);
    layout->setSpacing(0);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(kHeaderControlSpacing);

    if (vfo == Vfo::Main)
    {
        m_txBadge = new QLabel(QStringLiteral("TX"), this);
        m_txBadge->setObjectName(QStringLiteral("vfoTxBadge"));
        m_txBadge->setFixedSize(kTxBadgeWidth, kTxBadgeHeight);
        m_txBadge->setAlignment(Qt::AlignCenter);
        m_txBadge->setAccessibleName(QStringLiteral("%1 transmit indicator").arg(title));
        m_txBadge->setStyleSheet(
            QStringLiteral("QLabel#vfoTxBadge { background: #080b0f; border: 1px solid %1; border-radius: 0; "
                           "color: %2; font-size: %4px; font-weight: bold; } "
                           "QLabel#vfoTxBadge:enabled { border-color: %3; color: %3; } "
                           "QLabel#vfoTxBadge[transmitting=\"true\"]:enabled { background: %5; border-color: %3; "
                           "color: %6; }")
                .arg(UiTheme::Color::Border, UiTheme::Color::TextStatusLabel, UiTheme::Color::Danger)
                .arg(kTitleFontPixelSize)
                .arg(UiTheme::Color::PttActive)
                .arg(UiTheme::Color::TextBright));
    }

    m_identityButton = new QPushButton(vfoName(vfo), this);
    m_identityButton->setObjectName(QStringLiteral("vfoIdentityButton"));
    m_identityButton->setFixedSize(kIdentityButtonWidth, kHeaderButtonHeight);
    m_identityButton->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_identityButton->setFocusPolicy(Qt::NoFocus);
    m_identityButton->setAccessibleName(QStringLiteral("%1 indicator").arg(title));
    m_identityButton->setStyleSheet(receiverControlStyle());

    m_bandButton = new QPushButton(QStringLiteral("--"), this);
    m_bandButton->setObjectName(QStringLiteral("vfoBandButton"));
    m_bandButton->setFixedHeight(kHeaderButtonHeight);
    m_bandButton->setFixedWidth(kBandButtonWidth);
    m_bandButton->setCursor(Qt::PointingHandCursor);
    m_bandButton->setAccessibleName(QStringLiteral("%1 band").arg(title));
    m_bandButton->setStyleSheet(receiverControlStyle());
    connect(m_bandButton, &QPushButton::clicked, this, &VfoDisplay::bandClicked);

    m_modeButton = new QPushButton(QStringLiteral("--"), this);
    m_modeButton->setObjectName(QStringLiteral("vfoModeButton"));
    m_modeButton->setFixedSize(kModeButtonWidth, kHeaderButtonHeight);
    m_modeButton->setCursor(Qt::PointingHandCursor);
    m_modeButton->setAccessibleName(QStringLiteral("%1 mode").arg(title));
    m_modeButton->setStyleSheet(receiverControlStyle());
    connect(m_modeButton, &QPushButton::clicked, this, &VfoDisplay::modeClicked);

    auto createHeaderControl = [this, &title](const QString& control, int width)
    {
        auto* button = new QPushButton(control, this);
        button->setObjectName(QStringLiteral("vfo%1Button").arg(control).remove(QLatin1Char(' ')));
        button->setFixedSize(width, kHeaderButtonHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(QStringLiteral("%1 %2 control").arg(title, control));
        button->setStyleSheet(receiverControlStyle());
        connect(button, &QPushButton::clicked, this, [this, control]() { emit receiverControlClicked(control); });
        m_receiverControlButtons.insert(control, button);
        return button;
    };
    QPushButton* txPowerButton =
        vfo == Vfo::Main ? createHeaderControl(QStringLiteral("TX PWR"), kTxPowerButtonWidth) : nullptr;
    QPushButton* lanModButton =
        vfo == Vfo::Main ? createHeaderControl(QStringLiteral("MOD"), kLanModButtonWidth) : nullptr;
    if (txPowerButton)
    {
        txPowerButton->setText(QStringLiteral("PWR"));
    }
    auto createHeaderPlaceholder = [this](int width, const QString& name)
    {
        auto* placeholder = new QWidget(this);
        placeholder->setObjectName(name);
        placeholder->setFixedSize(width, kHeaderButtonHeight);
        return placeholder;
    };

    headerLayout->addWidget(m_identityButton);
    headerLayout->addStretch(1);
    if (m_txBadge)
    {
        headerLayout->addWidget(m_txBadge);
    }
    else
    {
        headerLayout->addWidget(createHeaderPlaceholder(kTxBadgeWidth, QStringLiteral("vfoTxBadgePlaceholder")));
    }
    headerLayout->addStretch(1);
    if (lanModButton)
    {
        headerLayout->addWidget(lanModButton);
    }
    else
    {
        headerLayout->addWidget(createHeaderPlaceholder(kLanModButtonWidth, QStringLiteral("vfoLanModPlaceholder")));
    }
    if (txPowerButton)
    {
        headerLayout->addWidget(txPowerButton);
    }
    else
    {
        headerLayout->addWidget(createHeaderPlaceholder(kTxPowerButtonWidth, QStringLiteral("vfoTxPowerPlaceholder")));
    }
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_bandButton);
    headerLayout->addWidget(m_modeButton);

    m_frequencyEdit = new QLineEdit(this);
    m_frequencyEdit->setObjectName(QStringLiteral("vfoFrequency"));
    m_frequencyEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_frequencyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_frequencyEdit->setFocusPolicy(Qt::ClickFocus);
    m_frequencyEdit->installEventFilter(this);
    m_frequencyEdit->setAccessibleName(QStringLiteral("%1 frequency").arg(title));
    m_frequencyEdit->setAccessibleDescription(QStringLiteral("Enter frequency in MHz, then press Enter."));
    m_frequencyEdit->setToolTip(QStringLiteral("Enter frequency in MHz, then press Enter"));
    m_frequencyEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0px; color: %1; }")
            .arg(UiTheme::Color::TextField));
    // The frequency row is seven pixels taller than the original layout. Its
    // line edit therefore gains seven pixels of height; this top margin keeps
    // the large receive text moving down by the same full seven pixels as the
    // fixed-height transmit label below it.
    m_frequencyEdit->setTextMargins(0, 8 + kFrequencyGroupVerticalOffset, 0, 0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QFont frequencyFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    frequencyFont.setFeature(QFont::Tag("tnum"), 1);
#else
    QFont frequencyFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#endif
    frequencyFont.setPixelSize(kFrequencyFontPixelSize);
    frequencyFont.setBold(true);
    m_frequencyEdit->setFont(frequencyFont);
    m_frequencyEditTimer = new QTimer(this);
    m_frequencyEditTimer->setObjectName(QStringLiteral("frequencyEditTimer"));
    m_frequencyEditTimer->setSingleShot(true);
    m_frequencyEditTimer->setInterval(kFrequencyEditTimeoutMs);
    connect(m_frequencyEditTimer, &QTimer::timeout, this,
            [this]()
            {
                m_frequencyEdit->clearFocus();
                finishFrequencyEditing();
            });
    connect(m_frequencyEdit, &QLineEdit::textEdited, this,
            [this]()
            {
                m_frequencyEditing = true;
                m_frequencyEditTimer->start();
            });
    connect(m_frequencyEdit, &QLineEdit::returnPressed, this,
            [this]()
            {
                emit frequencySubmitted(m_frequencyEdit->text());
                m_frequencyEdit->clearFocus();
            });
    connect(m_frequencyEdit, &QLineEdit::editingFinished, this, &VfoDisplay::finishFrequencyEditing);

    m_transmitFrequencyLabel = new QLabel(this);
    m_transmitFrequencyLabel->setObjectName(QStringLiteral("vfoTransmitFrequency"));
    m_transmitFrequencyLabel->setFixedHeight(kTransmitFrequencyHeight);
    m_transmitFrequencyLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_transmitFrequencyLabel->setContentsMargins(0, 0, kTransmitFrequencyRightInset, 0);
    m_transmitFrequencyLabel->setAccessibleName(QStringLiteral("%1 transmit frequency").arg(title));
    m_transmitFrequencyLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(UiTheme::Color::TextMuted));
    QFont transmitFrequencyFont = frequencyFont;
    transmitFrequencyFont.setPixelSize(kTransmitFrequencyFontPixelSize);
    transmitFrequencyFont.setBold(true);
    m_transmitFrequencyLabel->setFont(transmitFrequencyFont);
    clearTransmitFrequency();

    layout->addLayout(headerLayout);
    layout->addSpacing(kHeaderMeterSpacing);
    m_sMeter = new VfoSMeter(this);
    layout->addWidget(m_sMeter);
    layout->addSpacing(kMeterFrequencySpacing);

    auto createReceiverControl = [this, &title](const QString& control)
    {
        auto* button = new QPushButton(control, this);
        button->setObjectName(QStringLiteral("vfo%1Button").arg(control).remove(QLatin1Char(' ')));
        button->setFixedHeight(kReceiverControlHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setAccessibleName(QStringLiteral("%1 %2 control").arg(title, control));
        button->setStyleSheet(receiverControlStyle());
        connect(button, &QPushButton::clicked, this, [this, control]() { emit receiverControlClicked(control); });
        m_receiverControlButtons.insert(control, button);
        return button;
    };

    auto* secondaryControlGroup = new QWidget(this);
    secondaryControlGroup->setFixedSize(kSecondaryControlWidth + kReceiverControlSpacing + kXfcControlWidth,
                                        (kReceiverControlHeight * 2) + kReceiverControlSpacing);
    auto* secondaryControlLayout = new QVBoxLayout(secondaryControlGroup);
    secondaryControlLayout->setContentsMargins(0, 0, 0, 0);
    secondaryControlLayout->setSpacing(kReceiverControlSpacing);
    auto* offsetButton = createReceiverControl(QStringLiteral("OFFSET"));
    auto* toneButton = createReceiverControl(QStringLiteral("TONE"));
    offsetButton->setFixedWidth(kSecondaryControlWidth);
    toneButton->setFixedWidth(kSecondaryControlWidth);
    auto* offsetControlLayout = new QHBoxLayout;
    offsetControlLayout->setContentsMargins(0, 0, 0, 0);
    offsetControlLayout->setSpacing(kReceiverControlSpacing);
    offsetControlLayout->addWidget(offsetButton);
    if (vfo == Vfo::Main)
    {
        auto* xfcButton = createReceiverControl(QStringLiteral("XFC"));
        xfcButton->setFixedWidth(kXfcControlWidth);
        offsetControlLayout->addWidget(xfcButton);
    }
    else
    {
        auto* xfcPlaceholder = new QWidget(secondaryControlGroup);
        xfcPlaceholder->setFixedSize(kXfcControlWidth, kReceiverControlHeight);
        offsetControlLayout->addWidget(xfcPlaceholder);
    }
    secondaryControlLayout->addLayout(offsetControlLayout);
    auto* toneControlLayout = new QHBoxLayout;
    toneControlLayout->setContentsMargins(0, 0, 0, 0);
    toneControlLayout->setSpacing(kReceiverControlSpacing);
    toneControlLayout->addWidget(toneButton);
    if (vfo == Vfo::Main)
    {
        auto* compressorButton = createReceiverControl(QStringLiteral("COMP"));
        compressorButton->setFixedWidth(kXfcControlWidth);
        toneControlLayout->addWidget(compressorButton);
    }
    else
    {
        auto* compressorPlaceholder = new QWidget(secondaryControlGroup);
        compressorPlaceholder->setFixedSize(kXfcControlWidth, kReceiverControlHeight);
        toneControlLayout->addWidget(compressorPlaceholder);
    }
    secondaryControlLayout->addLayout(toneControlLayout);
    auto* frequencyControlLayout = new QHBoxLayout;
    frequencyControlLayout->setContentsMargins(0, 0, 0, 0);
    frequencyControlLayout->setSpacing(kReceiverControlSpacing);
    // Borrow seven pixels from the otherwise blank gap beneath the frequency
    // row. A wrapper with an equal bottom inset keeps OFFSET/TONE/XFC/COMP at
    // their original vertical position while only the two frequency strings
    // use the newly available space.
    auto* secondaryControlWrapper = new QWidget(this);
    auto* secondaryControlWrapperLayout = new QVBoxLayout(secondaryControlWrapper);
    secondaryControlWrapperLayout->setContentsMargins(0, 0, 0, kFrequencyGroupVerticalOffset);
    secondaryControlWrapperLayout->setSpacing(0);
    secondaryControlWrapperLayout->addWidget(secondaryControlGroup);
    frequencyControlLayout->addWidget(secondaryControlWrapper, 0, Qt::AlignBottom);
    auto* frequencyValueLayout = new QVBoxLayout;
    frequencyValueLayout->setContentsMargins(0, 0, 0, 0);
    frequencyValueLayout->setSpacing(0);
    frequencyValueLayout->addWidget(m_frequencyEdit, 1);
    frequencyValueLayout->addWidget(m_transmitFrequencyLabel);
    frequencyControlLayout->addLayout(frequencyValueLayout, 1);
    layout->addLayout(frequencyControlLayout, 1);
    layout->addSpacing(kSecondaryToPrimaryControlSpacing);

    auto* receiverControlLayout = new QHBoxLayout;
    receiverControlLayout->setContentsMargins(0, 0, 0, 0);
    receiverControlLayout->setSpacing(kReceiverControlSpacing);
    const QStringList controls = {
        QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("FILTERS"),
        QStringLiteral("PRE"), QStringLiteral("RFG"), QStringLiteral("SQL"),
    };
    for (const QString& control : controls)
    {
        receiverControlLayout->addWidget(createReceiverControl(control), 1);
    }
    layout->addLayout(receiverControlLayout);
    setOperatingEnabled(true);
    clearFrequency();
}

QString VfoDisplay::frequencyText() const
{
    return m_frequencyEdit ? m_frequencyEdit->text() : QString();
}

bool VfoDisplay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_frequencyEdit && event->type() == QEvent::FocusIn)
    {
        // Treat the initial click as the start of editing. Otherwise, a radio
        // update between the click and the first keystroke can reset the text
        // and move the cursor to the right edge.
        m_frequencyEditing = true;
        m_frequencyEditTimer->start();
    }
    return QWidget::eventFilter(watched, event);
}

void VfoDisplay::finishFrequencyEditing()
{
    m_frequencyEditTimer->stop();
    m_frequencyEditing = false;
    if (m_deferredFrequencyClear)
    {
        m_frequencyEdit->setText(QStringLiteral("---.---.---"));
    }
    else if (m_deferredFrequencyHz.has_value())
    {
        m_frequencyEdit->setText(sdr9700::ui::main_window::formatFrequency(*m_deferredFrequencyHz));
    }
    m_deferredFrequencyClear = false;
    m_deferredFrequencyHz.reset();
}

void VfoDisplay::setFrequencyHz(quint64 hz)
{
    if (!m_frequencyEdit)
    {
        return;
    }
    if (m_frequencyEditing)
    {
        m_deferredFrequencyHz = hz;
        m_deferredFrequencyClear = false;
        return;
    }
    m_frequencyEdit->setText(sdr9700::ui::main_window::formatFrequency(hz));
}

void VfoDisplay::setTransmitFrequencyHz(quint64 hz)
{
    if (!m_transmitFrequencyLabel)
    {
        return;
    }
    m_transmitFrequencyLabel->setText(QStringLiteral("TX: %1").arg(sdr9700::ui::main_window::formatFrequency(hz)));
    m_transmitFrequencyLabel->setAccessibleDescription(
        QStringLiteral("Transmit frequency %1 MHz").arg(hz / 1000000.0, 0, 'f', 6));
}

void VfoDisplay::clearTransmitFrequency()
{
    if (!m_transmitFrequencyLabel)
    {
        return;
    }
    m_transmitFrequencyLabel->clear();
    m_transmitFrequencyLabel->setAccessibleDescription(QString());
}

void VfoDisplay::clearFrequency()
{
    if (m_frequencyEdit)
    {
        if (m_frequencyEditing)
        {
            m_deferredFrequencyHz.reset();
            m_deferredFrequencyClear = true;
        }
        else
        {
            m_frequencyEdit->setText(QStringLiteral("---.---.---"));
        }
    }
    clearTransmitFrequency();
    setBandText(QStringLiteral("--"));
    setModeText(QStringLiteral("--"));
}

void VfoDisplay::setOperatingEnabled(bool enabled)
{
    m_operatingEnabled = enabled;
    setTuningEnabled(m_tuningEnabled);
    if (m_txBadge)
    {
        m_txBadge->setEnabled(enabled);
    }
    for (QPushButton* button : std::as_const(m_receiverControlButtons))
    {
        button->setEnabled(enabled);
    }
    if (!enabled)
    {
        setSMeterValue(0);
    }
    update();
}

void VfoDisplay::setTuningEnabled(bool enabled)
{
    m_tuningEnabled = enabled;
    const bool tuningAvailable = m_operatingEnabled && enabled;
    m_frequencyEdit->setEnabled(tuningAvailable);
    m_bandButton->setEnabled(tuningAvailable);
    m_modeButton->setEnabled(tuningAvailable);
    for (QPushButton* button : {m_bandButton, m_modeButton})
    {
        button->setProperty("active", tuningAvailable);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
    update();
}

void VfoDisplay::setBandText(const QString& text)
{
    m_bandButton->setText(text);
}

void VfoDisplay::setModeText(const QString& text)
{
    m_modeButton->setText(text);
}

void VfoDisplay::setSMeterValue(int rawValue)
{
    if (m_sMeter)
    {
        m_sMeter->setRawValue(rawValue);
    }
}

void VfoDisplay::setTransmitPowerMode(bool enabled)
{
    if (m_sMeter)
    {
        m_sMeter->setTransmitPowerMode(enabled);
    }
}

void VfoDisplay::setTransmitPowerWatts(double watts)
{
    if (m_sMeter)
    {
        m_sMeter->setPowerWatts(watts);
    }
}

void VfoDisplay::setTransmitSwr(double swr)
{
    // Meter reports can arrive after the radio has returned to receive. Do not
    // carry that final reading into the next transmission; each TX interval
    // starts unknown and displays only SWR sampled during that interval.
    if (!m_txBadge || !m_transmitting)
    {
        return;
    }
    m_transmitSwr = std::clamp(swr, 1.0, 6.0);
    m_transmitSwrValid = true;
    m_txBadge->setText(QString::number(m_transmitSwr, 'f', 2));
}

void VfoDisplay::setMaxTransmitPowerWatts(double watts)
{
    if (m_sMeter)
    {
        m_sMeter->setMaxPowerWatts(watts);
    }
}

void VfoDisplay::setSelected(bool selected)
{
    m_identityButton->setProperty("active", selected);
    m_identityButton->style()->unpolish(m_identityButton);
    m_identityButton->style()->polish(m_identityButton);
    m_identityButton->update();
}

void VfoDisplay::setTransmitting(bool transmitting)
{
    if (!m_txBadge)
    {
        return;
    }
    m_transmitting = transmitting;
    if (transmitting)
    {
        m_txBadge->setText(m_transmitSwrValid ? QString::number(m_transmitSwr, 'f', 2) : QStringLiteral("--"));
    }
    else
    {
        m_txBadge->setText(QStringLiteral("TX"));
        m_transmitSwrValid = false;
    }
    m_txBadge->setProperty("transmitting", transmitting);
    m_txBadge->style()->unpolish(m_txBadge);
    m_txBadge->style()->polish(m_txBadge);
    m_txBadge->update();
}

void VfoDisplay::setReceiverControlState(const QString& control, const QString& value, bool active)
{
    QPushButton* button = m_receiverControlButtons.value(control, nullptr);
    if (!button)
    {
        return;
    }
    if (control == QStringLiteral("PRE"))
    {
        button->setText(QStringLiteral("P.AMP"));
    }
    else if (control == QStringLiteral("TONE"))
    {
        button->setText(active && !value.isEmpty() ? value : control);
    }
    else if (control == QStringLiteral("OFFSET"))
    {
        button->setText(value.isEmpty() ? control : value);
    }
    else if (control == QStringLiteral("TX PWR"))
    {
        button->setText(value.isEmpty() ? QStringLiteral("PWR") : QStringLiteral("PWR %1").arg(value));
    }
    else
    {
        button->setText(value.isEmpty() ? control : QStringLiteral("%1 %2").arg(control, value));
    }
    button->setProperty("active", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

void VfoDisplay::setReceiverControlToolTip(const QString& control, const QString& toolTip) const
{
    if (QPushButton* button = m_receiverControlButtons.value(control, nullptr))
    {
        button->setToolTip(toolTip);
    }
}

QPoint VfoDisplay::bandMenuPosition() const
{
    return m_bandButton->mapToGlobal(QPoint(0, m_bandButton->height()));
}

QPoint VfoDisplay::modeMenuPosition() const
{
    return m_modeButton->mapToGlobal(QPoint(0, m_modeButton->height()));
}

QPoint VfoDisplay::receiverControlMenuPosition(const QString& control) const
{
    if (const QPushButton* button = m_receiverControlButtons.value(control, nullptr))
    {
        return button->mapToGlobal(QPoint(0, button->height()));
    }
    return mapToGlobal(rect().center());
}

void VfoDisplay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}
