#include "IcomRC28Controller.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "VfoSelectionController.h"
#include "models/SpectrumScopeModel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"
#ifdef HAVE_HIDAPI
#include "core/IcomRC28Manager.h"
#endif

#include <algorithm>
#include <cmath>
#include <QTimer>

using namespace sdr9700::ui::main_window;


IcomRC28Controller::IcomRC28Controller(MainWindow* window) : QObject(window), m_window(window) {}

void IcomRC28Controller::initialize()
{
#ifdef HAVE_HIDAPI
    m_window->m_icomRC28Manager = new IcomRC28Manager(m_window);
    refreshIcomRC28EncoderSettings();
    m_window->m_icomRC28SnapTimer = new QTimer(m_window);
    m_window->m_icomRC28SnapTimer->setSingleShot(true);
    m_window->m_icomRC28SnapTimer->setInterval(600);
    connect(m_window->m_icomRC28SnapTimer, &QTimer::timeout, this, &IcomRC28Controller::snapIcomRC28FrequencyToKhz);
    for (int i = 0; i < 2; ++i)
    {
        m_window->m_icomRC28HoldTimers[i] = new QTimer(m_window);
        m_window->m_icomRC28HoldTimers[i]->setSingleShot(true);
        connect(m_window->m_icomRC28HoldTimers[i], &QTimer::timeout, this,
                [this, i]()
                {
                    if (!m_window->m_icomRC28ButtonDown[i] || m_window->m_icomRC28HoldConsumed[i])
                    {
                        return;
                    }
                    m_window->m_icomRC28HoldConsumed[i] = true;
                    const QString field = i == 0 ? QStringLiteral("F1Hold") : QStringLiteral("F2Hold");
                    dispatchIcomRC28Action(IcomRC28Manager::settingsField(field, QStringLiteral("None")));
                });
    }
    connect(m_window->m_icomRC28Manager, &IcomRC28Manager::buttonPressed, this,
            &IcomRC28Controller::handleIcomRC28Button);
    connect(m_window->m_icomRC28Manager, &IcomRC28Manager::tuneSteps, this, &IcomRC28Controller::handleIcomRC28Tune);
    connect(m_window->m_icomRC28Manager, &IcomRC28Manager::multipleDevicesDetected, this,
            [this](const QString& deviceName)
            {
                qInfo(logIcomRC28()).noquote() << "Multiple devices detected:" << deviceName;
                m_window->showStatusMessage(QStringLiteral("Duplicate accessory blocked (%1)").arg(deviceName), 8000,
                                            MainWindow::StatusMessageKind::Warning);
            });
    connect(m_window->m_icomRC28Manager, &IcomRC28Manager::connectionChanged, this,
            [this](bool connected, const QString& deviceName)
            {
                qInfo(logIcomRC28()).noquote() << (connected ? "Connected" : "Disconnected") << deviceName;
                if (connected)
                {
                    updateIcomRC28Leds();
                }
                else if (m_window->m_icomRC28PttLatched)
                {
                    // A latched PTT request can remain active after the
                    // physical button has already returned to idle, so the
                    // manager's synthesized held-button releases cannot see
                    // it. Losing the accessory must always fail safe and
                    // explicitly unkey the radio.
                    m_window->m_icomRC28PttLatched = false;
                    setIcomRC28Ptt(false);
                }
                m_window->showStatusMessage(
                    connected ? QStringLiteral("Accessory connected (%1)").arg(deviceName)
                              : QStringLiteral("Accessory disconnected (%1)").arg(deviceName),
                    4000, connected ? MainWindow::StatusMessageKind::Info : MainWindow::StatusMessageKind::Warning);
            });
    m_window->m_icomRC28Manager->loadSettings();
#endif
}

void IcomRC28Controller::close()
{
#ifdef HAVE_HIDAPI
    if (m_window->m_icomRC28Manager)
    {
        m_window->m_icomRC28Manager->close();
    }
#endif
}

