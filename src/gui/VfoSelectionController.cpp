#include "VfoSelectionController.h"

#include "VfoController.h"
#include "VfoSelectionPanel.h"
#include "MainWindowHelpers.h"
#include "backend/IRadioBackend.h"
#include "core/LogCategories.h"

#include <QAction>
#include <QMenu>
#include <QTimer>

namespace
{
constexpr int kSelectionConfirmationTimeoutMs = 1000;
}

VfoSelectionController::VfoSelectionController(IRadioBackend* backend, VfoController* mainController,
                                               VfoController* subController, QWidget* displayParent, QObject* parent)
    : QObject(parent),
      m_backend(backend),
      m_mainController(mainController),
      m_subController(subController),
      m_panel(new VfoSelectionPanel(displayParent))
{
    m_selectionTimeoutTimer = new QTimer(this);
    m_selectionTimeoutTimer->setSingleShot(true);
    m_selectionTimeoutTimer->setInterval(kSelectionConfirmationTimeoutMs);
    connect(m_selectionTimeoutTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_selectionPending)
                {
                    return;
                }
                if (m_selectionRetryCount++ == 0 && m_backend)
                {
                    qWarning(logRadio()).noquote() << "VFO selection confirmation timed out; retrying";
                    m_backend->selectVfo(m_requestedVfo);
                    m_selectionTimeoutTimer->start();
                    return;
                }
                qCritical(logRadio()).noquote() << "VFO selection confirmation failed; releasing controls";
                m_selectionPending = false;
                m_selectionRetryCount = 0;
                m_selectedAction = {};
                setPttReady(!m_exchangePolicy.pending());
            });
    auto showBandMenu = [this](Vfo vfo, const QPoint& position)
    {
        VfoController* target = vfo == Vfo::Main ? m_mainController : m_subController;
        const VfoController* other = vfo == Vfo::Main ? m_subController : m_mainController;
        QMenu menu(m_panel);
        sdr9700::ui::main_window::styleCompactMenu(&menu);
        bool firstBand = true;
        for (const availableBands band : sdr9700::kRadioUiBandOrder)
        {
            if (band == other->band())
            {
                continue;
            }
            if (!firstBand)
            {
                menu.addSeparator();
            }
            firstBand = false;
            QAction* action = menu.addAction(sdr9700::radioBandMenuLabel(band));
            action->setData(static_cast<int>(band));
        }
        if (const QAction* chosen = menu.exec(position))
        {
            target->selectBand(static_cast<availableBands>(chosen->data().toInt()));
        }
    };
    connect(m_mainController, &VfoController::bandMenuRequested, this, showBandMenu);
    connect(m_subController, &VfoController::bandMenuRequested, this, showBandMenu);
    connect(m_panel, &VfoSelectionPanel::vfoRequested, this, [this](Vfo vfo) { selectVfo(vfo); });
    connect(m_panel, &VfoSelectionPanel::dualWatchRequested, this, [this](bool enabled) { requestDualWatch(enabled); });
    connect(m_panel, &VfoSelectionPanel::exchangeRequested, this, [this]() { requestMainSubExchange(); });
    if (m_backend)
    {
        connect(m_backend, &IRadioBackend::mainSubExchangeCompleted, this,
                [this]()
                {
                    if (!m_exchangePolicy.confirmRadio())
                    {
                        return;
                    }
                    m_mainController->applyCapturedControlExchange(m_subController);
                    const bool changed = m_selectedVfo != Vfo::Main;
                    m_selectedVfo = Vfo::Main;
                    m_requestedVfo = Vfo::Main;
                    m_panel->setSelectedVfo(Vfo::Main);
                    m_mainController->setSelected(m_radioReady);
                    m_subController->setSelected(false);
                    updateTransmitIndicators();
                    if (changed)
                    {
                        emit selectedVfoChanged(Vfo::Main);
                    }
                });
        connect(m_backend, &IRadioBackend::mainSubExchangeFailed, this,
                [this]()
                {
                    m_mainController->discardCapturedExchangeableControlState();
                    m_subController->discardCapturedExchangeableControlState();
                    m_exchangePolicy.reset();
                    m_panel->setExchangePending(false);
                    setPttReady(true);
                });
        connect(m_backend, &IRadioBackend::dualWatchTransitionPendingChanged, this,
                [this](bool pending)
                {
                    m_dualWatchPending = pending;
                    m_panel->setDualWatchPending(pending);
                    setPttReady(!pending && !m_selectionPending && !m_exchangePolicy.pending());
                });
        connect(m_backend, &IRadioBackend::pttChanged, this,
                [this](bool transmitting)
                {
                    qDebug(logRadio()).noquote()
                        << "PTT UI radio confirmation received state=" << (transmitting ? "TX" : "RX");
                    m_transmitting = transmitting;
                    updateTransmitIndicators();
                });
        connect(m_backend, &IRadioBackend::pttRequestAccepted, m_mainController, &VfoController::setTransmitRequested);
        connect(m_backend, &IRadioBackend::radioValueConfirmed, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    if (receiver != 0)
                    {
                        return;
                    }
                    if (func == funcVFOBandMS)
                    {
                        const Vfo selected = value.toBool() ? Vfo::Sub : Vfo::Main;
                        // The physical MAIN/SUB context also changes during
                        // receiver-routing batches. It confirms an explicit
                        // UI selection only while that request is pending; it
                        // must not later overwrite the operator's stable UI
                        // selection when a background batch restores MAIN.
                        if (!m_selectionPending || selected != m_requestedVfo)
                        {
                            return;
                        }
                        m_selectionPending = false;
                        m_selectionRetryCount = 0;
                        m_selectionTimeoutTimer->stop();
                        setPttReady(true);
                        m_requestedVfo = selected;
                        const bool changed = m_selectedVfo != selected;
                        m_selectedVfo = selected;
                        m_panel->setSelectedVfo(selected);
                        m_mainController->setSelected(m_radioReady && selected == Vfo::Main);
                        m_subController->setSelected(m_radioReady && selected == Vfo::Sub);
                        updateTransmitIndicators();
                        if (changed)
                        {
                            emit selectedVfoChanged(selected);
                        }
                        if (m_selectedAction && selected == m_requestedVfo)
                        {
                            auto action = std::move(m_selectedAction);
                            action();
                        }
                    }
                    else if (func == funcVFODualWatch)
                    {
                        const bool enabled = value.toBool();
                        m_dualWatchEnabled = enabled;
                        m_panel->setDualWatchEnabled(enabled);
                        m_subController->setOperatingEnabled(enabled);
                        if (!enabled)
                        {
                            if (m_selectedVfo != Vfo::Main && m_backend)
                            {
                                requestSelection(Vfo::Main);
                            }
                            m_subController->clearFrequency();
                        }
                    }
                });
        connect(m_backend, &IRadioBackend::readyChanged, this,
                [this](bool ready)
                {
                    if (!ready)
                    {
                        reset();
                    }
                });
    }
    reset();
    setControlsEnabled(false);
}

