#ifdef HAVE_HIDAPI
#include "IcomRC28SettingsPanel.h"

#include "SettingsPanelStyle.h"
#include "core/LogCategories.h"
#include "core/IcomRC28Manager.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <iterator>

namespace
{
struct ActionItem
{
    const char* id;
    const char* label;
};

constexpr ActionItem kActions[] = {
    {"None", "None"},
    {"ToggleLock", "Lock"},
    {"ToggleMainSub", "Switch Receiver"},
    {"ExchangeMainSub", "Swap MAIN and SUB"},
    {"CycleMode", "Mode"},
    {"ToggleMute", "Mute"},
    {"CycleStep", "Step"},
    {"StepDown", "Step Down"},
    {"StepUp", "Step Up"},
};

void populateActionCombo(QComboBox* combo, const QString& current)
{
    if (combo->count() == 0)
    {
        for (const auto& item : kActions)
        {
            combo->addItem(QString::fromUtf8(item.label), QString::fromLatin1(item.id));
        }
    }

    const int index = combo->findData(current);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QString actionLabel(const QString& actionId)
{
    const auto it = std::find_if(std::begin(kActions), std::end(kActions),
                                 [&actionId](const ActionItem& item) { return actionId == QLatin1String(item.id); });
    return it != std::end(kActions) ? QString::fromUtf8(it->label) : actionId;
}

} // namespace

IcomRC28SettingsPanel::IcomRC28SettingsPanel(IcomRC28Manager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager)
{
    buildUi();
    loadSettings();
    refreshDeviceInfo();

    if (m_manager)
    {
        connect(m_manager, &IcomRC28Manager::connectionChanged, this,
                [this](bool connected, const QString& deviceName)
                {
                    Q_UNUSED(connected)
                    Q_UNUSED(deviceName)
                    refreshDeviceInfo();
                });
        connect(m_manager, &IcomRC28Manager::multipleDevicesDetected, this,
                [this](const QString&) { refreshDeviceInfo(); });
    }
}

void IcomRC28SettingsPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 4, 12, 0);
    root->setSpacing(8);

    auto* deviceGroup = new QGroupBox(QStringLiteral("Status"), this);
    deviceGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* deviceForm = new QFormLayout(deviceGroup);
    deviceForm->setLabelAlignment(Qt::AlignRight);
    deviceForm->setSpacing(6);

    m_statusLabel = new QLabel(QStringLiteral("Not connected"), deviceGroup);
    m_devicePathLabel = new QLabel(QStringLiteral("-"), deviceGroup);
    m_serialLabel = new QLabel(QStringLiteral("-"), deviceGroup);
    for (auto* label : {m_statusLabel, m_devicePathLabel, m_serialLabel})
    {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    deviceForm->addRow(QStringLiteral("Status:\t"), m_statusLabel);
    deviceForm->addRow(QStringLiteral("Path:\t"), m_devicePathLabel);
    deviceForm->addRow(QStringLiteral("Serial:\t"), m_serialLabel);
    root->addWidget(deviceGroup);

    auto* mapGroup = new QGroupBox(QStringLiteral("Button Mapping"), this);
    mapGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* mapForm = new QFormLayout(mapGroup);
    mapForm->setLabelAlignment(Qt::AlignRight);
    mapForm->setHorizontalSpacing(6);
    mapForm->setVerticalSpacing(11);
    m_f1PressCombo = new QComboBox(mapGroup);
    m_f1HoldCombo = new QComboBox(mapGroup);
    m_f2PressCombo = new QComboBox(mapGroup);
    m_f2HoldCombo = new QComboBox(mapGroup);
    m_pttModeCombo = new QComboBox(mapGroup);
    m_f1PressCombo->setObjectName(QStringLiteral("icomRC28F1PressAction"));
    for (auto* combo : {m_f1PressCombo, m_f1HoldCombo, m_f2PressCombo, m_f2HoldCombo, m_pttModeCombo})
    {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }
    populateActionCombo(m_f1PressCombo, QStringLiteral("None"));
    populateActionCombo(m_f1HoldCombo, QStringLiteral("None"));
    populateActionCombo(m_f2PressCombo, QStringLiteral("None"));
    populateActionCombo(m_f2HoldCombo, QStringLiteral("None"));

    m_pttModeCombo->addItem(QStringLiteral("Disabled"), QStringLiteral("Disabled"));
    m_pttModeCombo->addItem(QStringLiteral("Momentary"), QStringLiteral("Momentary"));
    m_pttModeCombo->addItem(QStringLiteral("Latched"), QStringLiteral("Latched"));

    mapForm->addRow(QStringLiteral("F-1 (Short Press):\t"), m_f1PressCombo);
    mapForm->addRow(QStringLiteral("F-1 (Long Press):\t"), m_f1HoldCombo);
    mapForm->addRow(QStringLiteral("F-2 (Short Press):\t"), m_f2PressCombo);
    mapForm->addRow(QStringLiteral("F-2 (Long Press):\t"), m_f2HoldCombo);
    mapForm->addRow(QStringLiteral("Transmit:\t"), m_pttModeCombo);
    root->addWidget(mapGroup);

    auto* encoderGroup = new QGroupBox(QStringLiteral("Encoder"), this);
    encoderGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* encoderLayout = new QVBoxLayout(encoderGroup);
    encoderLayout->setContentsMargins(10, 10, 10, 10);
    encoderLayout->setSpacing(8);

    auto* sensitivityRow = new QHBoxLayout;
    auto* sensitivityLabel = new QLabel(QStringLiteral("Pulses per step:"), encoderGroup);
    m_sensitivitySlider = new QSlider(Qt::Horizontal, encoderGroup);
    m_sensitivitySlider->setRange(1, 10);
    m_sensitivitySlider->setTickInterval(1);
    m_sensitivitySlider->setTickPosition(QSlider::TicksBelow);
    m_sensitivityValueLabel = new QLabel(QStringLiteral("1"), encoderGroup);
    m_sensitivityValueLabel->setFixedWidth(24);
    m_sensitivityValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sensitivityRow->addWidget(sensitivityLabel);
    sensitivityRow->addWidget(m_sensitivitySlider, 1);
    sensitivityRow->addWidget(m_sensitivityValueLabel);
    encoderLayout->addLayout(sensitivityRow);

    m_autoSnapCheck = new QCheckBox(QStringLiteral("Auto-snap to nearest 1 kHz after rotation stops"), encoderGroup);
    encoderLayout->addWidget(m_autoSnapCheck);
    encoderLayout->addStretch(1);
    root->addWidget(encoderGroup, 1);

    connect(m_f1PressCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("F1Press"), m_f1PressCombo->currentData().toString()); });
    connect(m_f1HoldCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("F1Hold"), m_f1HoldCombo->currentData().toString()); });
    connect(m_f2PressCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("F2Press"), m_f2PressCombo->currentData().toString()); });
    connect(m_f2HoldCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { saveActionField(QStringLiteral("F2Hold"), m_f2HoldCombo->currentData().toString()); });
    connect(m_pttModeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { savePTTMode(m_pttModeCombo->currentData().toString()); });
    connect(m_sensitivitySlider, &QSlider::valueChanged, this, &IcomRC28SettingsPanel::saveSensitivity);
    connect(m_autoSnapCheck, &QCheckBox::toggled, this, &IcomRC28SettingsPanel::saveAutoSnap);
}

