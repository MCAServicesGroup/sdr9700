#pragma once

#include <QObject>
#include <QAudioDevice>
#include <QString>
#include <memory>

#include "Types.h"
#include "Vfo.h"

class IRadioBackend;
class VfoModel;
namespace sdr9700
{
class RadioState;
}
class SpectrumScopeModel;
class MeterController;
struct MeterSnapshot;

class RadioModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool transmitting READ isTransmitting NOTIFY transmittingChanged)
    Q_PROPERTY(int smeter READ smeter NOTIFY smeterChanged)

  public:
    explicit RadioModel(QObject* parent = nullptr);
    ~RadioModel() override;

    bool isConnected() const { return m_connected; }
    bool isReady() const { return m_ready; }
    bool isTransmitting() const { return m_transmitting; }
    QString radioName() const { return QStringLiteral("IC-9700"); }
    int smeter() const { return m_smeter; }

    VfoModel* vfo() const { return m_vfo; }
    sdr9700::RadioState* radioState() const { return m_radioState; }
    SpectrumScopeModel* spectrumScope() const { return m_spectrumScope; }
    MeterController* meterController() const { return m_meterController; }
    IRadioBackend* backend() const { return m_backend; }

  public slots:
    void connectToRadio(const QString& host, quint16 port, const QString& user, const QString& pass);
    void disconnectFromRadio();
    void setRxAudioDevice(const QAudioDevice& dev);
    void setTxAudioDevice(const QAudioDevice& dev);
    void stopLocalAudio();
    void setLanModLevel(int level);
    void setTuningStep(int step);
    void selectVfoMode();
    void selectRadioMemory(quint16 group, quint16 channel, Vfo targetVfo);
    void requestRadioMemory(quint16 group, quint16 channel);
    void requestSatelliteMemory(quint16 channel);
    void writeRadioMemory(MemoryType memory);

  signals:
    void connectionChanged(bool connected);
    void readyChanged(bool ready);
    void scopeSyncDegradedChanged(bool degraded);
    void smeterChanged(int value);
    void powerMeterChanged(double watts);
    void swrChanged(double swr);
    void alcChanged(double alc);
    void compressionMeterChanged(double db);
    void voltageMeterChanged(double volts);
    void currentMeterChanged(double amps);
    void meterSnapshotChanged(const MeterSnapshot& snapshot);
    void pttChanged(bool on);
    void transmittingChanged(bool on);
    void connectionStageChanged(ConnectionStage stage, const QString& message);
    void errorOccurred(ErrorCode code, const QString& message);
    void statusMessage(const QString& message, MessageSeverity severity);
    void networkQualityChanged(int rttMs);
    void sessionHeartbeat();
    void radioMemoryReceived(MemoryType memory);
    void audioDataReady(const QByteArray& pcm, int sampleRate, int channelCount, int receiverChannel);
    void spectrumActivity();

  private slots:
    void onBackendConnected();
    void onBackendDisconnected();
    void onBackendError(ErrorCode code, const QString& msg);
    void onBackendReadyChanged(bool ready);
    void onFrequencyChanged(quint64 hz);
    void onModeChanged(const QString& mode);
    void onMeterSnapshotChanged(const MeterSnapshot& snapshot);
    void onPttChanged(bool on);
    void onSpectrumDataReady(const QVector<float>& levels, double start, double end, bool outOfRange);

  private:
    IRadioBackend* m_backend{nullptr};
    VfoModel* m_vfo{nullptr};
    sdr9700::RadioState* m_radioState{nullptr};
    SpectrumScopeModel* m_spectrumScope{nullptr};
    MeterController* m_meterController{nullptr};

    bool m_connected{false};
    bool m_ready{false};
    bool m_transmitting{false};
    int m_smeter{0};
};