void IcomRC28Controller::dispatchIcomRC28Action(const QString& action)
{
    if (action.isEmpty() || action == QLatin1String("None"))
    {
        return;
    }

    if (action == QLatin1String("ToggleMute"))
    {
        m_window->toggleMute();
        return;
    }

    if (action == QLatin1String("ToggleLock"))
    {
        m_window->toggleControlLock();
        return;
    }

    if (!m_window->m_vfo || !m_window->radioUiReady() || m_window->m_controlsLocked)
    {
        return;
    }

    if (action == QLatin1String("ToggleMainSub"))
    {
        const Vfo target = m_window->m_vfoSelectionController->selectedVfo() == Vfo::Main ? Vfo::Sub : Vfo::Main;
        m_window->m_vfoSelectionController->selectVfo(target);
    }
    else if (action == QLatin1String("ExchangeMainSub"))
    {
        m_window->m_vfoSelectionController->requestMainSubExchange();
    }
    else if (action == QLatin1String("CycleStep") || action == QLatin1String("StepUp") ||
             action == QLatin1String("StepDown"))
    {
        const int current = m_window->tuningStepHz();
        const int direction = action == QLatin1String("StepDown") ? -1 : 1;
        AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHZSettingsKey),
                                         adjacentTuningStepHz(current, direction));
        m_window->updateStepButton();
        m_window->applyRadioTuningStep();
    }
    else if (action == QLatin1String("CycleMode"))
    {
        m_window->cycleMode();
    }
}

void IcomRC28Controller::setIcomRC28Ptt(bool on)
{
    if (!m_window->m_vfo || !m_window->m_model->isReady() || (on && !m_window->radioUiReady()))
    {
        return;
    }
    if (on)
    {
        m_window->beginMemoryPttFrequencyTransition();
    }
    else
    {
        m_window->m_pttActive = false;
    }
    m_window->m_vfo->setPtt(on);
}

void IcomRC28Controller::updateIcomRC28Leds()
{
#ifdef HAVE_HIDAPI
    if (!m_window->m_icomRC28Manager || !m_window->m_icomRC28Manager->isOpen())
    {
        return;
    }

    // Active-low: clearing a bit turns the LED on.
    uint8_t b = IcomRC28Manager::kLedsAllOff;
    b &= ~IcomRC28Manager::kLedBitLink; // LINK always on while connected

    if (m_window->m_txActive)
    {
        b &= ~IcomRC28Manager::kLedBitTx;
    }

    // F-key LEDs reflect their hold action's active/toggled state
    auto holdActionActive = [this](const QString& actionId) -> bool
    {
        if (actionId == QLatin1String("ToggleMute"))
        {
            return m_window->m_muted;
        }
        if (actionId == QLatin1String("ToggleLock"))
        {
            return m_window->m_controlsLocked;
        }
        return false;
    };

    if (holdActionActive(IcomRC28Manager::settingsField(QStringLiteral("F1Hold"), QStringLiteral("None"))))
    {
        b &= ~IcomRC28Manager::kLedBitF1;
    }
    if (holdActionActive(IcomRC28Manager::settingsField(QStringLiteral("F2Hold"), QStringLiteral("None"))))
    {
        b &= ~IcomRC28Manager::kLedBitF2;
    }

    m_window->m_icomRC28Manager->setIcomRC28Leds(b);
#endif
}

