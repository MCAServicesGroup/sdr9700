// QtTest invokes private slots through the generated meta-object.
#include "IRadioBackend.h"
#include "MainSubExchangeConfirmationPolicy.h"
#include "SameBandRefreshPolicy.h"
#include "VfoReceiverCommandRoute.h"
#include "VfoModel.h"
#include "RadioState.h"
#include "VfoController.h"
#include "VfoDisplay.h"
#include "VfoSelectionController.h"

#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QtTest>

class FakeRadioBackend : public IRadioBackend
{
  public:
    using IRadioBackend::IRadioBackend;

    void connectToRadio(const QString&, quint16, const QString&, const QString&) override {}
    void disconnectFromRadio() override {}
    void setFrequencyHz(quint64 hz) override
    {
        frequencyHz = hz;
        ++frequencyCalls;
    }
    void setMode(const QString& value) override
    {
        mode = value;
        ++modeCalls;
    }
    void setFilterWidth(int low, int high) override
    {
        filterLow = low;
        filterHigh = high;
    }
    void setNrEnabled(bool) override {}
    void setNrLevel(int value) override { nrLevel = value; }
    void setNbEnabled(bool) override {}
    void setNbLevel(int value) override { nbLevel = value; }
    void setPreampEnabled(bool) override {}
    void setPreampLevel(int value) override { preampLevel = value; }
    void setAttenuatorEnabled(bool) override {}
    void setAfGain(int value) override { afGain = value; }
    void setRfGain(int value) override { rfGain = value; }
    void setSquelch(bool on, int value) override
    {
        squelchOn = on;
        squelchLevel = value;
    }
    void setAgcMode(const QString&) override {}
    void setAutoNotch(bool) override {}
    void setManualNotch(bool) override {}
    void setCompressor(bool) override {}
    void setCompressorLevel(int value) override { compressorLevel = value; }
    void setXfcEnabled(bool) override {}
    void setRitEnabled(bool) override {}
    void setRitOffset(short value) override { ritOffset = value; }
    void setDuplexMode(duplexMode_t) override {}
    void setRepeaterOffsetHz(quint64) override {}
    void setToneAccessMode(rptAccessTxRx_t) override {}
    void setToneFrequency(ushort) override {}
    void setDtcsCode(ushort) override {}
    void setScopeEnabled(bool) override {}
    void setScopeSpanHz(quint64) override {}
    void setScopeMode(int) override {}
    void setScopeVfo(Vfo) override {}
    void setScopeFixedRangeHz(quint64, quint64) override {}
    void setSpectrumFramesPerSecond(int) override {}
    bool setPtt(bool value) override
    {
        ptt = value;
        if (pttAccepted)
        {
            emit pttRequestAccepted(value);
        }
        return pttAccepted;
    }
    void setTxPower(int value) override { txPower = value; }
    void setTuningStep(int) override {}
    void setDialLockEnabled(bool enabled) override { dialLockEnabled = enabled; }
    void selectVfo(Vfo value) override
    {
        selectedVfo = value;
        ++selectVfoCalls;
    }
    void exchangeMainSub() override { ++exchangeCalls; }
    void setVfoFrequencyHz(Vfo vfo, quint64 hz) override
    {
        requestedFrequencyVfo = vfo;
        requestedFrequencyHz = hz;
        ++vfoFrequencyCalls;
    }
    void setVfoMode(Vfo, const QString&) override {}
    void setVfoFilter(Vfo vfo, const QString& modeName, int filter) override
    {
        requestedFilterVfo = vfo;
        requestedFilterMode = modeName;
        requestedFilter = filter;
        ++vfoFilterCalls;
    }
    void applyVfoBandRecall(Vfo vfo, const VfoBandRecallRequest& recall) override
    {
        recalledVfo = vfo;
        recalledBand = recall;
        ++bandRecallCalls;
    }
    void requestVfoState(Vfo value) override
    {
        requestedVfoState = value;
        ++requestedVfoStateCalls;
    }
    void setVfoAgcMode(Vfo, const QString&) override {}
    void setVfoAttenuatorEnabled(Vfo, bool) override {}
    void setVfoNbEnabled(Vfo, bool enabled) override
    {
        requestedNbEnabled = enabled;
        ++vfoNbEnabledCalls;
    }
    void setVfoNbLevel(Vfo, int level) override
    {
        requestedNbLevel = level;
        ++vfoNbLevelCalls;
    }
    void setVfoNotch(Vfo, VfoNotch notch) override
    {
        requestedNotch = notch;
        ++vfoNotchCalls;
    }
    void setVfoNrEnabled(Vfo, bool enabled) override
    {
        requestedNrEnabled = enabled;
        ++vfoNrEnabledCalls;
    }
    void setVfoNrLevel(Vfo, int level) override
    {
        requestedNrLevel = level;
        ++vfoNrLevelCalls;
    }
    void setVfoPreampLevel(Vfo, int) override {}
    void setVfoRfGain(Vfo vfo, int level) override
    {
        requestedLevelVfo = vfo;
        requestedRfGain = level;
        ++vfoRfGainCalls;
    }
    void setVfoSquelch(Vfo vfo, int level) override
    {
        requestedLevelVfo = vfo;
        requestedSquelch = level;
        ++vfoSquelchCalls;
    }
    bool setDualWatchEnabled(bool enabled) override
    {
        ++dualWatchCalls;
        if (!dualWatchAccepted)
        {
            return false;
        }
        dualWatchEnabled = enabled;
        return true;
    }
    void pollFrequency() override { ++frequencyPollCalls; }
    void selectVfoMode() override {}
    void selectRadioMemory(quint16 group, quint16 channel, Vfo targetVfo) override
    {
        selectedMemoryGroup = group;
        selectedMemoryChannel = channel;
        selectedMemoryVfo = targetVfo;
        ++selectedMemoryCalls;
    }
    void requestRadioMemory(quint16, quint16) override {}
    void requestSatelliteMemory(quint16) override {}
    void writeRadioMemory(MemoryType) override {}

