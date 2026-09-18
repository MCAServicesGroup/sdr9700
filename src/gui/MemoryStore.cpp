#include "MemoryStore.h"

#include "MainWindowHelpers.h"
#include "RadioCapabilities.h"

#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace
{
constexpr quint16 kCsvFirstRadioMemoryChannel = 1;
constexpr quint16 kCsvLastUserRadioMemoryChannel = 99;

const QStringList& memoryCsvHeaders()
{
    static const QStringList headers{
        QStringLiteral("band"),         QStringLiteral("channel"),  QStringLiteral("name"),
        QStringLiteral("receiveHZ"),    QStringLiteral("mode"),     QStringLiteral("filter"),
        QStringLiteral("dataMode"),     QStringLiteral("scan"),     QStringLiteral("duplexMode"),
        QStringLiteral("offsetHZ"),     QStringLiteral("toneMode"), QStringLiteral("tone"),
        QStringLiteral("tsql"),         QStringLiteral("dsql"),     QStringLiteral("dtcs"),
        QStringLiteral("dtcsPolarity"), QStringLiteral("dtcsB"),    QStringLiteral("dtcsPolarityB"),
        QStringLiteral("dvSql"),        QStringLiteral("urCall"),   QStringLiteral("r1Call"),
        QStringLiteral("r2Call")};
    return headers;
}

QString memoryBandLabelForHz(quint64 hz)
{
    return sdr9700::radioBandShortLabel(sdr9700::radioBandForFrequency(hz));
}

quint16 memoryGroupForBandLabel(QString band)
{
    band = band.trimmed();
    bool ok = false;
    const uint numericBand = band.toUInt(&ok);
    if (ok && numericBand >= 1 && numericBand <= 3)
    {
        return static_cast<quint16>(numericBand);
    }
    return 0;
}

QString csvEscaped(QString value)
{
    const bool quote = value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) ||
                       value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'));
    if (!quote)
    {
        return value;
    }
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString csvLine(const QStringList& fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString& field : fields)
    {
        escaped.append(csvEscaped(field));
    }
    return escaped.join(QLatin1Char(','));
}

