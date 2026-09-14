// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtGlobal>
#include <QColor>
#include <cstdint>

enum ValueType
{
    typeNone = 0,
    typeFloat,
    typeFloatDiv,
    typeFloatDiv5,
    typeUChar,
    typeUShort,
    typeChar,
    typeShort,
    typeBinary,
    typeFreq,
    typeMode,
    typeLevel,
    typeVFO,
    typeString,
    typedB,
    typeSplitVFO,
    typeVFOInfo,
    typeSWR,
    typeDouble
};

enum connectionStatus_t
{
    connDisconnected,
    connConnecting,
    connConnected
};

// ConnectionStage is a machine-readable lifecycle contract shared by the
// radio stack and GUI. User-facing text is deliberately kept out of control
// flow so wording changes and future translation cannot alter reconnect or
// readiness behavior.
enum class ConnectionStage
{
    Unchanged,
    Disconnected,
    Connecting,
    WaitingForRadio,
    OpeningStreams,
    SyncingRadioState,
    Ready,
    Reconnecting,
    Disconnecting,
    Failed,
};

enum class MessageSeverity
{
    Info,
    Warning,
    Error,
};

enum underlay_t
{
    underlayNone,
    underlayPeakHold,
    underlayPeakBuffer,
    underlayAverageBuffer
};

enum connectionType_t
{
    connectionLAN
};

enum meter_t
{
    meterNone = 0,
    meterS,
    meterCenter,
    meterSWR,
    meterPower,
    meterALC,
    meterComp,
    meterVoltage,
    meterCurrent,
    meterRxdB,
    meterTxMod,
    meterRxAudio,
    meterAudio,
    meterLatency,
    meterdBu,
    meterdBuEMF,
    meterdBm,
    meterSubS,
    meterUnknown
};

extern const QString meterString[meterUnknown + 1];
enum radioMode_t
{
    modeLSB = 0, // 0
    modeUSB,     // 1
    modeAM,      // 2
    modeCW,      // 3
    modeRTTY,    // 4
    modeFM,      // 5
    modeCW_R,    // 6
    modeRTTY_R,  // 7
    modePSK,     // 8
    modePSK_R,   // 9
    modeLSB_D,   // 10
    modeUSB_D,   // 11
    modeDV,      // 12
    modeATV,     // 13
    modeDD,      // 14
    modeWFM,     // 15
    modeS_AMD,   // 16
    modeS_AML,   // 17
    modeS_AMU,   // 18
    modeUnknown = 0xff
};

enum selVFO_t
{
    activeVFO = 0,
    inactiveVFO = 1
};

enum vfo_t
{
    vfoA = 0,
    vfoB = 1,
    vfoMain = 0xD0,
    vfoSub = 0xD1,
    vfoCurrent = 0xfd,
    vfoMem = 0xfe,
    vfoUnknown = 0xff
};

enum duplexMode_t
{
    dmSplitOff = 0x00,
    dmSplitOn = 0x01,
    dmSimplex = 0x10,
    dmDupMinus = 0x11,
    dmDupPlus = 0x12,
    dmDupRPS = 0x13,
    dmDupAutoOn = 0x26,
    dmDupAutoOff = 0x36
};

// Repeater access mode names use transmit/receive order: T=tone, D=DTCS, N=none.
enum rptAccessTxRx_t
{
    ratrNN = 0x00,
    ratrTN = 0x01, // "TONE" (T only)
    ratrNT = 0x02, // "TSQL" (R only)
    ratrDD = 0x03, // "DTCS" (TR)
    ratrDN = 0x06, // "DTCS(T)"
    ratrTD = 0x07, // "TONE(T) / TSQL(R)"
    ratrDT = 0x08, // "DTCS(T) / TSQL(R)"
    ratrTT = 0x09, // "TONE(T) / TSQL(R)"
    ratrTONEoff,
    ratrTONEon,
    ratrTSQLoff,
    ratrTSQLon
};

// Modes where toneValue holds a DTCS code; all other non-ratrNN modes use a tone frequency.
inline bool isDtcsToneMode(rptAccessTxRx_t m)
{
    return m == ratrDN || m == ratrDD || m == ratrDT;
}

enum pttType_t
{
    pttCIV
};

enum vfoModeType_t
{
    vfoModeVfo,
    vfoModeMem,
    vfoModeSat
};

enum manufacturersType_t
{
    manufIcom = 0
};