void IcomRC28SettingsPanel::refreshDeviceInfo()
{
    if (!m_manager)
    {
        return;
    }

    const bool open = m_manager->isOpen();
    const bool blocked = m_manager->isBlockedByMultiple();
    const bool detected = !open && !blocked && !IcomRC28Manager::detectDevice().isEmpty();
    if (m_statusLabel)
    {
        m_statusLabel->setText(blocked ? QStringLiteral("Multiple devices detected")
                               : open  ? QStringLiteral("Connected")
                                       : (detected ? QStringLiteral("Detected") : QStringLiteral("Not connected")));
    }
    if (m_devicePathLabel)
    {
        m_devicePathLabel->setText(m_manager->devicePath().isEmpty() ? QStringLiteral("-") : m_manager->devicePath());
    }
    if (m_serialLabel)
    {
        m_serialLabel->setText(m_manager->serialNumber().isEmpty() ? QStringLiteral("-") : m_manager->serialNumber());
    }
}

void IcomRC28SettingsPanel::appendLog(const QString& text)
{
    qInfo(logIcomRC28()).noquote() << text;
}

void IcomRC28SettingsPanel::loadSettings()
{
    const QSignalBlocker blockF1Press(m_f1PressCombo);
    const QSignalBlocker blockF1Hold(m_f1HoldCombo);
    const QSignalBlocker blockF2Press(m_f2PressCombo);
    const QSignalBlocker blockF2Hold(m_f2HoldCombo);
    const QSignalBlocker blockPtt(m_pttModeCombo);
    const QSignalBlocker blockSensitivity(m_sensitivitySlider);
    const QSignalBlocker blockAutoSnap(m_autoSnapCheck);

    populateActionCombo(m_f1PressCombo,
                        IcomRC28Manager::settingsField(QStringLiteral("F1Press"), QStringLiteral("None")));
    populateActionCombo(m_f1HoldCombo,
                        IcomRC28Manager::settingsField(QStringLiteral("F1Hold"), QStringLiteral("None")));
    populateActionCombo(m_f2PressCombo,
                        IcomRC28Manager::settingsField(QStringLiteral("F2Press"), QStringLiteral("None")));
    populateActionCombo(m_f2HoldCombo,
                        IcomRC28Manager::settingsField(QStringLiteral("F2Hold"), QStringLiteral("None")));

    const QString pttMode = IcomRC28Manager::settingsField(QStringLiteral("PTTMode"), QStringLiteral("Disabled"));
    const int pttIndex = m_pttModeCombo->findData(pttMode);
    m_pttModeCombo->setCurrentIndex(pttIndex >= 0 ? pttIndex : 0);

    const int sensitivity =
        std::clamp(IcomRC28Manager::settingsField(QStringLiteral("sensitivity"), QStringLiteral("1")).toInt(), 1, 10);
    m_sensitivitySlider->setValue(sensitivity);
    m_sensitivityValueLabel->setText(QString::number(sensitivity));
    m_autoSnapCheck->setChecked(IcomRC28Manager::settingsField(QStringLiteral("autoSnap"), QStringLiteral("False")) ==
                                QLatin1String("True"));
}

