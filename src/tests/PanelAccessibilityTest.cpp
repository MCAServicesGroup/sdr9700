#include "DtmfDialog.h"
#include "MetersDialog.h"

#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <memory>

class PanelAccessibilityTest : public QObject
{
    Q_OBJECT

  private slots:
    void dtmfControlsHaveUsableInitialState();
    void utilityDialogsAreResizableAndFrameless();
    void invalidTransmitMetersCanBeCleared();
    void metersSurviveRepeatedUpdatesAndDestruction();
};

void PanelAccessibilityTest::dtmfControlsHaveUsableInitialState()
{
    DtmfDialog dialog;
    auto* display = dialog.findChild<QLineEdit*>();
    QVERIFY(display != nullptr);
    QCOMPARE(display->maxLength(), 16);
    QVERIFY(display->validator() != nullptr);
    QVERIFY(display->isEnabled());
    const auto buttons = dialog.findChildren<QPushButton*>();
    QVERIFY(buttons.size() >= 19);
    for (const auto* button : buttons)
    {
        QVERIFY(!button->accessibleName().isEmpty() || !button->accessibleDescription().isEmpty());
    }
}

void PanelAccessibilityTest::invalidTransmitMetersCanBeCleared()
{
    MetersDialog meters;
    meters.setPowerMeter(38.0);
    meters.setAlc(1.0);
    meters.setCompressionMeter(4.0);
    meters.setVoltageMeter(13.8);
    meters.setCurrentMeter(8.2);

    meters.clearPowerMeter();
    meters.clearAlc();
    meters.clearCompressionMeter();
    meters.clearVoltageMeter();
    meters.clearCurrentMeter();

    QStringList labelTexts;
    for (const auto* label : meters.findChildren<QLabel*>())
    {
        labelTexts.append(label->text());
    }
    QVERIFY(!labelTexts.contains(QStringLiteral("38.0 W")));
    QVERIFY(!labelTexts.contains(QStringLiteral("1.00")));
    QVERIFY(!labelTexts.contains(QStringLiteral("4.0 dB")));
    QVERIFY(!labelTexts.contains(QStringLiteral("13.8 V")));
    QVERIFY(!labelTexts.contains(QStringLiteral("8.2 A")));
    QVERIFY(labelTexts.contains(QStringLiteral("-- W")));
    QVERIFY(labelTexts.contains(QStringLiteral("-- dB")));
    QVERIFY(labelTexts.contains(QStringLiteral("-- V")));
    QVERIFY(labelTexts.contains(QStringLiteral("-- A")));
}

void PanelAccessibilityTest::utilityDialogsAreResizableAndFrameless()
{
    DtmfDialog dtmf;
    MetersDialog meters;
    for (QDialog* dialog : {static_cast<QDialog*>(&dtmf), static_cast<QDialog*>(&meters)})
    {
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(dialog->maximumWidth() > dialog->minimumWidth());
        QVERIFY(dialog->maximumHeight() > dialog->minimumHeight());
        auto* closeButton = dialog->findChild<QPushButton*>(QString(), Qt::FindChildrenRecursively);
        QVERIFY(closeButton != nullptr);
    }

    QVERIFY(meters.findChild<QWidget*>(QStringLiteral("dialogFooterSeparator")) == nullptr);
    QVERIFY(meters.findChild<QPushButton*>(QStringLiteral("metersCloseButton")) == nullptr);
    QVERIFY(dtmf.findChild<QWidget*>(QStringLiteral("dialogFooterSeparator")) == nullptr);
    QVERIFY(dtmf.findChild<QWidget*>(QStringLiteral("dialogButtonBox")) == nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("receiveMeters")) != nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("transmitMeters")) != nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("audioMeters")) != nullptr);
    QVERIFY(meters.findChild<QGroupBox*>(QStringLiteral("radioMeters")) != nullptr);
    QStringList groupTitles;
    for (const auto* group : meters.findChildren<QGroupBox*>())
    {
        groupTitles.append(group->title());
    }
    QCOMPARE(groupTitles, QStringList({QStringLiteral("Audio"), QStringLiteral("Radio"), QStringLiteral("Receive"),
                                       QStringLiteral("Transmit")}));
    QStringList labelTexts;
    for (const auto* label : meters.findChildren<QLabel*>())
    {
        labelTexts.append(label->text());
    }
    QVERIFY(labelTexts.contains(QStringLiteral("Audio Average")));
    QVERIFY(labelTexts.contains(QStringLiteral("Audio Peak")));
    QVERIFY(!labelTexts.contains(QStringLiteral("Microphone Average")));
    QVERIFY(!labelTexts.contains(QStringLiteral("Microphone Peak")));
}

void PanelAccessibilityTest::metersSurviveRepeatedUpdatesAndDestruction()
{
    for (int iteration = 0; iteration < 20; ++iteration)
    {
        auto meters = std::make_unique<MetersDialog>();
        for (int level = 0; level < 100; ++level)
        {
            // Exercise both repeated values, which must not rebuild the style
            // sheet, and threshold transitions that legitimately change it.
            const sdr9700::audio::TxAudioMeterState state =
                level < 25   ? sdr9700::audio::TxAudioMeterState::NoActivity
                : level < 50 ? sdr9700::audio::TxAudioMeterState::RecommendedHeadroom
                : level < 75 ? sdr9700::audio::TxAudioMeterState::NearFullScale
                             : sdr9700::audio::TxAudioMeterState::FullScaleDetected;
            const double levelDb = -60.0 + level * 0.6;
            const quint32 clipped = state == sdr9700::audio::TxAudioMeterState::FullScaleDetected ? quint32(level) : 0U;
            meters->setTransmitAudioMeter(state, levelDb, levelDb, clipped);
            meters->setTransmitAudioMeter(state, levelDb, levelDb, clipped);
        }
    }
}

QTEST_MAIN(PanelAccessibilityTest)

#include "PanelAccessibilityTest.moc"
