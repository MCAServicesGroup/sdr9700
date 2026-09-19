#pragma once

#include "Vfo.h"
#include "MainSubExchangePolicy.h"

#include <QObject>
#include <functional>

class IRadioBackend;
class VfoController;
class VfoSelectionPanel;
class QWidget;
class QTimer;

class VfoSelectionController : public QObject
{
    Q_OBJECT

  public:
    explicit VfoSelectionController(IRadioBackend* backend, VfoController* mainController, VfoController* subController,
                                    QWidget* displayParent, QObject* parent = nullptr);

    VfoSelectionPanel* panel() const { return m_panel; }
    Vfo selectedVfo() const { return m_selectedVfo; }
    bool receiverContextAvailable() const;
    void setControlsEnabled(bool enabled);
    void setRadioReady(bool ready);
    void setReceiverContextReady(bool ready);
    void runWhenSelected(Vfo vfo, std::function<void()> action);
    bool selectVfo(Vfo vfo);
    bool requestMainSubExchange();
    bool requestDualWatch(bool enabled);
    void completeExchangeScopeSync(bool releaseReceiverContext = false);

  signals:
    void selectedVfoChanged(Vfo vfo);
    void pttReadyChanged(bool ready);

  private:
    void reset();
    void requestSelection(Vfo vfo);
    bool receiverPairAvailable() const;
    void setPttReady(bool ready);
    void updateTransmitIndicators();

    IRadioBackend* m_backend{nullptr};
    VfoController* m_mainController{nullptr};
    VfoController* m_subController{nullptr};
    VfoSelectionPanel* m_panel{nullptr};
    Vfo m_selectedVfo{Vfo::Main};
    Vfo m_requestedVfo{Vfo::Main};
    bool m_selectionPending{false};
    sdr9700::MainSubExchangePolicy m_exchangePolicy;
    bool m_pttReady{true};
    bool m_transmitting{false};
    bool m_radioReady{false};
    bool m_controlsEnabled{false};
    bool m_receiverContextReady{true};
    bool m_dualWatchEnabled{false};
    bool m_dualWatchPending{false};
    std::function<void()> m_selectedAction;
    QTimer* m_selectionTimeoutTimer{nullptr};
    int m_selectionRetryCount{0};
};