struct LpfHpf
{
    LpfHpf() : lpf(0), hpf(0) {}
    LpfHpf(ushort lpf, ushort hpf) : lpf(lpf), hpf(hpf) {}
    ushort lpf = 0;
    ushort hpf = 0;
};

struct RptrAccessData
{
    rptAccessTxRx_t accessMode = ratrNN;
    bool useSecondaryVFO = false;
    bool turnOffTone = false;
    bool turnOffTSQL = false;
    bool usingSequence = false;
    int sequence = 0;
};

struct ModeInfo
{
    ModeInfo()
        : mk(modeUnknown), reg(0xff), filter(0xff), VFO(activeVFO), data(0xff), name(""), bwMin(0), bwMax(0), pass(0)
    {
    }
    ModeInfo(radioMode_t mk, quint8 reg, QString name, int bwMin, int bwMax)
        : mk(mk), reg(reg), filter(0xff), VFO(activeVFO), data(0xff), name(name), bwMin(bwMin), bwMax(bwMax), pass(0)
    {
    }
    radioMode_t mk;
    quint8 reg;
    quint8 filter;
    selVFO_t VFO;
    quint8 data;
    QString name;
    int bwMin;
    int bwMax;
    int pass;
};

struct AntennaInfo
{
    quint8 antenna;
    bool rx;
};

struct ScopeData
{
    bool valid{false};
    QByteArray data;
    uchar receiver{0};
    uchar mode{0};
    uchar fixedEdge{0};
    bool oor{false};
    double startFreq{0.0};
    double endFreq{0.0};
};

struct ToneInfo
{
    ToneInfo() : tone(670), name("67.0"), tinv(false), rinv(false), useSecondaryVFO(false) {}
    explicit ToneInfo(short tone) : tone(tone), name(""), tinv(false), rinv(false), useSecondaryVFO(false) {}
    ToneInfo(short tone, QString name) : tone(tone), name(name), tinv(false), rinv(false), useSecondaryVFO(false) {}
    ToneInfo(short tone, QString name, bool tinv, bool rinv, bool useSecondaryVFO)
        : tone(tone), name(name), tinv(tinv), rinv(rinv), useSecondaryVFO(useSecondaryVFO)
    {
    }
    ushort tone;
    QString name;
    bool tinv;
    bool rinv;
    bool useSecondaryVFO;
};

enum breakIn_t
{
    brkinOff = 0x00,
    brkinSemi = 0x01,
    brkinFull = 0x02
};

struct Frequency
{
    Frequency() : Hz(0), MHzDouble(0.0), VFO(activeVFO) {}
    Frequency(quint64 Hz, double MHzDouble, selVFO_t VFO) : Hz(Hz), MHzDouble(MHzDouble), VFO(VFO) {}
    quint64 Hz;
    double MHzDouble;
    selVFO_t VFO;
};

struct DateKind
{
    uint16_t year;
    quint8 month;
    quint8 day;
};

struct TimeKind
{
    quint8 hours;
    quint8 minutes;
    bool isMinus;
};

struct MeterKind
{
    double value;
    meter_t type;
};

// Funcs and funcString MUST be updated at the same time, missing commas concatenate strings!
enum Funcs
{
    funcNone,
    // CI-V group 00-0f: VFO information.
    funcSep,
    funcFreqTR,
    funcModeTR,
    funcBandEdgeFreq,
    funcFreqGet,
    funcModeGet,
    funcFreqSet,
    funcModeSet,
    funcVFOSwapAB,
    funcVFOSwapMS,
    funcVFOEqualAB,
    funcVFOEqualMS,
    funcVFODualWatchOff,
    funcVFODualWatchOn,
    funcVFODualWatch,
    funcVFOMainSelect,
    funcVFOSubSelect,
    funcVFOASelect,
    funcVFOBSelect,
    funcVFOBandMS,
    funcMemoryMode,
    funcMemoryWrite,
    funcMemoryToVFO,
    funcMemoryClear,
    funcReadFreqOffset,
    funcSendFreqOffset,
    funcScanning,
    funcVFOModeSelect,
    funcSplitStatus,

    // CI-V group 10-13: basic settings.
    funcSepA,
    funcTuningStep,
    funcAttenuator,
    funcAntenna,
    funcSpeech,

    // CI-V group 14: level controls.
    funcSepB,
    funcAfGain,
    funcRfGain,
    funcSquelch,
    funcAPFLevel,
    funcNRLevel,
    funcIFShift,
    funcPBTInner,
    funcPBTOuter,
    funcCwPitch,
    funcRFPower,
    funcKeySpeed,
    funcNotchFilter,
    funcCompressorLevel,
    funcBreakInDelay,
    funcNBLevel,
    funcDigiSelShift,
    funcDriveGain,
    funcMonitorGain,
    funcVoxGain,
    funcAntiVoxGain,
    funcBackLightLevel,