void VfoSelectionController::completeExchangeScopeSync(bool releaseReceiverContext)
{
    if (!m_exchangePolicy.confirmScope())
    {
        return;
    }
    if (releaseReceiverContext)
    {
        // A scope timeout must not strand the radio-routing controls behind
        // SpectrumScopeController's still-locked frame gate. Spectrum clicks
        // remain locked independently until a valid scope confirmation arrives.
        setReceiverContextReady(true);
    }
    m_panel->setExchangePending(false);
    setPttReady(true);
}

void VfoSelectionController::requestSelection(Vfo vfo)
{
    m_selectedAction = {};
    if (!m_backend)
    {
        return;
    }
    m_requestedVfo = vfo;
    m_selectionPending = true;
    m_selectionRetryCount = 0;
    setPttReady(false);
    m_backend->selectVfo(vfo);
    m_selectionTimeoutTimer->start();
}

void VfoSelectionController::runWhenSelected(Vfo vfo, std::function<void()> action)
{
    if (!action)
    {
        return;
    }
    if (!m_selectionPending && m_selectedVfo == vfo)
    {
        action();
        return;
    }
    requestSelection(vfo);
    m_selectedAction = std::move(action);
}

bool VfoSelectionController::selectVfo(Vfo vfo)
{
    if (!receiverPairAvailable() || m_selectionPending)
    {
        return false;
    }
    if (m_selectedVfo == vfo)
    {
        return true;
    }
    requestSelection(vfo);
    return true;
}

bool VfoSelectionController::requestMainSubExchange()
{
    if (!receiverPairAvailable() || m_selectionPending || !m_exchangePolicy.request())
    {
        return false;
    }
    m_selectionPending = false;
    m_mainController->captureExchangeableControlState();
    m_subController->captureExchangeableControlState();
    m_panel->setExchangePending(true);
    setPttReady(false);
    m_backend->exchangeMainSub();
    return true;
}

bool VfoSelectionController::requestDualWatch(bool enabled)
{
    if (!receiverContextAvailable() || m_selectionPending)
    {
        return false;
    }
    return m_backend->setDualWatchEnabled(enabled);
}

void VfoSelectionController::setControlsEnabled(bool enabled)
{
    m_controlsEnabled = enabled;
    m_panel->setControlsEnabled(enabled);
}

void VfoSelectionController::setRadioReady(bool ready)
{
    m_radioReady = ready;
    m_panel->setRadioReady(ready);
    m_mainController->setSelected(ready && m_selectedVfo == Vfo::Main);
    m_subController->setSelected(ready && m_selectedVfo == Vfo::Sub);
}

void VfoSelectionController::setReceiverContextReady(bool ready)
{
    m_receiverContextReady = ready;
    m_panel->setReceiverContextReady(ready);
}

bool VfoSelectionController::receiverContextAvailable() const
{
    return m_backend && m_controlsEnabled && m_radioReady && m_receiverContextReady && !m_transmitting &&
           !m_exchangePolicy.pending() && !m_dualWatchPending;
}

bool VfoSelectionController::receiverPairAvailable() const
{
    return receiverContextAvailable() && m_dualWatchEnabled;
}

void VfoSelectionController::reset()
{
    m_selectedVfo = Vfo::Main;
    m_requestedVfo = Vfo::Main;
    m_selectionPending = false;
    m_selectionRetryCount = 0;
    m_selectionTimeoutTimer->stop();
    m_exchangePolicy.reset();
    m_selectedAction = {};
    m_panel->setExchangePending(false);
    m_panel->setDualWatchPending(false);
    setPttReady(true);
    m_transmitting = false;
    m_dualWatchEnabled = false;
    m_dualWatchPending = false;
    m_panel->setSelectedVfo(Vfo::Main);
    m_mainController->setSelected(m_radioReady);
    m_subController->setSelected(false);
    m_panel->setDualWatchEnabled(false);
    m_mainController->setOperatingEnabled(true);
    m_subController->setOperatingEnabled(false);
    m_subController->clearFrequency();
    updateTransmitIndicators();
}

void VfoSelectionController::setPttReady(bool ready)
{
    if (m_pttReady == ready)
    {
        return;
    }
    m_pttReady = ready;
    emit pttReadyChanged(ready);
}

void VfoSelectionController::updateTransmitIndicators()
{
    m_mainController->setTransmitting(m_transmitting);
    m_subController->setTransmitting(false);
}
