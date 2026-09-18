#pragma once

#include "CachingQueue.h"
#include "RadioIdentities.h"
#include "Types.h"

#include <QByteArray>
#include <QColor>
#include <QString>
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

namespace sdr9700
{

// The structures in this file form the compiled IC-9700 capability schema.
// Register bytes and limits are protocol facts consumed by Commander; display
// names and ordering are application policy consumed by the GUI. Keeping both
// in typed tables makes unsupported commands explicit and avoids runtime radio
// definition files whose contents could disagree with the compiled parser.

inline QString radioString(const char* value)
{
    return QString::fromUtf8(value);
}

struct RadioCommandDef
{
    Funcs func{funcNone};
    const char* hexData{nullptr};
    int minVal{0};
    int maxVal{0};
    bool padRight{false};
    bool command29{false};
    bool getCommand{false};
    bool setCommand{false};
    uchar bytes{0};
    bool admin{false};
};

struct RadioPeriodicDef
{
    Funcs func{funcNone};
    const char* priority{nullptr};
    qint8 receiver{0};
};

struct RadioModeDef
{
    radioMode_t mode{modeUnknown};
    quint8 reg{0};
    const char* name{nullptr};
    int minHz{0};
    int maxHz{0};
};

struct RadioToneDef
{
    short reg;
    const char* name;
};

struct RadioSpanDef
{
    uchar reg;
    const char* name;
    unsigned int freq;
};

struct RadioInputDef
{
    inputTypes type;
    qint8 reg;
    const char* name;
};

struct RadioStepDef
{
    quint8 num;
    const char* name;
    quint64 hz;
};

struct RadioGenericDef
{
    quint8 num;
    const char* name;
};

struct RadioFilterDef
{
    quint8 num;
    const char* name;
    unsigned int modes;
};

struct RadioBandDef
{
    const char* region;
    availableBands band;
    quint64 start;
    quint64 end;
    quint64 defaultFrequency;
    double range;
    int memGroup;
    qint8 bytes;
    bool antennas;
    float power;
    const char* color;
    const char* name;
    qint64 offset;
};

struct RadioMeterDef
{
    meter_t meter{meterUnknown};
    int radioVal{0};
    double actualVal{0.0};
    bool redLine{false};
};

inline constexpr RadioCommandDef kRadioCommands[] = {
    {funcFreqTR, "00", 0, 0, false, false, false, false, 0, false},
    {funcModeTR, "01", 0, 0, false, false, false, false, 0, false},
    {funcBandEdgeFreq, "02", 0, 0, false, false, true, true, 0, false},
    {funcFreqGet, "03", 0, 0, false, false, true, false, 0, false},
    {funcModeGet, "04", 0, 0, false, false, true, false, 0, false},
    {funcFreqSet, "05", 0, 0, false, false, false, true, 0, false},
    {funcModeSet, "06", 0, 0, false, false, false, true, 0, false},
    {funcVFOModeSelect, "07", 0, 0, false, false, true, true, 0, false},
    {funcVFOASelect, "0700", 0, 0, false, false, true, false, 0, false},
    {funcVFOBSelect, "0701", 0, 0, false, false, true, false, 0, false},
    {funcVFOEqualAB, "07a0", 0, 0, false, false, true, false, 0, false},
    {funcVFOSwapMS, "07b0", 0, 0, false, false, true, false, 0, false},
    {funcVFOMainSelect, "07d0", 0, 0, false, false, true, false, 0, false},
    {funcVFOSubSelect, "07d1", 0, 0, false, false, true, false, 0, false},
    {funcVFOBandMS, "07d2", 0, 1, false, false, true, true, 0, false},
    {funcMemoryMode, "08", 1, 107, false, false, true, true, 0, false},
    {funcMemoryToVFO, "0a", 0, 0, false, false, true, false, 0, false},
    {funcReadFreqOffset, "0c", 0, 0, false, false, true, false, 0, false},
    {funcSendFreqOffset, "0d", 0, 9999, false, false, false, true, 0, false},
    {funcScanning, "0e", 0, 0, false, false, true, true, 0, false},
    {funcSplitStatus, "0f", 0, 99, false, false, true, true, 0, false},
    {funcTuningStep, "10", 0, 11, false, false, true, true, 0, false},
    {funcAttenuator, "11", 0, 10, false, false, true, true, 0, false},
    {funcSpeech, "13", 0, 2, false, false, true, true, 0, false},
    {funcAfGain, "1401", 0, 255, false, false, true, true, 0, false},
    {funcRfGain, "1402", 0, 255, false, false, true, true, 0, false},
    {funcSquelch, "1403", 0, 255, false, false, true, true, 0, false},
    {funcNRLevel, "1406", 0, 255, false, false, true, true, 0, false},
    {funcPBTInner, "1407", 0, 255, false, false, true, true, 0, false},
    {funcPBTOuter, "1408", 0, 255, false, false, true, true, 0, false},
    {funcCwPitch, "1409", 300, 900, false, false, true, true, 0, false},
    {funcRFPower, "140a", 0, 255, false, false, true, true, 0, false},
    {funcKeySpeed, "140c", 6, 48, false, false, true, true, 0, false},
    {funcManualNotchWidth, "140d", 0, 255, false, false, true, true, 0, false},
    {funcCompressorLevel, "140e", 0, 255, false, false, true, true, 0, false},
    {funcBreakInDelay, "140f", 0, 255, false, false, true, true, 0, false},
    {funcNBLevel, "1412", 0, 255, false, false, true, true, 0, false},
    {funcMonitorGain, "1415", 0, 255, false, false, true, true, 0, false},
    {funcVoxGain, "1416", 0, 255, false, false, true, true, 0, false},
    {funcAntiVoxGain, "1417", 0, 255, false, false, true, true, 0, false},
    {funcSMeterSqlStatus, "1501", 0, 1, false, false, true, true, 0, false},
    {funcSMeter, "1502", 0, 255, false, false, true, true, 0, false},
    {funcVariousSql, "1505", 0, 1, false, false, true, true, 0, false},
    {funcOverflowStatus, "1507", 0, 1, false, false, true, true, 0, false},
    {funcPowerMeter, "1511", 0, 255, false, false, true, true, 0, false},
    {funcSWRMeter, "1512", 0, 255, false, false, true, true, 0, false},
    {funcALCMeter, "1513", 0, 255, false, false, true, true, 0, false},
    {funcCompMeter, "1514", 0, 255, false, false, true, true, 0, false},
    {funcVdMeter, "1515", 0, 255, false, false, true, true, 0, false},
    {funcIdMeter, "1516", 0, 255, false, false, true, true, 0, false},
    {funcPreamp, "1602", 0, 3, false, false, true, true, 0, false},
    {funcAGCTimeConstant, "1612", 1, 3, false, false, true, true, 0, false},
    {funcNoiseBlanker, "1622", 0, 1, false, false, true, true, 0, false},
    {funcNoiseReduction, "1640", 0, 1, false, false, true, true, 0, false},
    {funcAutoNotch, "1641", 0, 1, false, false, true, true, 0, false},
    {funcRepeaterTone, "1642", 0, 1, false, false, true, true, 0, false},
    {funcRepeaterTSQL, "1643", 0, 1, false, false, true, true, 0, false},
    {funcCompressor, "1644", 0, 1, false, false, true, true, 0, false},
    {funcMonitor, "1645", 0, 1, false, false, true, true, 0, false},
    {funcVox, "1646", 0, 1, false, false, true, true, 0, false},
    {funcBreakIn, "1647", 0, 1, false, false, true, true, 0, false},
    {funcManualNotch, "1648", 0, 1, false, false, true, true, 0, false},
    {funcAFCSetting, "164a", 0, 1, false, false, true, true, 0, false},
    {funcRepeaterDTCS, "164b", 0, 1, false, false, true, true, 0, false},
    {funcTwinPeakFilter, "164f", 0, 1, false, false, true, true, 0, false},
    {funcDialLock, "1650", 0, 1, false, false, true, true, 0, false},
    {funcFilterShape, "1656", 0, 31, false, false, true, true, 0, false},
    {funcSSBTXBandwidth, "1658", 0, 2, false, false, true, true, 0, false},
    {funcVFODualWatch, "1659", 0, 1, false, false, true, true, 0, false},
    {funcSatelliteMode, "165a", 0, 1, false, false, true, true, 0, false},
    {funcRepeaterCSQL, "165b", 0, 2, false, false, true, true, 0, false},
    {funcGPSTXMode, "165c", 0, 1, false, false, true, true, 0, false},
    {funcToneSquelchType, "165d", 0, 9, false, false, true, true, 0, false},
    {funcIPPlus, "1665", 0, 1, false, false, true, true, 0, false},
    {funcSendCW, "17", 0, 0, false, false, false, true, 30, false},
    {funcPowerControl, "18", 0, 1, false, false, true, true, 0, false},
    {funcTransceiverId, "1900", 0, 0, false, false, true, true, 0, false},
    {funcMemoryContents, "1a00", 1, 107, false, false, true, true, 0, false},
    {funcBandStackReg, "1a01", 1, 3, false, false, true, true, 0, false},
    {funcFilterWidth, "1a03", 50, 10000, false, false, true, true, 0, false},
    {funcTimeOutTimer, "1a050041", 0, 5, false, false, true, true, 1, false},
    {funcQuickSplit, "1a050043", 0, 1, false, false, true, true, 0, false},
    {funcREFAdjust, "1a050072", 0, 0, false, false, true, true, 0, false},
    {funcREFAdjustFine, "1a050073", 0, 0, false, false, true, true, 0, false},
    {funcACCAModLevel, "1a050112", 0, 255, false, false, true, true, 0, false},
    {funcUSBModLevel, "1a050113", 0, 255, false, false, true, true, 0, false},
    {funcLANModLevel, "1a050114", 0, 255, false, false, true, true, 0, false},
    {funcDATAOffMod, "1a050115", 0, 5, false, false, true, true, 0, false},
    {funcDATA1Mod, "1a050116", 0, 5, false, false, true, true, 0, false},
    {funcCIVTransceive, "1a050127", 0, 1, false, false, true, true, 0, false},
    {funcDate, "1a050179", 0, 0, false, false, true, true, 0, false},
    {funcTime, "1a050180", 0, 0, false, false, true, true, 0, false},
    {funcUTCOffset, "1a050184", 0, 0, false, false, true, true, 0, false},
    {funcDashRatio, "1a050224", 0, 0, false, false, true, true, 0, false},
    {funcDataModeWithFilter, "1a06", 0, 0, false, false, true, true, 0, false},
    {funcSatelliteMemory, "1a07", 0, 99, false, false, true, true, 0, false},
    {funcToneFreq, "1b00", 0, 9999, false, false, true, true, 0, false},
    {funcTSQLFreq, "1b01", 0, 9999, false, false, true, true, 0, false},
    {funcDTCSCode, "1b02", 0, 9999, false, false, true, true, 0, false},
    {funcCSQLCode, "1b07", 0, 9999, false, false, true, true, 0, false},
    {funcTransceiverStatus, "1c00", 0, 1, false, false, true, true, 0, false},
    {funcXFCStatus, "1c02", 0, 1, false, false, true, true, 0, false},
    {funcRitFreq, "2100", -999, 999, false, false, true, true, 0, false},
    {funcRitStatus, "2101", 0, 1, false, false, true, true, 0, false},
    {funcGPSPosition, "2300", 0, 0, false, false, true, true, 0, false},
    {funcSelectedFreq, "2500", 0, 0, false, false, true, true, 0, false},
    {funcUnselectedFreq, "2501", 0, 0, false, false, true, true, 0, false},
    {funcSelectedMode, "2600", 0, 0, false, false, true, true, 0, false},
    {funcUnselectedMode, "2601", 0, 0, false, false, true, true, 0, false},
    {funcScopeWaveData, "2700", 0, 0, false, false, true, true, 0, false},
    {funcScopeOnOff, "2710", 0, 1, false, false, true, true, 0, false},
    {funcScopeDataOutput, "2711", 0, 1, false, false, true, true, 0, false},
    {funcScopeMainSub, "2712", 0, 1, false, false, true, true, 0, false},
    {funcScopeMode, "2714", 0, 3, false, false, true, true, 0, false},
    {funcScopeSpan, "2715", 2500, 500000, false, false, true, true, 0, false},
    {funcScopeEdge, "2716", 1, 4, false, false, true, true, 0, false},
    {funcScopeHold, "2717", 0, 1, false, false, true, true, 0, false},
    {funcScopeRef, "2719", 0, 255, false, false, true, true, 0, false},
    {funcScopeSpeed, "271a", 0, 2, false, false, true, true, 0, false},
    {funcScopeDuringTX, "271b", 0, 1, false, false, true, true, 0, false},
    {funcScopeCenterType, "271c", 0, 2, false, false, true, true, 0, false},
    {funcScopeVBW, "271d", 0, 1, false, false, true, true, 0, false},
    {funcScopeFixedEdgeFreq, "271e", 0, 0, false, false, true, true, 0, false},
    {funcVoiceTX, "2800", 0, 8, false, false, true, true, 0, false},
    {funcFA, "fa", 0, 0, false, false, false, false, 0, false},
    {funcFB, "fb", 0, 0, false, false, false, false, 0, false},
};

// funcSMeter is intentionally absent from the general periodic table.
// RadioBackend polls it with m_smeterPollTimer so meter routing can alternate
// MAIN and SUB deliberately during Dual Watch and suppress new polls while a
// receiver-context transaction is active. Adding it here would create a second
// independent polling source and undermine that ordering policy.
inline constexpr RadioPeriodicDef kRadioPeriodicCommands[] = {
    {funcAttenuator, "Medium Low", 0},
    {funcCompressor, "Medium Low", 0},
    {funcDATAOffMod, "Medium High", 0},
    {funcDATA1Mod, "Medium High", 0},
    {funcIPPlus, "Medium Low", 0},
    {funcMonitorGain, "Medium Low", 0},
    {funcMonitor, "Medium Low", 0},
    {funcNoiseBlanker, "Medium Low", 0},
    {funcNoiseReduction, "Medium Low", 0},
    {funcOverflowStatus, "High", 0},
    {funcPreamp, "Medium Low", 0},
    {funcRfGain, "Medium", 0},
    {funcRFPower, "Medium", 0},
    {funcScopeMainSub, "Medium High", 0},
    {funcScopeMode, "Medium High", -1},
    {funcScopeRef, "Medium", -1},
    {funcScopeSpan, "Medium High", -1},
    {funcScopeSpeed, "Medium", -1},
    {funcSelectedFreq, "Medium", 0},
    {funcSelectedMode, "Medium", 0},
    {funcSplitStatus, "Medium", 0},
    {funcSquelch, "Medium Low", 0},
    {funcToneSquelchType, "Medium Low", 0},
    {funcTransceiverStatus, "High", 0},
    {funcTuningStep, "Medium Low", 0},
    {funcUnselectedFreq, "Medium", 0},
    {funcUnselectedMode, "Medium", 0},
    {funcVFODualWatch, "Medium High", 0},
    {funcVox, "Medium Low", 0},
};

inline constexpr RadioModeDef kRadioModes[] = {
    {static_cast<radioMode_t>(0), 0, "LSB", 50, 3600},  {static_cast<radioMode_t>(1), 1, "USB", 50, 3600},
    {static_cast<radioMode_t>(2), 2, "AM", 200, 10000}, {static_cast<radioMode_t>(3), 3, "CW", 50, 3600},
    {static_cast<radioMode_t>(4), 4, "RTTY", 50, 2700}, {static_cast<radioMode_t>(5), 5, "FM", 0, 0},
    {static_cast<radioMode_t>(6), 7, "CW-R", 50, 3600}, {static_cast<radioMode_t>(7), 8, "RTTY-R", 50, 2700},
    {static_cast<radioMode_t>(12), 17, "DV", 0, 0},     {static_cast<radioMode_t>(14), 22, "DD", 0, 0},
};

inline constexpr RadioToneDef kRadioCtcss[] = {
    {670, "67.0"},   {693, "69.3"},   {719, "71.9"},   {744, "74.4"},   {770, "77.0"},   {797, "79.7"},
    {825, "82.5"},   {854, "85.4"},   {885, "88.5"},   {915, "91.5"},   {948, "94.8"},   {974, "97.4"},
    {1000, "100.0"}, {1035, "103.5"}, {1072, "107.2"}, {1109, "110.9"}, {1148, "114.8"}, {1188, "118.8"},
    {1230, "123.0"}, {1273, "127.3"}, {1318, "131.8"}, {1365, "136.5"}, {1413, "141.3"}, {1462, "146.2"},
    {1514, "151.4"}, {1567, "156.7"}, {1598, "159.8"}, {1622, "162.2"}, {1655, "165.5"}, {1679, "167.9"},
    {1738, "173.8"}, {1773, "177.3"}, {1799, "179.9"}, {1835, "183.5"}, {1862, "186.2"}, {1928, "192.8"},
    {1966, "196.6"}, {1995, "199.5"}, {2035, "203.5"}, {2065, "206.5"}, {2107, "210.7"}, {2181, "218.1"},
    {2257, "225.7"}, {2291, "229.1"}, {2336, "233.6"}, {2418, "241.8"}, {2503, "250.3"}, {2541, "254.1"},
};

inline constexpr short kRadioDtcs[] = {
    23,  25,  26,  31,  32,  36,  43,  47,  51,  53,  54,  65,  71,  72,  73,  74,  114, 115, 116, 122, 125,
    131, 132, 134, 143, 145, 152, 155, 156, 162, 165, 172, 174, 205, 212, 223, 225, 226, 243, 244, 245, 246,
    251, 252, 255, 261, 263, 265, 266, 271, 274, 306, 311, 315, 325, 331, 332, 343, 346, 351, 356, 364, 365,
    371, 411, 412, 413, 423, 431, 432, 445, 446, 452, 454, 455, 462, 464, 465, 466, 503, 506, 516, 523, 526,
    532, 546, 565, 606, 612, 624, 627, 631, 632, 654, 662, 664, 703, 712, 723, 731, 732, 734, 746, 754,
};

inline constexpr RadioSpanDef kRadioSpans[] = {
    {0, "\302\2612.5 kHz", 2500},   {1, "\302\2615 kHz", 5000},     {2, "\302\26110 kHz", 10000},
    {3, "\302\26125 kHz", 25000},   {4, "\302\26150 kHz", 50000},   {5, "\302\261100 kHz", 100000},
    {6, "\302\261250 kHz", 250000}, {7, "\302\261500 kHz", 500000},
};

inline constexpr RadioInputDef kRadioInputs[] = {
    {static_cast<inputTypes>(0), 0, "MIC"},  {static_cast<inputTypes>(1), 1, "ACC"},
    {static_cast<inputTypes>(5), 2, "M/A"},  {static_cast<inputTypes>(3), 3, "USB"},
    {static_cast<inputTypes>(10), 4, "M/U"}, {static_cast<inputTypes>(4), 5, "LAN"},
};

inline constexpr RadioStepDef kRadioTuningSteps[] = {};

inline constexpr RadioGenericDef kRadioPreamps[] = {
    {0, "INT/EXT OFF"},
    {1, "INT ON"},
    {2, "EXT ON"},
    {3, "INT/EXT ON"},
};

inline constexpr std::array<RadioGenericDef, 0> kRadioAntennas{};

inline constexpr RadioGenericDef kRadioAttenuators[] = {
    {0, "0 dB"},
    {10, "-10 dB"},
};

inline constexpr RadioGenericDef kRadioRoofing[] = {};

inline constexpr RadioGenericDef kRadioScopeModes[] = {
    {0, "Center Mode"},
    {1, "Fixed Mode"},
    {2, "Scroll-C"},
    {3, "Scroll-F"},
};

inline constexpr RadioFilterDef kRadioFilters[] = {
    {1, "FIL1", 0},
    {2, "FIL2", 0},
    {3, "FIL3", 0},
};

// Region codes follow the IC-9700 regional band-edge tables rather than a
// generic amateur-band plan. Keep these edges within IC-9700 hardware coverage
// even when a jurisdiction permits operation beyond the transceiver's tuning
// range; the GUI must not offer a frequency the selected radio cannot accept.
inline constexpr RadioBandDef kRadioBands[] = {
    {"", band23cm, 1240000000ULL, 1300000000ULL, 1296100000ULL, 1300, 3, 5, true, 10.0F, "#ffff0000", "23cm", 0},
    {"1", band70cm, 430000000ULL, 440000000ULL, 432100000ULL, 450, 2, 5, true, 75.0F, "#ff00ff00", "70cm", 0},
    {"2", band70cm, 430000000ULL, 450000000ULL, 432100000ULL, 450, 2, 5, true, 75.0F, "#ff00ff00", "70cm", 0},
    {"3", band70cm, 430000000ULL, 440000000ULL, 432100000ULL, 450, 2, 5, true, 75.0F, "#ff00ff00", "70cm", 0},
    {"1", band2m, 144000000ULL, 146000000ULL, 144200000ULL, 148, 1, 5, true, 100.0F, "#ff0000ff", "2m", 0},
    {"2", band2m, 144000000ULL, 148000000ULL, 144200000ULL, 148, 1, 5, true, 100.0F, "#ff0000ff", "2m", 0},
    {"3", band2m, 144000000ULL, 148000000ULL, 144200000ULL, 148, 1, 5, true, 100.0F, "#ff0000ff", "2m", 0},
};

inline constexpr availableBands kRadioUiBandOrder[] = {band2m, band70cm, band23cm};

inline const RadioBandDef* radioBandDefinition(availableBands band)
{
    const auto it = std::find_if(std::cbegin(kRadioBands), std::cend(kRadioBands),
                                 [band](const RadioBandDef& def) { return def.band == band; });
    return it != std::cend(kRadioBands) ? &(*it) : nullptr;
}

inline availableBands radioBandForFrequency(quint64 hz)
{
    const auto it = std::find_if(std::cbegin(kRadioBands), std::cend(kRadioBands),
                                 [hz](const RadioBandDef& def) { return hz >= def.start && hz <= def.end; });
    return it != std::cend(kRadioBands) ? it->band : bandUnknown;
}

inline bool radioBandEdges(availableBands band, quint64* start, quint64* end)
{
    bool found = false;
    quint64 low = 0;
    quint64 high = 0;

    for (const RadioBandDef& def : kRadioBands)
    {
        if (def.band != band)
        {
            continue;
        }
        if (!found)
        {
            low = def.start;
            high = def.end;
            found = true;
            continue;
        }
        low = std::min(low, def.start);
        high = std::max(high, def.end);
    }

    if (found)
    {
        if (start)
        {
            *start = low;
        }
        if (end)
        {
            *end = high;
        }
    }
    return found;
}

inline int radioBandUiIndex(availableBands band)
{
    for (std::size_t i = 0; i < std::size(kRadioUiBandOrder); ++i)
    {
        if (kRadioUiBandOrder[i] == band)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline QString radioBandShortLabel(availableBands band)
{
    const RadioBandDef* def = radioBandDefinition(band);
    return def ? radioString(def->name).toUpper() : QStringLiteral("BAND");
}

inline QString radioBandMenuLabel(availableBands band)
{
    const RadioBandDef* def = radioBandDefinition(band);
    if (!def)
    {
        return QStringLiteral("BAND");
    }

    const int defaultMhz = static_cast<int>((def->defaultFrequency + 500000ULL) / 1000000ULL);
    return QStringLiteral("%1 (%2 MHZ)").arg(radioString(def->name).toUpper(), QString::number(defaultMhz));
}

inline quint64 radioBandDefaultFrequency(availableBands band)
{
    const RadioBandDef* def = radioBandDefinition(band);
    return def ? def->defaultFrequency : 0;
}

inline double radioBandMaxPowerWatts(availableBands band)
{
    const RadioBandDef* def = radioBandDefinition(band);
    return def ? def->power : 0.0;
}

inline int radioBandMemoryKey(availableBands band)
{
    const RadioBandDef* def = radioBandDefinition(band);
    return def ? static_cast<int>(def->start / 1000000ULL) : -1;
}

inline constexpr RadioMeterDef kRadioMeters[] = {
    {meterALC, 0, 0, false},        {meterALC, 120, 1, true},       {meterALC, 255, 2, false},
    {meterComp, 0, 0, false},       {meterComp, 130, 15, true},     {meterComp, 210, 25.5, false},
    {meterCurrent, 0, 0, false},    {meterCurrent, 121, 10, false}, {meterCurrent, 241, 20, true},
    {meterPower, 0, 0, false},      {meterPower, 21, 5, false},     {meterPower, 43, 10, false},
    {meterPower, 65, 15, false},    {meterPower, 83, 20, false},    {meterPower, 95, 25, false},
    {meterPower, 105, 30, false},   {meterPower, 114, 35, false},   {meterPower, 124, 40, false},
    {meterPower, 143, 50, false},   {meterPower, 183, 75, false},   {meterPower, 213, 100, true},
    {meterPower, 255, 120, false},  {meterSWR, 0, 1, false},        {meterSWR, 48, 1.5, false},
    {meterSWR, 80, 2, false},       {meterSWR, 120, 3, true},       {meterSWR, 240, 6, false},
    {meterVoltage, 0, 0, false},    {meterVoltage, 13, 10, false},  {meterVoltage, 185, 13.8, true},
    {meterVoltage, 241, 16, false},
};

inline void addRadioCommand(radioCapabilities& radioCaps, const RadioCommandDef& def)
{
    const QByteArray data = QByteArray::fromHex(QByteArray(def.hexData));
    radioCaps.commands.insert(def.func,
                              FuncType(def.func, funcString[int(def.func)], data, def.minVal, def.maxVal, def.padRight,
                                       def.command29, def.getCommand, def.setCommand, def.bytes, def.admin));
    radioCaps.commandsReverse.insert(data, def.func);
}

template <typename Container>
inline void appendGenericTypes(Container& target, const RadioGenericDef* defs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        const RadioGenericDef& def = defs[i];
        target.push_back(genericType(def.num, radioString(def.name)));
    }
}

inline void populateRadioCapabilities(radioCapabilities& radioCaps)
{
    radioCaps.filename = radioString("IC-9700 built-in capabilities");
    radioCaps.modelName = radioString("IC-9700");
    radioCaps.radioControlModel = 3081;
    radioCaps.manufacturer = manufIcom;
    radioCaps.numReceiver = 2;
    radioCaps.numVFO = 2;
    radioCaps.spectSeqMax = 11;
    radioCaps.spectAmpMax = 160;
    radioCaps.spectLenMax = 475;
    radioCaps.hasSpectrum = true;
    radioCaps.hasLan = true;
    radioCaps.hasEthernet = true;
    radioCaps.hasWiFi = false;
    radioCaps.hasQuickSplitCommand = false;
    // The IC-9700 supports D-STAR at the radio level, but SDR9700 does not yet
    // implement the DD/DV data-mode workflows behind these capability flags.
    radioCaps.hasDD = false;
    radioCaps.hasDV = false;
    radioCaps.hasTransmit = true;
    radioCaps.hasFDcomms = true;
    radioCaps.hasCommand29 = false;
    radioCaps.memGroups = 3;
    radioCaps.memories = 107;
    radioCaps.memStart = 1;
    radioCaps.satMemories = 99;
    radioCaps.memFormat = radioString("%1.1a %2.2b %4.1c %5.5f %10.1g %11.1h %12.1i %13.1j %14.1m %15.3n %18.3o "
                                      "%21.1p %22.2q %24.1r %25.3s %28.8t %36.8u %44.8v %52.16z");
    radioCaps.satFormat = radioString(
        "%1.2b %3.5f %8.1g %9.1h %10.1i %11.1l %12.1m %13.3n %16.3o %19.1p %20.2q %22.1r %23.8t %31.8u %39.8v %47.5F "
        "%52.1G %53.1H %54.1I %55.1K %56.1M %57.3N %60.3O %63.1P %64.2Q %66.1R %67.8T %75.8U %83.8V %91.16z");

    for (const RadioCommandDef& def : kRadioCommands)
    {
        addRadioCommand(radioCaps, def);
    }

    for (const RadioPeriodicDef& def : kRadioPeriodicCommands)
    {
        const QString priority = radioString(def.priority);
        radioCaps.periodic.append(PeriodicType(def.func, priority, priorityValue(priority), def.receiver));
    }

    for (const RadioModeDef& def : kRadioModes)
    {
        radioCaps.modes.push_back(ModeInfo(def.mode, def.reg, radioString(def.name), def.minHz, def.maxHz));
    }

    for (const RadioToneDef& def : kRadioCtcss)
    {
        radioCaps.ctcss.push_back(ToneInfo(def.reg, radioString(def.name)));
    }

    for (const short reg : kRadioDtcs)
    {
        radioCaps.dtcs.push_back(ToneInfo(reg, QString::number(reg).rightJustified(4, '0')));
    }

    for (const RadioSpanDef& def : kRadioSpans)
    {
        radioCaps.scopeCenterSpans.push_back(centerSpanData(def.reg, radioString(def.name), def.freq));
    }

    for (const RadioInputDef& def : kRadioInputs)
    {
        radioCaps.inputs.append(radioInput(def.type, def.reg, radioString(def.name)));
    }

    for (const RadioStepDef& def : kRadioTuningSteps)
    {
        radioCaps.steps.push_back(StepType(def.num, radioString(def.name), def.hz));
    }

    appendGenericTypes(radioCaps.preamps, kRadioPreamps, std::size(kRadioPreamps));
    appendGenericTypes(radioCaps.antennas, kRadioAntennas.data(), std::size(kRadioAntennas));
    appendGenericTypes(radioCaps.attenuators, kRadioAttenuators, std::size(kRadioAttenuators));

    for (const RadioFilterDef& def : kRadioFilters)
    {
        radioCaps.filters.push_back(filterType(def.num, radioString(def.name), def.modes));
    }

    for (const RadioBandDef& def : kRadioBands)
    {
        radioCaps.bands.push_back(bandType(radioString(def.region), def.band, def.start, def.end, def.range,
                                           def.memGroup, def.bytes, def.antennas, def.power,
                                           QColor(radioString(def.color)), radioString(def.name), def.offset));
    }

    for (const RadioMeterDef& def : kRadioMeters)
    {
        if (def.redLine)
        {
            radioCaps.meterLines[def.meter] = def.actualVal;
        }
        radioCaps.meters[def.meter].insert(def.radioVal, def.actualVal);
    }

    appendGenericTypes(radioCaps.roofing, kRadioRoofing, sizeof(kRadioRoofing) / sizeof(RadioGenericDef));
    appendGenericTypes(radioCaps.scopeModes, kRadioScopeModes, std::size(kRadioScopeModes));
}

} // namespace sdr9700