    // CI-V group 15: meter readings.
    funcSepC,
    funcSMeterSqlStatus,
    funcSMeter,
    funcAbsoluteMeter,
    funcMeterType,
    funcCenterMeter,
    funcVariousSql,
    funcPowerMeter,
    funcSWRMeter,
    funcALCMeter,
    funcCompMeter,
    funcVdMeter,
    funcIdMeter,

    // CI-V group 16: function toggles.
    funcSepD,
    funcPreamp,
    funcAGC,
    funcNoiseBlanker,
    funcAudioPeakFilter,
    funcNoiseReduction,
    funcAutoNotch,
    funcRepeaterTone,
    funcRepeaterTSQL,
    funcRepeaterDTCS,
    funcRepeaterCSQL,
    funcCompressor,
    funcMonitor,
    funcVox,
    funcBreakIn,
    funcManualNotch,
    funcDigiSel,
    funcTwinPeakFilter,
    funcDialLock,
    funcRXAntenna,
    funcManualNotchWidth,
    funcSSBTXBandwidth,
    funcMainSubTracking,
    funcSatelliteMode,
    funcDSQLSetting,
    funcToneSquelchType,
    funcIPPlus,
    funcRoofingFilter,
    funcFilterShape,

    // CI-V group 17-19: CW, power, and transceiver ID.
    funcSepE,
    funcSendCW,
    funcPowerControl,
    funcTransceiverId,
    // CI-V group 1A00-1A04: memory and filter settings.
    funcSepF,
    funcMemoryContents,
    funcBandStackReg,
    funcFilterWidth,
    funcAGCTimeConstant,

    // CI-V group 1A0500: set-mode options.
    funcSepG,
    // Audio tone and beep controls.
    funcSSBRXHPFLPF,
    funcSSBRXBass,
    funcSSBRXTreble,
    funcAMRXHPFLPF,
    funcAMRXBass,
    funcAMRXTreble,
    funcFMRXHPFLPF,
    funcFMRXBass,
    funcFMRXTreble,
    funcCWRXHPFLPF,
    funcRTTYRXHPFLPF,
    funcSSBTXBass,
    funcSSBTXTreble,
    funcAMTXBass,
    funcAMTXTreble,
    funcFMTXBass,
    funcFMTXTreble,
    funcBeepLevel,
    funcBeepLevelLimit,
    funcBeepConfirmation,
    funcBandEdgeBeep,
    funcBeepMain,
    funcBeepSub,

    funcRFSQLControl,
    funcTXDelayHF,
    funcTXDelay50m,
    funcTimeOutTimer,
    funcTimeOutCIV,

    funcQuickDualWatch,
    funcQuickSplit,
    funcAutoRepeater,
    funcTransverter,
    funcTransverterOffset,
    funcLockFunction,
    funcREFAdjust,
    funcREFAdjustFine,
    funcACCAModLevel,
    funcACCBModLevel,
    funcUSBModLevel,
    funcLANModLevel,
    funcSPDIFModLevel,
    funcDATAOffMod,
    funcDATA1Mod,
    funcDATA2Mod,
    funcDATA3Mod,
    funcCIVTransceive,
    funcTime,
    funcDate,
    funcUTCOffset,
    funcCLOCK2,
    funcCLOCK2UTCOffset,
    funcCLOCK2Name,
    funcDashRatio,
    funcScanSpeed,
    funcScanResume,
    funcRecorderMode,
    funcRecorderTX,
    funcRecorderRX,
    funcRecorderSplit,
    funcRecorderPTTAuto,
    funcRecorderPreRec,
    funcRXAntConnector,
    funcAntennaSelectMode,
    funcNBDepth,
    funcNBWidth,
    funcVOXDelay,
    funcVOXVoiceDelay,
    funcAPFType,
    funcAPFTypeLevel,
    funcPSKTone,
    funcRTTYMarkTone,
    funcToneFreq,
    funcTSQLFreq,
    funcDTCSCode,
    funcCSQLCode,
    funcTXFreqMon,
    funcReadUserTXFreqs,
    funcCIVOutput,
    funcVoiceTXLevel,
    funcMainSubPrefix,
    funcAFCSetting,