    quint64 frequencyHz{0};
    QString mode;
    int filterLow{0};
    int filterHigh{0};
    int compressorLevel{0};
    int preampLevel{-1};
    int nrLevel{-1};
    int nbLevel{-1};
    int afGain{-1};
    int rfGain{-1};
    bool squelchOn{false};
    int squelchLevel{-1};
    short ritOffset{0};
    bool ptt{false};
    bool pttAccepted{true};
    int txPower{-1};
    int frequencyCalls{0};
    int frequencyPollCalls{0};
    int modeCalls{0};
    int selectVfoCalls{0};
    Vfo selectedVfo{Vfo::Main};
    Vfo requestedVfoState{Vfo::Main};
    int requestedVfoStateCalls{0};
    bool dualWatchEnabled{false};
    bool dialLockEnabled{false};
    bool dualWatchAccepted{true};
    int dualWatchCalls{0};
    int exchangeCalls{0};
    quint16 selectedMemoryGroup{0};
    quint16 selectedMemoryChannel{0};
    Vfo selectedMemoryVfo{Vfo::Main};
    int selectedMemoryCalls{0};
    Vfo recalledVfo{Vfo::Main};
    VfoBandRecallRequest recalledBand;
    int bandRecallCalls{0};
    Vfo requestedFrequencyVfo{Vfo::Main};
    quint64 requestedFrequencyHz{0};
    int vfoFrequencyCalls{0};
    Vfo requestedFilterVfo{Vfo::Main};
    QString requestedFilterMode;
    int requestedFilter{0};
    int vfoFilterCalls{0};
    bool requestedNbEnabled{false};
    int requestedNbLevel{0};
    int vfoNbEnabledCalls{0};
    int vfoNbLevelCalls{0};
    VfoNotch requestedNotch{VfoNotch::Off};
    int vfoNotchCalls{0};
    bool requestedNrEnabled{false};
    int requestedNrLevel{0};
    int vfoNrEnabledCalls{0};
    int vfoNrLevelCalls{0};
    Vfo requestedLevelVfo{Vfo::Main};
    int requestedRfGain{-1};
    int requestedSquelch{-1};
    int vfoRfGainCalls{0};
    int vfoSquelchCalls{0};
};

class VfoBackendTest : public QObject
{
    Q_OBJECT

  private slots:
    void radioBackedRequestsWaitForConfirmation();
    void localControlsUpdateAndForward();
    void boundedRequestsAreClampedBeforeForwarding();
    void reportsRejectedPttRequest();
    void pollsReceiveFrequencyAfterConfirmedUnkey();
    void receiverCommandRouteSelectsCommandsAndRestoresInOrder();
    void mapsVfoToReceiverByte();
    void alternatesInactiveMeterSamplesDuringDualWatch();
    void suppressesReceiverMeterPollingDuringContextTransitions();
    void exchangeModeConfirmationAlwaysRequiresReceiverFrequency();
    void survivesRepeatedExchangeAndTunePressure();
    void sameBandRefreshIsEdgeTriggered();
    void radioStateKeepsReceiverAndBandRecallIsolated();
    void radioStateInvalidatesLiveStateButKeepsSessionRecallSeparate();
    void vfoDisplayConsumesConfirmedRadioStateWithoutReceiverBleed();
    void keepsTransmitFrequencyLabelStableAcrossRepeaterPtt();
    void filtersMenuKeepsControlColumnsAligned();
    void levelMenuUsesSameRoundedPercentageAsControl_data();
    void levelMenuUsesSameRoundedPercentageAsControl();
    void receiverLevelsIgnoreStaleReadbacks_data();
    void receiverLevelsIgnoreStaleReadbacks();
    void receiverLevelTimeoutRefreshesAuthoritativeState();
    void mainSubExchangeKeepsReceiverLevelState();
    void controllerFrequencyRequestWaitsForRadioConfirmation();
    void bandRecallRejectsPressureUntilCompleteIdentitySettles();
    void uiSelectionIgnoresBackgroundReceiverRouting();
    void dualWatchRequestReportsBackendAcceptance();
    void receiverPairActionsRespectLifecycleGates();
};

void VfoBackendTest::levelMenuUsesSameRoundedPercentageAsControl_data()
{
    QTest::addColumn<int>("receiver");

    QTest::newRow("main") << 0;
    QTest::newRow("sub") << 1;
}

void VfoBackendTest::levelMenuUsesSameRoundedPercentageAsControl()
{
    QFETCH(int, receiver);

    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(receiver == 0 ? Vfo::Main : Vfo::Sub, &backend, &state, &parent);

    Frequency frequency;
    frequency.Hz = receiver == 0 ? 146520000 : 433920000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), receiver);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), receiver);
    emit backend.radioValueConfirmed(funcSquelch, 180, receiver);

    auto* squelchButton = controller.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    QVERIFY(squelchButton);
    QCOMPARE(squelchButton->text(), QStringLiteral("SQL 71%"));

    bool inspected = false;
    QTimer::singleShot(0, this,
                       [&inspected]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu);
                           auto* slider = menu->findChild<QSlider*>(QStringLiteral("vfoSQLLevelSlider"));
                           auto* label = menu->findChild<QLabel*>(QStringLiteral("vfoSQLLevelLabel"));
                           QVERIFY(slider);
                           QVERIFY(label);
                           QCOMPARE(slider->value(), 180);
                           QCOMPARE(label->text(), QStringLiteral("71%"));
                           inspected = true;
                           menu->close();
                       });
    emit controller.display()->receiverControlClicked(QStringLiteral("SQL"));
    QVERIFY(inspected);
}

void VfoBackendTest::receiverLevelsIgnoreStaleReadbacks_data()
{
    QTest::addColumn<int>("receiver");
    QTest::addColumn<int>("function");
    QTest::addColumn<QString>("control");

    QTest::newRow("main-rfg") << 0 << int(funcRfGain) << QStringLiteral("RFG");
    QTest::newRow("main-sql") << 0 << int(funcSquelch) << QStringLiteral("SQL");
    QTest::newRow("sub-rfg") << 1 << int(funcRfGain) << QStringLiteral("RFG");
    QTest::newRow("sub-sql") << 1 << int(funcSquelch) << QStringLiteral("SQL");
}

