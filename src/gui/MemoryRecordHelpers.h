#pragma once

#include "MemoryConstants.h"
#include "MainWindowHelpers.h"
#include "MemoryStore.h"
#include "RadioCapabilities.h"

#include <algorithm>
#include <cstring>

namespace sdr9700::memory
{
using namespace sdr9700::ui::main_window;
inline quint32 radioMemoryKey(quint16 group, quint16 channel)
{
    return (static_cast<quint32>(group) << 16) | static_cast<quint32>(channel);
}

inline QString radioMemoryId(quint16 group, quint16 channel)
{
    return group == kRadioMemorySatelliteGroup
               ? QStringLiteral("satellite:%1").arg(channel, 2, 10, QLatin1Char('0'))
               : QStringLiteral("radio:%1:%2").arg(group).arg(channel, 3, 10, QLatin1Char('0'));
}

inline bool radioMemoryIsEditable(quint16 group, quint16 channel)
{
    return group >= kRadioMemoryFirstGroup && group <= kRadioMemoryLastGroup && channel >= kRadioMemoryFirstChannel &&
           channel <= kRadioMemoryLastUserChannel;
}

inline QString radioMemoryChannelLabel(quint16 group, quint16 channel)
{
    if (group == kRadioMemorySatelliteGroup)
    {
        return QStringLiteral("SAT-%1").arg(channel, 2, 10, QLatin1Char('0'));
    }
    if (channel >= 100 && channel <= 105)
    {
        const int scanPair = ((channel - 100) / 2) + 1;
        const QChar edge = ((channel - 100) % 2) == 0 ? QLatin1Char('A') : QLatin1Char('B');
        return QStringLiteral("%1-%2%3").arg(memoryBandLabelForGroup(group)).arg(scanPair).arg(edge);
    }
    if (channel == 106 || channel == 107)
    {
        return QStringLiteral("%1-C%2").arg(memoryBandLabelForGroup(group)).arg(channel - 105);
    }
    return QStringLiteral("%1-%2").arg(memoryBandLabelForGroup(group)).arg(channel, 3, 10, QLatin1Char('0'));
}

inline QString radioMemoryBandLabel(quint16 group)
{
    return group == kRadioMemorySatelliteGroup ? QStringLiteral("SAT") : memoryBandLabelForGroup(group);
}

inline QString radioMemorySlotLabel(quint16 group, quint16 channel)
{
    if (group == kRadioMemorySatelliteGroup)
    {
        return QStringLiteral("%1").arg(channel, 2, 10, QLatin1Char('0'));
    }
    if (channel >= 100 && channel <= 105)
    {
        const int scanPair = ((channel - 100) / 2) + 1;
        const QChar edge = ((channel - 100) % 2) == 0 ? QLatin1Char('A') : QLatin1Char('B');
        return QStringLiteral("%1%2").arg(scanPair).arg(edge);
    }
    if (channel == 106 || channel == 107)
    {
        return QStringLiteral("C%1").arg(channel - 105);
    }
    return QStringLiteral("%1").arg(channel, 3, 10, QLatin1Char('0'));
}

inline QString radioMemoryName(const MemoryType& memory)
{
    int length = 0;
    while (length < static_cast<int>(sizeof(memory.name)) && memory.name[length] != '\0')
    {
        ++length;
    }
    return QString::fromLatin1(memory.name, length).trimmed();
}

inline QString memoryCharField(const char* field, int size)
{
    int length = 0;
    while (length < size && field[length] != '\0')
    {
        ++length;
    }
    return QString::fromLatin1(field, length).trimmed();
}

inline bool radioMemoryIsStored(const MemoryType& memory)
{
    return !memory.del && memory.frequency.Hz > 0;
}

inline MemoryType radioMemoryWithLocalNameFallback(MemoryType memory)
{
    // The IC-9700 permits an occupied memory channel to have an empty name.
    // SDR9700 still needs a stable, useful label for that record in its local
    // database, table, editor, and CSV output. Normalize a copy at the radio
    // ingest boundary so every local consumer sees the same value. This
    // normalization does not itself issue a radio write; a later explicit edit
    // and Save remains an ordinary operator-requested radio write. Deleted or
    // otherwise empty slots deliberately retain an empty name because they do
    // not represent database records.
    if (!radioMemoryIsStored(memory) || !radioMemoryName(memory).isEmpty())
    {
        return memory;
    }

    std::fill_n(memory.name, sizeof(memory.name), '\0');
    const QByteArray fallback = memoryFrequencyLabel(memory.frequency.Hz).toLatin1().left(sizeof(memory.name));
    std::copy(fallback.cbegin(), fallback.cend(), memory.name);
    return memory;
}

inline int recordDuplexModeFromRadio(quint8 duplex)
{
    switch (duplex)
    {
    case 1:
        return dmDupMinus;
    case 2:
        return dmDupPlus;
    default:
        return dmSimplex;
    }
}

inline quint64 defaultOffsetForModeAndHz(duplexMode_t mode, quint64 hz)
{
    const QVector<OffsetPreset> presets = offsetPresetsForHz(hz);
    const auto preset = std::find_if(presets.cbegin(), presets.cend(),
                                     [mode](const OffsetPreset& option) { return option.mode == mode; });
    if (preset != presets.cend())
    {
        return preset->hz;
    }
    return 0;
}

inline quint64 normalizedOffsetForModeAndHz(duplexMode_t mode, quint64 rawOffsetHz, quint64 receiveHz)
{
    if (mode != dmDupMinus && mode != dmDupPlus)
    {
        return 0;
    }

    for (const OffsetPreset& preset : offsetPresetsForHz(receiveHz))
    {
        if (preset.mode != mode)
        {
            continue;
        }
        if (rawOffsetHz == preset.hz || rawOffsetHz * 100ULL == preset.hz)
        {
            return preset.hz;
        }
    }
    if (rawOffsetHz == 0)
    {
        return defaultOffsetForModeAndHz(mode, receiveHz);
    }
    return rawOffsetHz;
}

inline quint8 radioDuplexFromRecord(int duplexMode)
{
    switch (static_cast<duplexMode_t>(duplexMode))
    {
    case dmDupMinus:
        return 1;
    case dmDupPlus:
        return 2;
    default:
        return 0;
    }
}

inline ushort toneValueFromRadioText(const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
    {
        return 0;
    }
    return static_cast<ushort>(value * 10.0 + 0.5);
}

inline QString normalizedToneText(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
    {
        return QString();
    }

    const ushort value = toneValueFromRadioText(text);
    return value > 0 ? toneFrequencyLabel(value) : text;
}

inline bool memoryToneModeUsesTxTone(rptAccessTxRx_t toneMode)
{
    return toneMode == ratrTN || toneMode == ratrTT || toneMode == ratrTD;
}

inline bool memoryToneModeUsesRxTone(rptAccessTxRx_t toneMode)
{
    return toneMode == ratrNT || toneMode == ratrTT || toneMode == ratrTD || toneMode == ratrDT;
}

inline int modeRegisterFromLabel(const QString& mode)
{
    if (mode == QLatin1String("LSB"))
    {
        return modeLSB;
    }
    if (mode == QLatin1String("USB"))
    {
        return modeUSB;
    }
    if (mode == QLatin1String("AM"))
    {
        return modeAM;
    }
    if (mode == QLatin1String("CW"))
    {
        return modeCW;
    }
    if (mode == QLatin1String("DV"))
    {
        return modeDV;
    }
    return modeFM;
}

inline quint8 memoryModeRegister(radioMode_t mode)
{
    switch (mode)
    {
    case modeCW_R:
        return 7;
    case modeRTTY_R:
        return 8;
    case modeDV:
        return 17;
    case modeDD:
        return 22;
    default:
        return static_cast<quint8>(mode);
    }
}

inline radioMode_t memoryModeKindFromRegister(quint8 reg)
{
    switch (reg)
    {
    case 7:
        return modeCW_R;
    case 8:
        return modeRTTY_R;
    case 17:
        return modeDV;
    case 22:
        return modeDD;
    default:
        return static_cast<radioMode_t>(reg);
    }
}

inline MemoryRecord recordFromRadioMemory(const MemoryType& radioMemory)
{
    MemoryRecord memory;
    memory.id = radioMemoryId(radioMemory.group, radioMemory.channel);
    memory.group = radioMemory.group;
    memory.channel = radioMemory.channel;
    memory.readOnly = !radioMemoryIsEditable(radioMemory.group, radioMemory.channel);
    memory.name = radioMemoryName(radioMemory);
    if (memory.name.isEmpty())
    {
        memory.name = memoryFrequencyLabel(radioMemory.frequency.Hz);
    }
    memory.receiveHz = radioMemory.frequency.Hz;
    memory.mode = memoryModeKindFromRegister(radioMemory.mode);
    memory.filter = radioMemory.filter;
    memory.dataMode = radioMemory.datamode;
    memory.scan = radioMemory.scan;
    memory.band = sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(memory.receiveHz));
    memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
    memory.duplexMode = recordDuplexModeFromRadio(radioMemory.duplex);
    memory.offsetHz = normalizedOffsetForModeAndHz(static_cast<duplexMode_t>(memory.duplexMode),
                                                   radioMemory.duplexOffset.Hz, memory.receiveHz);
    memory.shift = offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
    memory.toneMode = radioMemory.tonemode;
    memory.toneOption = toneOptionLabel(static_cast<rptAccessTxRx_t>(memory.toneMode));
    memory.tone = radioMemory.tone;
    memory.tsql = radioMemory.tsql;
    memory.dsql = radioMemory.dsql;
    memory.dtcs = radioMemory.dtcs;
    memory.dtcsPolarity = radioMemory.dtcsp;
    memory.dtcsB = radioMemory.dtcsB;
    memory.dtcsPolarityB = radioMemory.dtcspB;
    memory.dvSql = radioMemory.dvsql;
    memory.urCall = memoryCharField(radioMemory.UR, sizeof radioMemory.UR);
    memory.r1Call = memoryCharField(radioMemory.R1, sizeof radioMemory.R1);
    memory.r2Call = memoryCharField(radioMemory.R2, sizeof radioMemory.R2);
    if (isDtcsToneMode(static_cast<rptAccessTxRx_t>(memory.toneMode)))
    {
        memory.toneValue = memory.dtcs;
    }
    else if (memory.toneMode != ratrNN)
    {
        const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
        memory.toneValue = toneValueFromRadioText(toneMode == ratrNT || toneMode == ratrDT ? memory.tsql : memory.tone);
    }
    memory.toneFrequency = memoryToneFrequencyLabel(static_cast<rptAccessTxRx_t>(memory.toneMode), memory.toneValue);
    return memory;
}

