#include "RadioCommandController.h"
#include "DialogFooter.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "MainTitleBar.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "UiTheme.h"
#include "backend/IRadioBackend.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <memory>
#include <utility>

using namespace sdr9700::ui::main_window;


RadioCommandController::RadioCommandController(MainWindow* window, CompressorLevelSetter compressorLevelSetter)
    : QObject(window), m_window(window), m_compressorLevelSetter(std::move(compressorLevelSetter))
{
    if (!m_compressorLevelSetter)
    {
        m_compressorLevelSetter = [window](int value) { window->m_vfo->setCompressorLevel(value); };
    }
}

void RadioCommandController::toggleMute()
{
    m_window->m_muted = !m_window->m_muted;
    if (m_window->m_muted)
    {
        m_window->m_savedAfGain = m_window->m_currentAfGain;
        m_window->m_currentAfGain = 0;
        if (m_window->m_titleBar)
        {
            m_window->m_titleBar->setVolume(0);
            m_window->m_titleBar->setMuted(true);
        }
        m_window->onAfGainChanged(0);
    }
    else
    {
        const int restored = std::clamp(m_window->m_savedAfGain, 0, 255);
        m_window->m_currentAfGain = restored;
        if (m_window->m_titleBar)
        {
            m_window->m_titleBar->setVolume(restored);
            m_window->m_titleBar->setMuted(false);
        }
        m_window->onAfGainChanged(restored);
    }
    m_window->updateIcomRC28Leds();
}