    funcGPSTXMode,
    funcSatelliteMemory,
    funcGPSPosition,
    funcMemoryGroup,

    funcSepG2,
    // CI-V group 1A0501: spectrum display settings.
    funcMonitorSignalWidth,
    funcScopeAveraging,
    funcSpectrumFillType,
    funcSpectrumFillColor,
    funcSpectrumLineColor,
    funcSpectrumPeakColor,
    funcWaterfallSet,
    funcWaterfallSpeed,
    funcWaterfallHeight,
    funcWaterfallPeakLevel,
    funcMarkerAutoHide,

    funcSepG3,
    // CI-V group 1A0502: scope edge settings.

    funcScopeEdge1a,
    funcScopeEdge2a,
    funcScopeEdge3a,
    funcScopeEdge4a,
    funcScopeEdge1b,
    funcScopeEdge2b,
    funcScopeEdge3b,
    funcScopeEdge4b,
    funcScopeEdge1c,
    funcScopeEdge2c,
    funcScopeEdge3c,
    funcScopeEdge4c,
    funcScopeEdge1d,
    funcScopeEdge2d,
    funcScopeEdge3d,
    funcScopeEdge4d,
    funcScopeEdge1e,
    funcScopeEdge2e,
    funcScopeEdge3e,
    funcScopeEdge4e,
    funcScopeEdge1f,
    funcScopeEdge2f,
    funcScopeEdge3f,
    funcScopeEdge4f,
    funcScopeEdge1g,
    funcScopeEdge2g,
    funcScopeEdge3g,
    funcScopeEdge4g,
    funcScopeEdge1h,
    funcScopeEdge2h,
    funcScopeEdge3h,
    funcScopeEdge4h,
    funcScopeEdge1i,
    funcScopeEdge2i,
    funcScopeEdge3i,
    funcScopeEdge4i,
    funcScopeEdge1j,
    funcScopeEdge2j,
    funcScopeEdge3j,
    funcScopeEdge4j,
    funcScopeEdge1k,
    funcScopeEdge2k,
    funcScopeEdge3k,
    funcScopeEdge4k,
    funcScopeEdge1l,
    funcScopeEdge2l,
    funcScopeEdge3l,
    funcScopeEdge4l,
    funcScopeEdge1m,
    funcScopeEdge2m,
    funcScopeEdge3m,
    funcScopeEdge4m,
    funcScopeEdge1n,
    funcScopeEdge2n,
    funcScopeEdge3n,
    funcScopeEdge4n,
    funcScopeEdge1o,
    funcScopeEdge2o,
    funcScopeEdge3o,
    funcScopeEdge4o,
    funcScopeEdge1p,
    funcScopeEdge2p,
    funcScopeEdge3p,
    funcScopeEdge4p,
    funcScopeEdge1q,
    funcScopeEdge2q,
    funcScopeEdge3q,
    funcScopeEdge4q,
    funcScopeEdge1r,
    funcScopeEdge2r,
    funcScopeEdge3r,
    funcScopeEdge4r,
    funcScopeEdge1s,
    funcScopeEdge2s,
    funcScopeEdge3s,
    funcScopeEdge4s,

    // CI-V group 1A06-1A0A: data mode and AF mute.
    funcSepH,
    funcDataModeWithFilter,
    funcAFMute,
    funcOverflowStatus,

    // CI-V group 1C: transceiver status.
    funcSepI,
    funcTransceiverStatus,
    funcTunerStatus,
    funcXFCStatus,
    funcTXFreq,

    // CI-V group 1E: TX frequency limits.
    funcSepJ,
    funcAvailableTXFreq,
    funcTXBandEdgeFreq,
    funcNumUserTXBandEdgeFreq,
    funcUserTxBandEdge,

    // CI-V group 21: RIT state.
    funcSepK,
    funcRitFreq,
    funcRitStatus,
    funcRitTXStatus,

    // CI-V group 25/26: selected and unselected VFO state.
    funcSepL,
    funcSelectedFreq,
    funcUnselectedFreq,
    funcSelectedMode,
    funcUnselectedMode,
    funcFreq,
    funcMode,

    // CI-V group 27: scope control.
    funcSepM,
    funcScopeWaveData,
    funcScopeOnOff,
    funcScopeDataOutput,
    funcScopeMainSub,
    funcScopeSingleDual,
    funcScopeMode,
    funcScopeSpan,
    funcScopeEdge,
    funcScopeHold,
    funcScopeRef,
    funcScopeSpeed,
    funcScopeVBW,
    funcScopeRBW,
    funcScopCenterFreq,
    funcScopeDuringTX,
    funcScopeCenterType,
    funcScopeFixedEdgeFreq,
    // CI-V group 28: voice transmit.
    funcSepN,
    funcVoiceTX,

