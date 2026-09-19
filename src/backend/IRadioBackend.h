#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QByteArray>
#include <QAudioDevice>
#include <QtGlobal>
#include <optional>
#include "Types.h"
#include "Vfo.h"

// A complete, receiver-targeted request assembled from confirmed radio state.
// The backend applies only populated fields and then reads the result back;
// callers must not treat submission as confirmation that the radio accepted it.
struct VfoBandRecallRequest
{
    quint64 frequencyHz{0};
    std::optional<QString> mode;
    std::optional<int> filter;
    std::optional<duplexMode_t> duplexMode;
    std::optional<quint64> repeaterOffsetHz;
    std::optional<rptAccessTxRx_t> toneAccessMode;
    std::optional<ushort> toneFrequency;
    std::optional<ushort> toneSquelchFrequency;
    std::optional<ushort> dtcsCode;
};

class IRadioBackend : public QObject
{
    Q_OBJECT

  public:
    explicit IRadioBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~IRadioBackend() override = default;

  public slots:
    virtual void connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass) = 0;
    virtual void disconnectFromRadio() = 0;

    virtual void setFrequencyHz(quint64 hz) = 0;
    virtual void setMode(const QString& mode) = 0;
    virtual void setFilterWidth(int lowHz, int highHz) = 0;

    virtual void setNrEnabled(bool on) = 0;
    virtual void setNrLevel(int level) = 0; // 1-15
    virtual void setNbEnabled(bool on) = 0;
    virtual void setNbLevel(int level) = 0; // 1-10
    virtual void setPreampEnabled(bool on) = 0;
    virtual void setPreampLevel(int level) = 0;
    virtual void setAttenuatorEnabled(bool on) = 0;
    virtual void setAfGain(int level) = 0;            // 0-255
    virtual void setRfGain(int level) = 0;            // 0-255
    virtual void setSquelch(bool on, int level) = 0;
    virtual void setAgcMode(const QString& mode) = 0; // "fast","mid","slow"
    virtual void setAutoNotch(bool on) = 0;
    virtual void setManualNotch(bool on) = 0;
    virtual void setCompressor(bool on) = 0;
    virtual void setCompressorLevel(int level) = 0; // 0-255
    virtual void setXfcEnabled(bool on) = 0;
    virtual void setRitEnabled(bool on) = 0;
    virtual void setRitOffset(short hz) = 0;
    virtual void setDuplexMode(duplexMode_t mode) = 0;
    virtual void setRepeaterOffsetHz(quint64 hz) = 0;
    virtual void setToneAccessMode(rptAccessTxRx_t mode) = 0;
    virtual void setToneFrequency(ushort tone) = 0;
    virtual void setDtcsCode(ushort code) = 0;

    virtual void setScopeEnabled(bool on) = 0;
    virtual void setScopeSpanHz(quint64 hz) = 0;
    virtual void setScopeMode(int mode) = 0; // 0=center, 1=fixed
    virtual void setScopeVfo(Vfo vfo) = 0;
    virtual void setScopeFixedRangeHz(quint64 startHz, quint64 endHz) = 0;
    virtual void setSpectrumFramesPerSecond(int framesPerSecond) = 0;

    virtual bool setPtt(bool on) = 0;
    virtual void setTxPower(int level) = 0; // 0-255
    virtual void setTuningStep(int step) = 0;
    virtual void setDialLockEnabled(bool on) = 0;
    virtual void selectVfo(Vfo vfo) = 0;
    virtual void exchangeMainSub() = 0;
    virtual void setVfoFrequencyHz(Vfo vfo, quint64 hz) = 0;
    virtual void setVfoMode(Vfo vfo, const QString& mode) = 0;
    virtual void setVfoFilter(Vfo vfo, const QString& mode, int filter) = 0;
    virtual void applyVfoBandRecall(Vfo vfo, const VfoBandRecallRequest& recall) = 0;
    virtual void requestVfoState(Vfo vfo) = 0;
    virtual void setVfoAgcMode(Vfo vfo, const QString& mode) = 0;
    virtual void setVfoAttenuatorEnabled(Vfo vfo, bool on) = 0;
    virtual void setVfoNbEnabled(Vfo vfo, bool on) = 0;
    virtual void setVfoNbLevel(Vfo vfo, int level) = 0;
    virtual void setVfoNotch(Vfo vfo, VfoNotch notch) = 0;
    virtual void setVfoNrEnabled(Vfo vfo, bool on) = 0;
    virtual void setVfoNrLevel(Vfo vfo, int level) = 0;
    virtual void setVfoPreampLevel(Vfo vfo, int level) = 0;
    virtual void setVfoRfGain(Vfo vfo, int level) = 0;
    virtual void setVfoSquelch(Vfo vfo, int level) = 0;
    // Returns true only when the backend started the asynchronous transition.
    // A true result is acceptance, not radio confirmation; callers still wait
    // for radioValueConfirmed(funcVFODualWatch, ...).
    virtual bool setDualWatchEnabled(bool on) = 0;

    virtual void pollFrequency() = 0;
    virtual void selectVfoMode() = 0;
    virtual void selectRadioMemory(quint16 group, quint16 channel, Vfo targetVfo) = 0;
    virtual void requestRadioMemory(quint16 group, quint16 channel) = 0;
    virtual void requestSatelliteMemory(quint16 channel) = 0;
    virtual void writeRadioMemory(MemoryType memory) = 0;

    virtual void setRxAudioDevice(const QAudioDevice& dev) { Q_UNUSED(dev) }
    virtual void setTxAudioDevice(const QAudioDevice& dev) { Q_UNUSED(dev) }
    virtual void stopLocalAudio() {}
    virtual void setLanModLevel(int level) { Q_UNUSED(level) } // 0-255
    virtual void sendDtmf(const QString& digits) { Q_UNUSED(digits) }

  signals:
    void connected();
    void disconnected();
    void readyChanged(bool ready);
    void scopeSyncDegradedChanged(bool degraded);
    void connectionStageChanged(ConnectionStage stage, const QString& message);
    void errorOccurred(ErrorCode code, const QString& message);
    void statusMessage(const QString& message, MessageSeverity severity);

    void frequencyChanged(quint64 hz);
    void modeChanged(const QString& mode);
    void filterChanged(int lowHz, int highHz);
    void smeterChanged(int smeter);           // 0-9+(OVF) mapped to 0-255
    void powerMeterChanged(double watts);     // calibrated RF output power in watts
    void swrChanged(double swr);              // calibrated SWR ratio (e.g. 1.0 = perfect)
    void alcChanged(double alc);              // calibrated ALC meter value (0.0-2.0, 1.0 = ALC limit)
    void compressionMeterChanged(double db);  // calibrated compression meter value in dB
    void voltageMeterChanged(double volts);   // calibrated drain voltage
    void currentMeterChanged(double amps);    // calibrated drain current
    void agcModeChanged(const QString& mode); // "fast","mid","slow"
    void autoNotchChanged(bool on);
    void manualNotchChanged(bool on);
    void compressorChanged(bool on);
    void compressorLevelChanged(int level);
    void xfcChanged(bool on);
    void ritEnabledChanged(bool on);
    void ritOffsetChanged(short hz);
    void pttChanged(bool on);
    // Operator intent accepted by the backend. This precedes radio readback
    // and must not be presented as confirmed transmit state.
    void pttRequestAccepted(bool on);
    void nrChanged(bool on);
    void nrLevelChanged(int level);
    void nbChanged(bool on);
    void nbLevelChanged(int level);
    void preampChanged(bool on);
    void preampLevelChanged(int level);
    void attenuatorChanged(bool on);
    void rfGainChanged(int level);
    void squelchChanged(bool on, int level);
    void txPowerChanged(int level);
    void duplexModeChanged(duplexMode_t mode);
    void repeaterOffsetChanged(quint64 hz);
    void toneAccessModeChanged(rptAccessTxRx_t mode);
    void toneFrequencyChanged(ushort tone);
    void dtcsCodeChanged(ushort code);
    void radioMemoryReceived(MemoryType memory);
    void radioValueUpdated(Funcs func, QVariant value, uchar receiver);
    // Emitted for every parsed, receiver-correlated radio value, including a
    // confirmation whose payload equals the value already held by
    // CachingQueue. State projections need the confirmation stream because
    // they may intentionally invalidate one receiver after a band/context
    // transition while the lower protocol cache still retains its last value.
    void radioValueConfirmed(Funcs func, QVariant value, uchar receiver);
    void dualWatchTransitionPendingChanged(bool pending);
    void mainSubExchangeCompleted();
    void mainSubExchangeFailed();

    void spectrumDataReady(const QVector<float>& levels, double startMhz, double endMhz, bool outOfRange);

    void networkQualityChanged(int rttMs);
    void sessionHeartbeat();

    void txAudioLevelChanged(int peak, int rms);

    void audioDataReady(const QByteArray& pcm, int sampleRate, int channelCount);
};