void VfoBackendTest::receiverLevelsIgnoreStaleReadbacks()
{
    QFETCH(int, receiver);
    QFETCH(int, function);
    QFETCH(QString, control);
    const auto func = static_cast<Funcs>(function);
    const Vfo vfo = receiver == 0 ? Vfo::Main : Vfo::Sub;

    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(vfo, &backend, &state, &parent);

    Frequency frequency;
    frequency.Hz = receiver == 0 ? 146520000 : 433920000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), receiver);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), receiver);
    emit backend.radioValueConfirmed(func, 59, receiver);

    auto* button = controller.display()->findChild<QPushButton*>(QStringLiteral("vfo%1Button").arg(control));
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("%1 23%").arg(control));

    bool submitted = false;
    QTimer::singleShot(0, this,
                       [&submitted, control]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu);
                           auto* slider = menu->findChild<QSlider*>(QStringLiteral("vfo%1LevelSlider").arg(control));
                           QVERIFY(slider);
                           slider->setValue(102);
                           QVERIFY(QMetaObject::invokeMethod(slider, "sliderReleased", Qt::DirectConnection));
                           submitted = true;
                           menu->close();
                       });
    emit controller.display()->receiverControlClicked(control);
    QVERIFY(submitted);
    QCOMPARE(backend.requestedLevelVfo, vfo);
    if (func == funcRfGain)
    {
        QCOMPARE(backend.vfoRfGainCalls, 1);
        QCOMPARE(backend.requestedRfGain, 102);
    }
    else
    {
        QCOMPARE(backend.vfoSquelchCalls, 1);
        QCOMPARE(backend.requestedSquelch, 102);
    }

    emit backend.radioValueConfirmed(func, 80, receiver);
    QCOMPARE(button->text(), QStringLiteral("%1 23%").arg(control));
    emit backend.radioValueConfirmed(func, 102, receiver);
    QCOMPARE(button->text(), QStringLiteral("%1 40%").arg(control));
}

void VfoBackendTest::receiverLevelTimeoutRefreshesAuthoritativeState()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(Vfo::Sub, &backend, &state, &parent);

    Frequency frequency;
    frequency.Hz = 433920000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 1);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 1);
    emit backend.radioValueConfirmed(funcSquelch, 59, 1);

    auto* button = controller.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    QVERIFY(button);
    bool submitted = false;
    QTimer::singleShot(0, this,
                       [&submitted]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu);
                           auto* slider = menu->findChild<QSlider*>(QStringLiteral("vfoSQLLevelSlider"));
                           QVERIFY(slider);
                           slider->setValue(102);
                           QVERIFY(QMetaObject::invokeMethod(slider, "sliderReleased", Qt::DirectConnection));
                           submitted = true;
                           menu->close();
                       });
    emit controller.display()->receiverControlClicked(QStringLiteral("SQL"));
    QVERIFY(submitted);
    emit backend.radioValueConfirmed(funcSquelch, 80, 1);
    QCOMPARE(button->text(), QStringLiteral("SQL 23%"));

    QTRY_COMPARE_WITH_TIMEOUT(backend.requestedVfoStateCalls, 1, 1500);
    QCOMPARE(backend.requestedVfoState, Vfo::Sub);
    emit backend.radioValueConfirmed(funcSquelch, 80, 1);
    QCOMPARE(button->text(), QStringLiteral("SQL 31%"));
}

void VfoBackendTest::mainSubExchangeKeepsReceiverLevelState()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController mainController(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);

    Frequency mainFrequency;
    mainFrequency.Hz = 146520000;
    Frequency subFrequency;
    subFrequency.Hz = 433920000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(subFrequency), 1);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 1);
    emit backend.radioValueConfirmed(funcSquelch, 48, 0);
    emit backend.radioValueConfirmed(funcSquelch, 70, 1);
    emit backend.radioValueConfirmed(funcRfGain, 200, 0);
    emit backend.radioValueConfirmed(funcRfGain, 100, 1);

    auto* mainSquelch = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    auto* subSquelch = subController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    auto* mainRfGain = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoRFGButton"));
    auto* subRfGain = subController.display()->findChild<QPushButton*>(QStringLiteral("vfoRFGButton"));
    QVERIFY(mainSquelch);
    QVERIFY(subSquelch);
    QVERIFY(mainRfGain);
    QVERIFY(subRfGain);
    QCOMPARE(mainSquelch->text(), QStringLiteral("SQL 19%"));
    QCOMPARE(subSquelch->text(), QStringLiteral("SQL 27%"));
    QCOMPARE(mainRfGain->text(), QStringLiteral("RFG 78%"));
    QCOMPARE(subRfGain->text(), QStringLiteral("RFG 39%"));

    mainController.captureExchangeableControlState();
    subController.captureExchangeableControlState();
    mainController.applyCapturedControlExchange(&subController);

    QCOMPARE(mainSquelch->text(), QStringLiteral("SQL 19%"));
    QCOMPARE(subSquelch->text(), QStringLiteral("SQL 27%"));
    QCOMPARE(mainRfGain->text(), QStringLiteral("RFG 78%"));
    QCOMPARE(subRfGain->text(), QStringLiteral("RFG 39%"));
}

