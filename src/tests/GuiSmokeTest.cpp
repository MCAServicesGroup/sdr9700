// QtTest invokes private slots through the generated meta-object.
#include "ConfirmationDialog.h"
#include "AudioDevicesSettingsPanel.h"
#include "SettingsDialog.h"
#include "AppSettings.h"
#include "MainWindowHelpers.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QShortcut>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QtTest>
#include <algorithm>

class GuiSmokeTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void settingsPagesKeepUniformSizeWithoutClipping_data();
    void settingsPagesKeepUniformSizeWithoutClipping();
    void settingsDialogOpensSearchesAndCloses();
    void settingsFindShortcutFocusesSearch();
    void audioSettingsChangesAreForwarded();
    void codecPendingReconnectTracksConnectionAndSelection();
    void codecNoticeKeepsLayoutStable_data();
    void codecNoticeKeepsLayoutStable();
    void deferredAudioPageShowsPendingCodec();
    void audioDeviceChoicesPreserveDefaultAndUnavailablePreferences();
    void receiveAndTransmitDeviceSelectionsAreIndependent();
#ifdef HAVE_HIDAPI
    void rc28ButtonActionsAreOrderedAndSupported();
#endif
    void confirmationDialogsUseSafeSemanticButtons();
};

void GuiSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void GuiSmokeTest::settingsPagesKeepUniformSizeWithoutClipping_data()
{
    QTest::addColumn<bool>("startWithSpectrum");
    QTest::newRow("audio-first") << false;
    QTest::newRow("spectrum-first") << true;
}

void GuiSmokeTest::settingsPagesKeepUniformSizeWithoutClipping()
{
    QFETCH(bool, startWithSpectrum);
    QWidget host;
    host.resize(1100, 800);
    SettingsDialog dialog(startWithSpectrum ? SettingsDialog::Page::SpectrumScope : SettingsDialog::Page::AudioDevices,
                          &host);
    // Reproduce MainWindow's pre-show placement, including adjustSize().
    sdr9700::ui::centerWindowOn(&dialog, &host);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    const QSize expectedSize(780, 520);
    QCOMPARE(dialog.size(), expectedSize);
    QCOMPARE(dialog.minimumSize(), expectedSize);
    QCOMPARE(dialog.maximumSize(), expectedSize);
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    auto* pages = dialog.findChild<QStackedWidget*>(QStringLiteral("settingsPages"));
    auto* scroll = dialog.findChild<QScrollArea*>();
    QVERIFY(navigation != nullptr);
    QVERIFY(pages != nullptr);
    QVERIFY(scroll != nullptr);
    // Reserve a real scrollbar's width even on platforms with overlay bars.
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int categoryIndex = 0; categoryIndex < navigation->topLevelItemCount(); ++categoryIndex)
        {
            auto* category = navigation->topLevelItem(categoryIndex);
            for (int pageIndex = 0; pageIndex < category->childCount(); ++pageIndex)
            {
                auto* item = category->child(pageIndex);
                navigation->setCurrentItem(item);
                QTest::qWait(20);
                QCOMPARE(dialog.size(), expectedSize);
                if (item->text(0) != QStringLiteral("Spectrum Scope"))
                {
                    continue;
                }
                const QWidget* viewport = scroll->viewport();
                int resetButtons = 0;
                for (auto* button : pages->currentWidget()->findChildren<QPushButton*>())
                {
                    const QRect bounds(button->mapTo(viewport, QPoint()), button->size());
                    QVERIFY2(bounds.left() >= 0 && bounds.right() < viewport->width(),
                             qPrintable(QStringLiteral("Clipped spectrum button: %1").arg(button->text())));
                    resetButtons += button->text() == QStringLiteral("Reset") ? 1 : 0;
                }
                QCOMPARE(resetButtons, 3);
                for (auto* combo : pages->currentWidget()->findChildren<QComboBox*>())
                {
                    const QRect bounds(combo->mapTo(viewport, QPoint()), combo->size());
                    QVERIFY(bounds.left() >= 0 && bounds.right() < viewport->width());
                }
            }
        }
    }
}

