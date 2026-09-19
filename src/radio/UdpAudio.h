#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QByteArray>
#include <atomic>
#include <QVector>
#include <QMap>
#include <QUuid>

#include <QtEndian>

#include <QBuffer>
#include <QQueue>
#include <QThread>
#include <QElapsedTimer>

#include <QDebug>

#include "PacketTypes.h"

#include "UdpBase.h"

#include "AudioHandler.h"
#include "RxAudioStartPolicy.h"
#include "TxAudioMeterPolicy.h"

class UdpAudio : public UdpBase
{
    Q_OBJECT

  public:
    UdpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup,
             audioSetup txSetup, QUdpSocket* boundSocket = nullptr);
    ~UdpAudio();

    int audioLatency = 0;

  signals:
    void ready();
    void haveAudioData(audioPacket data);

    void setupTxAudio(audioSetup setup);
    void setupRxAudio(audioSetup setup);

    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                      bool over);
    // Transmit meter publication. The receive path keeps the legacy quantised
    // haveRxLevels route; the capture path carries the typed measurement block
    // with transport health as separate arguments.
    void haveTxMeter(const sdr9700::audio::TxAudioMeterBlock& block, quint16 configuredLatency, quint16 measuredLatency,
                     bool under, bool over);

  public slots:
    void enableAudio();
    void setRxAudioDevice(const QAudioDevice& device);
    void setTxAudioDevice(const QAudioDevice& device);
    void stopLocalAudio();
    void changeLatency(quint16 value);
    void setVolume(quint8 value);
    void setTxActive(bool active);
    void getRxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getTxMeter(const sdr9700::audio::TxAudioMeterBlock& block, quint16 configuredLatency, quint16 measuredLatency,
                    bool under, bool over);
    void receiveAudioData(audioPacket audio);
    void queueDtmfPcm(const QByteArray& pcm);

  private slots:
    void onRxAudioInitFailed();
    void onTxAudioInitFailed();
    void sendNextTxAudioFrame();
    void sendNextDtmfFrame();

  private:
    void sendAudioBuffer(const QByteArray& data);
    qint64 nextTxAudioFramesDue(QElapsedTimer& clock, qint64& framesSent);
    void dataReceived();
    void startAudio();
    void startTxAudio();
    void stopTxAudio();
    void stopAudioWorker(AudioHandlerBase*& handler, QThread*& workerThread, const char* name);
    audioSetup rxSetup;
    audioSetup txSetup;

    uint16_t sendAudioSeq = 0;

    AudioHandlerBase* rxaudio = nullptr;
    QThread* rxAudioThread = nullptr;

    AudioHandlerBase* txaudio = nullptr;
    QThread* txAudioThread = nullptr;

    QTimer* txAudioTimer = nullptr;
    bool enableTx = true;

    std::atomic_bool m_txActive{false};
    QQueue<QByteArray> m_txAudioQueue;
    QByteArray m_dtmfPcm;
    QByteArray m_dtmfFrame;
    QByteArray m_txSilenceFrame;
    qsizetype m_dtmfPcmOffset{0};
    QTimer* m_dtmfTimer{nullptr};
    bool m_dtmfTimerActive{false};
    QElapsedTimer m_txPumpClock;
    qint64 m_txFramesSent{0};
    QElapsedTimer m_dtmfPumpClock;
    qint64 m_dtmfFramesSent{0};
    int m_txSilencePacketBytes = 640; // 20 ms, 16 kHz, mono 16-bit PCM.

    bool m_audioReady = false;
    sdr9700::audio::RxAudioStartPolicy m_rxAudioStartPolicy;
    bool m_transportReadyEmitted = false;

    QElapsedTimer audioClock;
    bool audioHaveBase = false;
    quint32 audioBaseSeq = 0;
    qint64 audioBaseNs = 0;
    int audioPktMs = 20; // IC-9700 LAN audio frames are currently handled as 20 ms packets.
    int latencyCounter = 0;
    int rxPacketDiagnosticsCount = 0;
};
