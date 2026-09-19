#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUuid>

#include <QtEndian>

#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "PacketTypes.h"
#include "TxAudioMeterPolicy.h"
#include "AudioHandler.h"
#include "UdpBase.h"
#include "UdpCivData.h"
#include "UdpAudio.h"
#include "RadioSessionWatchdog.h"
#include "RadioSessionOwnership.h"
#include "RadioSessionCorrelation.h"
#include "RadioSessionRecoveryStore.h"
#include "RetainedSessionRemovalPolicy.h"

class UdpHandler : public UdpBase
{
    Q_OBJECT

  public:
    static constexpr int audioLevelBufferSize = 4;

    UdpHandler(UdpConnectionSettings settings, audioSetup rxAudio, audioSetup txAudio);
    ~UdpHandler();

    bool streamOpened = false;

    UdpCivData* civ = nullptr;
    UdpAudio* audio = nullptr;

    quint8 numRadios{0};
    QList<radio_cap_packet> radios;

  public slots:
    void enableAudio();
    void setRxAudioDevice(const QAudioDevice& device);
    void setTxAudioDevice(const QAudioDevice& device);
    void stopLocalAudio();
    void receiveDataFromUserToRadio(QByteArray data);
    void receiveFromCivStream(const QByteArray& data);
    void receiveAudioData(const audioPacket& data);
    void changeLatency(quint16 value);
    void setVolume(quint8 value);
    void init();
    void shutdown();
    void setCurrentRadio(quint8 radio);
    void getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under,
                     bool over);
    void getTxMeter(const sdr9700::audio::TxAudioMeterBlock& block, quint16 configuredLatency, quint16 measuredLatency,
                    bool under, bool over);
    void setPttActive(bool active);
    void queueDtmfPcm(const QByteArray& pcm);
    void beginStandbyWakeHold();

  signals:
    void haveDataFromPort(QByteArray data);
    void haveAudioData(audioPacket data);
    void haveNetworkError(errorType);
    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveNetworkStatus(networkStatus);
    void haveNetworkAudioLevels(networkAudioLevels);
    void haveTxAudioMeter(const sdr9700::audio::TxAudioMeterBlock& block);
    void requestRadioSelection(QList<radio_cap_packet> radios);
    void setRadioUsage(quint8, bool admin, quint8 busy, QString name, QString mac);
    void streamReady();
    void sessionHeartbeat();

  private:
    void sendAreYouThere();
    void monitorSessionHealth();

    void dataReceived();

    void sendRequestStream();
    void sendLogin();
    QByteArray createTokenPacket(uint8_t magic);
    void sendToken(uint8_t magic);
    bool requestRetainedSessionRecovery(const QString& ownerName);
    void closeStreams();
    bool releaseAuthenticationToken(bool waitForAcknowledgement);
    void beginStreamShutdown();
    void waitForStreamShutdownSettle(int timeoutMs);
    bool reserveStreamPorts();
    void startMediaStreamsWhenReady();
    void reclaimPredecessorTransports(const sdr9700::RadioSessionRecoveryRecord& predecessor);
    void beginPredecessorTokenRemoval(const sdr9700::RadioSessionRecoveryRecord& predecessor);
    void sendPredecessorTokenRemovalAttempt();
    void completePredecessorTokenRemoval();

    bool gotA8ReplyID = false;
    bool gotAuthOK = false;

    bool sentPacketLogin = false;
    bool sentPacketConnect = false;

    bool radioInUse = false;
    bool m_shuttingDown = false;
    bool m_disconnectStatusReceived = false;
    bool m_tokenRemovalAcknowledged = false;
    bool m_staleSessionReclaimInProgress = false;
    bool m_retainedTokenResetAttempted = false;
    sdr9700::RadioSessionOwnership m_sessionOwnership;
    sdr9700::RadioSessionRequest m_loginRequest;
    sdr9700::RadioSessionRequest m_tokenRenewalRequest;
    sdr9700::RadioSessionRequest m_tokenRemovalRequest;
    sdr9700::RadioSessionRequest m_streamRequest;
    bool m_civStreamReady = false;
    bool m_civProbeSent = false;
    bool m_civDataObserved = false;
    bool m_preReadinessPayloadLogged = false;
    bool m_civTransportReady = false;
    bool m_audioTransportReady = false;
    bool m_healthFailureReported = false;
    bool m_audioSilenceReported = false;
    bool m_currentStreamGrantObserved = false;
    bool m_predecessorTransportsReclaimed = false;
    int m_staleSessionReclaimAttempts = 0;
    QString m_predecessorOwnerName;
    QByteArray m_predecessorRemovalPacket;
    sdr9700::RetainedSessionRemovalPolicy m_predecessorRemovalPolicy;
    RadioSessionWatchdog m_sessionWatchdog;

    quint16 controlPort;
    quint16 civPort;
    quint16 audioPort;

    quint16 civLocalPort;
    quint16 audioLocalPort;
    QUdpSocket* civPortReservation = nullptr;
    QUdpSocket* audioPortReservation = nullptr;

    audioSetup rxSetup;
    audioSetup txSetup;

    quint16 reauthInterval = 60000;
    QString devName;
    QString compName;
    QString audioType;
    bool m_foreignSessionReported{false};
    QString username;
    quint16 tokRequest{0};
    quint32 token{0};
    quint8 macaddress[6]{};
    quint8 guid[GUIDLEN]{};
    bool useGuid = false;
    QByteArray usernameEncoded;
    QByteArray passwordEncoded;

    QTimer* tokenTimer = nullptr;
    QTimer* civReadinessTimer = nullptr;
    QTimer* predecessorRemovalTimer = nullptr;

    quint8 civId = 0;
    quint16 rxSampleRates = 0;
    quint16 txSampleRates = 0;
    networkStatus status;

    quint8 audioLevelsRxPeak[audioLevelBufferSize];
    quint8 audioLevelsRxRMS[audioLevelBufferSize];
    quint8 audioLevelsRxPosition = 0;
    static quint8 findMean(const quint8* data);
    static quint8 findMax(const quint8* data);
};
