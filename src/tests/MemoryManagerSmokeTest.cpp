// QtTest invokes private slots through the generated meta-object.
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainTitleBar.h"
#include "AppBuildConfig.h"
#include "AppInfo.h"
#include "AppPaths.h"
#include "AppSettings.h"
#include "MemoryEditorPolicy.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryDatabase.h"
#include "MemorySyncController.h"
#include "RadioChooserDialog.h"
#include "RadioCommandController.h"
#include "RadioProfile.h"
#include "StatusBarController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "VfoSelectionController.h"
#include "backend/IRadioBackend.h"
#include "backend/RadioBackend.h"
#include "backend/RadioRouter.h"
#include "radio/Commander.h"
#include <QScopeGuard>
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QAction>
#include <QAbstractItemDelegate>
#include <QComboBox>
#include <QCloseEvent>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QMenu>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QtTest>
#include <algorithm>
#include <utility>

class MemoryManagerSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void memoryActivationRoutesByBand_data();
    void memoryActivationRoutesByBand();
    void newInstallationCanAddRadioProfile();
    void constructsMemoryManagerUi();
    void memoryManagerShowsCachedVerificationAndLiveSyncProgress();
    void unnamedRadioMemoryIsPersistedWithFrequencyName();
    void memoryVisibilitySettingsHideOptionalCategoriesByDefault();
    void mainWindowRetainsFixedFramelessDesign();
    void spectrumFrameRateSelectorDefaultsAndPersists();
    void fileMenuTracksRadioConnection();
    void selectorButtonsAvoidDynamicStyleSheets();
    void compressorMenuReflectsConfirmedLevel();
    void utilityWindowIsDestroyedWithHost();
    void backendReadinessWaitsForBothVfos();
    void backendQueuesStartupMainStateAsScopedAction();
    void backendQueuesStartupSubStateAsScopedAction();
    void backendQueuesPostExchangeSettingsForBothReceivers();
    void memoryPttDoesNotReselectMemoryChannel();
    void backendKeepsUnkeyPendingAcrossStaleReadback();
    void radioControlsDoNotWaitForInitialMemorySync();
    void quitActionDefersWindowClose();
    void persistentStatusMessageCanBeClearedByOwner();
    void automationIndicatorReflectsClientCount();
    void titleBarSpeakerTogglesMute();
};

void MemoryManagerSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(QDir(sdr9700::configDirectory()).removeRecursively() || !QDir(sdr9700::configDirectory()).exists());
    QVERIFY(QDir(sdr9700::dataDirectory()).removeRecursively() || !QDir(sdr9700::dataDirectory()).exists());
}

void MemoryManagerSmokeTest::memoryActivationRoutesByBand_data()
{
    QTest::addColumn<quint64>("mainHz");
    QTest::addColumn<quint64>("subHz");
    QTest::addColumn<bool>("dualWatch");
    QTest::addColumn<quint16>("group");
    QTest::addColumn<quint64>("memoryHz");
    QTest::addColumn<bool>("expectSub");
    struct Band
    {
        const char* name;
        quint16 group;
        quint64 frequencyHz;
    };
    constexpr Band bands[] = {{"2m", 1, 145000000}, {"70cm", 2, 435000000}, {"23cm", 3, 1295000000}};
    // Every ordered receiver pair includes both arrangements of each band
    // combination. Select every memory band with SUB both active and inactive.
    for (const Band& main : bands)
    {
        for (const Band& sub : bands)
        {
            if (main.group == sub.group)
            {
                continue;
            }
            for (const Band& memory : bands)
            {
                for (const bool dualWatch : {true, false})
                {
                    const QByteArray name = QByteArray(main.name) + "-main-" + sub.name + "-sub-" + memory.name +
                                            "-memory-" + (dualWatch ? "dual" : "single");
                    QTest::newRow(name.constData())
                        << main.frequencyHz << sub.frequencyHz << dualWatch << memory.group
                        << quint64(memory.frequencyHz + 500000) << (dualWatch && sub.group == memory.group);
                }
            }
        }
    }
}

