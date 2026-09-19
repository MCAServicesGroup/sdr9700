#include "AudioDevicesSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"

#include <algorithm>
#include <QAudioDevice>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMediaDevices>
#include <QSignalBlocker>
#include <QVBoxLayout>

AudioDevicesSettingsPanel::AudioDevicesSettingsPanel(QWidget* parent) : QWidget(parent)
{
    m_mediaDevices = new QMediaDevices(this);
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 4, 12, 0);
    vbox->setSpacing(8);

    auto* inputGroup = new QGroupBox("Input (Computer to Radio)", this);
    inputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* inputForm = new QFormLayout(inputGroup);
    inputForm->setSpacing(8);

    m_inputCombo = new QComboBox(inputGroup);
    m_inputCombo->setObjectName(QStringLiteral("audioInputDevice"));
    const AppSettings& settings = AppSettings::instance();
    const QByteArray savedIn = QByteArray::fromBase64(settings.value("audioInputDeviceID").toString().toLatin1());
    repopulateDeviceCombo(m_inputCombo, QMediaDevices::audioInputs(), savedIn, QMediaDevices::defaultAudioInput());

    inputForm->addRow("Device:", m_inputCombo);
    vbox->addWidget(inputGroup);

    auto* outputGroup = new QGroupBox("Output (Radio to Computer)", this);
    outputGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* outputLayout = new QGridLayout(outputGroup);
    outputLayout->setHorizontalSpacing(8);
    outputLayout->setVerticalSpacing(0);
    outputLayout->setColumnStretch(1, 1);
    // Control spacing and notice spacing are independent: compact help text
    // must not reduce the operator-requested gap between the selectors.
    outputLayout->setRowMinimumHeight(1, 13);

    m_outputCombo = new QComboBox(outputGroup);
    m_outputCombo->setObjectName(QStringLiteral("audioOutputDevice"));

    m_outputChannelsCombo = new QComboBox(outputGroup);
    m_outputChannelsCombo->setObjectName(QStringLiteral("audioOutputChannels"));
    m_outputChannelsCombo->addItem(QStringLiteral("Mono mix (MAIN + SUB)"), 1);
    m_outputChannelsCombo->addItem(QStringLiteral("Stereo (MAIN left, SUB right)"), 2);

    const QByteArray savedOut = QByteArray::fromBase64(settings.value("audioOutputDeviceID").toString().toLatin1());
    const int savedOutputChannels = std::clamp(settings.value("audioOutputChannels", 2).toInt(), 1, 2);
    repopulateDeviceCombo(m_outputCombo, QMediaDevices::audioOutputs(), savedOut, QMediaDevices::defaultAudioOutput());
    if (const int idx = m_outputChannelsCombo->findData(savedOutputChannels); idx >= 0)
    {
        m_outputChannelsCombo->setCurrentIndex(idx);
    }

    auto* outputDeviceLabel = new QLabel(QStringLiteral("Device:"), outputGroup);
    outputDeviceLabel->setBuddy(m_outputCombo);
    auto* codecLabel = new QLabel(QStringLiteral("Playback:"), outputGroup);
    codecLabel->setBuddy(m_outputChannelsCombo);
    outputLayout->addWidget(outputDeviceLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    outputLayout->addWidget(m_outputCombo, 0, 1);
    outputLayout->addWidget(codecLabel, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    outputLayout->addWidget(m_outputChannelsCombo, 2, 1);
    m_codecNotice = new QWidget(outputGroup);
    m_codecNotice->setObjectName(QStringLiteral("audioCodecNotice"));
    auto* codecLayout = new QVBoxLayout(m_codecNotice);
    codecLayout->setContentsMargins(0, 10, 0, 0);
    codecLayout->setSpacing(2);
    m_codecPendingLabel = new QLabel(QStringLiteral("Requires Radio Reconnect"), m_codecNotice);
    m_codecPendingLabel->setObjectName(QStringLiteral("audioCodecPendingReconnect"));
    m_codecPendingLabel->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::Warning)));
    codecLayout->addWidget(m_codecPendingLabel);
    m_codecPendingHint = new QLabel(QStringLiteral("Disconnect and reconnect to the radio to apply."), m_codecNotice);
    m_codecPendingHint->setObjectName(QStringLiteral("audioCodecPendingHint"));
    m_codecPendingHint->setWordWrap(true);
    m_codecPendingHint->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::TextMuted)));
    codecLayout->addWidget(m_codecPendingHint);
    m_codecPendingLabel->setToolTip(m_codecPendingHint->text());
    // The notice occupies its own row below the controls. Hiding the whole
    // row removes its text and top gap without changing control alignment.
    for (QLabel* notice : {m_codecPendingLabel, m_codecPendingHint})
    {
        QSizePolicy policy = notice->sizePolicy();
        policy.setVerticalPolicy(notice == m_codecPendingLabel ? QSizePolicy::Fixed : QSizePolicy::Preferred);
        notice->setSizePolicy(policy);
        notice->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }
    outputLayout->addWidget(m_codecNotice, 3, 1);
    updateCodecPendingState();
    vbox->addWidget(outputGroup);
    vbox->addStretch(1);

    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                AppSettings::instance().setValue(
                    "audioInputDeviceID", QString::fromLatin1(m_inputCombo->currentData().toByteArray().toBase64()));
                emit audioSettingsChanged();
            });
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                AppSettings::instance().setValue(
                    "audioOutputDeviceID", QString::fromLatin1(m_outputCombo->currentData().toByteArray().toBase64()));
                emit audioSettingsChanged();
            });
    connect(m_outputChannelsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                AppSettings::instance().setValue("audioOutputChannels",
                                                 std::clamp(m_outputChannelsCombo->currentData().toInt(), 1, 2));
                updateCodecPendingState();
                emit audioSettingsChanged();
            });
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &AudioDevicesSettingsPanel::refreshInputDevices);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &AudioDevicesSettingsPanel::refreshOutputDevices);
}