inline MemoryType radioMemoryFromRecord(const MemoryRecord& memory, quint16 group, quint16 channel)
{
    MemoryType radioMemory;
    radioMemory.group = group;
    radioMemory.channel = channel;
    radioMemory.scan = static_cast<quint8>(qBound(0, memory.scan, 3));
    radioMemory.frequency.Hz = memory.receiveHz;
    radioMemory.frequency.VFO = activeVFO;
    radioMemory.mode = memoryModeRegister(static_cast<radioMode_t>(memory.mode));
    radioMemory.filter = static_cast<quint8>(qBound(1, memory.filter, 3));
    radioMemory.datamode = static_cast<quint8>(qBound(0, memory.dataMode, 1));
    radioMemory.duplex = radioDuplexFromRecord(memory.duplexMode);
    radioMemory.tonemode = static_cast<quint8>(memory.toneMode);
    radioMemory.dsql = static_cast<quint8>(qBound(0, memory.dsql, 2));
    radioMemory.dtcsp = static_cast<quint8>(qBound(0, memory.dtcsPolarity, 3));
    radioMemory.dtcspB = static_cast<quint8>(qBound(0, memory.dtcsPolarityB, 3));
    radioMemory.dvsql = static_cast<quint8>(qBound(0, memory.dvSql, 99));
    radioMemory.duplexOffset.Hz = memory.offsetHz;
    radioMemory.duplexOffset.VFO = activeVFO;
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (isDtcsToneMode(toneMode))
    {
        radioMemory.dtcs = memory.dtcs;
        radioMemory.dtcsB = memory.dtcsB;
    }
    // Mixed tone modes share payload fields: ratrDT writes DTCS for transmit
    // and TSQL for receive. Keep those decisions explicit so a later tone-mode
    // cleanup cannot accidentally drop half of the radio memory definition.
    if (memoryToneModeUsesTxTone(toneMode))
    {
        // CSV import and the editor validate required tone fields before a write
        // is queued. Keep the radio payload faithful to those explicit fields so
        // bad memory data fails at the schema boundary instead of being repaired
        // here with hidden inferred values.
        radioMemory.tone = normalizedToneText(memory.tone);
    }
    if (memoryToneModeUsesRxTone(toneMode))
    {
        radioMemory.tsql = normalizedToneText(memory.tsql);
    }
    const QByteArray name = memory.name.toLatin1().left(kRadioMemoryNameMaxChars);
    std::copy(name.cbegin(), name.cend(), radioMemory.name);
    const QByteArray ur = memory.urCall.toLatin1().left(sizeof radioMemory.UR);
    const QByteArray r1 = memory.r1Call.toLatin1().left(sizeof radioMemory.R1);
    const QByteArray r2 = memory.r2Call.toLatin1().left(sizeof radioMemory.R2);
    std::copy(ur.cbegin(), ur.cend(), radioMemory.UR);
    std::copy(r1.cbegin(), r1.cend(), radioMemory.R1);
    std::copy(r2.cbegin(), r2.cend(), radioMemory.R2);
    return radioMemory;
}

