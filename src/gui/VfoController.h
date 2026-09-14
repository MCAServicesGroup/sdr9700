#pragma once

#include "Vfo.h"
#include "models/RadioState.h"
#include "radio/RadioCapabilities.h"

#include <QObject>
#include <QTimer>
#include <array>
#include <optional>

class IRadioBackend;
class VfoDisplay;
class QWidget;
class VfoController : public QObject
{
    Q_OBJECT

  public:
    explicit VfoController(Vfo vfo, IRadioBackend* backend, sdr9700::RadioState* radioState, QWidget* displayParent,
                           QObject* parent = nullptr);

    Vfo vfo() const { return m_vfo; }
    VfoDisplay* display() const { return m_display; }
    void requestFrequencyHz(quint64 hz);
    void setFrequencyHz(quint64 hz);
    void clearFrequency();
    void setOperatingEnabled(bool enabled);
    void setUserInteractionEnabled(bool enabled);
    void setTuningInteractionEnabled(bool enabled);
    void setSelected(bool selected);
    void setTransmitting(bool transmitting);
    void setLanModLevel(int value);
    void captureExchangeableControlState();
    void discardCapturedExchangeableControlState();
    void applyCapturedControlExchange(VfoController* other);
    availableBands band() const;
    quint64 frequencyHz() const;
    bool hasPublishedState() const { return stateReady() && (!m_backend || m_initialStatePublished); }
    bool selectBand(availableBands requestedBand);

  signals:
    void bandMenuRequested(Vfo vfo, const QPoint& position);
    void toneMenuRequested(Vfo vfo, const QPoint& position);
    void offsetMenuRequested(Vfo vfo, const QPoint& position);
    void compressorMenuRequested(Vfo vfo, const QPoint& position);
    void frequencyChanged(quint64 hz);
    void frequencyRecenterRequested(Vfo vfo, quint64 hz);
    void statePublished(Vfo vfo);
    void lanModLevelChanged(int value);

  private:
    struct ExchangeableControlState
    {
        int agcMode{0};
        bool attenuatorEnabled{false};
        bool nbEnabled{false};
        bool autoNotchEnabled{false};
        bool manualNotchEnabled{false};
        bool nrEnabled{false};
        int preampLevel{0};
    };

    const sdr9700::RadioState::Receiver* confirmedReceiverState() const;
    QString confirmedMode() const;
    std::optional<int> confirmedFilter() const;
    std::optional<duplexMode_t> confirmedDuplexMode() const;
    std::optional<quint64> confirmedRepeaterOffsetHz() const;
    std::optional<rptAccessTxRx_t> confirmedToneAccessMode() const;
    std::optional<ushort> confirmedToneFrequency() const;
    std::optional<ushort> confirmedDtcsCode() const;
    void applyRadioState();
    void showReceiverControlMenu(const QString& control);
    void showModeMenu();
    bool stateReady() const;
    void publishConfirmedState();
    void updateDisplayEnabled();
    void updateReceiverControlDisplay();
    void updateTransmitFrequencyDisplay();
    void requestReceiverLevel(Funcs func, int level);
    void applyReceiverLevelConfirmation(Funcs func, const QVariant& value, uchar receiver);
    void receiverLevelConfirmationTimedOut(Funcs func);
    void clearPendingReceiverLevels();
    void applyExchangeableControlState(const ExchangeableControlState& state);
    bool pendingBandRecallIdentityIsConfirmed() const;
    void finishPendingBandRecall();

    const Vfo m_vfo;
    IRadioBackend* m_backend{nullptr};
    sdr9700::RadioState* m_radioState{nullptr};
    VfoDisplay* m_display{nullptr};
    // These fallback fields support backend-free UI fixtures. A live
    // controller reads the corresponding values exclusively from RadioState.
    std::optional<quint64> m_fallbackFrequencyHz;
    std::optional<quint64> m_publishedFrequencyHz;
    availableBands m_fallbackBand{bandUnknown};
    std::array<quint64, std::size(sdr9700::kRadioUiBandOrder)> m_lastBandFrequencyHz{};
    int m_agcMode{0};
    bool m_attenuatorEnabled{false};
    bool m_nbEnabled{false};
    int m_nbLevel{5};
    bool m_nbLevelReceived{false};
    bool m_autoNotchEnabled{false};
    bool m_autoNotchReceived{false};
    bool m_manualNotchEnabled{false};
    bool m_manualNotchReceived{false};
    bool m_nrEnabled{false};
    int m_nrLevel{5};
    bool m_nrLevelReceived{false};
    int m_preampLevel{0};
    int m_rfGain{0};
    int m_squelch{0};
    int m_txPower{0};
    int m_lanModLevel{128};
    QString m_fallbackMode;
    duplexMode_t m_fallbackDuplexMode{dmSimplex};
    quint64 m_fallbackRepeaterOffsetHz{0};
    rptAccessTxRx_t m_fallbackToneAccessMode{ratrNN};
    ushort m_fallbackToneFrequency{670};
    ushort m_fallbackDtcsCode{23};
    bool m_xfcEnabled{false};
    bool m_compressorEnabled{false};
    bool m_operatingEnabled{true};
    bool m_userInteractionEnabled{false};
    bool m_initialStatePublished{false};
    QTimer m_initialPublishTimer;
    QTimer m_bandRecallSettleTimer;
    QTimer m_bandRecallTimeoutTimer;
    QTimer m_rfGainConfirmationTimer;
    QTimer m_squelchConfirmationTimer;
    std::optional<int> m_pendingRfGain;
    std::optional<int> m_pendingSquelch;
    std::optional<availableBands> m_pendingBandRecall;
    std::optional<ExchangeableControlState> m_capturedExchangeState;
};