void MemoryManagerSmokeTest::memoryActivationRoutesByBand()
{
    QFETCH(quint64, mainHz);
    QFETCH(quint64, subHz);
    QFETCH(bool, dualWatch);
    QFETCH(quint16, group);
    QFETCH(quint64, memoryHz);
    QFETCH(bool, expectSub);

    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    QVERIFY(controller != nullptr);
    QVERIFY(table != nullptr);
    controller->setRadioProfileId(QUuid::createUuid());
    MemoryType memory;
    memory.group = group;
    memory.channel = 25;
    memory.frequency.Hz = memoryHz;
    std::copy_n("ROUTING TEST", 12, memory.name);
    model.radioMemoryReceived(memory);
    QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    QCOMPARE(table->rowCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendReadyChanged", Q_ARG(bool, true)));

    auto* backend = static_cast<RadioBackend*>(model.backend());
    backend->radioValueConfirmed(funcVFODualWatch, QVariant::fromValue(true), 0);
    Frequency frequency;
    frequency.Hz = mainHz;
    backend->radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 0);
    frequency.Hz = subHz;
    backend->radioValueConfirmed(funcFreqGet, QVariant::fromValue(frequency), 1);
    backend->radioValueConfirmed(funcVFODualWatch, QVariant::fromValue(dualWatch), 0);
    QCoreApplication::processEvents();

    // Capture actual CI-V output without opening a radio connection. Seed the
    // backend's confirmed frequency cache as a live session would, then start
    // in the opposite physical context to expose reliance on polling state.
    Commander commander;
    sdr9700::populateRadioCapabilities(commander.radioCaps);
    commander.haveRadioCaps = true;
    commander.setCIVAddr(0xA2);
    backend->m_currentMainFrequencyHz = mainHz;
    backend->m_currentSubFrequencyHz = subHz;
    backend->m_commander = &commander;
    backend->m_sessionActive = std::make_shared<std::atomic_bool>(true);
    const auto detachCommander = qScopeGuard(
        [backend]()
        {
            backend->m_sessionActive->store(false);
            backend->m_commander = nullptr;
            backend->m_sessionActive.reset();
        });
    auto* vfoSelection = window.findChild<VfoSelectionController*>();
    QVERIFY(vfoSelection != nullptr);
    vfoSelection->setControlsEnabled(true);
    vfoSelection->setRadioReady(true);
    vfoSelection->setReceiverContextReady(true);
    commander.receiveCommand(funcSelectVFO, QVariant::fromValue(expectSub ? vfoMain : vfoSub), 0);
    QSignalSpy wireSpy(&commander, &Commander::dataForComm);
    QVERIFY(QMetaObject::invokeMethod(table, "cellDoubleClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
    const auto hasChannelCommand = [&wireSpy]()
    {
        return std::any_of(wireSpy.cbegin(), wireSpy.cend(), [](const QList<QVariant>& emission)
                           { return emission.at(0).toByteArray().mid(4) == QByteArray::fromHex("080025"); });
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasChannelCommand(), 1500);
    bool selectedSub = !expectSub;
    bool sawSelection = false;
    bool sawTune = false;
    for (const auto& emission : wireSpy)
    {
        const QByteArray command = emission.at(0).toByteArray().mid(4);
        if (command == QByteArray::fromHex("07d0") || command == QByteArray::fromHex("07d1"))
        {
            selectedSub = command == QByteArray::fromHex("07d1");
            sawSelection = true;
        }
        if (!command.isEmpty() && (command.front() == '\x05' || command.front() == '\x25'))
        {
            sawTune = true;
        }
        if (command == QByteArray::fromHex("080025"))
        {
            QVERIFY(sawSelection);
            QCOMPARE(selectedSub, expectSub);
            break;
        }
    }
    const quint64 targetHz = expectSub ? subHz : mainHz;
    QCOMPARE(sawTune, sdr9700::radioBandForFrequency(targetHz) != sdr9700::radioBandForFrequency(memoryHz));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusMessageLabel"));
    QVERIFY(status != nullptr);
    QCOMPARE(status->text(), QStringLiteral("Selected memory on %1: ROUTING TEST")
                                 .arg(expectSub ? QStringLiteral("SUB") : QStringLiteral("MAIN")));
}

void MemoryManagerSmokeTest::memoryManagerShowsCachedVerificationAndLiveSyncProgress()
{
    const QUuid profileId = QUuid::createUuid();
    MemoryType storedMemory;
    storedMemory.group = 1;
    storedMemory.channel = 1;
    storedMemory.frequency.Hz = 145000000;
    std::copy_n("DATABASE TEST", 13, storedMemory.name);

    MemoryDatabase database;
    QString databaseError;
    QVERIFY2(database.open(&databaseError), qPrintable(databaseError));
    QVERIFY2(database.store(profileId, storedMemory, &databaseError), qPrintable(databaseError));

    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    auto* statusLabel = window.findChild<QLabel*>(QStringLiteral("memoryManagerStatusLabel"));
    auto* progressBar = window.findChild<QProgressBar*>(QStringLiteral("memoryManagerProgressBar"));
    auto* memoryTable = window.findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    QVERIFY(controller != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(progressBar != nullptr);
    QVERIFY(memoryTable != nullptr);

    controller->setRadioProfileId(profileId);
    QCOMPARE(statusLabel->text(), QStringLiteral("Waiting to verify 1 cached memory with the radio (0/420)"));
    QVERIFY(!progressBar->isHidden());
    QCOMPARE(progressBar->value(), 0);
    QCOMPARE(progressBar->maximum(), 420);
    QCOMPARE(memoryTable->rowCount(), 1);
    QVERIFY(!memoryTable->item(0, 0)->data(sdr9700::memory::kMemoryVerifiedThisSessionRole).toBool());
    QVERIFY(!memoryTable->item(0, 0)->text().isEmpty());
    QCOMPARE(memoryTable->item(0, 0)->foreground().color(), QColor(QLatin1String(UiTheme::Color::TextMuted)));
    QVERIFY(memoryTable->item(0, 0)->toolTip().contains(QStringLiteral("local cache")));

    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendConnected"));
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendReadyChanged", Q_ARG(bool, true)));
    QVERIFY(!statusLabel->text().startsWith(QStringLiteral("Syncing 2M channel")));
    model.spectrumActivity();
    model.spectrumActivity();
    model.spectrumActivity();
    QTRY_VERIFY_WITH_TIMEOUT(statusLabel->text().startsWith(QStringLiteral("Syncing 2M channel 001")), 1500);
    MemoryType liveMemory = storedMemory;
    liveMemory.frequency.Hz = 145500000;
    liveMemory.frequency.MHzDouble = 145.5;

    // Exercise the complete 420-slot response volume rather than short-cutting
    // controller state. The first stored response preserves the cached row;
    // every other response authoritatively confirms an empty radio slot.
    for (quint16 group = 1; group <= 3; ++group)
    {
        for (quint16 channel = 1; channel <= 107; ++channel)
        {
            MemoryType reply;
            reply.group = group;
            reply.channel = channel;
            reply.del = true;
            if (group == storedMemory.group && channel == storedMemory.channel)
            {
                reply = liveMemory;
            }
            model.radioMemoryReceived(reply);
            QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
            if (group == storedMemory.group && channel == storedMemory.channel)
            {
                // The table may reflect live replies immediately, but the
                // durable generation remains unchanged until finalization.
                QCOMPARE(database.memories(profileId, &databaseError).constFirst().frequency.Hz,
                         storedMemory.frequency.Hz);
            }
        }
    }
    for (quint16 channel = 1; channel <= 99; ++channel)
    {
        if (channel == 99)
        {
            continue;
        }
        MemoryType reply;
        reply.group = 0;
        reply.channel = channel;
        reply.sat = true;
        reply.del = true;
        model.radioMemoryReceived(reply);
        QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    }
    QTRY_VERIFY_WITH_TIMEOUT(statusLabel->text().startsWith(QStringLiteral("Finalizing radio memory sync")), 500);
    auto* syncController = controller->findChild<MemorySyncController*>();
    QVERIFY(syncController != nullptr);
    syncController->setMemoryPollIntervalSeconds(0);
    QCOMPARE(syncController->memoryPollIntervalSeconds(), 0);
    QVERIFY(!syncController->periodicRefreshScheduled());
    syncController->setMemoryPollIntervalSeconds(600);
    QCOMPARE(syncController->memoryPollIntervalSeconds(), 600);
    QVERIFY(syncController->periodicRefreshScheduled());
    QTRY_COMPARE_WITH_TIMEOUT(syncController->missingRetryRound(), 1, 1500);

    MemoryType recoveredReply;
    recoveredReply.group = 0;
    recoveredReply.channel = 99;
    recoveredReply.sat = true;
    recoveredReply.del = true;
    model.radioMemoryReceived(recoveredReply);
    QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    QTRY_COMPARE_WITH_TIMEOUT(statusLabel->text(), QStringLiteral("1 memory"), 1000);
    QVERIFY(progressBar->isHidden());
    QCOMPARE(memoryTable->rowCount(), 1);
    QVERIFY(memoryTable->item(0, 0)->data(sdr9700::memory::kMemoryVerifiedThisSessionRole).toBool());
    QCOMPARE(database.memories(profileId, &databaseError).constFirst().frequency.Hz, liveMemory.frequency.Hz);
    QVERIFY(database.syncState(profileId, &databaseError).complete);

    // A later sweep that never receives the occupied slot must retry only the
    // missing key, finish after the bounded retry budget, and downgrade the
    // retained database row to explicitly cached provenance. Startup and the
    // memory UI must not remain locked forever around an unresponsive slot.
    controller->forceRadioMemorySync();
    for (quint16 group = 1; group <= 3; ++group)
    {
        for (quint16 channel = 1; channel <= 107; ++channel)
        {
            if (group == storedMemory.group && channel == storedMemory.channel)
            {
                continue;
            }
            MemoryType reply;
            reply.group = group;
            reply.channel = channel;
            reply.del = true;
            model.radioMemoryReceived(reply);
            QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
        }
    }
    for (quint16 channel = 1; channel <= 99; ++channel)
    {
        MemoryType reply;
        reply.group = 0;
        reply.channel = channel;
        reply.sat = true;
        reply.del = true;
        model.radioMemoryReceived(reply);
        QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);
    }
    QTRY_COMPARE_WITH_TIMEOUT(statusLabel->text(), QStringLiteral("1 total (0 verified, 1 cached; 1 slot unanswered)"),
                              3500);
    QVERIFY(!memoryTable->item(0, 0)->data(sdr9700::memory::kMemoryVerifiedThisSessionRole).toBool());
    QCOMPARE(database.memories(profileId, &databaseError).constFirst().frequency.Hz, liveMemory.frequency.Hz);
    const MemoryDatabaseSyncState partialState = database.syncState(profileId, &databaseError);
    QCOMPARE(partialState.receivedSlotCount, 419);
    QVERIFY(!partialState.complete);

    // Unknown receiver bands fall back to MAIN. Exercise the table connection.
    auto* vfoSelection = window.findChild<VfoSelectionController*>();
    QVERIFY(vfoSelection != nullptr);
    vfoSelection->setControlsEnabled(true);
    vfoSelection->setRadioReady(true);
    vfoSelection->setReceiverContextReady(true);
    QVERIFY(QMetaObject::invokeMethod(memoryTable, "cellDoubleClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
    auto* statusMessageLabel = window.findChild<QLabel*>(QStringLiteral("statusMessageLabel"));
    QVERIFY(statusMessageLabel != nullptr);
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Selected memory on MAIN: DATABASE TEST"));

    // Highlighting SUB must not override the MAIN fallback.
    model.backend()->radioValueConfirmed(funcVFODualWatch, QVariant::fromValue<bool>(true), 0);
    QCoreApplication::processEvents();
    vfoSelection->setReceiverContextReady(false);
    QVERIFY(QMetaObject::invokeMethod(memoryTable, "cellDoubleClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
    QCOMPARE(statusMessageLabel->text(),
             QStringLiteral("Wait for the current receiver change before selecting a memory"));
    vfoSelection->setReceiverContextReady(true);
    QVERIFY(vfoSelection->selectVfo(Vfo::Sub));
    model.backend()->radioValueConfirmed(funcVFOBandMS, QVariant::fromValue<bool>(true), 0);
    QCoreApplication::processEvents();
    vfoSelection->setReceiverContextReady(true);
    QVERIFY(QMetaObject::invokeMethod(memoryTable, "cellDoubleClicked", Q_ARG(int, 0), Q_ARG(int, 0)));
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Selected memory on MAIN: DATABASE TEST"));
}

void MemoryManagerSmokeTest::unnamedRadioMemoryIsPersistedWithFrequencyName()
{
    const QUuid profileId = QUuid::createUuid();
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    QVERIFY(controller != nullptr);
    controller->setRadioProfileId(profileId);

    MemoryType unnamed;
    unnamed.group = 1;
    unnamed.channel = 25;
    unnamed.frequency.Hz = 145500000;
    model.radioMemoryReceived(unnamed);
    QCoreApplication::sendPostedEvents(controller, QEvent::MetaCall);

    MemoryDatabase database;
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const QVector<MemoryType> stored = database.memories(profileId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.constFirst().group, quint16(1));
    QCOMPARE(stored.constFirst().channel, quint16(25));
    QCOMPARE(sdr9700::memory::radioMemoryName(stored.constFirst()), QStringLiteral("145.500.000"));
}

void MemoryManagerSmokeTest::memoryVisibilitySettingsHideOptionalCategoriesByDefault()
{
    AppSettings::instance().remove(QStringLiteral("memoryShowSpecialMemories"));
    AppSettings::instance().remove(QStringLiteral("memoryShowSatelliteMemories"));

    const QUuid profileId = QUuid::createUuid();
    MemoryType ordinary;
    ordinary.group = 1;
    ordinary.channel = 1;
    ordinary.frequency.Hz = 145000000;
    std::copy_n("ORDINARY", 8, ordinary.name);
    MemoryType special = ordinary;
    special.channel = 100;
    std::copy_n("SCAN EDGE", 9, special.name);
    MemoryType satellite = ordinary;
    satellite.group = 0;
    satellite.channel = 1;
    satellite.sat = true;
    std::copy_n("SATELLITE", 9, satellite.name);

    MemoryDatabase database;
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    QVERIFY2(database.store(profileId, ordinary, &error), qPrintable(error));
    QVERIFY2(database.store(profileId, special, &error), qPrintable(error));
    QVERIFY2(database.store(profileId, satellite, &error), qPrintable(error));

    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* controller = window.findChild<MemoryController*>();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    auto* filter = window.findChild<QComboBox*>(QStringLiteral("memoryManagerBandFilter"));
    QVERIFY(controller != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(filter != nullptr);
    controller->setRadioProfileId(profileId);

    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2M"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("001 [ORDINARY]"));
    QCOMPARE(filter->findData(QStringLiteral("special")), -1);
    QCOMPARE(filter->findData(QStringLiteral("satellite")), -1);

    table->selectRow(0);
    const QList<QPushButton*> buttons = window.findChildren<QPushButton*>();
    const auto editButtonIt = std::find_if(buttons.cbegin(), buttons.cend(), [](const QPushButton* button)
                                           { return button->text() == QLatin1String("Edit"); });
    QVERIFY(editButtonIt != buttons.cend());
    QString editBand;
    QString editChannel;
    QTimer::singleShot(100, QCoreApplication::instance(),
                       [&]()
                       {
                           auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                           if (!dialog)
                           {
                               return;
                           }
                           if (auto* band = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorBand")))
                           {
                               editBand = band->text();
                               QVERIFY(band->isReadOnly());
                           }
                           if (auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel")))
                           {
                               editChannel = combo->currentText();
                           }
                           dialog->reject();
                       });
    (*editButtonIt)->click();
    QCOMPARE(editBand, QStringLiteral("2M"));
    QCOMPARE(editChannel, QStringLiteral("001 [ORDINARY]"));

    const auto addButtonIt = std::find_if(buttons.cbegin(), buttons.cend(), [](const QPushButton* button)
                                          { return button->text() == QLatin1String("Add"); });
    QVERIFY(addButtonIt != buttons.cend());
    QString firstEmptyChannel;
    QTimer::singleShot(100, QCoreApplication::instance(),
                       [&]()
                       {
                           auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                           if (!dialog)
                           {
                               return;
                           }
                           auto* frequency = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorFrequency"));
                           auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel"));
                           if (frequency && channel)
                           {
                               QVERIFY(channel->findChild<QAbstractItemDelegate*>(
                                           QStringLiteral("memoryEditorChannelDelegate")) != nullptr);
                               frequency->setText(QStringLiteral("145.500000"));
                               firstEmptyChannel = channel->currentText();
                           }
                           dialog->reject();
                       });
    (*addButtonIt)->click();
    QCOMPARE(firstEmptyChannel, QStringLiteral("002 [EMPTY]"));

    controller->setShowSpecialMemories(true);
    QCOMPARE(table->rowCount(), 2);
    QVERIFY(filter->findData(QStringLiteral("special")) >= 0);

    controller->setShowSatelliteMemories(true);
    QCOMPARE(table->rowCount(), 3);
    QVERIFY(filter->findData(QStringLiteral("satellite")) >= 0);

    controller->setShowSpecialMemories(false);
    controller->setShowSatelliteMemories(false);
    QCOMPARE(table->rowCount(), 1);
}

void MemoryManagerSmokeTest::newInstallationCanAddRadioProfile()
{
    RadioProfileStore::instance().load();
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());

    RadioChooserDialog dialog;
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(dialog.maximumWidth() > dialog.minimumWidth());
    QVERIFY(dialog.maximumHeight() > dialog.minimumHeight());
    auto* addButton = dialog.findChild<QPushButton*>(QStringLiteral("addRadioProfileButton"));
    auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("saveRadioProfileButton"));
    auto* connectButton = dialog.findChild<QPushButton*>(QStringLiteral("connectRadioButton"));
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>(QStringLiteral("radioChooserButtonBox"));
    auto* nameEdit = dialog.findChild<QLineEdit*>(QStringLiteral("radioProfileName"));
    auto* hostEdit = dialog.findChild<QLineEdit*>(QStringLiteral("radioProfileHost"));
    QVERIFY(addButton != nullptr);
    QVERIFY(saveButton != nullptr);
    QVERIFY(connectButton != nullptr);
    QVERIFY(buttonBox != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(hostEdit != nullptr);
    QCOMPARE(buttonBox->buttonRole(connectButton), QDialogButtonBox::AcceptRole);
    QVERIFY(buttonBox->button(QDialogButtonBox::Cancel) != nullptr);
    QVERIFY(!nameEdit->isEnabled());
    QVERIFY(!hostEdit->isEnabled());

    addButton->click();

    QVERIFY(nameEdit->isEnabled());
    QVERIFY(hostEdit->isEnabled());
    QVERIFY(RadioProfileStore::instance().profiles().isEmpty());
    nameEdit->setText(QStringLiteral("Test IC-9700"));
    hostEdit->setText(QStringLiteral("192.0.2.1"));
    QVERIFY(saveButton->isEnabled());
    saveButton->click();

    QCOMPARE(RadioProfileStore::instance().profiles().size(), 1);
    const RadioProfile savedProfile = RadioProfileStore::instance().profiles().constFirst();
    QCOMPARE(savedProfile.name, QStringLiteral("Test IC-9700"));
    QCOMPARE(savedProfile.host, QStringLiteral("192.0.2.1"));
    QVERIFY(RadioProfileStore::instance().removeProfile(savedProfile.id));
}

void MemoryManagerSmokeTest::constructsMemoryManagerUi()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    const QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
    const auto memoryWindowIt =
        std::find_if(topLevelWidgets.cbegin(), topLevelWidgets.cend(),
                     [](const QWidget* candidate) { return candidate->objectName() == QLatin1String("memoryWindow"); });
    QDialog* memoryWindow =
        memoryWindowIt != topLevelWidgets.cend() ? qobject_cast<QDialog*>(*memoryWindowIt) : nullptr;
    QVERIFY(memoryWindow != nullptr);
    QVERIFY(memoryWindow->windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(memoryWindow->maximumWidth() > memoryWindow->minimumWidth());
    QVERIFY(memoryWindow->maximumHeight() > memoryWindow->minimumHeight());
    auto* table = memoryWindow->findChild<QTableWidget*>(QStringLiteral("memoryManagerTable"));
    auto* windowMenu = window.findChild<QMenu*>(QStringLiteral("windowMenu"));
    QVERIFY(table != nullptr);
    QVERIFY(windowMenu != nullptr);
    QVERIFY(memoryWindow->findChild<QWidget*>(QStringLiteral("memoryEditorPane")) == nullptr);
    QVERIFY(memoryWindow->findChild<QWidget*>(QStringLiteral("dialogFooterSeparator")) == nullptr);
    QVERIFY(memoryWindow->findChild<QWidget*>(QStringLiteral("dialogButtonBox")) == nullptr);
    QCOMPARE(table->columnCount(), 7);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Band"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Channel"));
    for (int column = 0; column < 6; ++column)
    {
        const auto expectedMode = column == 1 ? QHeaderView::Stretch : QHeaderView::Fixed;
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(column), expectedMode);
    }
    QCOMPARE(table->horizontalHeader()->height(), 32);

    memoryWindow->show();
    QVERIFY(QMetaObject::invokeMethod(windowMenu, "aboutToShow"));
    const QList<QAction*> windowActions = windowMenu->actions();
    const auto memoryWindowAction = std::find_if(windowActions.cbegin(), windowActions.cend(), [](const QAction* action)
                                                 { return action->text() == QLatin1String("Memory Manager"); });
    QVERIFY(memoryWindowAction != windowActions.cend());
    memoryWindow->hide();
    QCOMPARE(table->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);
    QVERIFY(table->styleSheet().contains(QLatin1String(UiTheme::Color::MenuBar)));

    const QList<QPushButton*> memoryButtons = memoryWindow->findChildren<QPushButton*>();
    const auto addButtonIt = std::find_if(memoryButtons.cbegin(), memoryButtons.cend(), [](const QPushButton* button)
                                          { return button->text() == QLatin1String("Add"); });
    QPushButton* addMemoryButton = addButtonIt != memoryButtons.cend() ? *addButtonIt : nullptr;
    QVERIFY(addMemoryButton != nullptr);
    bool foundEditorDialog = false;
    bool editorWasModal = false;
    bool editorWasFrameless = false;
    bool editorHadTitleBar = false;
    bool editorHadScrollArea = false;
    QString initialEditorBandText;
    QString initialEditorChannelText;
    QString derivedEditorBandText;
    QString derivedEditorChannelText;
    QTimer::singleShot(100, QCoreApplication::instance(),
                       [&]()
                       {
                           auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                           if (!dialog)
                           {
                               return;
                           }
                           foundEditorDialog = dialog->objectName() == QLatin1String("memoryEditorDialog");
                           editorWasModal = dialog->isModal();
                           editorWasFrameless = dialog->windowFlags().testFlag(Qt::FramelessWindowHint);
                           editorHadTitleBar =
                               dialog->findChild<QWidget*>(QStringLiteral("memoryEditorTitleBar")) != nullptr;
                           editorHadScrollArea =
                               dialog->findChild<QWidget*>(QStringLiteral("memoryEditorScrollArea")) != nullptr;
                           auto* bandEdit = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorBand"));
                           auto* channelCombo = dialog->findChild<QComboBox*>(QStringLiteral("memoryEditorChannel"));
                           auto* frequencyEdit = dialog->findChild<QLineEdit*>(QStringLiteral("memoryEditorFrequency"));
                           if (bandEdit && channelCombo && frequencyEdit)
                           {
                               QVERIFY(bandEdit->isReadOnly());
                               QCOMPARE(bandEdit->alignment(), Qt::AlignCenter);
                               initialEditorBandText = bandEdit->text();
                               initialEditorChannelText = channelCombo->currentText();
                               frequencyEdit->setText(QStringLiteral("443.050000"));
                               derivedEditorBandText = bandEdit->text();
                               derivedEditorChannelText = channelCombo->currentText();
                           }
                           dialog->reject();
                       });
    addMemoryButton->click();
    QVERIFY(foundEditorDialog);
    QVERIFY(editorWasModal);
    QVERIFY(editorWasFrameless);
    QVERIFY(editorHadTitleBar);
    QVERIFY(editorHadScrollArea);
    QVERIFY(initialEditorBandText.isEmpty());
    QVERIFY(initialEditorChannelText.isEmpty());
    QCOMPARE(derivedEditorBandText, QStringLiteral("70CM"));
    QCOMPARE(derivedEditorChannelText, QStringLiteral("001 [EMPTY]"));
    QCOMPARE(sdr9700::memory::memoryEditorDialogSize(QSize(1366, 768)), QSize(520, 620));
    QCOMPARE(sdr9700::memory::memoryEditorDialogSize(QSize(1024, 600)), QSize(520, 576));
}

void MemoryManagerSmokeTest::mainWindowRetainsFixedFramelessDesign()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    QString expectedTitle = QStringLiteral("SDR9700 v%1").arg(QString::fromLatin1(APP_VERSION));
#if SDR9700_DEBUG_BUILD
    expectedTitle += QStringLiteral(" (DEBUG)");
#endif
    QCOMPARE(window.windowTitle(), expectedTitle);
    QCOMPARE(window.minimumSize(), window.maximumSize());
    QCOMPARE(window.minimumSize(), QSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight));
    QVERIFY(window.findChild<QTableWidget*>(QStringLiteral("memoryBrowserTable")) == nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("vfoDisplayStrip")) != nullptr);
    QVERIFY(window.findChild<QSlider*>(QStringLiteral("titleLanModSlider")) == nullptr);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("vfoMODButton")) != nullptr);
}