void GuiSmokeTest::settingsDialogOpensSearchesAndCloses()
{
    AppSettings::instance().remove(
        QString::fromLatin1(sdr9700::ui::main_window::kMemoryShowSpecialMemoriesSettingsKey));
    AppSettings::instance().remove(
        QString::fromLatin1(sdr9700::ui::main_window::kMemoryShowSatelliteMemoriesSettingsKey));
    SettingsDialog dialog(SettingsDialog::Page::MemoryManager);
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(dialog.maximumSize(), dialog.minimumSize());
    dialog.show();

    QTRY_VERIFY(dialog.isVisible());
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(search != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(navigation->topLevelItemCount() > 0);
    QVERIFY(!navigation->itemsExpandable());
    QVERIFY(!navigation->expandsOnDoubleClick());
    QTreeWidgetItem* category = navigation->topLevelItem(0);
    QVERIFY(category != nullptr);
    QVERIFY(category->childCount() > 0);
    QVERIFY(!category->flags().testFlag(Qt::ItemIsSelectable));
    QVERIFY(category->isExpanded());
    auto* showSpecial = dialog.findChild<QCheckBox*>(QStringLiteral("memoryManagerShowSpecialMemories"));
    auto* showSatellite = dialog.findChild<QCheckBox*>(QStringLiteral("memoryManagerShowSatelliteMemories"));
    auto* pollInterval = dialog.findChild<QComboBox*>(QStringLiteral("memoryManagerPollInterval"));
    QVERIFY(showSpecial != nullptr);
    QVERIFY(showSatellite != nullptr);
    QVERIFY(pollInterval != nullptr);
    QCOMPARE(pollInterval->count(), 6);
    QCOMPARE(pollInterval->itemText(0), QStringLiteral("Off"));
    QCOMPARE(pollInterval->itemData(0).toInt(), 0);
    QCOMPARE(pollInterval->itemData(5).toInt(), 3600);
    QVERIFY(!showSpecial->isChecked());
    QVERIFY(!showSatellite->isChecked());
    const QList<QLabel*> descriptions = dialog.findChildren<QLabel*>();
    QVERIFY(std::any_of(descriptions.cbegin(), descriptions.cend(),
                        [](const QLabel* label) { return label->text().contains(QStringLiteral("scan-edge pairs")); }));
    QVERIFY(std::any_of(descriptions.cbegin(), descriptions.cend(), [](const QLabel* label)
                        { return label->text().contains(QStringLiteral("paired receive/transmit records")); }));
    QSignalSpy specialChanged(&dialog, &SettingsDialog::memoryShowSpecialMemoriesChanged);
    QSignalSpy satelliteChanged(&dialog, &SettingsDialog::memoryShowSatelliteMemoriesChanged);
    showSpecial->click();
    showSatellite->click();
    QCOMPARE(specialChanged.count(), 1);
    QCOMPARE(satelliteChanged.count(), 1);
    QTreeWidgetItem* selectedPage = navigation->currentItem();
    QVERIFY(selectedPage != nullptr);
    QVERIFY(selectedPage->parent() != nullptr);
    QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                      navigation->visualItemRect(category).center());
    QCOMPARE(navigation->currentItem(), selectedPage);
    QTreeWidgetItemIterator itemIterator(navigation);
    while (*itemIterator)
    {
        QTreeWidgetItem* item = *itemIterator;
        QVERIFY(!item->toolTip(0).isEmpty());
        QVERIFY(item->toolTip(0) != item->data(0, Qt::UserRole + 1).toString());
        ++itemIterator;
    }

    search->setText(QStringLiteral("spectrum"));
    QCoreApplication::processEvents();
    QVERIFY(!search->text().isEmpty());

    dialog.close();
    QTRY_VERIFY(!dialog.isVisible());
}

void GuiSmokeTest::settingsFindShortcutFocusesSearch()
{
    SettingsDialog dialog(SettingsDialog::Page::MemoryManager);
    dialog.show();
    QTRY_VERIFY(dialog.isVisible());
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(search != nullptr);
    QVERIFY(navigation != nullptr);
    Q_UNUSED(navigation)
    auto* shortcut = dialog.findChild<QShortcut*>();
    QVERIFY(shortcut != nullptr);
    QCOMPARE(shortcut->key(), QKeySequence(QKeySequence::Find));
    search->setText(QStringLiteral("spectrum"));
    QVERIFY(QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection));
    QCOMPARE(search->selectedText(), QStringLiteral("spectrum"));
}

