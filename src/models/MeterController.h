#pragma once

#include "TxAudioMeterPolicy.h"

#include <QElapsedTimer>
#include <QObject>

struct MeterSnapshot
{
    int sMeter{0};
    bool sMeterValid{false};
    double powerWatts{0.0};
    bool powerValid{false};
    double swr{1.0};
    bool swrValid{false};
    double alc{0.0};
    bool alcValid{false};
    double compressionDb{0.0};
    bool compressionValid{false};
    double voltageVolts{0.0};
    bool voltageValid{false};
    double currentAmps{0.0};
    bool currentValid{false};
    // Local processed-input meter. These describe the operator's capture chain,
    // not radio drive, and are deliberately independent of PTT state.
    sdr9700::audio::TxAudioMeterState txAudioState{sdr9700::audio::TxAudioMeterState::Invalid};
    double txAudioRmsDb{sdr9700::audio::kMeterDisplayFloorDb};
    double txAudioPeakDb{sdr9700::audio::kMeterDisplayFloorDb};
    quint32 txAudioFullScaleCount{0};
};

class QTimer;

class MeterController : public QObject
{
    Q_OBJECT

  public:
    explicit MeterController(QObject* parent = nullptr);

  public slots:
    void reset();
    void resetReceiveMeter();
    void resetTransmitMeters();
    void setSMeter(int value);
    void setPowerMeter(double watts);
    void setSwr(double swr);
    void setAlc(double alc);
    void setCompressionMeter(double db);
    void setVoltageMeter(double volts);
    void setCurrentMeter(double amps);
    void setTransmitAudioMeter(const sdr9700::audio::TxAudioMeterBlock& block);
    // Invalidates the local meter. Call on disconnect, input failure, converter
    // failure, or audio-device restart -- never on a PTT or DTMF transition.
    void resetTransmitAudioMeter();

  signals:
    void snapshotChanged(const MeterSnapshot& snapshot);

  private:
    void scheduleFlush();
    void flush();
    void advanceTransmitAudioMeter();
    bool transmitAudioMeterSettling() const;

    MeterSnapshot m_snapshot;
    QTimer* m_flushTimer{nullptr};
    bool m_dirty{false};
    sdr9700::audio::TxAudioMeterPresentation m_txPresentation;
    sdr9700::audio::TxAudioMeterBlock m_pendingTxBlock;
    QElapsedTimer m_txClock;
};

Q_DECLARE_METATYPE(MeterSnapshot)