QVector<QStringList> parseCsvRows(const QString& text, QStringList* errors)
{
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool quoteClosed = false;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (quoted)
        {
            if (ch == QLatin1Char('"'))
            {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"'))
                {
                    field.append(ch);
                    ++i;
                }
                else
                {
                    quoted = false;
                    quoteClosed = true;
                }
            }
            else
            {
                field.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            if (!field.isEmpty() || quoteClosed)
            {
                if (errors)
                {
                    errors->append(
                        QStringLiteral("Malformed CSV: quote inside an unquoted field at character %1").arg(i + 1));
                }
                return {};
            }
            quoted = true;
        }
        else if (ch == QLatin1Char(','))
        {
            row.append(field);
            field.clear();
            quoteClosed = false;
        }
        else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
        {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
            {
                ++i;
            }
            row.append(field);
            rows.append(row);
            row.clear();
            field.clear();
            quoteClosed = false;
        }
        else
        {
            if (quoteClosed)
            {
                if (errors)
                {
                    errors->append(
                        QStringLiteral("Malformed CSV: unexpected text after a closing quote at character %1")
                            .arg(i + 1));
                }
                return {};
            }
            field.append(ch);
        }
    }
    if (quoted)
    {
        if (errors)
        {
            errors->append(QStringLiteral("Malformed CSV: unterminated quoted field"));
        }
        return {};
    }
    if (!field.isEmpty() || !row.isEmpty())
    {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

QString csvValue(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    const int index = indexes.value(key, -1);
    return index >= 0 && index < row.size() ? row.at(index) : QString();
}

int csvInt(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    return csvValue(row, indexes, key).toInt();
}

bool csvHasValue(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    return !csvValue(row, indexes, key).trimmed().isEmpty();
}

bool csvRowIsBlank(const QStringList& row)
{
    return std::all_of(row.cbegin(), row.cend(), [](const QString& field) { return field.trimmed().isEmpty(); });
}

void validateCsvIntegerField(const QStringList& row, const QHash<QString, int>& indexes, const QString& key,
                             int rowNumber, QStringList* errors)
{
    const QString text = csvValue(row, indexes, key).trimmed();
    bool ok = false;
    const int value = text.toInt(&ok);
    Q_UNUSED(value)
    if (!ok && errors)
    {
        errors->append(QStringLiteral("Row %1: %2 must be an integer").arg(rowNumber).arg(key));
    }
}

void validateCsvUInt64Field(const QStringList& row, const QHash<QString, int>& indexes, const QString& key,
                            int rowNumber, QStringList* errors)
{
    const QString text = csvValue(row, indexes, key).trimmed();
    bool ok = false;
    const quint64 value = text.toULongLong(&ok);
    Q_UNUSED(value)
    if (!ok && errors)
    {
        errors->append(QStringLiteral("Row %1: %2 must be an unsigned integer").arg(rowNumber).arg(key));
    }
}

void validateCsvNumericFields(const QStringList& row, const QHash<QString, int>& indexes, int rowNumber,
                              QStringList* errors)
{
    // The CSV file is the operator-facing backup format. Reject malformed
    // numeric values here so bad imports cannot silently become channel 0,
    // simplex, FIL1, or another valid-looking default before the radio write.
    validateCsvIntegerField(row, indexes, QStringLiteral("channel"), rowNumber, errors);
    validateCsvUInt64Field(row, indexes, QStringLiteral("receiveHZ"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("mode"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("filter"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dataMode"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("scan"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("duplexMode"), rowNumber, errors);
    validateCsvUInt64Field(row, indexes, QStringLiteral("offsetHZ"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("toneMode"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dsql"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dtcs"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dtcsPolarity"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dtcsB"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dtcsPolarityB"), rowNumber, errors);
    validateCsvIntegerField(row, indexes, QStringLiteral("dvSql"), rowNumber, errors);
}

ushort toneValueFromRadioText(const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
    {
        return 0;
    }
    return static_cast<ushort>(value * 10.0 + 0.5);
}

ushort memoryToneValueFromFields(rptAccessTxRx_t toneMode, const QString& tone, const QString& tsql, ushort dtcs)
{
    if (isDtcsToneMode(toneMode))
    {
        return dtcs;
    }
    if (toneMode == ratrNN)
    {
        return 0;
    }
    if (toneMode == ratrNT)
    {
        return toneValueFromRadioText(tsql);
    }
    // For TX-capable tone modes, toneValue tracks the TX tone shown in compact
    // labels. Do not infer it from RX tone; CSV import rejects missing required
    // tone fields instead of repairing them at runtime.
    return toneValueFromRadioText(tone);
}

void validateMemoryRecord(const MemoryRecord& memory, const QStringList& row, const QHash<QString, int>& indexes,
                          int rowNumber, QStringList* errors)
{
    if (!errors)
    {
        return;
    }

    auto addError = [errors, rowNumber](const QString& message)
    { errors->append(QStringLiteral("Row %1: %2").arg(rowNumber).arg(message)); };

    if (memory.group == 0)
    {
        addError(QStringLiteral("band must be 1, 2, or 3"));
    }
    if (memory.channel < kCsvFirstRadioMemoryChannel || memory.channel > kCsvLastUserRadioMemoryChannel)
    {
        addError(QStringLiteral("channel must be 1-99"));
    }
    if (memory.receiveHz == 0)
    {
        addError(QStringLiteral("receiveHZ must be a positive integer"));
    }
    const sdr9700::RadioBandDef* frequencyBand =
        sdr9700::radioBandDefinition(sdr9700::radioBandForFrequency(memory.receiveHz));
    if (!frequencyBand || frequencyBand->memGroup != memory.group)
    {
        addError(QStringLiteral("receiveHZ is not in the selected radio band"));
    }
    if (memory.name.size() > sdr9700::memory::kRadioMemoryNameMaxChars)
    {
        addError(QStringLiteral("name is limited to %1 characters").arg(sdr9700::memory::kRadioMemoryNameMaxChars));
    }
    if (QString::fromLatin1(memory.name.toLatin1()) != memory.name)
    {
        addError(QStringLiteral("name must contain Latin-1 characters supported by the radio"));
    }

    static constexpr radioMode_t validModes[] = {modeLSB, modeUSB,  modeAM,     modeCW, modeRTTY,
                                                 modeFM,  modeCW_R, modeRTTY_R, modeDV, modeDD};
    if (std::find(std::cbegin(validModes), std::cend(validModes), static_cast<radioMode_t>(memory.mode)) ==
        std::cend(validModes))
    {
        addError(QStringLiteral("mode is not supported by the IC-9700 memory editor"));
    }
    if (memory.filter < 1 || memory.filter > 3)
    {
        addError(QStringLiteral("filter must be 1-3"));
    }
    if (memory.dataMode < 0 || memory.dataMode > 1)
    {
        addError(QStringLiteral("dataMode must be 0 or 1"));
    }
    if (memory.scan < 0 || memory.scan > 3)
    {
        addError(QStringLiteral("scan must be 0-3"));
    }
    const auto duplex = static_cast<duplexMode_t>(memory.duplexMode);
    if (duplex != dmSimplex && duplex != dmDupMinus && duplex != dmDupPlus)
    {
        addError(QStringLiteral("duplexMode must be simplex, minus, or plus"));
    }
    if (duplex == dmSimplex && memory.offsetHz != 0)
    {
        addError(QStringLiteral("offsetHZ must be 0 for simplex memories"));
    }
    if ((duplex == dmDupMinus || duplex == dmDupPlus) && memory.offsetHz == 0)
    {
        addError(QStringLiteral("offsetHZ must be positive for repeater memories"));
    }
    if (memory.dsql < 0 || memory.dsql > 2)
    {
        addError(QStringLiteral("dsql must be 0-2"));
    }
    if (memory.dtcsPolarity < 0 || memory.dtcsPolarity > 3 || memory.dtcsPolarityB < 0 || memory.dtcsPolarityB > 3)
    {
        addError(QStringLiteral("DTCS polarity values must be 0-3"));
    }
    if (memory.dvSql < 0 || memory.dvSql > 99)
    {
        addError(QStringLiteral("dvSql must be 0-99"));
    }
    const auto validCall = [](const QString& value)
    { return value.size() <= 8 && QString::fromLatin1(value.toLatin1()) == value; };
    if (!validCall(memory.urCall) || !validCall(memory.r1Call) || !validCall(memory.r2Call))
    {
        addError(QStringLiteral("D-STAR callsigns are limited to 8 Latin-1 characters"));
    }

    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    switch (toneMode)
    {
    case ratrNN:
        break;
    case ratrTN:
        if (!csvHasValue(row, indexes, QStringLiteral("tone")))
        {
            addError(QStringLiteral("tone is required when toneMode is TONE TX"));
        }
        break;
    case ratrNT:
        if (!csvHasValue(row, indexes, QStringLiteral("tsql")))
        {
            addError(QStringLiteral("tsql is required when toneMode is TONE RX"));
        }
        break;
    case ratrTT:
    case ratrTD:
        if (!csvHasValue(row, indexes, QStringLiteral("tone")) || !csvHasValue(row, indexes, QStringLiteral("tsql")))
        {
            addError(QStringLiteral("tone and tsql are required when toneMode uses TX and RX tones"));
        }
        break;
    case ratrDN:
        if (!csvHasValue(row, indexes, QStringLiteral("dtcs")))
        {
            addError(QStringLiteral("dtcs is required when toneMode is DTCS TX"));
        }
        break;
    case ratrDD:
        if (!csvHasValue(row, indexes, QStringLiteral("dtcs")) || !csvHasValue(row, indexes, QStringLiteral("dtcsB")))
        {
            addError(QStringLiteral("dtcs and dtcsB are required when toneMode uses TX and RX DTCS"));
        }
        break;
    case ratrDT:
        if (!csvHasValue(row, indexes, QStringLiteral("dtcs")) || !csvHasValue(row, indexes, QStringLiteral("tsql")))
        {
            addError(QStringLiteral("dtcs and tsql are required when toneMode uses DTCS TX and tone RX"));
        }
        break;
    default:
        addError(QStringLiteral("toneMode is not supported by SDR9700 memory import"));
        break;
    }

    const auto validTone = [](const QString& value)
    {
        if (value.trimmed().isEmpty())
        {
            return true;
        }
        const ushort tone = toneValueFromRadioText(value);
        return std::any_of(std::cbegin(sdr9700::ui::main_window::kTonePresets),
                           std::cend(sdr9700::ui::main_window::kTonePresets),
                           [tone](const auto& preset) { return preset.tone == tone; });
    };
    if (!validTone(memory.tone) || !validTone(memory.tsql))
    {
        addError(QStringLiteral("tone and tsql must use IC-9700 preset frequencies"));
    }
    const auto validDtcs = [](ushort code)
    {
        return std::find(std::cbegin(sdr9700::ui::main_window::kDtcsCodes),
                         std::cend(sdr9700::ui::main_window::kDtcsCodes),
                         code) != std::cend(sdr9700::ui::main_window::kDtcsCodes);
    };
    if (!validDtcs(memory.dtcs) || !validDtcs(memory.dtcsB))
    {
        addError(QStringLiteral("dtcs and dtcsB must use IC-9700 preset codes"));
    }
}

QString normalizedToneText(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
    {
        return QString();
    }

    const ushort value = toneValueFromRadioText(text);
    return value > 0 ? sdr9700::ui::main_window::toneFrequencyLabel(value) : text;
}

quint64 csvUInt64(const QStringList& row, const QHash<QString, int>& indexes, const QString& key)
{
    return csvValue(row, indexes, key).toULongLong();
}

} // namespace

int memoryBandKeyForHz(quint64 hz)
{
    return sdr9700::radioBandMemoryKey(sdr9700::radioBandForFrequency(hz));
}

QString memoryBandLabelForGroup(quint16 group)
{
    switch (group)
    {
    case 0:
        return QStringLiteral("SAT");
    case 1:
        return QStringLiteral("2M");
    case 2:
        return QStringLiteral("70CM");
    case 3:
        return QStringLiteral("23CM");
    default:
        return QString();
    }
}

QByteArray memoriesExportCsv(const QVector<MemoryRecord>& memories)
{
    QStringList lines;
    lines.append(csvLine(memoryCsvHeaders()));
    for (const MemoryRecord& memory : memories)
    {
        lines.append(csvLine({QString::number(memory.group),
                              QString::number(memory.channel),
                              memory.name,
                              QString::number(memory.receiveHz),
                              QString::number(memory.mode),
                              QString::number(memory.filter),
                              QString::number(memory.dataMode),
                              QString::number(memory.scan),
                              QString::number(memory.duplexMode),
                              QString::number(memory.offsetHz),
                              QString::number(memory.toneMode),
                              memory.tone,
                              memory.tsql,
                              QString::number(memory.dsql),
                              QString::number(memory.dtcs),
                              QString::number(memory.dtcsPolarity),
                              QString::number(memory.dtcsB),
                              QString::number(memory.dtcsPolarityB),
                              QString::number(memory.dvSql),
                              memory.urCall,
                              memory.r1Call,
                              memory.r2Call}));
    }
    return lines.join(QLatin1Char('\n')).append(QLatin1Char('\n')).toUtf8();
}


QVector<MemoryRecord> memoriesFromCsv(const QByteArray& data, QStringList* errors)
{
    QStringList localErrors;
    QStringList* importErrors = errors ? errors : &localErrors;
    importErrors->clear();

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString csvText = decoder(data);
    if (decoder.hasError())
    {
        importErrors->append(QStringLiteral("The CSV file is not valid UTF-8"));
        return {};
    }
    const QVector<QStringList> rows = parseCsvRows(csvText, importErrors);
    if (rows.isEmpty())
    {
        return {};
    }

    QHash<QString, int> indexes;
    const QStringList headers = rows.first();
    for (int i = 0; i < headers.size(); ++i)
    {
        const QString header = headers.at(i).trimmed();
        if (indexes.contains(header))
        {
            importErrors->append(QStringLiteral("Duplicate CSV column: %1").arg(header));
        }
        indexes.insert(header, i);
    }
    for (const QString& header : memoryCsvHeaders())
    {
        if (!indexes.contains(header))
        {
            importErrors->append(QStringLiteral("Missing CSV column: %1").arg(header));
        }
    }
    if (!importErrors->isEmpty())
    {
        return {};
    }

    QVector<MemoryRecord> memories;
    QSet<quint32> importedChannels;
    memories.reserve(rows.size() - 1);
    for (int i = 1; i < rows.size(); ++i)
    {
        const QStringList& row = rows.at(i);
        if (csvRowIsBlank(row))
        {
            continue;
        }
        if (row.size() != headers.size())
        {
            importErrors->append(
                QStringLiteral("Row %1: expected %2 fields, found %3").arg(i + 1).arg(headers.size()).arg(row.size()));
            continue;
        }

        const int rowErrorCount = importErrors->size();
        validateCsvNumericFields(row, indexes, i + 1, importErrors);
        if (importErrors->size() != rowErrorCount)
        {
            continue;
        }

        const int channel = csvInt(row, indexes, QStringLiteral("channel"));
        const int dtcs = csvInt(row, indexes, QStringLiteral("dtcs"));
        const int dtcsB = csvInt(row, indexes, QStringLiteral("dtcsB"));
        const auto validateUnsigned16 = [i, importErrors](int value, const QString& field)
        {
            if (value < 0 || value > std::numeric_limits<quint16>::max())
            {
                importErrors->append(
                    QStringLiteral("Row %1: %2 is outside the supported unsigned 16-bit range").arg(i + 1).arg(field));
            }
        };
        validateUnsigned16(channel, QStringLiteral("channel"));
        validateUnsigned16(dtcs, QStringLiteral("dtcs"));
        validateUnsigned16(dtcsB, QStringLiteral("dtcsB"));
        if (importErrors->size() != rowErrorCount)
        {
            continue;
        }

        MemoryRecord memory;
        memory.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        memory.group = memoryGroupForBandLabel(csvValue(row, indexes, QStringLiteral("band")));
        memory.channel = static_cast<quint16>(channel);
        memory.name = csvValue(row, indexes, QStringLiteral("name"));
        memory.receiveHz = csvUInt64(row, indexes, QStringLiteral("receiveHZ"));
        memory.mode = csvInt(row, indexes, QStringLiteral("mode"));
        memory.filter = csvInt(row, indexes, QStringLiteral("filter"));
        memory.dataMode = csvInt(row, indexes, QStringLiteral("dataMode"));
        memory.scan = csvInt(row, indexes, QStringLiteral("scan"));
        memory.duplexMode = csvInt(row, indexes, QStringLiteral("duplexMode"));
        memory.offsetHz = csvUInt64(row, indexes, QStringLiteral("offsetHZ"));
        memory.toneMode = csvInt(row, indexes, QStringLiteral("toneMode"));
        memory.tone = normalizedToneText(csvValue(row, indexes, QStringLiteral("tone")));
        memory.tsql = normalizedToneText(csvValue(row, indexes, QStringLiteral("tsql")));
        memory.dsql = csvInt(row, indexes, QStringLiteral("dsql"));
        memory.dtcs = static_cast<ushort>(dtcs);
        memory.dtcsPolarity = csvInt(row, indexes, QStringLiteral("dtcsPolarity"));
        memory.dtcsB = static_cast<ushort>(dtcsB);
        memory.dtcsPolarityB = csvInt(row, indexes, QStringLiteral("dtcsPolarityB"));
        memory.toneValue = memoryToneValueFromFields(static_cast<rptAccessTxRx_t>(memory.toneMode), memory.tone,
                                                     memory.tsql, memory.dtcs);
        memory.dvSql = csvInt(row, indexes, QStringLiteral("dvSql"));
        memory.urCall = csvValue(row, indexes, QStringLiteral("urCall"));
        memory.r1Call = csvValue(row, indexes, QStringLiteral("r1Call"));
        memory.r2Call = csvValue(row, indexes, QStringLiteral("r2Call"));
        memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
        memory.band = memoryBandLabelForHz(memory.receiveHz);
        memory.shift =
            sdr9700::ui::main_window::offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
        memory.toneOption = sdr9700::ui::main_window::toneOptionLabel(static_cast<rptAccessTxRx_t>(memory.toneMode));
        memory.toneFrequency = sdr9700::ui::main_window::memoryToneFrequencyLabel(
            static_cast<rptAccessTxRx_t>(memory.toneMode), memory.toneValue);
        validateMemoryRecord(memory, row, indexes, i + 1, importErrors);
        const quint32 channelKey = (quint32(memory.group) << 16U) | memory.channel;
        if (importedChannels.contains(channelKey))
        {
            importErrors->append(QStringLiteral("Row %1: duplicate band/channel %2/%3")
                                     .arg(i + 1)
                                     .arg(memory.group)
                                     .arg(memory.channel));
        }
        if (importErrors->size() == rowErrorCount)
        {
            importedChannels.insert(channelKey);
            memories.append(memory);
        }
    }
    return memories;
}

bool writeMemoriesCsvFile(const QString& path, const QVector<MemoryRecord>& memories, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error)
        {
            *error = file.errorString();
        }
        return false;
    }

    const QByteArray data = memoriesExportCsv(memories);
    if (file.write(data) != static_cast<qint64>(data.size()) || !file.commit())
    {
        if (error)
        {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

QVector<MemoryRecord> readMemoriesCsvFile(const QString& path, QStringList* errors, QString* fileError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (fileError)
        {
            *fileError = file.errorString();
        }
        return {};
    }
    return memoriesFromCsv(file.readAll(), errors);
}