void MemoryManagerSmokeTest::spectrumFrameRateSelectorDefaultsAndPersists()
{
    AppSettings& settings = AppSettings::instance();
    settings.remove(QStringLiteral("spectrumScopeFramesPerSecond"));

    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* selector = window.findChild<QComboBox*>(QStringLiteral("spectrumFramesPerSecondSelector"));
    QVERIFY(selector != nullptr);
    QCOMPARE(selector->count(), 5);
    QCOMPARE(selector->currentData().toInt(), 30);
    QCOMPARE(selector->itemText(0), QStringLiteral("10"));
    QCOMPARE(selector->itemText(4), QStringLiteral("30"));

    const auto buttons = window.findChildren<QToolButton*>();
    const auto previous = std::find_if(buttons.cbegin(), buttons.cend(), [](const QToolButton* button)
                                       { return button->accessibleName() == QStringLiteral("Previous FPS"); });
    QVERIFY(previous != buttons.cend());
    (*previous)->click();
    QCOMPARE(selector->currentData().toInt(), 25);
    QCOMPARE(settings.value(QStringLiteral("spectrumScopeFramesPerSecond")).toInt(), 25);
    settings.remove(QStringLiteral("spectrumScopeFramesPerSecond"));
}

void MemoryManagerSmokeTest::fileMenuTracksRadioConnection()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* connectionAction = window.findChild<QAction*>(QStringLiteral("radioConnectionAction"));
    QVERIFY(connectionAction != nullptr);
    QCOMPARE(connectionAction->text(), QStringLiteral("Connect to Radio"));

    QSignalSpy connectionSpy(&model, &RadioModel::connectionChanged);
    QSignalSpy stageSpy(&model, &RadioModel::connectionStageChanged);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendConnected"));
    QVERIFY(model.isConnected());
    QCOMPARE(connectionAction->text(), QStringLiteral("Disconnect from Radio"));

    connectionAction->trigger();
    QCOMPARE(stageSpy.count(), 1);
    QCOMPARE(stageSpy.constFirst().constFirst().value<ConnectionStage>(), ConnectionStage::Disconnecting);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendDisconnected"));
    QVERIFY(!model.isConnected());
    QVERIFY(connectionSpy.count() >= 2);
    QCOMPARE(connectionAction->text(), QStringLiteral("Connect to Radio"));
}