void GuiSmokeTest::audioSettingsChangesAreForwarded()
{
    SettingsDialog dialog(SettingsDialog::Page::AudioDevices);
    QSignalSpy changedSpy(&dialog, &SettingsDialog::audioSettingsChanged);
    dialog.show();

    auto* channels = dialog.findChild<QComboBox*>(QStringLiteral("audioOutputChannels"));
    QVERIFY(channels != nullptr);
    QCOMPARE(channels->count(), 2);
    QCOMPARE(channels->itemText(0), QStringLiteral("Mono mix (MAIN + SUB)"));
    QCOMPARE(channels->itemText(1), QStringLiteral("Stereo (MAIN left, SUB right)"));
    channels->setCurrentIndex(channels->currentIndex() == 0 ? 1 : 0);
    QCOMPARE(changedSpy.count(), 1);
}

void GuiSmokeTest::codecPendingReconnectTracksConnectionAndSelection()
{
    AppSettings::instance().setValue("audioOutputChannels", 2);
    SettingsDialog dialog(SettingsDialog::Page::AudioDevices);
    auto* channels = dialog.findChild<QComboBox*>(QStringLiteral("audioOutputChannels"));
    auto* pending = dialog.findChild<QLabel*>(QStringLiteral("audioCodecPendingReconnect"));
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("audioCodecPendingHint"));
    QVERIFY(channels != nullptr);
    QVERIFY(pending != nullptr);
    QVERIFY(hint != nullptr);
    QVERIFY(pending->isHidden());
    QVERIFY(hint->isHidden());
    QCOMPARE(pending->text(), QStringLiteral("Requires Radio Reconnect"));
    QCOMPARE(hint->text(), QStringLiteral("Disconnect and reconnect to the radio to apply."));

    dialog.setAudioConnectionState(true, 2);
    QVERIFY(pending->isHidden());
    channels->setCurrentIndex(channels->findData(1));
    QVERIFY(!pending->isHidden());
    QVERIFY(!hint->isHidden());
    channels->setCurrentIndex(channels->findData(2));
    QVERIFY(pending->isHidden());
    channels->setCurrentIndex(channels->findData(1));
    QVERIFY(!pending->isHidden());
    dialog.setAudioConnectionState(false, 2);
    QVERIFY(pending->isHidden());
    QVERIFY(hint->isHidden());
    dialog.setAudioConnectionState(true, 1);
    QVERIFY(pending->isHidden());
    channels->setCurrentIndex(channels->findData(2));
    QVERIFY(!pending->isHidden());
    dialog.setAudioConnectionState(false, 1);
    QVERIFY(pending->isHidden());
    channels->setCurrentIndex(channels->findData(1));
    QVERIFY(pending->isHidden());
    AppSettings::instance().setValue("audioOutputChannels", 2);
}

void GuiSmokeTest::codecNoticeKeepsLayoutStable_data()
{
    QTest::addColumn<int>("fontPixelSize");
    QTest::newRow("normal-font") << 13;
    QTest::newRow("larger-font") << 18;
}

void GuiSmokeTest::codecNoticeKeepsLayoutStable()
{
    QFETCH(int, fontPixelSize);
    AppSettings::instance().setValue("audioOutputChannels", 2);
    SettingsDialog dialog(SettingsDialog::Page::AudioDevices);
    QFont font = dialog.font();
    font.setPixelSize(fontPixelSize);
    dialog.setFont(font);
    dialog.setAudioConnectionState(true, 2);
    auto* channels = dialog.findChild<QComboBox*>(QStringLiteral("audioOutputChannels"));
    QVERIFY(channels != nullptr);
    QLabel* codecLabel = nullptr;
    int codecLabelCount = 0;
    for (QLabel* label : dialog.findChildren<QLabel*>())
    {
        if (label->text() == QStringLiteral("Playback:"))
        {
            codecLabel = label;
            ++codecLabelCount;
        }
    }
    QVERIFY(codecLabel != nullptr);
    QCOMPARE(codecLabelCount, 1);
    // This test measures codec-driven layout changes, so finish placement
    // before showing and use UtilityWindow's prepositioned path. A fixed wait
    // cannot guarantee that its deferred centering passes have drained on a
    // busy CI runner; those passes can move the window during our assertions.
    dialog.setProperty("prepositionedBeforeShow", true);
    dialog.centerOnHost();
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    const auto geometryInDialog = [&dialog](QWidget* widget)
    { return QRect(widget->mapTo(&dialog, QPoint()), widget->size()); };
    const QRect comboGeometry = geometryInDialog(channels);
    auto* outputDevice = dialog.findChild<QComboBox*>(QStringLiteral("audioOutputDevice"));
    auto* notice = dialog.findChild<QLabel*>(QStringLiteral("audioCodecPendingReconnect"));
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("audioCodecPendingHint"));
    QVERIFY(outputDevice != nullptr);
    QVERIFY(notice != nullptr);
    QVERIFY(hint != nullptr);
    QCOMPARE(comboGeometry.top() - geometryInDialog(outputDevice).bottom() - 1, 13);
    const QRect labelGeometry = geometryInDialog(codecLabel);
    QVERIFY2(qAbs(labelGeometry.center().y() - comboGeometry.center().y()) <= 1,
             "Codec label must be vertically centered on its combo box, not on the notice area");
    const QRect fieldGeometry = geometryInDialog(channels->parentWidget());
    const QRect windowGeometry = dialog.geometry();
    QVERIFY(notice->parentWidget()->isHidden());

    // Test both showing and hiding the notice after layout events settle.
    // Width-only checks miss QLabel height changes that move its text baseline.
    for (int selectedChannels : {1, 2, 1, 2})
    {
        channels->setCurrentIndex(channels->findData(selectedChannels));
        QTest::qWait(20);
        QCOMPARE(geometryInDialog(channels), comboGeometry);
        QCOMPARE(geometryInDialog(codecLabel), labelGeometry);
        QCOMPARE(dialog.geometry(), windowGeometry);
        if (selectedChannels == 1)
        {
            QVERIFY(!notice->parentWidget()->isHidden());
            QCOMPARE(geometryInDialog(notice).top() - comboGeometry.bottom() - 1, 10);
            QCOMPARE(geometryInDialog(hint).top() - geometryInDialog(notice).bottom() - 1, 2);
            QVERIFY(geometryInDialog(channels->parentWidget()).height() > fieldGeometry.height());
        }
        else
        {
            QVERIFY(notice->parentWidget()->isHidden());
            QCOMPARE(geometryInDialog(channels->parentWidget()), fieldGeometry);
        }
    }
}