    // CI-V response codes.
    funcSepO,
    funcFA,
    funcFB,

    // sdr9700 internal function entries.
    funcSelectVFO,
    funcSeparator,
    funcLCDWaterfall,
    funcLCDSpectrum,
    funcLCDNothing,
    funcPageUp,
    funcPageDown,
    funcVFOFrequency,
    funcVFOMode,
    funcRadioControlFunction,
    funcRadioControlLevel,
    funcRadioControlParam,
    funcRXAudio,
    funcTXAudio,
    // This MUST be the last defined func.
    funcLastFunc
};

// Any changes to these strings WILL break compiled radio capability definitions; add new entries at the end.
// Missing commas concatenate adjacent strings. Definition is in Types.cpp.
extern const QString funcString[funcLastFunc];

struct SpanType
{
    SpanType() : num(0), name(), freq(0) {}
    SpanType(int num, QString name, unsigned int freq) : num(num), name(name), freq(freq) {}
    int num;
    QString name;
    unsigned int freq;
};

struct FuncType
{
    FuncType()
        : cmd(funcNone),
          name("None"),
          data(),
          minVal(0),
          maxVal(0),
          padr(false),
          cmd29(false),
          getCmd(false),
          setCmd(false),
          bytes(0),
          admin(false)
    {
    }
    FuncType(Funcs cmd, QString name, QByteArray data, int minVal, int maxVal, bool padr, bool cmd29, bool getCmd,
             bool setCmd, uchar bytes, bool admin)
        : cmd(cmd),
          name(name),
          data(data),
          minVal(minVal),
          maxVal(maxVal),
          padr(padr),
          cmd29(cmd29),
          getCmd(getCmd),
          setCmd(setCmd),
          bytes(bytes),
          admin(admin)
    {
    }
    Funcs cmd;
    QString name;
    QByteArray data;
    int minVal;
    int maxVal;
    bool padr;
    bool cmd29;
    bool getCmd;
    bool setCmd;
    uchar bytes;
    bool admin;
};

struct StepType
{
    StepType() : num(0), name(), hz(0) {}
    StepType(quint8 num, QString name, quint64 hz) : num(num), name(name), hz(hz) {}
    quint8 num;
    QString name;
    quint64 hz;
};

struct SpectrumBounds
{
    SpectrumBounds() : start(0.0), end(0.0), edge(0) {}
    SpectrumBounds(double start, double end, uchar edge) : start(start), end(end), edge(edge) {}
    double start;
    double end;
    uchar edge;
};

enum class ErrorCode
{
    Unknown,
    AuthFailure,
    ConnectionFailed,
    Disconnected,
    // The radio explicitly reports a session owned by another client. This is
    // terminal for the current attempt: sdr9700 must not retry automatically
    // or send teardown traffic for a session it cannot prove it owns.
    RadioBusy,
    InvalidRadio,
    PortReservationFailed,
    // LAN login and media allocation succeeded, but a directed CI-V probe did
    // not. Keep this distinct from a generic disconnect so RadioBackend can
    // run the bounded retained-session/standby bootstrap policy.
    CommandPlaneUnavailable,
};

struct errorType
{
    errorType() : alert(false) {}
    errorType(bool alert, const QString& device, const QString& message, ErrorCode code)
        : alert(alert), code(code), device(device), message(message)
    {
    }

    bool alert;
    ErrorCode code{ErrorCode::Unknown};
    QString device;
    QString message;
};

