#include "VfoController.h"
#include "VfoDisplay.h"
#include "VfoSelectionPanel.h"
#include "VfoSMeter.h"
#include "UiTheme.h"

#include <cmath>
#include <QFocusEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

class VfoDisplayTest : public QObject
{
    Q_OBJECT

  private slots:
    void controllersKeepIndependentIdentityAndFrequency();
    void manualFrequencyEditingDefersRadioUpdates();
    void selectionPanelPublishesRequestsAndAppliesConfirmedState();
    void dialLockDisablesTuningButLeavesOperationalControlsEnabled();
};

void VfoDisplayTest::manualFrequencyEditingDefersRadioUpdates()
{
    VfoDisplay display(Vfo::Main);
    auto* frequency = display.findChild<QLineEdit*>(QStringLiteral("vfoFrequency"));
    QVERIFY(frequency);

    display.setFrequencyHz(145500000ULL);
    frequency->setCursorPosition(3);
    QFocusEvent focusIn(QEvent::FocusIn, Qt::MouseFocusReason);
    QCoreApplication::sendEvent(frequency, &focusIn);
    display.setFrequencyHz(433920000ULL);
    QCOMPARE(frequency->text(), QStringLiteral("145.500.000"));
    QCOMPARE(frequency->cursorPosition(), 3);
    QVERIFY(QMetaObject::invokeMethod(frequency, "editingFinished", Qt::DirectConnection));
    QCOMPARE(frequency->text(), QStringLiteral("433.920.000"));

    QCoreApplication::sendEvent(frequency, &focusIn);
    display.setFrequencyHz(1296000000ULL);
    auto* editTimer = display.findChild<QTimer*>(QStringLiteral("frequencyEditTimer"));
    QVERIFY(editTimer);
    editTimer->setInterval(1);
    QTRY_COMPARE(frequency->text(), QStringLiteral("1296.000.000"));

    frequency->setText(QStringLiteral("146.52"));
    QVERIFY(
        QMetaObject::invokeMethod(frequency, "textEdited", Qt::DirectConnection, Q_ARG(QString, frequency->text())));
    display.setFrequencyHz(433920000ULL);
    QCOMPARE(frequency->text(), QStringLiteral("146.52"));

    QVERIFY(QMetaObject::invokeMethod(frequency, "editingFinished", Qt::DirectConnection));
    QCOMPARE(frequency->text(), QStringLiteral("433.920.000"));

    frequency->setText(QStringLiteral("1296."));
    QVERIFY(
        QMetaObject::invokeMethod(frequency, "textEdited", Qt::DirectConnection, Q_ARG(QString, frequency->text())));
    display.clearFrequency();
    QCOMPARE(frequency->text(), QStringLiteral("1296."));
    QVERIFY(QMetaObject::invokeMethod(frequency, "editingFinished", Qt::DirectConnection));
    QCOMPARE(frequency->text(), QStringLiteral("---.---.---"));
}

