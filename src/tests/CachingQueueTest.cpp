// QtTest invokes private slots through the generated meta-object.
#include "CachingQueue.h"

#include <QSemaphore>
#include <QtTest>

#include <atomic>
#include <memory>
#include <mutex>

namespace
{
class CommandCollector
{
  public:
    void append(Funcs command)
    {
        std::lock_guard locker(m_mutex);
        m_commands.append(command);
    }

    int size() const
    {
        std::lock_guard locker(m_mutex);
        return m_commands.size();
    }

    QVector<Funcs> snapshot() const
    {
        std::lock_guard locker(m_mutex);
        return m_commands;
    }

  private:
    mutable std::mutex m_mutex;
    QVector<Funcs> m_commands;
};
} // namespace

struct UnsupportedCachePayload
{
    int value{0};
};
Q_DECLARE_METATYPE(UnsupportedCachePayload)

class CachingQueueTest : public QObject
{
    Q_OBJECT

  private slots:
    void init();
    void cleanupTestCase();
    void mapsPriorityNames();
    void sharedQueueHasDiagnosticName();
    void queueItemsHaveStableIdentity();
    void dispatchesImmediateCommandsInOrder();
    void receivesAndCachesAuthoritativeValues();
    void comparesRegisteredValueTypes();
    void deliversUnsupportedPayloadTypesConservatively();
    void resetClearsSessionState();
    void vfoBandReadDoesNotChangeRoutingState();
    void emitsCacheChangesWithoutHoldingMutex();
    void deliversValueArrivingDuringBatchEmission();
    void restartsAfterExplicitShutdown();
    void reportsQueueDiagnostics();
    void deduplicatesRepeatedCacheRefreshes();
    void cachePeekDoesNotRequestRefresh();
    void cachePeekReturnsReceiverSpecificValueWithoutRequestingRefresh();
    void boundsCommandQueue();
    void recurringWorkSurvivesImmediatePressure();
};

void CachingQueueTest::init()
{
    CachingQueue::getInstance()->resetSessionState();
}

void CachingQueueTest::cleanupTestCase()
{
    CachingQueue::shutdownInstance();
}

void CachingQueueTest::mapsPriorityNames()
{
    QCOMPARE(priorityValue(QStringLiteral("Immediate")), int(kPriorityImmediate));
    QCOMPARE(priorityValue(QStringLiteral("Medium High")), int(kPriorityMediumHigh));
    QCOMPARE(priorityValue(QStringLiteral("Lowest")), int(kPriorityLowest));
    QCOMPARE(priorityValue(QStringLiteral("invalid")), int(kPriorityNone));
}

void CachingQueueTest::sharedQueueHasDiagnosticName()
{
    QCOMPARE(CachingQueue::getInstance()->objectName(), QStringLiteral("CachingQueue()"));
}

void CachingQueueTest::queueItemsHaveStableIdentity()
{
    const QueueItem first(funcFreqGet, QVariant());
    const QueueItem copy(first);
    const QueueItem second(funcFreqGet, QVariant());

    QCOMPARE(copy.id, first.id);
    QVERIFY(second.id > first.id);
    QCOMPARE(copy, first);
}

void CachingQueueTest::dispatchesImmediateCommandsInOrder()
{
    CachingQueue* queue = CachingQueue::getInstance();
    const auto dispatched = std::make_shared<CommandCollector>();
    QObject receiver;
    connect(
        queue, &CachingQueue::haveCommand, &receiver,
        [dispatched](Funcs func, const QVariant&, uchar) { dispatched->append(func); }, Qt::DirectConnection);

    queue->add(kPriorityImmediate, funcFreqGet);
    queue->add(kPriorityImmediate, funcModeGet);

    QTRY_COMPARE(dispatched->size(), 2);
    const QVector<Funcs> commands = dispatched->snapshot();
    QCOMPARE(commands.at(0), funcFreqGet);
    QCOMPARE(commands.at(1), funcModeGet);
}

void CachingQueueTest::receivesAndCachesAuthoritativeValues()
{
    CachingQueue* queue = CachingQueue::getInstance();
    const auto deliveryCount = std::make_shared<std::atomic_int>(0);
    QObject receiver;
    connect(
        queue, &CachingQueue::sendValues, &receiver, [deliveryCount](const QVector<CacheItem>&)
        { deliveryCount->fetch_add(1, std::memory_order_release); }, Qt::DirectConnection);

    queue->receiveValue(funcRfGain, 123, 0);

    QTRY_COMPARE(deliveryCount->load(std::memory_order_acquire), 1);
    const CacheItem cached = queue->getCache(funcRfGain, 0);
    QCOMPARE(cached.command, funcRfGain);
    QCOMPARE(cached.value.toInt(), 123);
    QVERIFY(cached.reply.isValid());
}