void IcomRC28SettingsPanel::saveActionField(const QString& field, const QString& value)
{
    IcomRC28Manager::setSettingsField(field, value);
    appendLog(QStringLiteral("%1 -> %2").arg(field, actionLabel(value)));
}

void IcomRC28SettingsPanel::savePTTMode(const QString& mode)
{
    IcomRC28Manager::setSettingsField(QStringLiteral("PTTMode"), mode);
    appendLog(QStringLiteral("PTTMode -> %1").arg(mode));
}

void IcomRC28SettingsPanel::saveSensitivity(int value)
{
    const QString text = QString::number(std::clamp(value, 1, 10));
    if (m_sensitivityValueLabel)
    {
        m_sensitivityValueLabel->setText(text);
    }
    IcomRC28Manager::setSettingsField(QStringLiteral("sensitivity"), text);
    emit encoderSettingsChanged(QStringLiteral("sensitivity"), text);
    appendLog(QStringLiteral("sensitivity -> %1").arg(text));
}

void IcomRC28SettingsPanel::saveAutoSnap(bool on)
{
    const QString value = on ? QStringLiteral("True") : QStringLiteral("False");
    IcomRC28Manager::setSettingsField(QStringLiteral("autoSnap"), value);
    emit encoderSettingsChanged(QStringLiteral("autoSnap"), value);
    appendLog(QStringLiteral("autoSnap -> %1").arg(value));
}

#endif