void RadioCommandController::cycleMode()
{
    if (!m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    const QStringList modes = m_window->m_vfo->availableModes();
    if (modes.isEmpty())
    {
        return;
    }

    const QString current = m_window->m_vfo->mode();
    const int index = modes.indexOf(current);
    const int nextIndex = index >= 0 ? (index + 1) % modes.size() : 0;
    m_window->leaveMemoryModeForManualChange();
    m_window->m_vfo->setMode(modes.at(nextIndex));
}

void RadioCommandController::showOffsetMenu(const QPoint& position, quint64 receiveFrequencyHz)
{
    if (!m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked)
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    const auto* simplexAction = menu.addAction(QStringLiteral("SIMPLEX"));
    menu.addSeparator();
    // The clicked controller already owns a confirmed frequency for its logical
    // VFO. Do not consult the shared radio model here: selecting the other VFO
    // is asynchronous, so that model can still contain the formerly selected
    // side's frequency while this menu is being opened. Using it caused MAIN
    // and SUB to offer one another's band-specific offset presets.
    const QVector<OffsetPreset> presets = offsetPresetsForHz(receiveFrequencyHz);
    QVector<QAction*> presetActions;
    presetActions.reserve(presets.size());
    for (const OffsetPreset& preset : presets)
    {
        if (!presetActions.isEmpty())
        {
            menu.addSeparator();
        }
        presetActions.append(menu.addAction(preset.label));
    }
    menu.addSeparator();
    const auto* customAction = menu.addAction(QStringLiteral("CUSTOM"));

    const QAction* selected = menu.exec(position);
    if (!selected)
    {
        return;
    }

    if (selected == simplexAction)
    {
        applyOffsetSelection(dmSimplex, m_window->m_repeaterOffsetHz);
        return;
    }

    for (int i = 0; i < presetActions.size(); ++i)
    {
        if (selected == presetActions.at(i))
        {
            const OffsetPreset& preset = presets.at(i);
            applyOffsetSelection(preset.mode, preset.hz);
            return;
        }
    }

    if (selected == customAction)
    {
        showCustomOffsetDialog();
    }
}

void RadioCommandController::showCustomOffsetDialog()
{
    if (m_window->m_controlsLocked)
    {
        return;
    }

    QDialog dialog(m_window);
    dialog.setWindowTitle(QStringLiteral("Custom Offset"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(UiTheme::Size::DialogContentMargin, 10, UiTheme::Size::DialogContentMargin, 0);
    layout->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    auto* form = new QFormLayout;
    auto* direction = new QComboBox(&dialog);
    direction->addItem(QStringLiteral("+"), QVariant::fromValue<int>(dmDupPlus));
    direction->addItem(QStringLiteral("-"), QVariant::fromValue<int>(dmDupMinus));
    direction->setCurrentIndex(m_window->m_duplexMode == dmDupMinus ? 1 : 0);

    auto* offset = new QDoubleSpinBox(&dialog);
    offset->setDecimals(3);
    offset->setRange(0.001, 99.999);
    offset->setSingleStep(0.005);
    offset->setSuffix(QStringLiteral(" MHz"));
    offset->setValue(std::max<quint64>(1, m_window->m_repeaterOffsetHz) / 1000000.0);

    form->addRow(QStringLiteral("Direction"), direction);
    form->addRow(QStringLiteral("Offset"), offset);
    layout->addLayout(form);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(&dialog);
    footer.buttonBox->addButton(QDialogButtonBox::Cancel);
    footer.buttonBox->addButton(QStringLiteral("Set"), QDialogButtonBox::AcceptRole);
    layout->addWidget(footer.widget);
    connect(footer.buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(footer.buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    m_window->centerPopupWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const auto mode = static_cast<duplexMode_t>(direction->currentData().toInt());
    const quint64 offsetHz = static_cast<quint64>(offset->value() * 1000000.0 + 0.5);
    applyOffsetSelection(mode, offsetHz);
}

void RadioCommandController::applyOffsetSelection(duplexMode_t mode, quint64 offsetHz)
{
    if (!m_window->m_vfo || m_window->m_controlsLocked)
    {
        return;
    }

    m_window->leaveMemoryModeForManualChange();

    if (mode == dmSimplex)
    {
        m_window->m_vfo->setDuplexMode(dmSimplex);
        return;
    }

    m_window->m_vfo->setRepeaterOffsetHz(offsetHz);
    m_window->m_vfo->setDuplexMode(mode);
}

void RadioCommandController::showToneMenu(const QPoint& position)
{
    if (!m_window->m_vfo || !m_window->m_model->isReady())
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);
    menu.setStyleSheet(menu.styleSheet() + QStringLiteral("QMenu::item { padding-right: 34px; }"));

    auto styleToneGridButton = [](QPushButton* button)
    {
        button->setFixedSize(54, 24);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                             "color: %3; font-size: 11px; }"
                                             "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                                  .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                       UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
    };

    auto addCtcssMenu = [this, &menu, styleToneGridButton](QMenu* parent, const QString& title, rptAccessTxRx_t mode)
    {
        auto* submenu = parent->addMenu(title);
        styleCompactMenu(submenu);
        auto* panel = new QWidget(submenu);
        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        static constexpr int kColumns = 4;
        int index = 0;
        for (const TonePreset& preset : kTonePresets)
        {
            auto* button = new QPushButton(QString::fromLatin1(preset.label), panel);
            styleToneGridButton(button);
            const ushort tone = preset.tone;
            connect(button, &QPushButton::clicked, this,
                    [this, &menu, submenu, mode, tone]()
                    {
                        applyToneSelection(mode, tone);
                        submenu->close();
                        menu.close();
                    });
            grid->addWidget(button, index / kColumns, index % kColumns);
            ++index;
        }
        auto* action = new QWidgetAction(submenu);
        action->setDefaultWidget(panel);
        submenu->addAction(action);
    };

    auto addDtcsMenu = [this, &menu, styleToneGridButton](QMenu* parent, const QString& title, rptAccessTxRx_t mode)
    {
        auto* submenu = parent->addMenu(title);
        styleCompactMenu(submenu);
        auto* panel = new QWidget(submenu);
        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(6, 6, 6, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        static constexpr int kColumns = 6;
        int index = 0;
        for (const ushort code : kDtcsCodes)
        {
            auto* button = new QPushButton(dtcsCodeLabel(code), panel);
            styleToneGridButton(button);
            connect(button, &QPushButton::clicked, this,
                    [this, &menu, submenu, mode, code]()
                    {
                        applyToneSelection(mode, code);
                        submenu->close();
                        menu.close();
                    });
            grid->addWidget(button, index / kColumns, index % kColumns);
            ++index;
        }
        auto* action = new QWidgetAction(submenu);
        action->setDefaultWidget(panel);
        submenu->addAction(action);
    };

    addCtcssMenu(&menu, QStringLiteral("TONE"), ratrTN);
    menu.addSeparator();
    addCtcssMenu(&menu, QStringLiteral("TSQL"), ratrTT);
    menu.addSeparator();
    addDtcsMenu(&menu, QStringLiteral("DTCS"), ratrDD);
    menu.addSeparator();
    const auto* offAction = menu.addAction(QStringLiteral("OFF"));

    const QAction* selected = menu.exec(position);
    if (!selected)
    {
        return;
    }

    if (selected == offAction)
    {
        applyToneSelection(ratrNN, 0);
    }
}

void RadioCommandController::applyToneSelection(rptAccessTxRx_t mode, ushort value)
{
    if (!m_window->m_vfo)
    {
        return;
    }

    const bool dtcs = isDtcsToneMode(mode);
    m_window->leaveMemoryModeForManualChange();

    if (mode == ratrNN)
    {
        m_window->m_vfo->setToneAccessMode(mode);
        return;
    }

    if (dtcs)
    {
        m_window->m_vfo->setDtcsCode(value);
    }
    else
    {
        m_window->m_vfo->setToneFrequency(value);
    }
    m_window->m_vfo->setToneAccessMode(mode);
}

void RadioCommandController::showCompressorMenu(const QPoint& position)
{
    if (!m_window->m_vfo || !m_window->m_model->isReady())
    {
        return;
    }

    QMenu menu(m_window);
    styleCompactMenu(&menu);

    auto* panel = new QWidget(&menu);
    auto* panelLayout = new QHBoxLayout(panel);
    panelLayout->setContentsMargins(8, 6, 8, 6);
    panelLayout->setSpacing(6);

    auto levelText = [](int value) { return QStringLiteral("%1%").arg(std::clamp(value, 0, 255) * 100 / 255); };

    const bool levelKnown = m_window->m_vfo->compressorLevelKnown();
    const bool compressorEnabled = m_window->m_vfo->compressorOn();
    const int initialValue = compressorEnabled ? std::max(1, levelKnown ? m_window->m_vfo->compressorLevel() : 1) : 0;
    auto* valueLabel = new QLabel(levelText(initialValue), panel);
    valueLabel->setObjectName(QStringLiteral("compressorLevelLabel"));
    valueLabel->setFixedWidth(30);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; background: transparent; }")
            .arg(UiTheme::Color::TextMuted));

    auto* slider = new QSlider(Qt::Horizontal, panel);
    slider->setObjectName(QStringLiteral("compressorLevelSlider"));
    slider->setRange(0, 255);
    slider->setValue(initialValue);
    slider->setFixedWidth(110);
    slider->setFixedHeight(20);
    slider->setAccessibleName(QStringLiteral("Speech compressor level"));
    slider->setAccessibleDescription(QStringLiteral("Set to zero to turn speech compression off."));
    auto requestedEnabled = std::make_shared<bool>(compressorEnabled);
    connect(slider, &QSlider::valueChanged, this,
            [this, valueLabel, levelText, requestedEnabled](int value)
            {
                valueLabel->setText(levelText(value));
                if (value == 0)
                {
                    if (*requestedEnabled)
                    {
                        *requestedEnabled = false;
                        m_window->m_vfo->setCompressor(false);
                    }
                    return;
                }
                m_compressorLevelSetter(value);
                if (!*requestedEnabled)
                {
                    *requestedEnabled = true;
                    m_window->m_vfo->setCompressor(true);
                }
            });
    connect(m_window->m_vfo, &VfoModel::compressorLevelChanged, slider,
            [slider, valueLabel, levelText, requestedEnabled](int value)
            {
                if (!*requestedEnabled)
                {
                    return;
                }
                const QSignalBlocker block(slider);
                slider->setValue(std::max(1, value));
                valueLabel->setText(levelText(slider->value()));
            });
    connect(m_window->m_vfo, &VfoModel::compressorChanged, slider,
            [this, slider, valueLabel, levelText, requestedEnabled](bool enabled)
            {
                *requestedEnabled = enabled;
                const int value = enabled ? std::max(1, m_window->m_vfo->compressorLevel()) : 0;
                const QSignalBlocker block(slider);
                slider->setValue(value);
                valueLabel->setText(levelText(value));
            });

    panelLayout->addWidget(slider);
    panelLayout->addWidget(valueLabel);

    auto* panelAction = new QWidgetAction(&menu);
    panelAction->setDefaultWidget(panel);
    menu.addAction(panelAction);
    menu.exec(position);
}

int RadioCommandController::tuningStepHz() const
{
    return std::clamp(
        AppSettings::instance().value(QString::fromLatin1(kTuningStepHZSettingsKey), kDefaultTuningStepHZ).toInt(), 1,
        10000000);
}

void RadioCommandController::applyRadioTuningStep()
{
    if (!m_window->m_model || !m_window->m_model->isReady())
    {
        return;
    }

    const int step = radioTuningStepForHz(tuningStepHz());
    if (step >= 0)
    {
        m_window->m_model->setTuningStep(step);
    }
}