void VfoBackendTest::filtersMenuKeepsControlColumnsAligned()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(Vfo::Main, &backend, &state, &parent);

    Frequency frequency;
    frequency.Hz = 146520000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 2;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    emit backend.radioValueUpdated(funcNBLevel, 128, 0);
    emit backend.radioValueUpdated(funcNRLevel, 128, 0);
    emit backend.radioValueUpdated(funcAutoNotch, false, 0);
    emit backend.radioValueUpdated(funcManualNotch, false, 0);

    auto* filtersButton = controller.display()->findChild<QPushButton*>(QStringLiteral("vfoFILTERSButton"));
    QVERIFY(filtersButton);
    filtersButton->setEnabled(true);
    bool inspected = false;
    QTimer::singleShot(0, this,
                       [&backend, &inspected]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu);
                           const QStringList names = {QStringLiteral("FILTER"), QStringLiteral("NB"),
                                                      QStringLiteral("NOTCH"), QStringLiteral("NR")};
                           int nameX = -1;
                           int comboX = -1;
                           int comboWidth = -1;
                           for (const QString& name : names)
                           {
                               auto* nameLabel = menu->findChild<QLabel*>(QStringLiteral("vfoFilters%1Name").arg(name));
                               auto* combo = menu->findChild<QComboBox*>(QStringLiteral("vfoFilters%1Combo").arg(name));
                               QVERIFY(nameLabel);
                               QVERIFY(combo);
                               if (nameX < 0)
                               {
                                   nameX = nameLabel->x();
                                   comboX = combo->x();
                                   comboWidth = combo->width();
                               }
                               QCOMPARE(nameLabel->x(), nameX);
                               QCOMPARE(combo->x(), comboX);
                               QCOMPARE(combo->width(), comboWidth);
                           }
                           auto* filterCombo = menu->findChild<QComboBox*>(QStringLiteral("vfoFiltersFILTERCombo"));
                           QVERIFY(filterCombo);
                           filterCombo->setCurrentIndex(2);
                           QCOMPARE(filterCombo->currentText(), QStringLiteral("FIL2"));
                           QCOMPARE(backend.vfoFilterCalls, 1);
                           QCOMPARE(backend.requestedFilterVfo, Vfo::Main);
                           QCOMPARE(backend.requestedFilterMode, QStringLiteral("FM"));
                           QCOMPARE(backend.requestedFilter, 3);
                           ModeInfo confirmedMode;
                           confirmedMode.mk = modeFM;
                           confirmedMode.name = QStringLiteral("FM");
                           confirmedMode.filter = 3;
                           emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(confirmedMode), 0);
                           QCOMPARE(filterCombo->currentText(), QStringLiteral("FIL3"));

                           auto* notchCombo = menu->findChild<QComboBox*>(QStringLiteral("vfoFiltersNOTCHCombo"));
                           QVERIFY(notchCombo);
                           notchCombo->setCurrentIndex(1);
                           QCOMPARE(backend.vfoNotchCalls, 1);
                           QCOMPARE(backend.requestedNotch, VfoNotch::Auto);
                           QCOMPARE(notchCombo->currentText(), QStringLiteral("OFF"));
                           emit backend.radioValueUpdated(funcManualNotch, true, 0);
                           QCOMPARE(notchCombo->currentText(), QStringLiteral("ON"));
                           inspected = true;
                           menu->close();
                       });
    filtersButton->click();
    QVERIFY(inspected);
}

void VfoBackendTest::exchangeModeConfirmationAlwaysRequiresReceiverFrequency()
{
    constexpr quint8 kMainFrequency = 0x10;
    constexpr quint8 kSubFrequency = 0x04;

    // Exercise every possible byte-sized combination repeatedly. This is
    // intentionally much larger than the minimum truth-table test because
    // the hardware defect appeared only under sustained exchange pressure.
    for (int pass = 0; pass < 500; ++pass)
    {
        for (int raw = 0; raw <= 0xff; ++raw)
        {
            const auto confirmations = static_cast<quint8>(raw);
            QCOMPARE(sdr9700::backend::exchangeModeMayConfirm(confirmations, kMainFrequency),
                     (confirmations & kMainFrequency) != 0);
            QCOMPARE(sdr9700::backend::exchangeModeMayConfirm(confirmations, kSubFrequency),
                     (confirmations & kSubFrequency) != 0);
        }
    }
}

void VfoBackendTest::bandRecallRejectsPressureUntilCompleteIdentitySettles()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(Vfo::Main, &backend, &state, &parent);

    Frequency frequency;
    frequency.Hz = 432100000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    frequency.Hz = 145250000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);

    QVERIFY(controller.selectBand(band2m));
    QCOMPARE(backend.bandRecallCalls, 0);
    QCOMPARE(backend.vfoFrequencyCalls, 0);

    QVERIFY(controller.selectBand(band70cm));
    QCOMPARE(backend.bandRecallCalls, 1);
    QVERIFY(!controller.selectBand(band23cm));
    QCOMPARE(backend.bandRecallCalls, 1);

    frequency.Hz = 432100000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    QVERIFY(!controller.selectBand(band23cm));
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    QVERIFY(!controller.selectBand(band23cm));

    // The steady poller continues reporting unrelated receiver state while a
    // band identity settles. Those updates must not restart the quiet-period
    // timer forever; under the real 250 ms poll cadence that left the control
    // locked until its five-second recovery timeout.
    for (int i = 0; i < 8; ++i)
    {
        emit backend.radioValueConfirmed(funcRfGain, QVariant::fromValue<ushort>(static_cast<ushort>(100 + i)), 0);
        QTest::qWait(40);
    }
    QVERIFY(controller.selectBand(band23cm));
    QCOMPARE(backend.vfoFrequencyCalls, 1);
}

void VfoBackendTest::dualWatchRequestReportsBackendAcceptance()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController mainController(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);
    VfoSelectionController selection(&backend, &mainController, &subController, &parent);
    selection.setRadioReady(true);
    selection.setControlsEnabled(true);

    backend.dualWatchAccepted = false;
    QVERIFY(!backend.dualWatchAccepted);
    QVERIFY(!selection.requestDualWatch(true));
    QCOMPARE(backend.dualWatchCalls, 1);
    QVERIFY(!backend.dualWatchEnabled);

    backend.dualWatchAccepted = true;
    QVERIFY(selection.requestDualWatch(true));
    QCOMPARE(backend.dualWatchCalls, 2);
    QVERIFY(backend.dualWatchEnabled);
}

void VfoBackendTest::uiSelectionIgnoresBackgroundReceiverRouting()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController mainController(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);
    VfoSelectionController selection(&backend, &mainController, &subController, &parent);
    selection.setRadioReady(true);
    selection.setControlsEnabled(true);
    emit backend.radioValueConfirmed(funcVFODualWatch, true, 0);

    QVERIFY(selection.selectVfo(Vfo::Sub));
    QCOMPARE(backend.selectedVfo, Vfo::Sub);
    QCOMPARE(selection.selectedVfo(), Vfo::Main);

    // A stale MAIN observation cannot complete the pending SUB request.
    emit backend.radioValueConfirmed(funcVFOBandMS, false, 0);
    QCOMPARE(selection.selectedVfo(), Vfo::Main);

    emit backend.radioValueConfirmed(funcVFOBandMS, true, 0);
    QCOMPARE(selection.selectedVfo(), Vfo::Sub);

    // Background command routing restores the physical radio context to
    // MAIN. That is not an operator selection and must not move the UI.
    emit backend.radioValueConfirmed(funcVFOBandMS, false, 0);
    QCOMPARE(selection.selectedVfo(), Vfo::Sub);
}

