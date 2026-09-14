// QtTest invokes private slots through the generated meta-object.
#include "MainWindowHelpers.h"

#include <QtTest>

using namespace sdr9700::ui::main_window;

class MainWindowHelpersTest : public QObject
{
    Q_OBJECT

  private slots:
    void parsesFrequencyText_data();
    void parsesFrequencyText();
    void rejectsInvalidFrequencyText_data();
    void rejectsInvalidFrequencyText();
    void formatsRadioValues();
    void mapsTuningSteps_data();
    void mapsTuningSteps();
    void movesBetweenTuningSteps();
    void providesBandSpecificOffsets();
    void identifiesModesWithSelectableAgcPresets();
    void preservesMemorySelectionAcrossPttFrequencyTransitions();
    void allowsRc28PttWhileControlsAreLocked();
    void keepsNewestSpectrumTuneIntentAcrossStaleReadbacks();
};

void MainWindowHelpersTest::parsesFrequencyText_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<quint64>("expectedHz");

    QTest::newRow("decimal-megahertz") << QStringLiteral("146.520") << quint64(146520000);
    QTest::newRow("grouped-frequency") << QStringLiteral("146.520.000") << quint64(146520000);
    QTest::newRow("comma-decimal") << QStringLiteral("146,520 MHz") << quint64(146520000);
    QTest::newRow("surrounding-space") << QStringLiteral(" 1296.100 MHz ") << quint64(1296100000);
    QTest::newRow("rounded-hertz") << QStringLiteral("146.520001") << quint64(146520001);
}

void MainWindowHelpersTest::parsesFrequencyText()
{
    QFETCH(QString, text);
    QFETCH(quint64, expectedHz);

    quint64 actualHz = 0;
    QVERIFY(parseFrequencyText(text, &actualHz));
    QCOMPARE(actualHz, expectedHz);
}

void MainWindowHelpersTest::rejectsInvalidFrequencyText_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << QString();
    QTest::newRow("not-a-number") << QStringLiteral("radio");
    QTest::newRow("below-minimum") << QStringLiteral("0.100");
    QTest::newRow("empty-middle-group") << QStringLiteral("146..000");
    QTest::newRow("oversized-kilohertz-group") << QStringLiteral("146.0520.000");
    QTest::newRow("oversized-hertz-group") << QStringLiteral("146.520.0000");
}

void MainWindowHelpersTest::rejectsInvalidFrequencyText()
{
    QFETCH(QString, text);

    quint64 actualHz = 42;
    QVERIFY(!parseFrequencyText(text, &actualHz));
    QCOMPARE(actualHz, quint64(42));
}

void MainWindowHelpersTest::formatsRadioValues()
{
    QCOMPARE(formatFrequency(146520000), QStringLiteral("146.520.000"));
    QCOMPARE(formatFrequency(432100000), QStringLiteral("432.100.000"));
    QCOMPARE(formatFrequency(999999999), QStringLiteral("999.999.999"));
    QCOMPARE(formatFrequency(1294713546), QStringLiteral("1294.713.546"));
    QCOMPARE(memoryFrequencyLabel(1296100000), QStringLiteral("1296.100.000"));
    QCOMPARE(formatOffsetMhz(600000), QStringLiteral("0.600"));
    QCOMPARE(offsetModeLabel(dmSimplex, 0), QStringLiteral("SIMPLEX"));
    QCOMPARE(offsetModeLabel(dmDupMinus, 600000), QStringLiteral("-0.600"));
    QCOMPARE(offsetModeLabel(dmDupPlus, 5000000), QStringLiteral("+5.000"));
    QCOMPARE(offsetModeLabel(dmDupMinus, 0), QStringLiteral("SIMPLEX"));
    QCOMPARE(offsetModeLabel(dmDupPlus, 0), QStringLiteral("SIMPLEX"));
    QCOMPARE(toneFrequencyLabel(1000), QStringLiteral("100.0"));
    QCOMPARE(dtcsCodeLabel(23), QStringLiteral("023"));
    QCOMPARE(toneOptionLabel(ratrTN), QStringLiteral("TONE"));
    QCOMPARE(toneOptionLabel(ratrTT), QStringLiteral("TSQL"));
    QCOMPARE(toneOptionLabel(ratrDD), QStringLiteral("DTCS"));
    QCOMPARE(memoryModeLabel(modeFM), QStringLiteral("FM"));
    QCOMPARE(radioLevelPercent(-1), 0);
    QCOMPARE(radioLevelPercent(0), 0);
    QCOMPARE(radioLevelPercent(127), 50);
    QCOMPARE(radioLevelPercent(180), 71);
    QCOMPARE(radioLevelPercent(255), 100);
    QCOMPARE(radioLevelPercent(256), 100);
}

void MainWindowHelpersTest::mapsTuningSteps_data()
{
    QTest::addColumn<int>("hz");
    QTest::addColumn<int>("radioStep");

    for (const StepPreset& preset : kStepPresets)
    {
        QTest::newRow(preset.label) << preset.hz << preset.radioStep;
    }
    QTest::newRow("unsupported") << 250 << -1;
}

void MainWindowHelpersTest::mapsTuningSteps()
{
    QFETCH(int, hz);
    QFETCH(int, radioStep);

    QCOMPARE(radioTuningStepForHz(hz), radioStep);
}