void VfoDisplayTest::controllersKeepIndependentIdentityAndFrequency()
{
    QWidget parent;
    VfoController mainController(Vfo::Main, nullptr, nullptr, &parent);
    VfoController subController(Vfo::Sub, nullptr, nullptr, &parent);

    QCOMPARE(mainController.vfo(), Vfo::Main);
    QCOMPARE(subController.vfo(), Vfo::Sub);
    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("---.---.---"));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("---.---.---"));

    QSignalSpy mainStatePublishedSpy(&mainController, &VfoController::statePublished);
    mainController.setFrequencyHz(145500000ULL);
    subController.setFrequencyHz(433920000ULL);
    QVERIFY(mainController.hasPublishedState());
    QCOMPARE(mainStatePublishedSpy.count(), 1);
    QCOMPARE(mainStatePublishedSpy.takeFirst().at(0).value<Vfo>(), Vfo::Main);

    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("145.500.000"));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("433.920.000"));
    auto* mainTransmitFrequency = mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTransmitFrequency"));
    QVERIFY(mainTransmitFrequency != nullptr);
    QCOMPARE(mainTransmitFrequency->font().pixelSize(), 13);
    QCOMPARE(mainTransmitFrequency->contentsMargins().right(), 6);
    QCOMPARE(mainTransmitFrequency->contentsMargins().top(), 0);
    QCOMPARE(mainTransmitFrequency->contentsMargins().bottom(), 0);
    QVERIFY(mainTransmitFrequency->text().isEmpty());
    mainController.display()->setTransmitFrequencyHz(146100000ULL);
    QCOMPARE(mainTransmitFrequency->text(), QStringLiteral("TX: 146.100.000"));
    QCOMPARE(mainTransmitFrequency->accessibleDescription(), QStringLiteral("Transmit frequency 146.100000 MHz"));
    mainController.display()->clearTransmitFrequency();
    QVERIFY(mainTransmitFrequency->text().isEmpty());
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->text(),
             QStringLiteral("MAIN"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->text(),
             QStringLiteral("SUB"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->text(),
             QStringLiteral("2M"));
    QCOMPARE(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->text(),
             QStringLiteral("70CM"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->text(),
             QStringLiteral("--"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->property("active"),
             QVariant(true));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->property("active"),
             QVariant(true));
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(), QStringLiteral("TX"));
    QVERIFY(subController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge")) == nullptr);
    auto* mainSMeter = mainController.display()->findChild<VfoSMeter*>();
    auto* subSMeter = subController.display()->findChild<VfoSMeter*>();
    QVERIFY(mainSMeter != nullptr);
    QVERIFY(subSMeter != nullptr);
    mainController.display()->setSMeterValue(120);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("Signal strength S9"));
    mainController.display()->setSMeterValue(130);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("Signal strength S9+05"));
    mainController.display()->setSMeterValue(255);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("Signal strength S9+60"));
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton")) != nullptr);
    auto* txPowerButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTXPWRButton"));
    auto* lanModButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoMODButton"));
    auto* headerTxBadge = mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"));
    auto* headerIdentityButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"));
    QVERIFY(txPowerButton != nullptr);
    QVERIFY(lanModButton != nullptr);
    QVERIFY(headerTxBadge != nullptr);
    QVERIFY(headerIdentityButton != nullptr);
    QCOMPARE(txPowerButton->width(), 68);
    QCOMPARE(txPowerButton->text(), QStringLiteral("PWR 0%"));
    mainController.setLanModLevel(128);
    QCOMPARE(lanModButton->text(), QStringLiteral("MOD 50%"));
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoTXPWRButton")) == nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoMODButton")) == nullptr);
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoXFCButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoXFCButton")) == nullptr);
    QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoCOMPButton")) != nullptr);
    QVERIFY(subController.display()->findChild<QPushButton*>(QStringLiteral("vfoCOMPButton")) == nullptr);
    auto* toneStateButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"));
    auto* offsetStateButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"));
    mainController.display()->setReceiverControlState(QStringLiteral("TONE"), QStringLiteral("TSQL 67.0"), true);
    mainController.display()->setReceiverControlState(QStringLiteral("OFFSET"), QStringLiteral("SIMPLEX"), false);
    mainController.display()->setReceiverControlState(QStringLiteral("PRE"), QString(), true);
    mainController.display()->setReceiverControlState(QStringLiteral("TX PWR"), QStringLiteral("65%"), true);
    QCOMPARE(toneStateButton->text(), QStringLiteral("TSQL 67.0"));
    QCOMPARE(offsetStateButton->text(), QStringLiteral("SIMPLEX"));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoPREButton"))->text(),
             QStringLiteral("P.AMP"));
    QCOMPARE(txPowerButton->text(), QStringLiteral("PWR 65%"));
    for (const QString& control :
         {QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("FILTERS"), QStringLiteral("PRE"),
          QStringLiteral("RFG"), QStringLiteral("TONE"), QStringLiteral("OFFSET"), QStringLiteral("SQL"),
          QStringLiteral("TXPWR"), QStringLiteral("MOD")})
    {
        QVERIFY(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfo%1Button").arg(control)) !=
                nullptr);
    }

    mainController.clearFrequency();
    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("---.---.---"));
    QCOMPARE(subController.display()->frequencyText(), QStringLiteral("433.920.000"));

    auto* mainFrequency = mainController.display()->findChild<QLineEdit*>(QStringLiteral("vfoFrequency"));
    QVERIFY(mainFrequency != nullptr);
    QCOMPARE(mainFrequency->textMargins().top(), 15);
    QCOMPARE(mainFrequency->textMargins().bottom(), 0);
    QVERIFY(!mainFrequency->isReadOnly());
    QSignalSpy submissionSpy(mainController.display(), &VfoDisplay::frequencySubmitted);
    mainFrequency->setText(QStringLiteral("not a frequency"));
    QVERIFY(QMetaObject::invokeMethod(mainFrequency, "returnPressed", Qt::DirectConnection));
    QCOMPARE(submissionSpy.count(), 1);
    QCOMPARE(mainController.display()->frequencyText(), QStringLiteral("---.---.---"));

    QVERIFY(headerIdentityButton->testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(headerIdentityButton->focusPolicy(), Qt::NoFocus);
    QCOMPARE(headerIdentityButton->accessibleName(), QStringLiteral("MAIN VFO indicator"));

    mainController.setSelected(true);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"))->property("active"),
             QVariant(true));
    mainController.setOperatingEnabled(false);
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->property("active"),
             QVariant(false));
    QCOMPARE(mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"))->property("active"),
             QVariant(false));
    for (const QString& control :
         {QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("FILTERS"), QStringLiteral("PRE"),
          QStringLiteral("RFG"), QStringLiteral("TONE"), QStringLiteral("OFFSET"), QStringLiteral("SQL"),
          QStringLiteral("TXPWR"), QStringLiteral("MOD")})
    {
        QVERIFY(!mainController.display()
                     ->findChild<QPushButton*>(QStringLiteral("vfo%1Button").arg(control))
                     ->isEnabled());
    }
    auto* txBadge = mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"));
    QVERIFY(txBadge);
    QVERIFY(!txBadge->isEnabled());
    mainController.setFrequencyHz(145500000ULL);
    mainController.setOperatingEnabled(true);
    QVERIFY(txBadge->isEnabled());
    mainController.display()->setTransmitPowerWatts(42.0);
    mainController.setTransmitting(true);
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->property("transmitting"),
             QVariant(true));
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(), QStringLiteral("--"));
    mainController.display()->setTransmitSwr(1.23);
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(),
             QStringLiteral("1.23"));
    mainController.display()->setTransmitSwr(99.0);
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(),
             QStringLiteral("6.00"));
    QCOMPARE(mainSMeter->accessibleName(), QStringLiteral("RF power meter"));
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("RF power 0.0 watts"));
    mainController.display()->setTransmitPowerWatts(42.0);
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("RF power 42.0 watts"));
    mainController.setTransmitting(false);
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(), QStringLiteral("TX"));
    QCOMPARE(mainSMeter->accessibleName(), QStringLiteral("Signal strength meter"));
    mainController.display()->setTransmitSwr(2.34);
    mainController.setTransmitting(true);
    QCOMPARE(mainController.display()->findChild<QLabel*>(QStringLiteral("vfoTxBadge"))->text(), QStringLiteral("--"));
    QCOMPARE(mainSMeter->accessibleDescription(), QStringLiteral("RF power 0.0 watts"));
    mainController.setTransmitting(false);

    parent.resize(640, 240);
    mainController.display()->setGeometry(0, 0, 484, mainController.display()->height());
    mainController.display()->show();
    parent.show();
    QApplication::processEvents();
    const auto* receiverButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoAGCButton"));
    const auto* toneButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoTONEButton"));
    const auto* offsetButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoOFFSETButton"));
    const auto* xfcButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoXFCButton"));
    const auto* compressorButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoCOMPButton"));
    QCOMPARE(toneButton->width(), 80);
    QCOMPARE(xfcButton->geometry().left() - offsetButton->geometry().right() - 1, 6);
    QCOMPARE(compressorButton->geometry().left() - toneButton->geometry().right() - 1, 6);
    const auto* rightReceiverButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    const QStringList bottomControls = {QStringLiteral("AGC"), QStringLiteral("ATT"), QStringLiteral("FILTERS"),
                                        QStringLiteral("PRE"), QStringLiteral("RFG"), QStringLiteral("SQL")};
    for (qsizetype index = 1; index < bottomControls.size(); ++index)
    {
        const auto* previous = mainController.display()->findChild<QPushButton*>(
            QStringLiteral("vfo%1Button").arg(bottomControls.at(index - 1)));
        const auto* current = mainController.display()->findChild<QPushButton*>(
            QStringLiteral("vfo%1Button").arg(bottomControls.at(index)));
        QCOMPARE(current->geometry().left() - previous->geometry().right() - 1, 6);
    }
    const auto* modeButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"));
    const auto* identityButton = mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoIdentityButton"));
    const int renderedInset = identityButton->y();
    QCOMPARE(mainController.display()->height() - receiverButton->geometry().bottom() - 1, renderedInset);
    const int toneBottom = toneButton->mapTo(mainController.display(), QPoint(0, toneButton->height() - 1)).y();
    QCOMPARE(receiverButton->y() - toneBottom - 1, 40);
    QCOMPARE(identityButton->x(), renderedInset);
    QCOMPARE(mainController.display()->width() - rightReceiverButton->geometry().right() - 1, renderedInset);
    const int identityToTxGap = txBadge->geometry().left() - identityButton->geometry().right() - 1;
    const int txToModGap = lanModButton->geometry().left() - txBadge->geometry().right() - 1;
    const int powerToBandGap =
        mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->geometry().left() -
        txPowerButton->geometry().right() - 1;
    QVERIFY(identityToTxGap > 6);
    QVERIFY(std::abs(txToModGap - identityToTxGap) <= 1);
    QCOMPARE(txPowerButton->geometry().left() - lanModButton->geometry().right() - 1, 6);
    QVERIFY(std::abs(powerToBandGap - identityToTxGap) <= 1);
    QCOMPARE(
        modeButton->geometry().left() -
            mainController.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"))->geometry().right() - 1,
        6);
    QVERIFY(txBadge->geometry().right() < txPowerButton->geometry().left());
    QCOMPARE(mainController.display()->width() - modeButton->geometry().right() - 1, renderedInset);

    // The active badge replaces "TX" with the live SWR. Verify the repolished
    // dynamic-property style keeps the border red after that text transition.
    mainController.setTransmitting(true);
    mainController.display()->setTransmitSwr(1.23);
    QApplication::processEvents();
    const QImage activeBadge = txBadge->grab().toImage();
    QCOMPARE(activeBadge.pixelColor(activeBadge.width() / 2, 0), QColor(UiTheme::Color::Danger));
    mainController.setTransmitting(false);
}