void VfoBackendTest::receiverPairActionsRespectLifecycleGates()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController mainController(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);
    VfoSelectionController selection(&backend, &mainController, &subController, &parent);

    QVERIFY(!selection.selectVfo(Vfo::Sub));
    QVERIFY(!selection.requestMainSubExchange());
    QCOMPARE(backend.selectVfoCalls, 0);
    QCOMPARE(backend.exchangeCalls, 0);

    selection.setRadioReady(true);
    selection.setControlsEnabled(true);
    QVERIFY(!selection.selectVfo(Vfo::Sub));
    QVERIFY(!selection.requestMainSubExchange());

    emit backend.radioValueConfirmed(funcVFODualWatch, true, 0);
    selection.setReceiverContextReady(false);
    QVERIFY(!selection.selectVfo(Vfo::Sub));
    QVERIFY(!selection.requestMainSubExchange());

    selection.setReceiverContextReady(true);
    QVERIFY(selection.selectVfo(Vfo::Sub));
    QCOMPARE(backend.selectVfoCalls, 1);
    QVERIFY(!selection.selectVfo(Vfo::Sub));
    QVERIFY(!selection.requestMainSubExchange());

    emit backend.radioValueConfirmed(funcVFOBandMS, true, 0);
    QVERIFY(selection.requestMainSubExchange());
    QCOMPARE(backend.exchangeCalls, 1);
    QVERIFY(!selection.selectVfo(Vfo::Main));
    QVERIFY(!selection.requestMainSubExchange());
}

void VfoBackendTest::controllerFrequencyRequestWaitsForRadioConfirmation()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(Vfo::Main, &backend, &state, &parent);

    Frequency confirmed;
    confirmed.Hz = 144200000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(confirmed), 0);
    QCOMPARE(controller.frequencyHz(), quint64(144200000));

    controller.requestFrequencyHz(145250000);
    QCOMPARE(backend.vfoFrequencyCalls, 1);
    QCOMPARE(backend.requestedFrequencyVfo, Vfo::Main);
    QCOMPARE(backend.requestedFrequencyHz, quint64(145250000));
    QCOMPARE(controller.frequencyHz(), quint64(144200000));

    controller.requestFrequencyHz(144200000);
    QCOMPARE(backend.vfoFrequencyCalls, 1);

    confirmed.Hz = 145250000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(confirmed), 0);
    QCOMPARE(controller.frequencyHz(), quint64(145250000));
}

void VfoBackendTest::vfoDisplayConsumesConfirmedRadioStateWithoutReceiverBleed()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController mainController(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);

    Frequency mainFrequency;
    mainFrequency.Hz = 145250000;
    Frequency subFrequency;
    subFrequency.Hz = 443250000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    Frequency mainOffset;
    mainOffset.Hz = 600000;
    Frequency subOffset;
    subOffset.Hz = 5000000;

    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(subFrequency), 1);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 1);

    QTRY_COMPARE(mainController.display()->frequencyText(), QStringLiteral("145.250.000"));
    QTRY_COMPARE(subController.display()->frequencyText(), QStringLiteral("443.250.000"));
    QCOMPARE(mainController.band(), band2m);
    QCOMPARE(subController.band(), band70cm);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->text(),
             QStringLiteral("FM"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("--"));

    auto* mainFiltersButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoFILTERSButton"));
    auto* subFiltersButton = subController.display()->findChild<QPushButton*>(QStringLiteral("vfoFILTERSButton"));
    QVERIFY(mainFiltersButton);
    QVERIFY(subFiltersButton);
    QCOMPARE(mainFiltersButton->property("active"), QVariant(true));
    QCOMPARE(subFiltersButton->property("active"), QVariant(true));
    emit backend.radioValueUpdated(funcNoiseBlanker, true, 0);
    emit backend.radioValueUpdated(funcNoiseReduction, true, 0);
    QCOMPARE(mainFiltersButton->text(), QStringLiteral("FILTERS"));
    QCOMPARE(mainFiltersButton->property("active"), QVariant(true));
    emit backend.radioValueUpdated(funcNBLevel, 255, 0);
    emit backend.radioValueUpdated(funcNRLevel, 0, 0);
    emit backend.radioValueUpdated(funcAutoNotch, false, 0);
    emit backend.radioValueUpdated(funcManualNotch, false, 0);
    emit backend.radioValueUpdated(funcNoiseBlanker, true, 1);
    emit backend.radioValueUpdated(funcNBLevel, 128, 1);
    emit backend.radioValueUpdated(funcAutoNotch, false, 1);
    emit backend.radioValueUpdated(funcManualNotch, false, 1);
    QCOMPARE(mainFiltersButton->toolTip(), QStringLiteral("FIL1 • NB 10 • NOTCH OFF • NR 1"));
    QCOMPARE(subFiltersButton->toolTip(), QStringLiteral("FIL1 • NB 6 • NOTCH OFF • NR —"));

    // A duplex direction without its companion offset is partial state. It
    // must not fabricate a confirmed zero-offset display while replies are
    // still arriving.
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupPlus), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("--"));
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0);
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(subOffset), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("-0.600"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"))->text(),
             QStringLiteral("+5.000"));

    RptrAccessData toneAccess;
    toneAccess.accessMode = ratrTN;
    emit backend.radioValueConfirmed(funcToneSquelchType, QVariant::fromValue(toneAccess), 0);
    emit backend.radioValueConfirmed(funcToneSquelchType, QVariant::fromValue(toneAccess), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE --"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE --"));
    emit backend.radioValueConfirmed(funcToneFreq, QVariant::fromValue(ToneInfo(885)), 0);
    emit backend.radioValueConfirmed(funcToneFreq, QVariant::fromValue(ToneInfo(1035)), 1);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE 88.5"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"))->text(),
             QStringLiteral("TONE 103.5"));

    for (int i = 0; i < 10000; ++i)
    {
        mainFrequency.Hz = 145250000 + static_cast<quint64>(i);
        emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
        QCOMPARE(subController.frequencyHz(), quint64(443250000));
        QCOMPARE(subController.display()->frequencyText(), QStringLiteral("443.250.000"));
    }

    // Disabling Dual Watch removes the live SUB receiver but intentionally
    // retains its session recall for a later re-enable.
    emit backend.radioValueConfirmed(funcVFODualWatch, QVariant::fromValue(false), 0);
    QCOMPARE(subController.frequencyHz(), quint64(0));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("---.---.---"));
    QCOMPARE(state.bandRecall(Vfo::Sub, band70cm)->frequencyHz, std::optional<quint64>(443250000));

    // A SUB reply queued before the OFF confirmation may arrive afterward.
    // It must not resurrect a receiver that the radio has confirmed is no
    // longer live.
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(subFrequency), 1);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 1);
    QCOMPARE(subController.frequencyHz(), quint64(0));
    QVERIFY(!state.receiver(Vfo::Sub).mode.has_value());

    emit backend.disconnected();
    emit backend.radioValueUpdated(funcNoiseBlanker, true, 0);
    emit backend.radioValueUpdated(funcNoiseReduction, true, 0);
    QCOMPARE(mainFiltersButton->text(), QStringLiteral("FILTERS"));
    QCOMPARE(mainFiltersButton->property("active"), QVariant(false));
}