void MemoryManagerSmokeTest::selectorButtonsAvoidDynamicStyleSheets()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    int selectorCount = 0;
    for (QPushButton* button : window.findChildren<QPushButton*>())
    {
        if (dynamic_cast<sdr9700::ui::main_window::TwoLineButton*>(button))
        {
            ++selectorCount;
            QVERIFY(button->styleSheet().isEmpty());
            sdr9700::ui::main_window::setCommandButtonActive(button, true);
            sdr9700::ui::main_window::setCommandButtonActive(button, false);
            QVERIFY(button->styleSheet().isEmpty());
        }
    }
    QVERIFY(selectorCount > 0);
    auto* compressorButton = window.findChild<QPushButton*>(QStringLiteral("vfoCOMPButton"));
    QVERIFY(compressorButton != nullptr);
    QVERIFY(!compressorButton->isCheckable());
    auto* rfGainButton = window.findChild<QPushButton*>(QStringLiteral("rfGainButton"));
    auto* frequencyField = window.findChild<QWidget*>(QStringLiteral("vfoFrequencyField"));
    const auto frequencyEdits = window.findChildren<QLineEdit*>(QStringLiteral("vfoFrequency"));
    auto* statusDateLabel = window.findChild<QLabel*>(QStringLiteral("statusDateLabel"));
    auto* statusTimeLabel = window.findChild<QLabel*>(QStringLiteral("statusTimeLabel"));
    QVERIFY(rfGainButton == nullptr);
    QVERIFY(frequencyField == nullptr);
    QCOMPARE(frequencyEdits.size(), 2);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("vfoMemoryNameField")) == nullptr);
    QVERIFY(window.findChild<QLineEdit*>(QStringLiteral("vfoMemoryNameLabel")) == nullptr);
    QVERIFY(statusDateLabel != nullptr);
    QVERIFY(statusTimeLabel != nullptr);
    for (const QLineEdit* frequencyEdit : frequencyEdits)
    {
        QCOMPARE(frequencyEdit->alignment(), Qt::AlignRight | Qt::AlignVCenter);
    }
    QCOMPARE(statusDateLabel->alignment(), Qt::AlignCenter);
    QCOMPARE(statusTimeLabel->alignment(), Qt::AlignCenter);
    const QFontMetrics frequencyMetrics(frequencyEdits.constFirst()->font());
    QCOMPARE(frequencyMetrics.horizontalAdvance(QStringLiteral("000.000.000")),
             frequencyMetrics.horizontalAdvance(QStringLiteral("111.111.111")));
}