void GuiSmokeTest::deferredAudioPageShowsPendingCodec()
{
    AppSettings::instance().setValue("audioOutputChannels", 1);
    SettingsDialog dialog(SettingsDialog::Page::MemoryManager);
    dialog.setAudioConnectionState(true, 2);
    auto* navigation = dialog.findChild<QTreeWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(navigation != nullptr);
    const auto pages = navigation->findItems(QStringLiteral("Audio Devices"), Qt::MatchExactly | Qt::MatchRecursive);
    QCOMPARE(pages.size(), 1);
    navigation->setCurrentItem(pages.front());
    auto* pending = dialog.findChild<QLabel*>(QStringLiteral("audioCodecPendingReconnect"));
    QVERIFY(pending != nullptr);
    QVERIFY(!pending->isHidden());
    dialog.setAudioConnectionState(true, 1);
    QVERIFY(pending->isHidden());
    AppSettings::instance().setValue("audioOutputChannels", 2);
}

void GuiSmokeTest::receiveAndTransmitDeviceSelectionsAreIndependent()
{
    AppSettings& settings = AppSettings::instance();
    const QByteArray microphoneID("sdr9700-test-microphone");
    const QByteArray speakerID("sdr9700-test-speaker");
    settings.setValue("audioInputDeviceID", QString::fromLatin1(microphoneID.toBase64()));
    settings.setValue("audioOutputDeviceID", QString::fromLatin1(speakerID.toBase64()));
    AudioDevicesSettingsPanel panel;
    auto* input = panel.findChild<QComboBox*>(QStringLiteral("audioInputDevice"));
    auto* output = panel.findChild<QComboBox*>(QStringLiteral("audioOutputDevice"));
    QVERIFY(input != nullptr);
    QVERIFY(output != nullptr);
    QSignalSpy changes(&panel, &AudioDevicesSettingsPanel::audioSettingsChanged);

    output->setCurrentIndex(0);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(input->currentData().toByteArray(), microphoneID);
    QCOMPARE(settings.value("audioInputDeviceID").toString(), QString::fromLatin1(microphoneID.toBase64()));
    QVERIFY(settings.value("audioOutputDeviceID").toString().isEmpty());

    output->setCurrentIndex(output->findData(speakerID));
    input->setCurrentIndex(0);
    QCOMPARE(changes.count(), 3);
    QCOMPARE(output->currentData().toByteArray(), speakerID);
    QCOMPARE(settings.value("audioOutputDeviceID").toString(), QString::fromLatin1(speakerID.toBase64()));
    QVERIFY(settings.value("audioInputDeviceID").toString().isEmpty());
    settings.remove("audioInputDeviceID");
    settings.remove("audioOutputDeviceID");
}