inline MemoryType deletedRadioMemory(quint16 group, quint16 channel)
{
    MemoryType memory;
    memory.group = group;
    memory.channel = channel;
    memory.del = true;
    return memory;
}

inline QVector<MemoryType> deletedStoredRadioMemories(const QVector<MemoryType>& memories)
{
    QVector<MemoryType> deletes;
    deletes.reserve(memories.size());
    for (const MemoryType& memory : memories)
    {
        if (radioMemoryIsStored(memory) && memory.group >= kRadioMemoryFirstGroup &&
            memory.group <= kRadioMemoryLastGroup && memory.channel >= kRadioMemoryFirstChannel &&
            memory.channel <= kRadioMemoryLastUserChannel)
        {
            deletes.append(deletedRadioMemory(memory.group, memory.channel));
        }
    }
    std::sort(deletes.begin(), deletes.end(), [](const MemoryType& left, const MemoryType& right)
              { return left.group == right.group ? left.channel < right.channel : left.group < right.group; });
    return deletes;
}

inline QStringList radioMemoryReadbackDifferences(const MemoryType& expected, const MemoryType& actual)
{
    QStringList differences;
    if (expected.group != actual.group || expected.channel != actual.channel)
    {
        differences.append(QStringLiteral("slot"));
        return differences;
    }
    if (expected.del)
    {
        if (radioMemoryIsStored(actual))
        {
            differences.append(QStringLiteral("not deleted"));
        }
        return differences;
    }
    if (!radioMemoryIsStored(actual))
    {
        differences.append(QStringLiteral("empty"));
        return differences;
    }

    const auto compare = [&differences](bool differs, const QString& field)
    {
        if (differs)
        {
            differences.append(field);
        }
    };
    compare(expected.frequency.Hz != actual.frequency.Hz, QStringLiteral("frequency"));
    compare(expected.mode != actual.mode, QStringLiteral("mode"));
    compare(expected.filter != actual.filter, QStringLiteral("filter"));
    compare(expected.datamode != actual.datamode, QStringLiteral("data mode"));
    compare(expected.scan != actual.scan, QStringLiteral("scan group"));
    compare(expected.duplex != actual.duplex, QStringLiteral("duplex mode"));
    if (expected.duplex == 1 || expected.duplex == 2)
    {
        const duplexMode_t duplexMode = expected.duplex == 1 ? dmDupMinus : dmDupPlus;
        const quint64 expectedOffset =
            normalizedOffsetForModeAndHz(duplexMode, expected.duplexOffset.Hz, expected.frequency.Hz);
        const quint64 actualOffset =
            normalizedOffsetForModeAndHz(duplexMode, actual.duplexOffset.Hz, actual.frequency.Hz);
        compare(expectedOffset != actualOffset, QStringLiteral("duplex offset"));
    }
    compare(expected.tonemode != actual.tonemode, QStringLiteral("tone mode"));
    compare(radioMemoryName(expected) != radioMemoryName(actual), QStringLiteral("name"));

    const auto toneMode = static_cast<rptAccessTxRx_t>(expected.tonemode);
    if (memoryToneModeUsesTxTone(toneMode))
    {
        compare(normalizedToneText(expected.tone) != normalizedToneText(actual.tone), QStringLiteral("TX tone"));
    }
    if (memoryToneModeUsesRxTone(toneMode))
    {
        compare(normalizedToneText(expected.tsql) != normalizedToneText(actual.tsql), QStringLiteral("RX tone"));
    }
    if (isDtcsToneMode(toneMode))
    {
        compare(expected.dtcs != actual.dtcs || expected.dtcsp != actual.dtcsp, QStringLiteral("TX DTCS"));
        if (toneMode == ratrDD)
        {
            compare(expected.dtcsB != actual.dtcsB || expected.dtcspB != actual.dtcspB, QStringLiteral("RX DTCS"));
        }
    }

    const radioMode_t mode = memoryModeKindFromRegister(expected.mode);
    if (mode == modeDV || mode == modeDD)
    {
        compare(expected.dsql != actual.dsql, QStringLiteral("digital squelch"));
        compare(expected.dvsql != actual.dvsql, QStringLiteral("digital code"));
        compare(memoryCharField(expected.UR, sizeof expected.UR) != memoryCharField(actual.UR, sizeof actual.UR),
                QStringLiteral("UR callsign"));
        compare(memoryCharField(expected.R1, sizeof expected.R1) != memoryCharField(actual.R1, sizeof actual.R1),
                QStringLiteral("R1 callsign"));
        compare(memoryCharField(expected.R2, sizeof expected.R2) != memoryCharField(actual.R2, sizeof actual.R2),
                QStringLiteral("R2 callsign"));
    }
    return differences;
}

inline bool radioMemoryReadbackMatches(const MemoryType& expected, const MemoryType& actual)
{
    return radioMemoryReadbackDifferences(expected, actual).isEmpty();
}


} // namespace sdr9700::memory