void MemoryManagerSmokeTest::compressorMenuReflectsConfirmedLevel()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendReadyChanged", Q_ARG(bool, true)));
    int requestedLevel = -1;
    RadioCommandController controller(&window, [&requestedLevel](int value) { requestedLevel = value; });

    bool inspectedUnknown = false;
    QTimer::singleShot(0, &window,
                       [&]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu != nullptr);
                           auto* slider = menu->findChild<QSlider*>();
                           QVERIFY(slider != nullptr);
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 0);
                           const QList<QLabel*> labels = menu->findChildren<QLabel*>();
                           QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel* label)
                                               { return label->text() == QStringLiteral("0%"); }));
                           model.vfo()->applyCompressorLevel(64);
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 0);
                           model.vfo()->clearCompressorLevel();
                           QVERIFY(slider->isEnabled());
                           inspectedUnknown = true;
                           menu->close();
                       });
    controller.showCompressorMenu(QPoint());
    QVERIFY(inspectedUnknown);

    model.vfo()->applyCompressorLevel(192);
    bool inspectedConfirmed = false;
    QTimer::singleShot(0, &window,
                       [&]()
                       {
                           auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                           QVERIFY(menu != nullptr);
                           auto* slider = menu->findChild<QSlider*>();
                           QVERIFY(slider != nullptr);
                           QVERIFY(slider->isEnabled());
                           QCOMPARE(slider->value(), 0);
                           const QList<QAction*> actions = menu->actions();
                           const auto enabledAction =
                               std::find_if(actions.cbegin(), actions.cend(), [](const QAction* action)
                                            { return action->text() == QStringLiteral("Enabled"); });
                           QVERIFY(enabledAction == actions.cend());
                           model.vfo()->applyCompressor(true);
                           QCOMPARE(slider->value(), 192);
                           model.vfo()->applyCompressor(false);
                           QCOMPARE(slider->value(), 0);
                           slider->setValue(200);
                           inspectedConfirmed = true;
                           menu->close();
                       });
    controller.showCompressorMenu(QPoint());
    QVERIFY(inspectedConfirmed);
    QCOMPARE(requestedLevel, 200);

    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendDisconnected"));
    QVERIFY(!model.vfo()->compressorLevelKnown());
}