struct MemoryType
{
    quint16 group = 0;
    quint16 channel = 0;
    quint8 split = 0;
    quint8 skip = 0;
    quint8 scan = 0;
    quint8 vfo = 0;
    quint8 vfoB = 0;
    Frequency frequency;
    Frequency frequencyB;
    qint16 clarifier = 0;
    bool clarRX = false;
    bool clarTX = false;
    quint8 mode = 0;
    quint8 modeB = 0;
    quint8 filter = 0;
    quint8 filterB = 0;
    quint8 datamode = 0;
    quint8 datamodeB = 0;
    quint8 duplex = 0;
    quint8 duplexB = 0;
    quint8 tonemode = 0;
    quint8 tonemodeB = 0;
    QString tone = "67.0";
    QString toneB = "67.0";
    QString tsql = "67.0";
    QString tsqlB = "67.0";
    quint8 dsql = 0;
    quint8 dsqlB = 0;
    quint16 dtcs = 23;
    quint16 dtcsB = 23;
    quint8 dtcsp = 0;
    quint8 dtcspB = 0;
    quint8 dvsql = 0;
    quint8 dvsqlB = 0;
    Frequency duplexOffset;
    Frequency duplexOffsetB;
    char UR[9]{};
    char URB[9]{};
    char R1[9]{};
    char R2[9]{};
    char R1B[9]{};
    char R2B[9]{};
    uchar tuningStep = 0;
    uchar tuningStepB = 0;
    quint16 progTs = 0;
    quint16 progTsB = 0;
    quint8 atten = 0;
    quint8 attenB = 0;
    quint8 preamp = 0;
    quint8 preampB = 0;
    quint8 antenna = 0;
    quint8 antennaB = 0;
    bool ipplus = false;
    bool ipplusB = false;
    char name[24]{}; // 1 more than the absolute max
    bool sat = false;
    bool del = false;
};

struct MemParserFormat
{
    MemParserFormat(char spec, int pos, int len) : spec(spec), pos(pos), len(len) {}
    char spec;
    int pos;
    int len;
};

struct CommandErrorType
{
    CommandErrorType()
        : func(funcNone), data(QByteArray()), value(QVariant()), receiver(0), minValue(0), maxValue(0), bytes(0)
    {
    }
    CommandErrorType(Funcs func, QByteArray data, QVariant value, uchar receiver, int minValue, int maxValue,
                     char bytes)
        : func(func), data(data), value(value), receiver(receiver), minValue(minValue), maxValue(maxValue), bytes(bytes)
    {
    }

    Funcs func;
    QByteArray data;
    QVariant value;
    uchar receiver;
    int minValue;
    int maxValue;
    uchar bytes;
};

enum audioType
{
    qtAudio
};
enum codecType
{
    LPCM,
    PCMU,
    OPUS
};

enum passbandActions
{
    passbandStatic,
    pbtInnerMove,
    pbtOuterMove,
    pbtMoving,
    passbandResizing
};

struct PeriodicType
{
    PeriodicType() : func(funcNone), priority(), prioVal(0), receiver(0) {}
    PeriodicType(Funcs func, QString priority, qint8 receiver)
        : func(func), priority(priority), prioVal(0), receiver(receiver)
    {
    }
    PeriodicType(Funcs func, QString priority, int prioVal, qint8 receiver)
        : func(func), priority(priority), prioVal(prioVal), receiver(receiver)
    {
    }
    Funcs func;
    QString priority;
    int prioVal;
    qint8 receiver;
};

struct VfoCommandType
{
    VfoCommandType() : freqFunc(funcNone), modeFunc(funcNone), vfo(vfoUnknown), receiver(0) {}
    VfoCommandType(Funcs freqFunc, Funcs modeFunc, vfo_t vfo, uchar receiver)
        : freqFunc(freqFunc), modeFunc(modeFunc), vfo(vfo), receiver(receiver)
    {
    }
    Funcs freqFunc;
    Funcs modeFunc;
    vfo_t vfo;
    uchar receiver;
};

struct RadioStateType
{
    RadioStateType() : vfoMode(vfoModeType_t::vfoModeVfo), vfo(vfoUnknown), receiver(0) {}
    vfoModeType_t vfoMode;
    vfo_t vfo;
    uchar receiver;
};

Q_DECLARE_METATYPE(Frequency)
Q_DECLARE_METATYPE(radioMode_t)
Q_DECLARE_METATYPE(vfo_t)
Q_DECLARE_METATYPE(duplexMode_t)
Q_DECLARE_METATYPE(rptAccessTxRx_t)
Q_DECLARE_METATYPE(pttType_t)
Q_DECLARE_METATYPE(RptrAccessData)
Q_DECLARE_METATYPE(Funcs)
Q_DECLARE_METATYPE(MemoryType)
Q_DECLARE_METATYPE(AntennaInfo)
Q_DECLARE_METATYPE(ScopeData)
Q_DECLARE_METATYPE(TimeKind)
Q_DECLARE_METATYPE(DateKind)
Q_DECLARE_METATYPE(ToneInfo)
Q_DECLARE_METATYPE(meter_t)
Q_DECLARE_METATYPE(MeterKind)
Q_DECLARE_METATYPE(SpectrumBounds)
Q_DECLARE_METATYPE(LpfHpf)