void VfoDisplayTest::selectionPanelPublishesRequestsAndAppliesConfirmedState()
{
    VfoSelectionPanel panel;
    QSignalSpy vfoSpy(&panel, &VfoSelectionPanel::vfoRequested);
    QSignalSpy dualWatchSpy(&panel, &VfoSelectionPanel::dualWatchRequested);
    QSignalSpy exchangeSpy(&panel, &VfoSelectionPanel::exchangeRequested);

    QCOMPARE(panel.selectedVfo(), Vfo::Main);
    QVERIFY(!panel.dualWatchEnabled());
    panel.setRadioReady(true);

    const auto buttons = panel.findChildren<QPushButton*>();
    QCOMPARE(buttons.size(), 4);
    QPushButton* exchangeButton = nullptr;
    for (QPushButton* button : buttons)
    {
        if (button->text() == QStringLiteral("SUB"))
        {
            button->click();
        }
        else if (button->text() == QStringLiteral("DUAL"))
        {
            button->click();
        }
        else if (button->text() == QStringLiteral("MAIN ↔ SUB"))
        {
            exchangeButton = button;
            button->click();
        }
    }

    QVERIFY(exchangeButton != nullptr);
    QVERIFY(!exchangeButton->isEnabled());
    QCOMPARE(vfoSpy.count(), 0);
    QCOMPARE(dualWatchSpy.count(), 1);
    QCOMPARE(exchangeSpy.count(), 0);
    QCOMPARE(dualWatchSpy.takeFirst().at(0).toBool(), true);
    QCOMPARE(panel.selectedVfo(), Vfo::Main);
    QVERIFY(!panel.dualWatchEnabled());

    panel.setSelectedVfo(Vfo::Sub);
    panel.setDualWatchEnabled(true);
    QCOMPARE(panel.selectedVfo(), Vfo::Sub);
    QVERIFY(panel.dualWatchEnabled());
    QPushButton* selectedSubButton = nullptr;
    for (QPushButton* button : buttons)
    {
        if (button->text() == QStringLiteral("SUB"))
        {
            selectedSubButton = button;
        }
    }
    QVERIFY(selectedSubButton != nullptr);
    QCOMPARE(selectedSubButton->accessibleDescription(), QStringLiteral("Selected receiver."));
    QVERIFY(exchangeButton->isEnabled());
    for (QPushButton* button : buttons)
    {
        if (button->text() == QStringLiteral("SUB"))
        {
            button->click();
        }
    }
    QCOMPARE(vfoSpy.count(), 1);
    QCOMPARE(vfoSpy.takeFirst().at(0).value<Vfo>(), Vfo::Sub);
    exchangeButton->click();
    QCOMPARE(exchangeSpy.count(), 1);

    QPushButton* mainButton = nullptr;
    QPushButton* subButton = nullptr;
    QPushButton* dualWatchButton = nullptr;
    for (QPushButton* button : buttons)
    {
        if (button->text() == QStringLiteral("MAIN"))
        {
            mainButton = button;
        }
        else if (button->text() == QStringLiteral("SUB"))
        {
            subButton = button;
        }
        else if (button->text() == QStringLiteral("DUAL"))
        {
            dualWatchButton = button;
        }
        else if (button->text() == QStringLiteral("MAIN ↔ SUB"))
        {
            exchangeButton = button;
        }
    }
    QVERIFY(mainButton != nullptr);
    QVERIFY(subButton != nullptr);
    QVERIFY(dualWatchButton != nullptr);
    QVERIFY(exchangeButton != nullptr);
    panel.setDualWatchEnabled(false);
    QVERIFY(!mainButton->isEnabled());
    QVERIFY(!subButton->isEnabled());
    QVERIFY(!exchangeButton->isEnabled());
    QVERIFY(dualWatchButton->isEnabled());
    panel.setDualWatchEnabled(true);
    panel.setRadioReady(false);
    QVERIFY(!mainButton->property("active").toBool());
    QVERIFY(!subButton->property("active").toBool());
    QVERIFY(!dualWatchButton->property("active").toBool());
    QVERIFY(!mainButton->isEnabled());
    QVERIFY(!subButton->isEnabled());
    panel.setRadioReady(true);
    QVERIFY(!mainButton->property("active").toBool());
    QVERIFY(subButton->property("active").toBool());
    QVERIFY(dualWatchButton->property("active").toBool());
    panel.setReceiverContextReady(false);
    QVERIFY(!exchangeButton->isEnabled());
    QVERIFY(!mainButton->isEnabled());
    QVERIFY(!subButton->isEnabled());
    QVERIFY(!dualWatchButton->isEnabled());
    panel.setReceiverContextReady(true);
    QVERIFY(exchangeButton->isEnabled());
    panel.setExchangePending(true);
    QVERIFY(!exchangeButton->isEnabled());
    panel.setReceiverContextReady(true);
    QVERIFY(!exchangeButton->isEnabled());
    panel.setExchangePending(false);
    QVERIFY(exchangeButton->isEnabled());
    panel.setDualWatchPending(true);
    QVERIFY(!mainButton->isEnabled());
    QVERIFY(!subButton->isEnabled());
    QVERIFY(!exchangeButton->isEnabled());
    QVERIFY(!dualWatchButton->isEnabled());
    panel.setDualWatchPending(false);
    QVERIFY(dualWatchButton->isEnabled());
    panel.show();
    QApplication::processEvents();
    const QPoint mainPosition = mainButton->mapTo(&panel, QPoint(0, 0));
    const QPoint exchangePosition = exchangeButton->mapTo(&panel, QPoint(0, 0));
    const QPoint subPosition = subButton->mapTo(&panel, QPoint(0, 0));
    const QPoint dualWatchPosition = dualWatchButton->mapTo(&panel, QPoint(0, 0));
    QCOMPARE(mainPosition.y(), subPosition.y());
    QVERIFY(mainPosition.x() < subPosition.x());
    QCOMPARE(subPosition.x() - mainPosition.x() - mainButton->width(), 0);
    QCOMPARE(exchangePosition.x(), mainPosition.x());
    QCOMPARE(mainPosition.y() - dualWatchPosition.y() - dualWatchButton->height(), 0);
    QCOMPARE(exchangePosition.y() - mainPosition.y() - mainButton->height(), 0);
    QCOMPARE(dualWatchPosition.x(), exchangePosition.x());
    QCOMPARE(mainButton->height(), 30);
    QCOMPARE(subButton->height(), 30);
    QCOMPARE(exchangeButton->height(), 30);
    QCOMPARE(dualWatchButton->height(), 34);
    QCOMPARE(mainButton->width(), 54);
    QCOMPARE(exchangeButton->width(), 108);
    QCOMPARE(subButton->width(), 54);
    QCOMPARE(mainButton->width() + subButton->width(), 108);
}