void MainWindowHelpersTest::movesBetweenTuningSteps()
{
    QCOMPARE(adjacentTuningStepHz(100, 1), 500);
    QCOMPARE(adjacentTuningStepHz(100, -1), 10);
    QCOMPARE(adjacentTuningStepHz(kStepPresets[std::size(kStepPresets) - 1].hz, 1), kStepPresets[0].hz);
    QCOMPARE(adjacentTuningStepHz(kStepPresets[0].hz, -1), kStepPresets[std::size(kStepPresets) - 1].hz);
    QCOMPARE(adjacentTuningStepHz(250, 1), kStepPresets[0].hz);
    QCOMPARE(adjacentTuningStepHz(250, -1), kStepPresets[std::size(kStepPresets) - 1].hz);
}

void MainWindowHelpersTest::providesBandSpecificOffsets()
{
    const QVector<OffsetPreset> twoMeter = offsetPresetsForHz(146520000);
    QCOMPARE(twoMeter.size(), 2);
    QCOMPARE(twoMeter.at(0).mode, dmDupMinus);
    QCOMPARE(twoMeter.at(0).hz, quint64(600000));
    QCOMPARE(twoMeter.at(1).mode, dmDupPlus);

    const QVector<OffsetPreset> seventyCentimeter = offsetPresetsForHz(440000000);
    QCOMPARE(seventyCentimeter.size(), 1);
    QCOMPARE(seventyCentimeter.constFirst().hz, quint64(5000000));

    QVERIFY(offsetPresetsForHz(50000000).isEmpty());
}

void MainWindowHelpersTest::identifiesModesWithSelectableAgcPresets()
{
    for (const QString& mode : {QStringLiteral("USB"), QStringLiteral("LSB"), QStringLiteral("CW"),
                                QStringLiteral("RTTY"), QStringLiteral("AM")})
    {
        QVERIFY2(agcPresetSelectableForMode(mode), qPrintable(mode));
    }
    for (const QString& mode : {QStringLiteral("FM"), QStringLiteral("DV"), QStringLiteral("DD")})
    {
        QVERIFY2(!agcPresetSelectableForMode(mode), qPrintable(mode));
        QCOMPARE(agcDisplayMode(mode, QStringLiteral("slow")), QStringLiteral("FAST"));
    }
    QCOMPARE(agcDisplayMode(QStringLiteral("USB"), QStringLiteral("slow")), QStringLiteral("SLOW"));
}

void MainWindowHelpersTest::preservesMemorySelectionAcrossPttFrequencyTransitions()
{
    constexpr quint64 receiveHz = 145410000;
    constexpr quint64 transmitHz = 144810000;
    constexpr quint64 intermediateHz = 145000000;

    QVERIFY(preserveMemorySelectionForReportedFrequency(receiveHz, transmitHz, true, true));
    QVERIFY(preserveMemorySelectionForReportedFrequency(receiveHz, intermediateHz, true, true));
    QVERIFY(preserveMemorySelectionForReportedFrequency(receiveHz, transmitHz, false, true));
    QVERIFY(preserveMemorySelectionForReportedFrequency(receiveHz, receiveHz, false, true));
    QVERIFY(!preserveMemorySelectionForReportedFrequency(receiveHz, intermediateHz, false, false));

    // A second PTT cycle must receive the same protection after the first one
    // has returned to the memory's receive frequency.
    QVERIFY(preserveMemorySelectionForReportedFrequency(receiveHz, transmitHz, true, true));
    QVERIFY(preserveMemorySelectionForReportedFrequency(receiveHz, receiveHz, false, true));
}

void MainWindowHelpersTest::allowsRc28PttWhileControlsAreLocked()
{
    QVERIFY(rc28PttAllowed(true, true, false));
    QVERIFY(rc28PttAllowed(true, true, true));
    QVERIFY(!rc28PttAllowed(false, true, true));
    QVERIFY(!rc28PttAllowed(true, false, true));
}

void MainWindowHelpersTest::keepsNewestSpectrumTuneIntentAcrossStaleReadbacks()
{
    constexpr quint64 baseHz = 146000000;
    quint64 pendingHz = 0;

    for (quint64 index = 1; index <= 500; ++index)
    {
        pendingHz = baseHz + index * 100;
        QCOMPARE(spectrumTuneDisplayFrequency(baseHz, pendingHz), pendingHz);

        // Replies for every earlier click may arrive after this newer intent.
        // None may clear or move the displayed target backward.
        for (quint64 stale = 0; stale < index; ++stale)
        {
            pendingHz = spectrumTunePendingAfterReadback(pendingHz, baseHz + stale * 100);
            QCOMPARE(pendingHz, baseHz + index * 100);
            QCOMPARE(spectrumTuneDisplayFrequency(baseHz + stale * 100, pendingHz), baseHz + index * 100);
        }
    }

    pendingHz = spectrumTunePendingAfterReadback(pendingHz, baseHz + 500 * 100);
    QCOMPARE(pendingHz, quint64(0));
    QCOMPARE(spectrumTuneDisplayFrequency(baseHz + 500 * 100, pendingHz), baseHz + 500 * 100);
}

QTEST_GUILESS_MAIN(MainWindowHelpersTest)
#include "MainWindowHelpersTest.moc"