void CachingQueueTest::deliversUnsupportedPayloadTypesConservatively()
{
    const QVariant payload = QVariant::fromValue(UnsupportedCachePayload{42});
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("Unsupported cache value.*")));
    QVERIFY(CachingQueue::cacheValuesDiffer(payload, payload));
}

void CachingQueueTest::comparesRegisteredValueTypes()
{
    QVERIFY(!CachingQueue::cacheValuesDiffer(123, 123));
    QVERIFY(CachingQueue::cacheValuesDiffer(123, 124));
    QVERIFY(!CachingQueue::cacheValuesDiffer(QStringLiteral("same"), QStringLiteral("same")));
    QVERIFY(CachingQueue::cacheValuesDiffer(QStringLiteral("before"), QStringLiteral("after")));
    QVERIFY(!CachingQueue::cacheValuesDiffer(QByteArray("same"), QByteArray("same")));
}

void CachingQueueTest::resetClearsSessionState()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->recordLocalRoutingState(funcVFOBandMS, true, 0);
    QCOMPARE(queue->getState().receiver, uchar(1));

    queue->resetSessionState();

    QCOMPARE(queue->getState().receiver, uchar(0));
    const CacheItem cached = queue->getCache(funcVFOBandMS, 0);
    QVERIFY(!cached.value.isValid());
}

void CachingQueueTest::vfoBandReadDoesNotChangeRoutingState()
{
    CachingQueue* queue = CachingQueue::getInstance();
    const auto dispatched = std::make_shared<CommandCollector>();
    QObject receiver;
    connect(
        queue, &CachingQueue::haveCommand, &receiver,
        [dispatched](Funcs func, const QVariant&, uchar) { dispatched->append(func); }, Qt::DirectConnection);
    queue->recordLocalRoutingState(funcVFOBandMS, true, 0);

    queue->add(kPriorityImmediate, funcVFOBandMS);

    QTRY_COMPARE(dispatched->size(), 1);
    QCOMPARE(queue->getState().receiver, uchar(1));
}

void CachingQueueTest::emitsCacheChangesWithoutHoldingMutex()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->receiveValue(funcRfGain, 100, 0);

    bool signalObserved = false;
    const QMetaObject::Connection connection = connect(
        queue, &CachingQueue::cacheUpdated, this,
        [queue, &signalObserved](const CacheItem&)
        {
            // A direct callback can safely re-enter the queue only when the
            // signal is emitted after updateCache releases its mutex.
            QCOMPARE(queue->getCache(funcRfGain, 0).value.toInt(), 101);
            signalObserved = true;
        },
        Qt::DirectConnection);

    queue->receiveValue(funcRfGain, 101, 0);
    disconnect(connection);
    QVERIFY(signalObserved);
}

void CachingQueueTest::deliversValueArrivingDuringBatchEmission()
{
    CachingQueue* queue = CachingQueue::getInstance();
    QSemaphore firstEmissionEntered;
    QSemaphore allowFirstEmissionToReturn;
    std::atomic_int emissionCount{0};

    const QMetaObject::Connection connection = connect(
        queue, &CachingQueue::sendValues, this,
        [&firstEmissionEntered, &allowFirstEmissionToReturn, &emissionCount](const QVector<CacheItem>&)
        {
            const int count = emissionCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (count == 1)
            {
                firstEmissionEntered.release();
                allowFirstEmissionToReturn.acquire();
            }
        },
        Qt::DirectConnection);

    queue->receiveValue(funcRfGain, 100, 0);
    QVERIFY(firstEmissionEntered.tryAcquire(1, 1000));

    // The worker is outside its condition-variable wait while the direct
    // receiver above holds the first emission. This notification is therefore
    // deliberately sent during the historical lost-wakeup window.
    queue->receiveValue(funcRfGain, 101, 0);
    allowFirstEmissionToReturn.release();

    QTRY_COMPARE(emissionCount.load(std::memory_order_acquire), 2);
    disconnect(connection);
}

void CachingQueueTest::restartsAfterExplicitShutdown()
{
    CachingQueue::shutdownInstance();
    CachingQueue* queue = CachingQueue::getInstance();
    const auto dispatched = std::make_shared<CommandCollector>();
    QObject receiver;
    connect(
        queue, &CachingQueue::haveCommand, &receiver,
        [dispatched](Funcs func, const QVariant&, uchar) { dispatched->append(func); }, Qt::DirectConnection);

    queue->add(kPriorityImmediate, funcFreqGet);

    QTRY_COMPARE(dispatched->size(), 1);
}