void VfoBackendTest::keepsTransmitFrequencyLabelStableAcrossRepeaterPtt()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);
    QWidget parent;
    VfoController controller(Vfo::Main, &backend, &state, &parent);
    VfoController subController(Vfo::Sub, &backend, &state, &parent);
    VfoSelectionController selection(&backend, &controller, &subController, &parent);
    auto* transmitFrequency = controller.display()->findChild<QLabel*>(QStringLiteral("vfoTransmitFrequency"));
    QVERIFY(transmitFrequency != nullptr);

    Frequency frequency;
    frequency.Hz = 147260000;
    ModeInfo mode;
    mode.mk = modeFM;
    mode.name = QStringLiteral("FM");
    mode.filter = 1;
    Frequency offset;
    offset.Hz = 600000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    emit backend.radioValueConfirmed(funcModeGet, QVariant::fromValue(mode), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupPlus), 0);
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(offset), 0);
    QTRY_COMPARE(transmitFrequency->text(), QStringLiteral("TX: 147.860.000"));

    // The accepted request precedes both the shifted frequency and confirmed
    // PTT-on readback in the physical-radio sequence.
    QVERIFY(backend.setPtt(true));
    frequency.Hz = 147860000;
    emit backend.radioValueConfirmed(funcSelectedFreq, QVariant::fromValue(frequency), 0);
    QCOMPARE(controller.display()->frequencyText(), QStringLiteral("147.860.000"));
    QCOMPARE(transmitFrequency->text(), QStringLiteral("TX: 147.860.000"));

    emit backend.pttChanged(true);
    QVERIFY(backend.setPtt(false));
    emit backend.pttChanged(false);
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(offset), 0);
    QCOMPARE(transmitFrequency->text(), QStringLiteral("TX: 147.860.000"));

    frequency.Hz = 147260000;
    emit backend.radioValueConfirmed(funcSelectedFreq, QVariant::fromValue(frequency), 0);
    QCOMPARE(transmitFrequency->text(), QStringLiteral("TX: 147.860.000"));

    // The first confirmed receive frequency releases the hold so later tuning
    // updates the derived transmit frequency normally.
    frequency.Hz = 147270000;
    emit backend.radioValueConfirmed(funcSelectedFreq, QVariant::fromValue(frequency), 0);
    QCOMPARE(transmitFrequency->text(), QStringLiteral("TX: 147.870.000"));
}

void VfoBackendTest::radioStateKeepsReceiverAndBandRecallIsolated()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);

    Frequency mainFrequency;
    mainFrequency.Hz = 145250000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(mainFrequency), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);
    Frequency mainOffset;
    mainOffset.Hz = 600000;
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0);

    Frequency subFrequency;
    subFrequency.Hz = 443250000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(subFrequency), 1);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupPlus), 1);
    Frequency subOffset;
    subOffset.Hz = 5000000;
    emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(subOffset), 1);
    emit backend.radioValueConfirmed(funcRfGain, 210, 0);
    emit backend.radioValueConfirmed(funcRfGain, 90, 1);
    emit backend.radioValueConfirmed(funcSquelch, 32, 0);
    emit backend.radioValueConfirmed(funcSquelch, 96, 1);

    QCOMPARE(state.receiver(Vfo::Main).frequencyHz, std::optional<quint64>(145250000));
    QCOMPARE(state.receiver(Vfo::Main).duplexMode, std::optional<duplexMode_t>(dmDupMinus));
    QCOMPARE(state.receiver(Vfo::Sub).frequencyHz, std::optional<quint64>(443250000));
    QCOMPARE(state.receiver(Vfo::Sub).duplexMode, std::optional<duplexMode_t>(dmDupPlus));
    QCOMPARE(state.receiver(Vfo::Main).rfGain, std::optional<int>(210));
    QCOMPARE(state.receiver(Vfo::Sub).rfGain, std::optional<int>(90));
    QCOMPARE(state.receiver(Vfo::Main).squelch, std::optional<int>(32));
    QCOMPARE(state.receiver(Vfo::Sub).squelch, std::optional<int>(96));

    const sdr9700::RadioState::BandRecall* main2m = state.bandRecall(Vfo::Main, band2m);
    const sdr9700::RadioState::BandRecall* sub70cm = state.bandRecall(Vfo::Sub, band70cm);
    QVERIFY(main2m != nullptr);
    QVERIFY(sub70cm != nullptr);
    QCOMPARE(main2m->repeaterOffsetHz, std::optional<quint64>(600000));
    QCOMPARE(sub70cm->repeaterOffsetHz, std::optional<quint64>(5000000));

    // Hammer receiver-tagged values to ensure no update can bleed across the
    // logical receiver boundary under sustained polling pressure.
    for (int i = 0; i < 10000; ++i)
    {
        mainOffset.Hz = 600000 + static_cast<quint64>(i);
        emit backend.radioValueConfirmed(funcReadFreqOffset, QVariant::fromValue(mainOffset), 0);
        QCOMPARE(state.receiver(Vfo::Sub).repeaterOffsetHz, std::optional<quint64>(5000000));
    }
}