void MemoryManagerSmokeTest::utilityWindowIsDestroyedWithHost()
{
    auto* host = new QWidget;
    QPointer<sdr9700::ui::UtilityWindow> utility = new sdr9700::ui::UtilityWindow(QStringLiteral("Test Utility"), host);

    QCOMPARE(utility->parentWidget(), host);
    QVERIFY(utility->isWindow());

    delete host;
    QVERIFY(utility.isNull());
}

void MemoryManagerSmokeTest::quitActionDefersWindowClose()
{
    RadioModel model;
    MainWindow window(&model, nullptr, false);
    // MainWindow normally opens the saved profile or chooser on its first event
    // turn. This test only exercises shutdown dispatch, so suppress that startup
    // callback before showing the window.
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    window.show();
    QVERIFY(window.isVisible());

    auto* quitAction = window.findChild<QAction*>(QStringLiteral("quitAction"));
    QVERIFY(quitAction != nullptr);
    quitAction->trigger();

    // Closing from inside QAction::triggered would tear down the native window
    // while QMenu is still dispatching its mouse-release event on macOS.
    QVERIFY(window.isVisible());
    QCoreApplication::sendPostedEvents(&window, QEvent::MetaCall);
    QVERIFY(!window.isVisible());

    // Shutdown disconnects model-to-UI delivery before the backend publishes
    // its final ready=false state. A late state signal and a repeated close
    // must not re-enter memory-table rebuilding during native teardown.
    model.readyChanged(false);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCloseEvent repeatedClose;
    QCoreApplication::sendEvent(&window, &repeatedClose);
    QVERIFY(repeatedClose.isAccepted());
}

void MemoryManagerSmokeTest::radioControlsDoNotWaitForInitialMemorySync()
{
    RadioModel model;
    MainWindow window(&model, nullptr, false);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);
    auto* memoryController = window.findChild<MemoryController*>();
    auto* speakerButton = window.findChild<QWidget*>(QStringLiteral("titleSpeakerMuteButton"));
    QVERIFY(memoryController != nullptr);
    QVERIFY(speakerButton != nullptr);
    QVERIFY(!memoryController->initialMemorySyncComplete());
    QVERIFY(!speakerButton->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendConnected"));
    QVERIFY(QMetaObject::invokeMethod(&model, "onBackendReadyChanged", Q_ARG(bool, true)));
    QCoreApplication::sendPostedEvents(&window, QEvent::MetaCall);

    QVERIFY(!memoryController->initialMemorySyncComplete());
    QVERIFY(speakerButton->isEnabled());
}

void MemoryManagerSmokeTest::backendReadinessWaitsForBothVfos()
{
    RadioBackend backend;
    QSignalSpy readySpy(&backend, &IRadioBackend::readyChanged);

    backend.m_initialMainFrequencyReceived = true;
    backend.m_initialMainModeReceived = true;
    backend.updateReadyState();
    QVERIFY(!backend.m_radioReady);
    QVERIFY(readySpy.isEmpty());

    backend.m_initialSubFrequencyReceived = true;
    backend.updateReadyState();
    QVERIFY(!backend.m_radioReady);
    QVERIFY(readySpy.isEmpty());

    backend.m_initialSubModeReceived = true;
    backend.updateReadyState();
    QVERIFY(backend.m_radioReady);
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.constFirst().constFirst().toBool(), true);
}