void CachingQueueTest::reportsQueueDiagnostics()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->add(kPriorityLowest, funcFreqGet, true);
    queue->add(kPriorityLowest, funcModeGet, true);

    QTRY_VERIFY(queue->diagnostics().highWaterMark >= 2);
    const CachingQueueDiagnostics diagnostics = queue->diagnostics();
    QVERIFY(diagnostics.depth >= 2);
    QCOMPARE(diagnostics.depthByPriority.value(kPriorityLowest), qsizetype(2));
    QCOMPARE(diagnostics.depthByPriority.value(kPriorityImmediate), diagnostics.depth - 2);
    QVERIFY(diagnostics.oldestItemAgeMs >= 0);
}

void CachingQueueTest::deduplicatesRepeatedCacheRefreshes()
{
    // Keep this as a five-digit request burst so the test exercises sustained
    // duplicate pressure, not only a handful of coincident callers.
    constexpr int kRefreshRequestCount = 10000;
    CachingQueue* queue = CachingQueue::getInstance();
    const quint64 dispatchedBefore = queue->diagnostics().dispatched;

    for (int i = 0; i < kRefreshRequestCount; ++i)
    {
        queue->getCache(funcRfGain, 0);
    }

    QTRY_COMPARE(queue->diagnostics().dispatched, dispatchedBefore + 1);
    QTest::qWait(100);
    QCOMPARE(queue->diagnostics().dispatched, dispatchedBefore + 1);
}

void CachingQueueTest::cachePeekDoesNotRequestRefresh()
{
    CachingQueue* queue = CachingQueue::getInstance();
    const quint64 dispatchedBefore = queue->diagnostics().dispatched;

    const CacheItem cached = queue->peekCache(funcModeGet, 0);
    QVERIFY(!cached.value.isValid());
    QTest::qWait(100);
    QCOMPARE(queue->diagnostics().dispatched, dispatchedBefore);
}

void CachingQueueTest::cachePeekReturnsReceiverSpecificValueWithoutRequestingRefresh()
{
    CachingQueue* queue = CachingQueue::getInstance();
    queue->receiveValue(funcModeGet, QStringLiteral("FM"), 1);
    const quint64 dispatchedBefore = queue->diagnostics().dispatched;

    QCOMPARE(queue->peekCache(funcModeGet, 1).value.toString(), QStringLiteral("FM"));
    QVERIFY(!queue->peekCache(funcModeGet, 0).value.isValid());
    QTest::qWait(100);
    QCOMPARE(queue->diagnostics().dispatched, dispatchedBefore);
}

void CachingQueueTest::boundsCommandQueue()
{
    CachingQueue* queue = CachingQueue::getInstance();
    for (int i = 0; i < 700; ++i)
    {
        queue->add(kPriorityLowest, QueueItem(funcRfGain, i, false, 0));
    }

    const CachingQueueDiagnostics diagnostics = queue->diagnostics();
    QVERIFY(diagnostics.depth <= 512);
    QVERIFY(diagnostics.droppedForCapacity > 0);
}

void CachingQueueTest::recurringWorkSurvivesImmediatePressure()
{
    CachingQueue* queue = CachingQueue::getInstance();
    const auto dispatched = std::make_shared<CommandCollector>();
    QObject receiver;
    connect(
        queue, &CachingQueue::haveCommand, &receiver,
        [dispatched](Funcs func, const QVariant&, uchar) { dispatched->append(func); }, Qt::DirectConnection);

    {
        std::lock_guard locker(queue->mutex);
        queue->queue.insert(kPriorityHighest, QueueItem(funcModeGet, true, 0));
        queue->queue.insert(kPriorityLowest, QueueItem(funcTransceiverId, true, 0));
        for (int i = 0; i < 40; ++i)
        {
            queue->queue.insert(kPriorityImmediate, QueueItem(funcRfGain, i, false, 0));
        }
        queue->m_queueWakeRequested = true;
    }
    queue->waiting.notify_one();

    QTRY_VERIFY(dispatched->size() >= 18);
    const QVector<Funcs> commands = dispatched->snapshot();
    int highestIndex = -1;
    int lowestIndex = -1;
    for (int i = 0; i < commands.size(); ++i)
    {
        if (commands.at(i) == funcModeGet && highestIndex < 0)
        {
            highestIndex = i;
        }
        if (commands.at(i) == funcTransceiverId && lowestIndex < 0)
        {
            lowestIndex = i;
        }
    }
    QVERIFY(highestIndex >= 0);
    QVERIFY(lowestIndex >= 0);
    QVERIFY(highestIndex <= int(CachingQueue::kMaximumImmediateBurst));
    QVERIFY(lowestIndex <= int(CachingQueue::kMaximumImmediateBurst * 2 + 1));
}

QTEST_GUILESS_MAIN(CachingQueueTest)
#include "CachingQueueTest.moc"
