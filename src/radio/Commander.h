#pragma once

#include "RadioCommander.h"
#include "CivRttEstimator.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <functional>

struct CommanderCorrelationDiagnostics
{
    qsizetype pendingReplies{0};
    qsizetype pendingReplyHighWaterMark{0};
    quint64 pendingReplyOverflows{0};
    quint64 pendingReplyExpirations{0};
    quint64 unattributedDirectedValues{0};
    quint64 ambiguousUnsolicitedFrames{0};
    quint64 acceptedAcknowledgements{0};
    quint64 rejectedAcknowledgements{0};
    quint64 deferredReplyReads{0};
    quint64 coalescedReplyReads{0};
    quint64 droppedReplyReads{0};
    quint64 drainedReplyFrames{0};
    quint64 correlationLostDuringParse{0};
    quint64 mainSMeterRequests{0};
    quint64 mainSMeterReplies{0};
    quint64 mainSMeterTimeouts{0};
    quint64 subSMeterRequests{0};
    quint64 subSMeterReplies{0};
    quint64 subSMeterTimeouts{0};
    qint64 resolvedReplyDrainMs{0};
    qint64 abandonedReplyDrainMs{0};
    qint64 replyTimeoutMs{0};
    quint64 rttSampleCount{0};
};

struct CommanderSchedulerDiagnostics
{
    qsizetype queuedCommands{0};
    qsizetype highWaterMark{0};
    quint64 coalescedCommands{0};
    quint64 droppedCommands{0};
    quint64 dispatchedCommands{0};
    quint64 transmittedFrames{0};
    quint64 scheduledFrames{0};
    quint64 directFrames{0};
};

class Commander : public RadioCommander
{
    Q_OBJECT
    friend class CommanderCodecTest;
    friend class MemoryManagerSmokeTest;

  public:
    explicit Commander(RadioCommander* parent = nullptr);
    explicit Commander(quint8 guid[GUIDLEN], RadioCommander* parent = nullptr);
    ~Commander();
    CommanderCorrelationDiagnostics correlationDiagnostics() const;
    CommanderSchedulerDiagnostics schedulerDiagnostics() const;
    void setOperatorSelectedVfo(vfo_t vfo);
    void scheduleInteractiveAction(Funcs func, uchar receiver, std::function<void()> action);
    void scheduleConfirmatoryAction(Funcs func, uchar receiver, std::function<void()> action);
    void executeReceiverScopedAction(uchar receiver, std::function<void()> action);
    void finishMainSubExchangeConfirmation();

  public slots:
    void commSetup(quint16 radioCivAddr, UdpConnectionSettings settings, audioSetup rxSetup,
                   audioSetup txSetup) override;
    void closeComm() override;

    void setRadioID(quint16 radioID) override;
    void setCIVAddr(quint16 newCivAddr) override;

    void handleNewData(const QByteArray& data) override;

    void receiveCommand(Funcs func, QVariant value, uchar receiver) override;
    void receiveCommandNoReadback(Funcs func, QVariant value, uchar receiver);
    void scheduleMeterRead(Funcs func, uchar receiver);
    void scheduleSMeterRead(uchar receiver);
    void abortSMeterRead(uchar receiver);
    void scheduleStartupRead(Funcs func, uchar receiver);
    void requestMainSubExchange();
    void requestReceiverScopedRead(Funcs func, uchar receiver);
    void discardPendingReplies(Funcs func);
    void readCurrentFrequencyAndMode();
    void sendStandbyWake();
    void setPttActive(bool active);
    void sendDtmfPcm(const QByteArray& pcm);
    void enableAudio();
    void setRxAudioDevice(const QAudioDevice& device);
    void setTxAudioDevice(const QAudioDevice& device);
    bool stopLocalAudio();

  signals:
    void mainSubExchangeDispatched();
    void commandTransmitted(Funcs func, uchar receiver, qint64 replyTimeoutMs);
    void standbyWakeHoldStarted();