void MemoryManagerSmokeTest::backendQueuesStartupSubStateAsScopedAction()
{
    Commander commander;
    QSignalSpy wireSpy(&commander, &Commander::dataForComm);

    RadioBackend::requestSubVfoStateForCommand(&commander);

    QVERIFY(wireSpy.isEmpty());
    QCOMPARE(commander.m_scheduledCommands.size(), 1);
    const Commander::ScheduledCommand& command = commander.m_scheduledCommands.constFirst();
    QCOMPARE(command.commandClass, Commander::ScheduledCommandClass::ConfirmatoryRead);
    QCOMPARE(command.func, funcFreqGet);
    QCOMPARE(command.receiver, uchar(1));
    QVERIFY(command.action);
}

void MemoryManagerSmokeTest::backendQueuesStartupMainStateAsScopedAction()
{
    Commander commander;
    QSignalSpy wireSpy(&commander, &Commander::dataForComm);

    RadioBackend::scheduleInitialMainVfoIdentityForCommand(&commander, false);

    QVERIFY(wireSpy.isEmpty());
    QCOMPARE(commander.m_scheduledCommands.size(), 1);
    const Commander::ScheduledCommand& command = commander.m_scheduledCommands.constFirst();
    QCOMPARE(command.commandClass, Commander::ScheduledCommandClass::ConfirmatoryRead);
    QCOMPARE(command.func, funcFreqGet);
    QCOMPARE(command.receiver, uchar(0));
    QVERIFY(command.action);

    commander.m_scheduledCommands.clear();
    RadioBackend::requestMainVfoStateForCommand(&commander);

    QVERIFY(wireSpy.isEmpty());
    QCOMPARE(commander.m_scheduledCommands.size(), 1);
    const Commander::ScheduledCommand& fullStateCommand = commander.m_scheduledCommands.constFirst();
    QCOMPARE(fullStateCommand.commandClass, Commander::ScheduledCommandClass::ConfirmatoryRead);
    QCOMPARE(fullStateCommand.func, funcFreqGet);
    QCOMPARE(fullStateCommand.receiver, uchar(0));
    QVERIFY(fullStateCommand.action);
}

void MemoryManagerSmokeTest::backendQueuesPostExchangeSettingsForBothReceivers()
{
    Commander commander;

    RadioBackend::schedulePostExchangeSettingsForCommand(&commander);

    QCOMPARE(commander.m_scheduledCommands.size(), 2);
    QSet<uchar> receivers;
    for (const Commander::ScheduledCommand& command : std::as_const(commander.m_scheduledCommands))
    {
        QCOMPARE(command.commandClass, Commander::ScheduledCommandClass::ConfirmatoryRead);
        QCOMPARE(command.func, funcSplitStatus);
        QVERIFY(command.action);
        receivers.insert(command.receiver);
    }
    QCOMPARE(receivers, QSet<uchar>({0, 1}));
}

void MemoryManagerSmokeTest::memoryPttDoesNotReselectMemoryChannel()
{
    Commander commander;
    sdr9700::populateRadioCapabilities(commander.radioCaps);
    commander.haveRadioCaps = true;
    commander.setCIVAddr(0xA2);
    QSignalSpy wireSpy(&commander, &Commander::dataForComm);

    RadioBackend::sendPttOnForCommand(&commander, true);

    QVERIFY(!wireSpy.isEmpty());
    bool requestedTransmitStatus = false;
    bool requestedSelectedFrequency = false;
    bool requestedSelectedMode = false;
    for (const auto& emission : wireSpy)
    {
        const QByteArray command = emission.at(0).toByteArray().mid(4);
        QVERIFY2(command.isEmpty() || command.front() != '\x08', "PTT must not reselect a memory channel");
        requestedTransmitStatus = requestedTransmitStatus || command.startsWith(QByteArray::fromHex("1c00"));
        requestedSelectedFrequency = requestedSelectedFrequency || command.startsWith(QByteArray::fromHex("25"));
        requestedSelectedMode = requestedSelectedMode || command.startsWith(QByteArray::fromHex("26"));
    }
    QVERIFY(requestedTransmitStatus);
    QVERIFY(requestedSelectedFrequency);
    QVERIFY(requestedSelectedMode);
}

void MemoryManagerSmokeTest::backendKeepsUnkeyPendingAcrossStaleReadback()
{
    RadioBackend backend;
    QSignalSpy pttSpy(&backend, &IRadioBackend::pttChanged);

    QVERIFY(backend.m_pttState.requestOn());
    backend.m_pttState.confirm(true);
    backend.m_pttState.requestOff();
    backend.m_pttOffConfirmationClock.restart();
    backend.m_pttOffConfirmationTimer->start();

    backend.m_radioRouter->route(CacheItem(funcTransceiverStatus, QVariant::fromValue<bool>(true), 0));
    QVERIFY(backend.m_pttState.confirmedActive());
    QVERIFY(backend.m_pttState.offPending());
    QVERIFY(backend.m_pttOffConfirmationTimer->isActive());
    QVERIFY(pttSpy.isEmpty());

    backend.m_radioRouter->route(CacheItem(funcTransceiverStatus, QVariant::fromValue<bool>(false), 0));
    QVERIFY(!backend.m_pttState.confirmedActive());
    QVERIFY(!backend.m_pttState.offPending());
    QVERIFY(!backend.m_pttOffConfirmationTimer->isActive());
    QCOMPARE(pttSpy.count(), 1);
    QCOMPARE(pttSpy.takeFirst().at(0).toBool(), false);
}