void VfoBackendTest::radioStateInvalidatesLiveStateButKeepsSessionRecallSeparate()
{
    FakeRadioBackend backend;
    sdr9700::RadioState state(&backend);

    Frequency frequency;
    frequency.Hz = 146940000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);

    frequency.Hz = 1296100000;
    emit backend.radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    QVERIFY(!state.receiver(Vfo::Main).duplexMode.has_value());
    QCOMPARE(state.receiver(Vfo::Main).band, band23cm);
    QCOMPARE(state.bandRecall(Vfo::Main, band2m)->duplexMode, std::optional<duplexMode_t>(dmDupMinus));

    // A confirmed value must repopulate the new receiver snapshot even when
    // its payload is identical to the prior band's value. The protocol cache
    // may regard this as unchanged, but it is new evidence for RadioState
    // after the band transition invalidated the old operating context.
    emit backend.radioValueConfirmed(funcSplitStatus, QVariant::fromValue(dmDupMinus), 0);
    QCOMPARE(state.receiver(Vfo::Main).duplexMode, std::optional<duplexMode_t>(dmDupMinus));

    emit backend.radioValueConfirmed(funcDialLock, QVariant::fromValue(true), 0);
    QCOMPARE(state.shared().dialLockEnabled, std::optional<bool>(true));
    emit backend.radioValueConfirmed(funcAfGain, 12, 0xff);
    emit backend.radioValueConfirmed(funcRFPower, 34, 0xff);
    emit backend.radioValueConfirmed(funcLANModLevel, 56, 0xff);
    emit backend.radioValueConfirmed(funcCompressor, true, 0xff);
    emit backend.radioValueConfirmed(funcCompressorLevel, 78, 0xff);
    QCOMPARE(state.shared().afGain, std::optional<int>(12));
    QCOMPARE(state.shared().txPower, std::optional<int>(34));
    QCOMPARE(state.shared().lanModLevel, std::optional<int>(56));
    QCOMPARE(state.shared().compressorEnabled, std::optional<bool>(true));
    QCOMPARE(state.shared().compressorLevel, std::optional<int>(78));

    emit backend.readyChanged(true);
    emit backend.readyChanged(false);
    QVERIFY(!state.receiver(Vfo::Main).frequencyHz.has_value());
    QVERIFY(!state.shared().dialLockEnabled.has_value());
    QVERIFY(!state.shared().afGain.has_value());
    QVERIFY(!state.shared().txPower.has_value());
    QVERIFY(!state.shared().lanModLevel.has_value());
    QVERIFY(!state.shared().compressorEnabled.has_value());
    QVERIFY(!state.shared().compressorLevel.has_value());
    // A temporary loss of readiness invalidates the live snapshot, but the
    // confirmed recall remains useful if this same radio session recovers.
    QCOMPARE(state.bandRecall(Vfo::Main, band2m)->duplexMode, std::optional<duplexMode_t>(dmDupMinus));

    emit backend.disconnected();
    QVERIFY(!state.receiver(Vfo::Main).frequencyHz.has_value());
    QVERIFY(!state.shared().selectedVfo.has_value());
    // Recall is session-local and must never leak from one connection into a
    // later radio session, even when the application process remains alive.
    QVERIFY(!state.bandRecall(Vfo::Main, band2m)->duplexMode.has_value());
}

void VfoBackendTest::sameBandRefreshIsEdgeTriggered()
{
    sdr9700::SameBandRefreshPolicy policy;

    QVERIFY(!policy.observe(0, 0));
    QVERIFY(!policy.observe(144200000, 432100000));
    QVERIFY(!policy.observe(144200000, 145000000));
    QVERIFY(policy.observe(144200000, 144200000));
    QVERIFY(!policy.observe(144200000, 144200000));

    QVERIFY(!policy.observe(432100000, 145100000));
    QVERIFY(!policy.observe(432100000, 433000000));
    QVERIFY(policy.observe(432100000, 432100000));

    policy.reset();
    QVERIFY(policy.observe(1296100000, 1296100000));
}

void VfoBackendTest::radioBackedRequestsWaitForConfirmation()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);
    QSignalSpy frequencySpy(&model, &VfoModel::frequencyChanged);
    QSignalSpy modeSpy(&model, &VfoModel::modeChanged);

    model.setFrequencyHz(146520000);
    model.setMode(QStringLiteral("DV"));

    QCOMPARE(backend.frequencyHz, quint64(146520000));
    QCOMPARE(backend.frequencyCalls, 1);
    QCOMPARE(backend.mode, QStringLiteral("DV"));
    QCOMPARE(backend.modeCalls, 1);
    QCOMPARE(model.frequencyHz(), quint64(145000000));
    QCOMPARE(model.mode(), QStringLiteral("FM"));
    QCOMPARE(frequencySpy.count(), 0);
    QCOMPARE(modeSpy.count(), 0);
}

void VfoBackendTest::localControlsUpdateAndForward()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);
    QSignalSpy filterSpy(&model, &VfoModel::filterChanged);
    QSignalSpy afGainSpy(&model, &VfoModel::afGainChanged);

    model.setFilterWidth(-3000, 3000);
    model.setAfGain(200);
    model.setPtt(true);

    QCOMPARE(model.filterLow(), -3000);
    QCOMPARE(model.filterHigh(), 3000);
    QCOMPARE(backend.filterLow, -3000);
    QCOMPARE(backend.filterHigh, 3000);
    QCOMPARE(filterSpy.count(), 1);
    QCOMPARE(backend.afGain, 200);
    QCOMPARE(afGainSpy.count(), 1);
    QVERIFY(backend.ptt);
}

