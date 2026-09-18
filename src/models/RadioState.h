#pragma once

#include "Types.h"
#include "Vfo.h"
#include "radio/RadioCapabilities.h"

#include <QObject>

#include <array>
#include <optional>

class IRadioBackend;

namespace sdr9700
{

// RadioState is the main-thread current-state projection for one physical radio. It is
// intentionally passive: it never sends CI-V, schedules polling, retries a
// transaction, or performs reply correlation. Those responsibilities remain
// in Commander and RadioBackend. This object only projects backend events that
// have already crossed those boundaries.
class RadioState final : public QObject
{
    Q_OBJECT

  public:
    // Receiver is a passive snapshot of values that have actually arrived
    // from the radio for one logical receiver. An empty value means SDR9700
    // does not currently have a radio-derived answer; defaults must not be
    // dressed up as confirmations.
    struct Receiver
    {
        std::optional<quint64> frequencyHz;
        std::optional<QString> mode;
        std::optional<int> filter;
        std::optional<duplexMode_t> duplexMode;
        std::optional<quint64> repeaterOffsetHz;
        std::optional<rptAccessTxRx_t> toneAccessMode;
        std::optional<ushort> toneFrequency;
        std::optional<ushort> toneSquelchFrequency;
        std::optional<ushort> dtcsCode;
        std::optional<int> agcMode;
        std::optional<bool> attenuatorEnabled;
        std::optional<bool> nbEnabled;
        std::optional<int> nbLevel;
        std::optional<bool> autoNotchEnabled;
        std::optional<bool> manualNotchEnabled;
        std::optional<bool> nrEnabled;
        std::optional<int> nrLevel;
        std::optional<int> preampLevel;
        std::optional<int> rfGain;
        std::optional<int> squelch;
        availableBands band{bandUnknown};
    };

    // BandRecall is SDR9700's confirmed-only history for a receiver/band
    // pair. It is deliberately not named BandStack: native IC-9700 Band
    // Stacking Register support is a separate, unverified protocol feature.
    struct BandRecall
    {
        std::optional<quint64> frequencyHz;
        std::optional<QString> mode;
        std::optional<int> filter;
        std::optional<duplexMode_t> duplexMode;
        std::optional<quint64> repeaterOffsetHz;
        std::optional<rptAccessTxRx_t> toneAccessMode;
        std::optional<ushort> toneFrequency;
        std::optional<ushort> toneSquelchFrequency;
        std::optional<ushort> dtcsCode;
    };

    struct Shared
    {
        bool ready{false};
        bool transmitting{false};
        std::optional<Vfo> selectedVfo;
        std::optional<bool> dualWatchEnabled;
        std::optional<bool> dialLockEnabled;
        std::optional<int> afGain;
        std::optional<int> txPower;
        std::optional<int> lanModLevel;
        std::optional<bool> compressorEnabled;
        std::optional<int> compressorLevel;
    };

    explicit RadioState(IRadioBackend* backend, QObject* parent = nullptr);

    const Receiver& receiver(Vfo vfo) const;
    const Shared& shared() const { return m_shared; }
    const BandRecall* bandRecall(Vfo vfo, availableBands band) const;

  signals:
    void receiverStateChanged(Vfo vfo);
    void sharedStateChanged();
    void bandRecallChanged(Vfo vfo, availableBands band);

  private:
    static std::size_t receiverIndex(Vfo vfo);
    void applyRadioValue(Funcs func, const QVariant& value, uchar receiverId);
    void setReady(bool ready);
    void setTransmitting(bool transmitting);
    void invalidateReceiver(Vfo vfo);
    void invalidateSession();
    BandRecall* mutableBandRecall(Vfo vfo, availableBands band);

    std::array<Receiver, 2> m_receivers;
    std::array<std::array<BandRecall, std::size(kRadioUiBandOrder)>, 2> m_bandRecall;
    Shared m_shared;
};

} // namespace sdr9700