void MemoryManagerSmokeTest::persistentStatusMessageCanBeClearedByOwner()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* statusMessageLabel = window.findChild<QLabel*>(QStringLiteral("statusMessageLabel"));
    const QObjectList children = window.children();
    const auto controller = std::find_if(children.cbegin(), children.cend(), [](const QObject* child)
                                         { return dynamic_cast<const StatusBarController*>(child) != nullptr; });
    QVERIFY(controller != children.cend());
    auto* statusBarController = dynamic_cast<StatusBarController*>(*controller);
    QVERIFY(statusMessageLabel != nullptr);
    QVERIFY(statusBarController != nullptr);

    const QString importMessage = QStringLiteral("Syncing radio memories before import...");
    statusBarController->showStatusMessage(importMessage, 0);
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Syncing radio memories before import"));

    statusBarController->clearPersistentStatusMessage(QStringLiteral("A different operation"));
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Syncing radio memories before import"));

    statusBarController->clearPersistentStatusMessage(importMessage);
    QVERIFY(statusMessageLabel->text().isEmpty());

    const QString recommendedLength(StatusBarController::kRecommendedStatusMessageCharacters, QLatin1Char('x'));
    statusBarController->showStatusMessage(recommendedLength, 0);
    QCOMPARE(statusMessageLabel->text(), recommendedLength);

    const QString warningLength(StatusBarController::kRecommendedStatusMessageCharacters + 1, QLatin1Char('x'));
    QTest::ignoreMessage(QtWarningMsg,
                         "Status message exceeds the recommended length characters=65 recommended=64 maximum=72");
    statusBarController->showStatusMessage(warningLength, 0);
    QCOMPARE(statusMessageLabel->text(), warningLength);

    const QString maximumLength(StatusBarController::kMaximumStatusMessageCharacters, QLatin1Char('x'));
    QTest::ignoreMessage(QtWarningMsg,
                         "Status message exceeds the recommended length characters=72 recommended=64 maximum=72");
    statusBarController->showStatusMessage(maximumLength, 0);
    QCOMPARE(statusMessageLabel->text(), maximumLength);

    const QString elidedLength(StatusBarController::kMaximumStatusMessageCharacters + 1, QLatin1Char('x'));
    QTest::ignoreMessage(
        QtWarningMsg,
        "Status message elided because it exceeds the maximum length characters=73 recommended=64 maximum=72");
    statusBarController->showStatusMessage(elidedLength, 0);
    QCOMPARE(statusMessageLabel->text().size(), StatusBarController::kMaximumStatusMessageCharacters);
    QVERIFY(statusMessageLabel->text().endsWith(QChar(0x2026)));
    QCOMPARE(statusMessageLabel->toolTip(), elidedLength);
    statusBarController->clearPersistentStatusMessage(maximumLength);
    QVERIFY(!statusMessageLabel->text().isEmpty());
    statusBarController->clearPersistentStatusMessage(elidedLength);
    QVERIFY(statusMessageLabel->text().isEmpty());
    QVERIFY(statusMessageLabel->toolTip().isEmpty());

    const QStringList punctuatedMessages = {
        QStringLiteral("Complete."),         QStringLiteral("Wait..."),   QStringLiteral("Warning!"),
        QStringLiteral("Continue?"),         QStringLiteral("Finished;"), QStringLiteral("Status:"),
        QStringLiteral("Unicode ellipsis…"),
    };
    for (const QString& message : punctuatedMessages)
    {
        statusBarController->showStatusMessage(message, 1000);
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char('.')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char('!')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char('?')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char(';')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QLatin1Char(':')), qPrintable(statusMessageLabel->text()));
        QVERIFY2(!statusMessageLabel->text().endsWith(QChar(0x2026)), qPrintable(statusMessageLabel->text()));
    }

    statusBarController->showStatusMessage(QStringLiteral("Radio ready."), 1);
    QCOMPARE(statusMessageLabel->text(), QStringLiteral("Radio ready"));
    QTRY_VERIFY_WITH_TIMEOUT(statusMessageLabel->text().isEmpty(), 100);

    emit model.statusMessage(QStringLiteral("Warning message"), MessageSeverity::Warning);
    QVERIFY(statusMessageLabel->styleSheet().contains(QString::fromLatin1(UiTheme::Color::Warning)));
    QVERIFY(!statusMessageLabel->styleSheet().contains(QStringLiteral("font-weight: bold")));

    emit model.statusMessage(QStringLiteral("Error message"), MessageSeverity::Error);
    QVERIFY(statusMessageLabel->styleSheet().contains(QString::fromLatin1(UiTheme::Color::Danger)));
    QVERIFY(!statusMessageLabel->styleSheet().contains(QStringLiteral("font-weight: bold")));
}

void MemoryManagerSmokeTest::automationIndicatorReflectsClientCount()
{
    RadioModel model;
    MainWindow window(&model);
    QCoreApplication::removePostedEvents(&window, QEvent::MetaCall);

    auto* indicator = window.findChild<QLabel*>(QStringLiteral("automationIndicator"));
    const QObjectList children = window.children();
    const auto controller = std::find_if(children.cbegin(), children.cend(), [](const QObject* child)
                                         { return dynamic_cast<const StatusBarController*>(child) != nullptr; });
    QVERIFY(controller != children.cend());
    auto* statusBarController = dynamic_cast<StatusBarController*>(*controller);
    QVERIFY(indicator != nullptr);
    QVERIFY(statusBarController != nullptr);

    statusBarController->setAutomationEnabled(true);
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("border: none")));
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("background: #f0a000")));
    QCOMPARE(indicator->toolTip(),
             QStringLiteral("Automation enabled.\n0 local clients connected.\nTransmit controls are unavailable."));

    statusBarController->setAutomationClientCount(1);
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("border: none")));
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("background: %1").arg(UiTheme::Color::Danger)));
    QCOMPARE(indicator->toolTip(),
             QStringLiteral("Automation enabled.\n1 local client connected.\nTransmit controls are unavailable."));

    statusBarController->setAutomationClientCount(0);
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("border: none")));
    QVERIFY(indicator->styleSheet().contains(QStringLiteral("background: #f0a000")));
}

void MemoryManagerSmokeTest::titleBarSpeakerTogglesMute()
{
    MainTitleBar titleBar;
    auto* lockButton = titleBar.findChild<QPushButton*>(QStringLiteral("titleLockButton"));
    QVERIFY(lockButton != nullptr);
    QVERIFY(!lockButton->icon().pixmap(QSize(18, 18)).isNull());

    auto* speakerButton = titleBar.findChild<QPushButton*>(QStringLiteral("titleSpeakerMuteButton"));
    QVERIFY(speakerButton != nullptr);
    QVERIFY(!speakerButton->icon().pixmap(QSize(18, 18)).isNull());
    QVERIFY(speakerButton->text().isEmpty());
    QCOMPARE(speakerButton->toolTip(), QStringLiteral("Mute audio"));

    const auto buttons = titleBar.findChildren<QPushButton*>();
    QVERIFY(std::none_of(buttons.cbegin(), buttons.cend(),
                         [](const QPushButton* button) { return button->text() == QStringLiteral("MUTE"); }));

    QSignalSpy muteSpy(&titleBar, &MainTitleBar::muteToggled);
    QTest::mouseClick(speakerButton, Qt::LeftButton);
    QCOMPARE(muteSpy.count(), 1);

    titleBar.setMuted(true);
    QVERIFY(!speakerButton->icon().pixmap(QSize(18, 18)).isNull());
    QVERIFY(speakerButton->text().isEmpty());
    QVERIFY(speakerButton->styleSheet().contains(QStringLiteral("border: 1px solid %1").arg(UiTheme::Color::Danger)));
    QCOMPARE(speakerButton->toolTip(), QStringLiteral("Unmute audio"));

    QTest::mouseClick(speakerButton, Qt::LeftButton);
    QCOMPARE(muteSpy.count(), 2);
    titleBar.setMuted(false);
    QVERIFY(speakerButton->text().isEmpty());
    QVERIFY(!speakerButton->icon().pixmap(QSize(18, 18)).isNull());
    QCOMPARE(speakerButton->toolTip(), QStringLiteral("Mute audio"));
}

QTEST_MAIN(MemoryManagerSmokeTest)

#include "MemoryManagerSmokeTest.moc"