  private:
    enum class FrameOrigin
    {
        SolicitedReply,
        UnsolicitedBroadcast
    };

    enum class ReplyParseResult
    {
        NotHandled,
        Parsed,
        Malformed
    };

    void commonSetup();
    void shutdownComm();

    enum class ScheduledCommandClass
    {
        Meter,
        StartupRead,
        ConfirmatoryRead,
        InteractiveSet
    };

    struct ScheduledCommand
    {
        ScheduledCommandClass commandClass{ScheduledCommandClass::StartupRead};
        Funcs func{funcNone};
        uchar receiver{0};
        std::function<void()> action;
        qint64 enqueuedAtMs{0};
    };

    void enqueueScheduledRead(ScheduledCommandClass commandClass, Funcs func, uchar receiver);
    void enqueueScheduledAction(ScheduledCommandClass commandClass, Funcs func, uchar receiver,
                                std::function<void()> action);
    void dispatchNextScheduledCommand();
    void logSchedulerDiagnostics();
    void resetScheduledCommands();

    void parseData(const QByteArray& dataInput);
    void parseCommand(FrameOrigin origin);
    ReplyParseResult parseFrequencyReply(Funcs& func, QVariant& value, uchar& receiver);
    ReplyParseResult parseModeReply(Funcs& func, QVariant& value, uchar& receiver);
    ReplyParseResult parseLevelMeterReply(Funcs func, QVariant& value);
    ReplyParseResult parseFeatureReply(Funcs func, QVariant& value, uchar receiver);
    ReplyParseResult parseScopeReply(Funcs func, QVariant& value, uchar& receiver);
    bool replyPayloadTooShort(Funcs func, int requiredBytes) const;
    bool appendSetCommandValue(Funcs func, const QVariant& value, uchar receiver, const FuncType& command,
                               QByteArray& payload);
    static quint8 bcdHexToUChar(quint8 in);
    quint8 bcdHexToUChar(quint8 hundreds, quint8 tensunits);
    static unsigned int bcdHexToUInt(quint8 hundreds, quint8 tensunits);
    static unsigned int bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits);

    QByteArray bcdEncodeChar(quint8 num);
    QByteArray bcdEncodeInt(quint16 num);
    QByteArray bcdEncodeInt(unsigned int num);
    QByteArray setMemory(MemoryType mem);
    Frequency parseFrequency();
    Frequency parseFrequency(QByteArray data, quint8 lastPosition);

    Frequency parseFreqData(const QByteArray& data, uchar receiver);
    quint64 parseFreqDataToInt(QByteArray data);
    Frequency parseFrequencyRptOffset(QByteArray data);
    bool parseMemory(QVector<MemParserFormat>* memParser, MemoryType* mem);
    void initializeMemoryForParsing(MemoryType& memory) const;
    void parseMemoryField(const MemParserFormat& format, const QByteArray& data, MemoryType& memory);
    QByteArray makeFreqPayloadRptOffset(Frequency freq);
    QByteArray makeFreqPayload(double frequency);
    QByteArray makeFreqPayload(Frequency freq, uchar numchars = 5);
    QByteArray encodeTone(quint16 tone, bool tinv, bool rinv) const;
    QByteArray encodeTone(quint16 tone) const;

    ToneInfo decodeTone(const QByteArray& eTone);
    uchar makeFilterWidth(ushort width, uchar receiver);

    uchar convertNumberToHex(uchar num);

    ModeInfo parseMode(uchar mode, uchar data, uchar filter, uchar receiver = 0, uchar vfo = 0);
    bool parseSpectrum(ScopeData& d, uchar receiver);
    bool decodeSpectrumSequence(quint8& sequence, quint8& sequenceMax) const;
    FuncType getCommand(Funcs func, QByteArray& payload, int value = INT_MIN, uchar receiver = 0);
    void rememberPendingReply(Funcs func, uchar receiver);
    bool pendingReplyReceiver(Funcs func, uchar* receiver);
    bool takePendingReplyReceiver(Funcs func, uchar* receiver);
    void discardExpiredPendingReplies();
    bool deferReplyReadIfBlocked(Funcs func, uchar receiver);
    bool enqueueDeferredReplyRead(Funcs func, uchar receiver);
    void beginReplyFamilyDrain(Funcs func, qint64 durationMs);
    void dispatchDeferredReplyReads();
    void dispatchReceiverScopedRead(Funcs func, uchar receiver);
    void finishReceiverScopedAction();
    void finishSMeterRead();
    void dispatchMainSubExchange();
    bool replyFamilyBlocked(Funcs func) const;
    bool replyFamilyDraining(Funcs func) const;

    QByteArray getLANAddr();
    QByteArray getACCAddr(quint8 ab);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);

    UdpHandler* udp = nullptr;
    QThread* udpHandlerThread = nullptr;

    void determineRadioCaps();
    QByteArray payloadIn;

    QByteArray payloadPrefix;
    QByteArray payloadSuffix;

    quint16 model = 0;
    uchar oldScopeMode{0};

    bool lookingForRadio{false};

    double frequencyMhz{0.0};
    quint16 civAddr{0};
    quint16 incomingCIVAddr{0};

    struct PendingReply
    {
        Funcs func{funcNone};
        uchar receiver{0};
        qint64 createdAtMs{0};
        qint64 expiresAtMs{0};
    };
    QVector<PendingReply> m_pendingReplies;
    struct DeferredReplyRead
    {
        Funcs func{funcNone};
        uchar receiver{0};
    };
    struct ReplyFamilyDrain
    {
        Funcs func{funcNone};
        qint64 untilMs{0};
    };
    QVector<DeferredReplyRead> m_deferredReplyReads;
    bool m_receiverScopedReadActive{false};
    bool m_smeterScopedReadActive{false};
    uchar m_smeterScopedReceiver{0xff};
    vfo_t m_operatorSelectedVfo{vfoMain};
    bool m_mainSubExchangeQueued{false};
    bool m_mainSubExchangeConfirmationPending{false};
    bool m_backgroundSinceMeter{false};
    qint64 m_lastBackgroundDispatchMs{-1};
    QVector<ReplyFamilyDrain> m_replyFamilyDrains;
    QTimer* m_replyDrainTimer{nullptr};
    CivRttEstimator m_rttEstimator;
    QElapsedTimer m_pendingCommandClock;
    CommanderCorrelationDiagnostics m_correlationDiagnostics;
    QVector<ScheduledCommand> m_scheduledCommands;
    QTimer* m_scheduledCommandTimer{nullptr};
    QTimer* m_schedulerDiagnosticsTimer{nullptr};
    CommanderSchedulerDiagnostics m_schedulerDiagnostics;
    CommanderSchedulerDiagnostics m_lastLoggedSchedulerDiagnostics;
    int m_consecutiveMeterDispatches{0};
    int m_consecutiveInteractiveDispatches{0};
    bool m_dispatchingScheduledCommand{false};
    bool m_suppressReadbackForCurrentCommand{false};
    bool m_shutdownComplete{false};

    ScopeData mainScopeData;
    ScopeData subScopeData;
    QElapsedTimer m_scopeAssemblyClocks[2];
    quint8 m_expectedScopeSequences[2]{0, 0};

    // 12 entries cover 10^0–10^11; BCD frequency data is at most 5 bytes (10 nibbles, indices 0–9).
    static constexpr quint64 kPow10[12] = {1,       10,       100,       1000,       10000,       100000,
                                           1000000, 10000000, 100000000, 1000000000, 10000000000, 100000000000};
    static_assert(std::size(kPow10) == 12);

#ifdef DEBUG_PARSE
    quint64 averageParseTime = 0;
    int numParseSamples = 0;
    int lowParse = 9999;
    int highParse = 0;
    QTime lastParseReport = QTime::currentTime();
#endif
};