void AudioDevicesSettingsPanel::setAudioConnectionState(bool connected, int activeOutputChannels)
{
    m_audioConnected = connected;
    m_activeOutputChannels = std::clamp(activeOutputChannels, 1, 2);
    updateCodecPendingState();
}

void AudioDevicesSettingsPanel::updateCodecPendingState()
{
    const bool pending = m_audioConnected && m_outputChannelsCombo->currentData().toInt() != m_activeOutputChannels;
    m_codecPendingLabel->setVisible(pending);
    m_codecPendingHint->setVisible(pending);
    m_codecNotice->setVisible(pending);
}

void AudioDevicesSettingsPanel::repopulateDeviceCombo(QComboBox* combo, const QList<QAudioDevice>& devices,
                                                      const QByteArray& preferredID, const QAudioDevice& defaultDevice)
{
    if (!combo)
    {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    const QString defaultName =
        defaultDevice.isNull() ? QStringLiteral("No device available") : defaultDevice.description();
    combo->addItem(QStringLiteral("System default (%1)").arg(defaultName), QByteArray{});
    for (const QAudioDevice& device : devices)
    {
        combo->addItem(device.description(), device.id());
    }
    const int preferredIndex = combo->findData(preferredID);
    if (preferredIndex >= 0)
    {
        combo->setCurrentIndex(preferredIndex);
    }
    else if (!preferredID.isEmpty())
    {
        combo->addItem(QStringLiteral("Saved device unavailable (using %1)").arg(defaultName), preferredID);
        combo->setCurrentIndex(combo->count() - 1);
    }
}

void AudioDevicesSettingsPanel::refreshInputDevices()
{
    const QByteArray selectedID =
        QByteArray::fromBase64(AppSettings::instance().value("audioInputDeviceID").toString().toLatin1());
    repopulateDeviceCombo(m_inputCombo, QMediaDevices::audioInputs(), selectedID, QMediaDevices::defaultAudioInput());
}

void AudioDevicesSettingsPanel::refreshOutputDevices()
{
    const QByteArray selectedID =
        QByteArray::fromBase64(AppSettings::instance().value("audioOutputDeviceID").toString().toLatin1());
    repopulateDeviceCombo(m_outputCombo, QMediaDevices::audioOutputs(), selectedID,
                          QMediaDevices::defaultAudioOutput());
}