void GuiSmokeTest::audioDeviceChoicesPreserveDefaultAndUnavailablePreferences()
{
    AppSettings& settings = AppSettings::instance();
    settings.remove("audioInputDeviceID");
    settings.remove("audioOutputDeviceID");
    {
        AudioDevicesSettingsPanel panel;
        for (const QString& name : {QStringLiteral("audioInputDevice"), QStringLiteral("audioOutputDevice")})
        {
            auto* combo = panel.findChild<QComboBox*>(name);
            QVERIFY(combo != nullptr);
            QVERIFY(combo->currentData().toByteArray().isEmpty());
            QVERIFY(combo->currentText().startsWith(QStringLiteral("System default (")));
        }
        // Opening the panel must not pin an arbitrary device from enumeration.
        QVERIFY(settings.value("audioInputDeviceID").toString().isEmpty());
        QVERIFY(settings.value("audioOutputDeviceID").toString().isEmpty());
    }

    const QByteArray unavailableID("sdr9700-test-unavailable-output");
    settings.setValue("audioOutputDeviceID", QString::fromLatin1(unavailableID.toBase64()));
    AudioDevicesSettingsPanel panel;
    auto* output = panel.findChild<QComboBox*>(QStringLiteral("audioOutputDevice"));
    QVERIFY(output != nullptr);
    QCOMPARE(output->currentData().toByteArray(), unavailableID);
    QVERIFY(output->currentText().startsWith(QStringLiteral("Saved device unavailable")));
    QCOMPARE(settings.value("audioOutputDeviceID").toString(), QString::fromLatin1(unavailableID.toBase64()));
    QSignalSpy changes(&panel, &AudioDevicesSettingsPanel::audioSettingsChanged);
    output->setCurrentIndex(0);
    QCOMPARE(changes.count(), 1);
    QVERIFY(settings.value("audioOutputDeviceID").toString().isEmpty());
}

#ifdef HAVE_HIDAPI
void GuiSmokeTest::rc28ButtonActionsAreOrderedAndSupported()
{
    SettingsDialog dialog(SettingsDialog::Page::IcomRC28);
    auto* actions = dialog.findChild<QComboBox*>(QStringLiteral("icomRC28F1PressAction"));
    QVERIFY(actions != nullptr);

    const QStringList expectedLabels = {QStringLiteral("None"),
                                        QStringLiteral("Lock"),
                                        QStringLiteral("Switch Receiver"),
                                        QStringLiteral("Swap MAIN and SUB"),
                                        QStringLiteral("Mode"),
                                        QStringLiteral("Mute"),
                                        QStringLiteral("Step"),
                                        QStringLiteral("Step Down"),
                                        QStringLiteral("Step Up")};
    const QStringList expectedIds = {QStringLiteral("None"),          QStringLiteral("ToggleLock"),
                                     QStringLiteral("ToggleMainSub"), QStringLiteral("ExchangeMainSub"),
                                     QStringLiteral("CycleMode"),     QStringLiteral("ToggleMute"),
                                     QStringLiteral("CycleStep"),     QStringLiteral("StepDown"),
                                     QStringLiteral("StepUp")};
    QCOMPARE(actions->count(), expectedLabels.size());
    for (int i = 0; i < actions->count(); ++i)
    {
        QCOMPARE(actions->itemText(i), expectedLabels.at(i));
        QCOMPARE(actions->itemData(i).toString(), expectedIds.at(i));
    }
    QCOMPARE(actions->findData(QStringLiteral("ToggleRit")), -1);
}
#endif

void GuiSmokeTest::confirmationDialogsUseSafeSemanticButtons()
{
    QMessageBox dialog;
    QPushButton* action = sdr9700::ui::configureConfirmationButtons(dialog, QStringLiteral("Import"), true);
    auto* cancel = qobject_cast<QPushButton*>(dialog.button(QMessageBox::Cancel));

    QVERIFY(action != nullptr);
    QVERIFY(cancel != nullptr);
    QCOMPARE(action->text(), QStringLiteral("Import"));
    QCOMPARE(dialog.buttonRole(action), QMessageBox::DestructiveRole);
    QCOMPARE(dialog.defaultButton(), cancel);
    QCOMPARE(dialog.escapeButton(), cancel);
    sdr9700::ui::configureMessageBoxWindow(dialog);
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(dialog.minimumSize(), dialog.maximumSize());
}

QTEST_MAIN(GuiSmokeTest)
#include "GuiSmokeTest.moc"