void IcomRC28Controller::handleIcomRC28Tune(int steps)
{
    qInfo(logIcomRC28()).noquote() << "Tune steps:" << steps;

    if (!m_window->m_vfo || !m_window->radioUiReady() || m_window->m_controlsLocked)
    {
        return;
    }

    if (steps == 0)
    {
        return;
    }

    refreshIcomRC28EncoderSettings();
    if (m_window->m_icomRC28Sensitivity > 1)
    {
        if (m_window->m_icomRC28PulseAccum != 0 && ((steps > 0) != (m_window->m_icomRC28PulseAccum > 0)))
        {
            m_window->m_icomRC28PulseAccum = 0;
        }
        m_window->m_icomRC28PulseAccum += steps;
        const int dividedSteps = m_window->m_icomRC28PulseAccum / m_window->m_icomRC28Sensitivity;
        m_window->m_icomRC28PulseAccum -= dividedSteps * m_window->m_icomRC28Sensitivity;
        if (dividedSteps == 0)
        {
            if (m_window->m_icomRC28AutoSnap && m_window->m_icomRC28SnapTimer)
            {
                m_window->m_icomRC28SnapTimer->start();
            }
            return;
        }
        steps = dividedSteps;
    }
    if (m_window->m_icomRC28AutoSnap && m_window->m_icomRC28SnapTimer)
    {
        m_window->m_icomRC28SnapTimer->start();
    }

    const int stepHz = m_window->tuningStepHz();
    const qint64 currentHz =
        static_cast<qint64>(m_window->m_pendingSpectrumScopeTuneHz > 0 ? m_window->m_pendingSpectrumScopeTuneHz
                                                                       : m_window->m_vfo->frequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * stepHz;
    m_window->scheduleSpectrumScopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}

void IcomRC28Controller::refreshIcomRC28EncoderSettings()
{
    const int sensitivity =
        std::clamp(IcomRC28Manager::settingsField(QStringLiteral("sensitivity"), QStringLiteral("1")).toInt(), 1, 10);
    if (sensitivity != m_window->m_icomRC28Sensitivity)
    {
        m_window->m_icomRC28PulseAccum = 0;
    }
    m_window->m_icomRC28Sensitivity = sensitivity;
    m_window->m_icomRC28AutoSnap =
        IcomRC28Manager::settingsField(QStringLiteral("autoSnap"), QStringLiteral("False")) == QLatin1String("True");
}

void IcomRC28Controller::snapIcomRC28FrequencyToKhz()
{
    if (!m_window->m_vfo || !m_window->radioUiReady() || m_window->m_controlsLocked)
    {
        return;
    }

    const quint64 currentHz = m_window->m_pendingSpectrumScopeTuneHz > 0 ? m_window->m_pendingSpectrumScopeTuneHz
                                                                         : m_window->m_vfo->frequencyHz();
    const quint64 snappedHz =
        m_window->clampFrequencyHzToActiveBand(static_cast<quint64>(std::llround(currentHz / 1000.0)) * 1000ULL);
    if (snappedHz == currentHz)
    {
        return;
    }

    if (m_window->m_spectrumScopeTuneCommitTimer)
    {
        m_window->m_spectrumScopeTuneCommitTimer->stop();
    }
    if (m_window->m_spectrumScopeTuneReleaseTimer)
    {
        m_window->m_spectrumScopeTuneReleaseTimer->stop();
    }
    m_window->m_pendingSpectrumScopeTuneHz = 0;
    m_window->m_spectrumScopeDisplayCenterHz = 0;
    m_window->m_spectrumScopeFixedPanStartHz = 0;
    m_window->m_spectrumScopeFixedPanEndHz = 0;
    if (m_window->m_spectrumScope)
    {
        m_window->m_spectrumScope->clearDisplayCenterHold();
    }
    m_window->leaveMemoryModeForManualChange();
    m_window->m_vfo->setFrequencyHz(snappedHz);
}

void IcomRC28Controller::handleIcomRC28Button(int button, int action)
{
    qInfo(logIcomRC28()).noquote() << "Button" << button << (action == 0 ? "press" : "release");

    if (!m_window->m_icomRC28Manager)
    {
        return;
    }

    if (button == 1 || button == 2)
    {
        const int index = button - 1;
        if (action == 0)
        {
            m_window->m_icomRC28ButtonDown[index] = true;
            m_window->m_icomRC28HoldConsumed[index] = false;
            if (m_window->m_icomRC28HoldTimers[index])
            {
                m_window->m_icomRC28HoldTimers[index]->start(600);
            }
            return;
        }

        m_window->m_icomRC28ButtonDown[index] = false;
        if (m_window->m_icomRC28HoldTimers[index] && m_window->m_icomRC28HoldTimers[index]->isActive() &&
            !m_window->m_icomRC28HoldConsumed[index])
        {
            m_window->m_icomRC28HoldTimers[index]->stop();
            const QString field = index == 0 ? QStringLiteral("F1Press") : QStringLiteral("F2Press");
            dispatchIcomRC28Action(IcomRC28Manager::settingsField(field, QStringLiteral("None")));
        }
        m_window->m_icomRC28HoldConsumed[index] = false;
        return;
    }

    if (button != 3)
    {
        return;
    }

    const QString mode = IcomRC28Manager::settingsField(QStringLiteral("PTTMode"), QStringLiteral("Disabled"));
    if (mode == QLatin1String("Disabled"))
    {
        return;
    }

    if (!rc28PttAllowed(m_window->m_vfo != nullptr, m_window->radioUiReady(), m_window->m_controlsLocked))
    {
        return;
    }

    if (action == 0)
    {
        if (mode == QLatin1String("Latched"))
        {
            m_window->m_icomRC28PttLatched = !m_window->m_icomRC28PttLatched;
            setIcomRC28Ptt(m_window->m_icomRC28PttLatched);
        }
        else
        {
            m_window->m_icomRC28PttLatched = true;
            setIcomRC28Ptt(true);
        }
    }
    else if (mode == QLatin1String("Momentary"))
    {
        m_window->m_icomRC28PttLatched = false;
        setIcomRC28Ptt(false);
    }
}