void VfoBackendTest::boundedRequestsAreClampedBeforeForwarding()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);

    model.setPreampLevel(10);
    model.setRitOffset(1200);
    model.setNrLevel(300);
    model.setNbLevel(300);
    model.setCompressorLevel(300);
    model.setRfGain(300);
    model.setSquelch(true, 300);
    model.setTxPower(300);

    QCOMPARE(backend.preampLevel, 3);
    QCOMPARE(backend.nrLevel, 15);
    QCOMPARE(backend.nbLevel, 10);
    QCOMPARE(backend.compressorLevel, 255);
    QCOMPARE(model.preampLevel(), 0);
    QCOMPARE(backend.ritOffset, short(999));
    QCOMPARE(model.ritHz(), short(0));
    QCOMPARE(backend.rfGain, 255);
    QVERIFY(backend.squelchOn);
    QCOMPARE(backend.squelchLevel, 255);
    QCOMPARE(backend.txPower, 255);
}

void VfoBackendTest::reportsRejectedPttRequest()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);
    backend.pttAccepted = false;

    QVERIFY(!model.setPtt(true));
    QVERIFY(backend.ptt);
}

void VfoBackendTest::pollsReceiveFrequencyAfterConfirmedUnkey()
{
    FakeRadioBackend backend;
    VfoModel model(&backend);

    // Initial and repeated receive-state reports do not need a refresh.
    model.applyPtt(false);
    QCOMPARE(backend.frequencyPollCalls, 0);

    model.applyPtt(true);
    QCOMPARE(backend.frequencyPollCalls, 0);
    model.applyPtt(false);
    QCOMPARE(backend.frequencyPollCalls, 1);

    // Duplicate readbacks must not create a poll storm, while every later
    // confirmed key-up/key-down cycle receives the same immediate refresh.
    model.applyPtt(false);
    QCOMPARE(backend.frequencyPollCalls, 1);
    model.applyPtt(true);
    model.applyPtt(false);
    QCOMPARE(backend.frequencyPollCalls, 2);
}

void VfoBackendTest::receiverCommandRouteSelectsCommandsAndRestoresInOrder()
{
    QStringList events;
    sdr9700::backend::routeVfoReceiverCommand(
        Vfo::Sub, Vfo::Main, [&events](Vfo selected)
        { events.append(selected == Vfo::Sub ? QStringLiteral("select-sub") : QStringLiteral("select-main")); },
        [&events](uchar receiver) { events.append(QStringLiteral("command-%1").arg(receiver)); });
    QCOMPARE(events,
             QStringList({QStringLiteral("select-sub"), QStringLiteral("command-1"), QStringLiteral("select-main")}));

    events.clear();
    sdr9700::backend::routeVfoReceiverCommand(
        Vfo::Sub, Vfo::Sub, [&events](Vfo) { events.append(QStringLiteral("select-sub")); },
        [&events](uchar receiver) { events.append(QStringLiteral("command-%1").arg(receiver)); });
    QCOMPARE(events, QStringList({QStringLiteral("select-sub"), QStringLiteral("command-1")}));

    events.clear();
    sdr9700::backend::routeVfoReceiverCommand(
        Vfo::Main, Vfo::Main, [&events](Vfo) { events.append(QStringLiteral("select-main")); },
        [&events](uchar receiver) { events.append(QStringLiteral("command-%1").arg(receiver)); });
    QCOMPARE(events, QStringList({QStringLiteral("select-main"), QStringLiteral("command-0")}));
}

void VfoBackendTest::mapsVfoToReceiverByte()
{
    QCOMPARE(sdr9700::backend::receiverForVfo(Vfo::Main), uchar(0));
    QCOMPARE(sdr9700::backend::receiverForVfo(Vfo::Sub), uchar(1));
}

void VfoBackendTest::alternatesInactiveMeterSamplesDuringDualWatch()
{
    for (int tick = 0; tick < 10; ++tick)
    {
        const Vfo expected = tick % 5 == 4 ? Vfo::Sub : Vfo::Main;
        QCOMPARE(sdr9700::backend::meterPollTarget(Vfo::Main, true, tick), expected);
    }
    QCOMPARE(sdr9700::backend::meterPollTarget(Vfo::Sub, true, 4), Vfo::Main);
    QCOMPARE(sdr9700::backend::meterPollTarget(Vfo::Sub, false, 4), Vfo::Sub);
}

void VfoBackendTest::suppressesReceiverMeterPollingDuringContextTransitions()
{
    using sdr9700::backend::receiverMeterPollAllowed;
    QVERIFY(receiverMeterPollAllowed(true, false, false, false));
    QVERIFY(!receiverMeterPollAllowed(false, false, false, false));
    QVERIFY(!receiverMeterPollAllowed(true, true, false, false));
    QVERIFY(!receiverMeterPollAllowed(true, false, true, false));
    QVERIFY(!receiverMeterPollAllowed(true, false, false, true));
}

void VfoBackendTest::survivesRepeatedExchangeAndTunePressure()
{
    constexpr int kIterationCount = 10000;
    // A five-digit mixed-pressure run covers many overlapping exchange,
    // tuning-holdoff, PTT-transition, and dual-watch meter phases.
    Vfo activeVfo = Vfo::Main;
    int pollTick = 0;
    int activeSamples = 0;
    int inactiveSamples = 0;

    for (int iteration = 0; iteration < kIterationCount; ++iteration)
    {
        const bool exchangePending = iteration % 7 == 0;
        const bool tuningHoldoff = iteration % 11 == 0;
        const bool pttTransition = iteration % 37 == 0;
        if (exchangePending)
        {
            activeVfo = activeVfo == Vfo::Main ? Vfo::Sub : Vfo::Main;
        }

        if (!sdr9700::backend::receiverMeterPollAllowed(true, pttTransition, exchangePending, tuningHoldoff))
        {
            continue;
        }

        const Vfo target = sdr9700::backend::meterPollTarget(activeVfo, true, pollTick++);
        if (target == activeVfo)
        {
            ++activeSamples;
        }
        else
        {
            ++inactiveSamples;
        }
    }

    QVERIFY(activeSamples > 0);
    QVERIFY(inactiveSamples > 0);
    QCOMPARE(activeSamples, inactiveSamples * 4 + (pollTick % 5));
}

QTEST_MAIN(VfoBackendTest)
#include "VfoBackendTest.moc"
