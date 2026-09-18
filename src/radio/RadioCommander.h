#pragma once

#include <QObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QDebug>

#include "Types.h"
#include "UdpHandler.h"
#include "RadioIdentities.h"
#include "CachingQueue.h"

// CI-V address used by SDR9700 as the controller when talking to the IC-9700
// over LAN. Radio replies addressed to this value are treated as responses to
// SDR9700-originated commands.
inline constexpr quint8 compCivAddr = 0xE1;

class RadioCommander : public QObject
{
    Q_OBJECT
    friend class CommanderCodecTest;

  public:
    explicit RadioCommander(QObject* parent = nullptr);
    explicit RadioCommander(quint8 guid[GUIDLEN], QObject* parent = nullptr);
    ~RadioCommander();

  public slots:
    void receiveAudioData(const audioPacket& data);
    void handlePortError(errorType err);
    void handleStatusUpdate(const networkStatus& status);
    void handleNetworkAudioLevels(const networkAudioLevels& levels);
    void changeLatency(const quint16 value);
    void radioSelection(const QList<radio_cap_packet>& radios);
    void radioUsage(quint8 radio, bool admin, quint8 busy, const QString& name, const QString& ip);
    void setCurrentRadio(quint8 radio);

    // RadioCommander owns the common LAN-status and audio signal surface, but
    // it is not a usable radio session by itself. Keeping the session methods
    // abstract prevents a partially initialized base object from accepting
    // commands and merely logging "not implemented" while the caller assumes
    // that work reached the IC-9700.
    virtual void commSetup(quint16 radioCivAddr, UdpConnectionSettings connectionSettings, audioSetup rxAudioSetup,
                           audioSetup txAudioSetup) = 0;
    virtual void closeComm() = 0;

    virtual void setRadioID(quint16 radioID) = 0;
    virtual void setCIVAddr(quint16 civAddr) = 0;

    virtual void handleNewData(const QByteArray& data) = 0;

    virtual void receiveCommand(Funcs func, QVariant value, uchar receiver) = 0;

  signals:
    void lanReady();

    void havePortError(errorType err);
    void haveStatusUpdate(const networkStatus& status);
    void haveSessionHeartbeat();

    void haveNetworkAudioLevels(const networkAudioLevels l);
    void dataForComm(const QByteArray& outData);
    void haveChangeLatency(quint16 value);
    void haveAudioData(audioPacket data);
    void initUdpHandler();
    void requestEnableAudio();
    void haveSetVolume(quint8 level);

    void requestRadioSelection(QList<radio_cap_packet> radios);
    void setRadioUsage(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void selectedRadio(quint8 radio);
    void radioReplyReceived(Funcs func, QVariant value, uchar receiver);

  protected:
    CachingQueue* queue;
    UdpConnectionSettings settings;
    audioSetup rxSetup;
    audioSetup txSetup;

    double getMeterCal(meter_t meter, int value);

    quint8 guid[GUIDLEN] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // This is not an optimistic connection flag. It becomes true only after a
    // syntactically valid CI-V reply addressed to SDR9700's controller address
    // proves that the selected radio is processing commands.
    bool radioPoweredOn = false;

    struct radioCapabilities radioCaps{};
    bool haveRadioCaps = false;

  private:
};
