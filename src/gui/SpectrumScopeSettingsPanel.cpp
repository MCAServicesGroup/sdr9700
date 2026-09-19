#include "SpectrumScopeSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"
#include "UiTheme.h"

#include <algorithm>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
constexpr auto kSpectrumScopeCenterLineColorSettingsKey = "spectrumScopeCenterLineColor";
constexpr auto kSpectrumScopeBackgroundColorSettingsKey = "spectrumScopeBackgroundColor";
constexpr auto kSpectrumScopeGridLineColorSettingsKey = "spectrumScopeGridLineColor";
constexpr auto kSpectrumScopeGridDensitySettingsKey = "spectrumScopeGridDensity";
constexpr auto kSpectrumScopeInvertMouseWheelSettingsKey = "spectrumScopeInvertMouseWheel";
const QColor kDefaultCenterLineColor(0xf5, 0xf7, 0xf8);
const QColor kDefaultBackgroundColor(0x08, 0x12, 0x1b);
const QColor kDefaultGridLineColor(0x6f, 0x89, 0x9e);
constexpr int kDefaultGridDensity = 1;
constexpr int kMinGridDensity = 0;
constexpr int kMaxGridDensity = 2;

QColor storedColor(const char* key, const QColor& defaultColor)
{
    const QColor color(
        AppSettings::instance().value(QString::fromLatin1(key), defaultColor.name(QColor::HexRgb)).toString());
    return color.isValid() ? color : defaultColor;
}

int storedGridDensity()
{
    return std::clamp(AppSettings::instance()
                          .value(QString::fromLatin1(kSpectrumScopeGridDensitySettingsKey), kDefaultGridDensity)
                          .toInt(),
                      kMinGridDensity, kMaxGridDensity);
}
} // namespace

SpectrumScopeSettingsPanel::SpectrumScopeSettingsPanel(QWidget* parent)
    : QWidget(parent),
      m_centerLineColor(storedColor(kSpectrumScopeCenterLineColorSettingsKey, kDefaultCenterLineColor)),
      m_backgroundColor(storedColor(kSpectrumScopeBackgroundColorSettingsKey, kDefaultBackgroundColor)),
      m_gridLineColor(storedColor(kSpectrumScopeGridLineColorSettingsKey, kDefaultGridLineColor)),
      m_gridDensity(storedGridDensity())
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 4, 12, 0);
    root->setSpacing(8);

    auto* backgroundGroup = new QGroupBox("Background", this);
    backgroundGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* backgroundLayout = new QVBoxLayout(backgroundGroup);
    backgroundLayout->setContentsMargins(10, 12, 10, 10);
    backgroundLayout->setSpacing(8);
    backgroundLayout->addLayout(
        makeColorRow(backgroundGroup, QStringLiteral("Color:"), &m_backgroundColorButton, &m_backgroundResetButton));
    root->addWidget(backgroundGroup);

    auto* centerLineGroup = new QGroupBox("Center Line", this);
    centerLineGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* centerLineLayout = new QVBoxLayout(centerLineGroup);
    centerLineLayout->setContentsMargins(10, 12, 10, 10);
    centerLineLayout->setSpacing(8);
    centerLineLayout->addLayout(
        makeColorRow(centerLineGroup, QStringLiteral("Color:"), &m_centerLineColorButton, &m_centerLineResetButton));
    root->addWidget(centerLineGroup);

    auto* gridGroup = new QGroupBox("Gridlines", this);
    gridGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* gridLayout = new QVBoxLayout(gridGroup);
    gridLayout->setContentsMargins(10, 12, 10, 10);
    gridLayout->setSpacing(8);

    auto* gridRow = new QHBoxLayout;
    auto* gridColorLabel = new QLabel("Color:", gridGroup);
    m_gridLineColorButton = new QPushButton(gridGroup);
    m_gridLineColorButton->setMinimumWidth(110);
    auto* densityLabel = new QLabel("Density:", gridGroup);
    m_gridDensityCombo = new QComboBox(gridGroup);
    m_gridDensityCombo->addItem(QStringLiteral("Fewer"), 0);
    m_gridDensityCombo->addItem(QStringLiteral("Normal"), 1);
    m_gridDensityCombo->addItem(QStringLiteral("More"), 2);
    m_gridLineResetButton = new QPushButton("Reset", gridGroup);
    if (const int index = m_gridDensityCombo->findData(m_gridDensity); index >= 0)
    {
        m_gridDensityCombo->setCurrentIndex(index);
    }
    gridRow->addWidget(gridColorLabel);
    gridRow->addWidget(m_gridLineColorButton);
    gridRow->addSpacing(16);
    gridRow->addWidget(densityLabel);
    gridRow->addWidget(m_gridDensityCombo);
    gridRow->addStretch(1);
    gridRow->addWidget(m_gridLineResetButton);
    gridLayout->addLayout(gridRow);
    root->addWidget(gridGroup);

    auto* wheelGroup = new QGroupBox("Mouse Wheel", this);
    wheelGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* wheelLayout = new QVBoxLayout(wheelGroup);
    wheelLayout->setContentsMargins(10, 12, 10, 10);
    wheelLayout->setSpacing(6);
    m_invertMouseWheelCheck = new QCheckBox("Reverse wheel tuning direction", wheelGroup);
    m_invertMouseWheelCheck->setChecked(
        AppSettings::instance()
            .value(QString::fromLatin1(kSpectrumScopeInvertMouseWheelSettingsKey), "False")
            .toBool());
    wheelLayout->addWidget(m_invertMouseWheelCheck);
    root->addWidget(wheelGroup);

    connect(m_centerLineColorButton, &QPushButton::clicked, this, &SpectrumScopeSettingsPanel::chooseCenterLineColor);
    connect(m_centerLineResetButton, &QPushButton::clicked, this, &SpectrumScopeSettingsPanel::resetCenterLineColor);
    connect(m_backgroundColorButton, &QPushButton::clicked, this, &SpectrumScopeSettingsPanel::chooseBackgroundColor);
    connect(m_backgroundResetButton, &QPushButton::clicked, this, &SpectrumScopeSettingsPanel::resetBackgroundColor);
    connect(m_gridLineColorButton, &QPushButton::clicked, this, &SpectrumScopeSettingsPanel::chooseGridLineColor);
    connect(m_gridLineResetButton, &QPushButton::clicked, this, &SpectrumScopeSettingsPanel::resetGridLineColor);
    connect(m_gridDensityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (m_gridDensityCombo)
                {
                    setGridDensity(m_gridDensityCombo->currentData().toInt(), true);
                }
            });
    connect(m_invertMouseWheelCheck, &QCheckBox::toggled, this,
            [this](bool checked)
            {
                AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeInvertMouseWheelSettingsKey),
                                                 checked);
                Q_EMIT reverseMouseWheelTuningChanged(checked);
            });

    updateColorButton(m_centerLineColorButton, m_centerLineColor);
    updateColorButton(m_backgroundColorButton, m_backgroundColor);
    updateColorButton(m_gridLineColorButton, m_gridLineColor);
    root->addStretch(1);
}