void VfoDisplayTest::dialLockDisablesTuningButLeavesOperationalControlsEnabled()
{
    QWidget parent;
    VfoController controller(Vfo::Main, nullptr, nullptr, &parent);
    controller.setFrequencyHz(145500000ULL);
    controller.setTuningInteractionEnabled(false);

    auto* frequencyEdit = controller.display()->findChild<QLineEdit*>();
    auto* bandButton = controller.display()->findChild<QPushButton*>(QStringLiteral("vfoBandButton"));
    auto* modeButton = controller.display()->findChild<QPushButton*>(QStringLiteral("vfoModeButton"));
    auto* squelchButton = controller.display()->findChild<QPushButton*>(QStringLiteral("vfoSQLButton"));
    QVERIFY(frequencyEdit != nullptr);
    QVERIFY(bandButton != nullptr);
    QVERIFY(modeButton != nullptr);
    QVERIFY(squelchButton != nullptr);
    QVERIFY(!frequencyEdit->isEnabled());
    QVERIFY(!bandButton->isEnabled());
    QVERIFY(!modeButton->isEnabled());
    QVERIFY(squelchButton->isEnabled());

    VfoSelectionPanel panel;
    QPushButton pttButton(QStringLiteral("PTT"));
    panel.setPttButton(&pttButton);
    panel.setRadioReady(true);
    panel.setControlsEnabled(false);
    QVERIFY(pttButton.isEnabled());
    for (QPushButton* button : panel.findChildren<QPushButton*>())
    {
        if (button != &pttButton)
        {
            QVERIFY(!button->isEnabled());
        }
    }
}

QTEST_MAIN(VfoDisplayTest)
#include "VfoDisplayTest.moc"
