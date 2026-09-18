// cppcheck-suppress-file unusedFunction
#include "CivSequenceGate.h"

#include <QtTest>
#include <algorithm>
#include <random>

class CivSequenceGateTest : public QObject
{
    Q_OBJECT

  private slots:
    void deliversInOrder();
    void suppressesDuplicates();
    void deliversOutOfOrderWithoutBlocking();
    void handlesRollover();
    void boundsDuplicateHistory();
    void survivesSeededLossDuplicateAndReorderSoak();
    void resetStartsIndependentSession();
};

void CivSequenceGateTest::deliversInOrder()
{
    CivSequenceGate gate;
    QCOMPARE(gate.accept(10, QByteArrayLiteral("a")), CivSequenceGateResult(QByteArrayLiteral("a")));
    QCOMPARE(gate.accept(11, QByteArrayLiteral("b")), CivSequenceGateResult(QByteArrayLiteral("b")));
}

void CivSequenceGateTest::suppressesDuplicates()
{
    CivSequenceGate gate;
    QVERIFY(gate.accept(10, QByteArrayLiteral("a")).has_value());
    QVERIFY(!gate.accept(10, QByteArrayLiteral("a")).has_value());
    QCOMPARE(gate.diagnostics().duplicatesSuppressed, quint64(1));
}

void CivSequenceGateTest::deliversOutOfOrderWithoutBlocking()
{
    CivSequenceGate gate;
    QVERIFY(gate.accept(10, QByteArrayLiteral("a")).has_value());
    QCOMPARE(gate.accept(12, QByteArrayLiteral("c")), CivSequenceGateResult(QByteArrayLiteral("c")));
    QCOMPARE(gate.accept(11, QByteArrayLiteral("b")), CivSequenceGateResult(QByteArrayLiteral("b")));
    QCOMPARE(gate.diagnostics().reordered, quint64(1));
}

void CivSequenceGateTest::handlesRollover()
{
    CivSequenceGate gate;
    QVERIFY(gate.accept(0xffff, QByteArrayLiteral("a")).has_value());
    QVERIFY(gate.accept(0, QByteArrayLiteral("b")).has_value());
    QVERIFY(gate.accept(1, QByteArrayLiteral("c")).has_value());
}

void CivSequenceGateTest::boundsDuplicateHistory()
{
    CivSequenceGate gate;
    for (qsizetype sequence = 0; sequence <= CivSequenceGate::kRecentSequenceWindow; ++sequence)
    {
        QVERIFY(gate.accept(static_cast<quint16>(sequence), QByteArrayLiteral("data")).has_value());
    }
    QCOMPARE(gate.diagnostics().highWaterMark, CivSequenceGate::kRecentSequenceWindow);
    QVERIFY(gate.accept(0, QByteArrayLiteral("new rollover")).has_value());
}

void CivSequenceGateTest::survivesSeededLossDuplicateAndReorderSoak()
{
    CivSequenceGate gate;
    std::mt19937 random(0x9700);
    QSet<quint16> expectedSequences;
    QSet<quint16> deliveredSequences;
    quint64 injectedDuplicates = 0;

    struct Datagram
    {
        quint16 sequence{0};
        QByteArray payload;
    };

    constexpr int kDatagramCount = 20000;
    constexpr int kBatchSize = 8;
    // Twenty thousand source datagrams intentionally cross sequence rollover
    // exposure while repeatedly injecting loss, duplication, and reordering.
    for (int batchStart = 0; batchStart < kDatagramCount; batchStart += kBatchSize)
    {
        QVector<Datagram> arrivals;
        for (int offset = 0; offset < kBatchSize && batchStart + offset < kDatagramCount; ++offset)
        {
            const int logicalIndex = batchStart + offset;
            const quint16 sequence = static_cast<quint16>(logicalIndex);
            if (logicalIndex % 23 == 0)
            {
                continue;
            }

            const QByteArray payload = QByteArray::number(sequence);
            expectedSequences.insert(sequence);
            arrivals.append({sequence, payload});
            if (logicalIndex % 7 == 0)
            {
                arrivals.append({sequence, payload});
                ++injectedDuplicates;
            }
        }

        std::shuffle(arrivals.begin(), arrivals.end(), random);
        for (const Datagram& datagram : arrivals)
        {
            const CivSequenceGateResult result = gate.accept(datagram.sequence, datagram.payload);
            if (result)
            {
                const quint16 delivered = result->toUShort();
                QVERIFY2(!deliveredSequences.contains(delivered), "CI-V sequence was delivered more than once");
                deliveredSequences.insert(delivered);
            }
        }
    }

    QCOMPARE(deliveredSequences, expectedSequences);
    QCOMPARE(gate.diagnostics().delivered, quint64(expectedSequences.size()));
    QCOMPARE(gate.diagnostics().duplicatesSuppressed, injectedDuplicates);
    QVERIFY(gate.diagnostics().reordered > 0);
    QVERIFY(gate.diagnostics().highWaterMark <= CivSequenceGate::kRecentSequenceWindow);
}

void CivSequenceGateTest::resetStartsIndependentSession()
{
    CivSequenceGate gate;
    QVERIFY(gate.accept(42, QByteArrayLiteral("old-session")).has_value());
    QVERIFY(!gate.accept(42, QByteArrayLiteral("duplicate")).has_value());

    gate.reset();

    QCOMPARE(gate.diagnostics().delivered, quint64(0));
    QCOMPARE(gate.diagnostics().duplicatesSuppressed, quint64(0));
    QCOMPARE(gate.accept(42, QByteArrayLiteral("new-session")),
             CivSequenceGateResult(QByteArrayLiteral("new-session")));
}

QTEST_GUILESS_MAIN(CivSequenceGateTest)
#include "CivSequenceGateTest.moc"