QHBoxLayout* SpectrumScopeSettingsPanel::makeColorRow(QWidget* parent, const QString& labelText,
                                                      QPushButton** colorButton, QPushButton** resetButton)
{
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(labelText, parent);
    *colorButton = new QPushButton(parent);
    (*colorButton)->setMinimumWidth(110);
    *resetButton = new QPushButton("Reset", parent);
    row->addWidget(label);
    row->addWidget(*colorButton);
    row->addStretch(1);
    row->addWidget(*resetButton);
    return row;
}

void SpectrumScopeSettingsPanel::chooseCenterLineColor()
{
    const QColor color = QColorDialog::getColor(m_centerLineColor, this, QStringLiteral("Spectrum Scope Center Line"));
    if (color.isValid())
    {
        setCenterLineColor(color, true);
    }
}

void SpectrumScopeSettingsPanel::chooseBackgroundColor()
{
    const QColor color = QColorDialog::getColor(m_backgroundColor, this, QStringLiteral("Spectrum Scope Background"));
    if (color.isValid())
    {
        setBackgroundColor(color, true);
    }
}

void SpectrumScopeSettingsPanel::chooseGridLineColor()
{
    const QColor color = QColorDialog::getColor(m_gridLineColor, this, QStringLiteral("Spectrum Scope Gridlines"));
    if (color.isValid())
    {
        setGridLineColor(color, true);
    }
}

void SpectrumScopeSettingsPanel::resetCenterLineColor()
{
    setCenterLineColor(kDefaultCenterLineColor, true);
}

void SpectrumScopeSettingsPanel::resetBackgroundColor()
{
    setBackgroundColor(kDefaultBackgroundColor, true);
}

void SpectrumScopeSettingsPanel::resetGridLineColor()
{
    setGridLineColor(kDefaultGridLineColor, true);
}

void SpectrumScopeSettingsPanel::setCenterLineColor(const QColor& color, bool persist)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_centerLineColor == normalized)
    {
        return;
    }

    m_centerLineColor = normalized;
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeCenterLineColorSettingsKey),
                                         m_centerLineColor.name(QColor::HexRgb));
    }
    updateColorButton(m_centerLineColorButton, m_centerLineColor);
    Q_EMIT centerLineColorChanged(m_centerLineColor);
}

void SpectrumScopeSettingsPanel::setBackgroundColor(const QColor& color, bool persist)
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
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeBackgroundColorSettingsKey),
                                         m_backgroundColor.name(QColor::HexRgb));
    }
    updateColorButton(m_backgroundColorButton, m_backgroundColor);
    Q_EMIT backgroundColorChanged(m_backgroundColor);
}

void SpectrumScopeSettingsPanel::setGridLineColor(const QColor& color, bool persist)
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
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeGridLineColorSettingsKey),
                                         m_gridLineColor.name(QColor::HexRgb));
    }
    updateColorButton(m_gridLineColorButton, m_gridLineColor);
    Q_EMIT gridLineColorChanged(m_gridLineColor);
}

void SpectrumScopeSettingsPanel::setGridDensity(int density, bool persist)
{
    const int normalized = std::clamp(density, kMinGridDensity, kMaxGridDensity);
    if (m_gridDensity == normalized)
    {
        return;
    }

    m_gridDensity = normalized;
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeGridDensitySettingsKey), m_gridDensity);
    }
    if (m_gridDensityCombo)
    {
        const QSignalBlocker blocker(m_gridDensityCombo);
        if (const int index = m_gridDensityCombo->findData(m_gridDensity); index >= 0)
        {
            m_gridDensityCombo->setCurrentIndex(index);
        }
    }
    Q_EMIT gridDensityChanged(m_gridDensity);
}

void SpectrumScopeSettingsPanel::updateColorButton(QPushButton* button, const QColor& color)
{
    if (!button)
    {
        return;
    }

    const QString colorName = color.name(QColor::HexRgb);
    button->setText(colorName.toUpper());
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; color: %3; "
                       "padding: 4px 12px; font-weight: bold; }"
                       "QPushButton:hover { border-color: %4; }")
            .arg(colorName, QLatin1String(UiTheme::Color::BorderLight),
                 color.lightness() > 140 ? QLatin1String(UiTheme::Color::PanelDark)
                                         : QLatin1String(UiTheme::Color::TextBright),
                 QLatin1String(UiTheme::Color::AccentBright)));
}
