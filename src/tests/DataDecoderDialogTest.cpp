// QtTest invokes private slots through the generated meta-object.
#include "DataDecoderDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QtTest>

class DataDecoderDialogTest : public QObject
{
    Q_OBJECT

  private slots:
    void usesPortableTableLayout();
    void decodesAudioOnlyWhileVisible();
};

void DataDecoderDialogTest::usesPortableTableLayout()
{
    DataDecoderDialog dialog;
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("ax25PacketTable"));
    auto* rawPacket = dialog.findChild<QPlainTextEdit*>(QStringLiteral("dataDecoderPacketDetails"));
    QVERIFY(table != nullptr);
    QVERIFY(rawPacket != nullptr);
    QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("dialogFooterSeparator")) == nullptr);
    auto* actionBox = dialog.findChild<QDialogButtonBox*>(QStringLiteral("dataDecoderActionBox"));
    QVERIFY(actionBox != nullptr);
    QCOMPARE(actionBox->buttons().size(), 3);
    auto* detailsGroup = qobject_cast<QGroupBox*>(rawPacket->parentWidget());
    QVERIFY(detailsGroup != nullptr);
    QCOMPARE(detailsGroup->title(), QStringLiteral("Packet Details"));
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->horizontalHeaderItem(3)->text(), QStringLiteral("Payload"));
    QCOMPARE(table->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);

    Ax25Frame frame;
    frame.receivedAt = QDateTime::fromString(QStringLiteral("2026-08-01T10:00:00.000"), Qt::ISODateWithMs);
    frame.protocol = QStringLiteral("AX.25 (1200)");
    frame.source = QStringLiteral("N0CALL-1");
    frame.destination = QStringLiteral("APRS");
    frame.path = QStringLiteral("WIDE1-1*,WIDE2-2");
    frame.payload = QStringLiteral("Test payload");
    QVERIFY(QMetaObject::invokeMethod(&dialog, "appendFrames", Qt::DirectConnection,
                                      Q_ARG(QVector<Ax25Frame>, QVector<Ax25Frame>{frame})));
    QCOMPARE(table->rowCount(), 1);
    QVERIFY(table->cellWidget(0, 2) == nullptr);
    table->setCurrentCell(0, 0);
    QVERIFY(rawPacket->toPlainText().contains(QStringLiteral("WIDE1-1*,WIDE2-2")));
    QVERIFY(rawPacket->toPlainText().endsWith(QStringLiteral(":Test payload")));
}

void DataDecoderDialogTest::decodesAudioOnlyWhileVisible()
{
    DataDecoderDialog dialog;
    QSignalSpy audioSpy(&dialog, &DataDecoderDialog::audioReceived);
    QSignalSpy resetSpy(&dialog, &DataDecoderDialog::resetDecoder);
    const QByteArray pcm(1920, '\0');

    dialog.processAudio(pcm, 48000, 2, 0);
    QCOMPARE(audioSpy.count(), 0);

    dialog.show();
    QCoreApplication::processEvents();
    QVERIFY(dialog.isVisible());
    dialog.processAudio(pcm, 48000, 2, 0);
    QCOMPARE(audioSpy.count(), 1);

    dialog.hide();
    QCoreApplication::processEvents();
    QCOMPARE(resetSpy.count(), 1);
    dialog.processAudio(pcm, 48000, 2, 0);
    QCOMPARE(audioSpy.count(), 1);
}

QTEST_MAIN(DataDecoderDialogTest)
#include "DataDecoderDialogTest.moc"
