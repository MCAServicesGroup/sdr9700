#include "LogCategories.h"
#include "CachingQueue.h"

#include <QMetaType>
#include <QThread>

#include <algorithm>
#include <array>

namespace
{
const QMap<QString, int> kPriorityMap = {{"None", 0},        {"Immediate", 1},   {"Highest", 2},
                                         {"High", 3},        {"Medium High", 5}, {"Medium", 7},
                                         {"Medium Low", 11}, {"Low", 19},        {"Lowest", 23}};
constexpr std::array<QueuePriority, 7> kFairPriorities = {kPriorityHighest, kPriorityHigh,      kPriorityMediumHigh,
                                                          kPriorityMedium,  kPriorityMediumLow, kPriorityLow,
                                                          kPriorityLowest};

} // namespace

int priorityValue(const QString& name)
{
    return kPriorityMap.value(name, 0);
}

CachingQueue* CachingQueue::instance{};
std::mutex CachingQueue::instanceMutex;

CachingQueue* CachingQueue::getInstance()
{
    std::lock_guard locker(instanceMutex);
    if (instance == nullptr)
    {
        qRegisterMetaType<QVector<CacheItem>>("QVector<CacheItem>");
        instance = new CachingQueue();
        instance->setObjectName(QStringLiteral("CachingQueue()"));
        instance->startWorker();
        qDebug(logRadio()).noquote() << "Created shared application CachingQueue";
    }
    return instance;
}

CachingQueue::~CachingQueue()
{
    stopWorker();
    std::lock_guard locker(instanceMutex);
    if (instance == this)
    {
        instance = nullptr;
    }
    qInfo(logRadio()).noquote() << "Destroying caching queue";
}

void CachingQueue::shutdownInstance()
{
    CachingQueue* queue = nullptr;
    {
        std::lock_guard locker(instanceMutex);
        queue = instance;
        instance = nullptr;
    }

    if (queue == nullptr)
    {
        return;
    }

    delete queue;
}

void CachingQueue::startWorker()
{
    aborted.store(false, std::memory_order_release);
    m_worker = std::thread([this]() { run(); });
}

void CachingQueue::stopWorker()
{
    aborted.store(true, std::memory_order_release);
    {
        std::lock_guard locker(mutex);
        waiting.notify_all();
    }
    if (m_worker.joinable())
    {
        Q_ASSERT(m_worker.get_id() != std::this_thread::get_id());
        m_worker.join();
    }
}

void CachingQueue::run()
{
    qInfo(logRadio()).noquote().nospace()
        << "Starting caching queue handler thread threadId=" << QThread::currentThreadId();

    std::unique_lock locker(mutex);
    QDeadlineTimer deadline(queueInterval);

    quint64 counter = kPriorityImmediate;
    quint64 immediateDispatchStreak = 0;
    qsizetype fairnessCursor = 0;

    while (!aborted.load(std::memory_order_acquire))
    {
        // With no queued commands, wait indefinitely for a cache update or a
        // new command. When commands exist, wake on the normal queue
        // interval so recurring polls keep their pacing.
        bool woke = true;
        if (queue.isEmpty())
        {
            // A value can arrive while this worker has dropped the mutex to
            // emit the preceding batch. condition_variable notifications are
            // not retained when no thread is waiting, so always inspect the
            // protected state before sleeping. Without this predicate, the
            // final radio reply in a quiet period can remain stranded until a
            // later value or command happens to wake the worker.
            waiting.wait(locker, [this]()
                         { return aborted.load(std::memory_order_acquire) || !items.isEmpty() || !queue.isEmpty(); });
        }
        else
        {
            woke = waiting.wait_for(locker, std::chrono::milliseconds(std::max<qint64>(0, deadline.remainingTime()))) ==
                   std::cv_status::no_timeout;
        }
        if (aborted.load(std::memory_order_acquire))
        {
            break;
        }

        QQueue<CacheItem> pendingItems;
        pendingItems.swap(items);

        const bool commandWakeRequested = m_queueWakeRequested;
        m_queueWakeRequested = false;

        QueueItem item;
        bool haveCommandToEmit = false;
        std::optional<CacheItem> changedCacheItem;
        const bool commandDispatchDue = !queue.isEmpty() && (!woke || commandWakeRequested);
        if (commandDispatchDue)
        {
            QueuePriority prio = kPriorityImmediate;
            const bool lowerPriorityWorkPending = queue.upperBound(kPriorityImmediate) != queue.cend();

            // Immediate work is latency-sensitive, but an unlimited stream of
            // it must not permanently starve recurring radio-state polls.
            if (queue.contains(kPriorityImmediate) && lowerPriorityWorkPending &&
                immediateDispatchStreak >= kMaximumImmediateBurst)
            {
                for (qsizetype offset = 0; offset < qsizetype(kFairPriorities.size()); ++offset)
                {
                    const qsizetype index = (fairnessCursor + offset) % qsizetype(kFairPriorities.size());
                    const QueuePriority candidate = kFairPriorities.at(index);
                    if (queue.contains(candidate))
                    {
                        prio = candidate;
                        fairnessCursor = (index + 1) % qsizetype(kFairPriorities.size());
                        break;
                    }
                }
            }

            // If no immediate commands are queued, rotate through lower priorities.
            if (prio == kPriorityImmediate && !queue.contains(prio))
            {
                if (counter % kPriorityHighest == 0)
                {
                    prio = kPriorityHighest;
                }
                else if (counter % kPriorityHigh == 0)
                {
                    prio = kPriorityHigh;
                }
                else if (counter % kPriorityMediumHigh == 0)
                {
                    prio = kPriorityMediumHigh;
                }
                else if (counter % kPriorityMedium == 0)
                {
                    prio = kPriorityMedium;
                }
                else if (counter % kPriorityMediumLow == 0)
                {
                    prio = kPriorityMediumLow;
                }
                else if (counter % kPriorityLow == 0)
                {
                    prio = kPriorityLow;
                }
                else if (counter % kPriorityLowest == 0)
                {
                    prio = kPriorityLowest;
                    counter = kPriorityImmediate;
                }
            }
            counter++;

            auto begin = queue.lowerBound(prio);
            auto end = queue.upperBound(prio);
            auto it = end;
            for (auto candidate = begin; candidate != end; ++candidate)
            {
                if (it == end || candidate.value().id < it.value().id)
                {
                    it = candidate;
                }
            }
            if (it != end)
            {
                item = it.value();
                queue.erase(it);
                // If this is a recurring command, add it back into the queue.
                if (item.recurring && prio != kPriorityImmediate)
                {
                    QueueItem recurringItem = item;
                    recurringItem.id = nextQueueItemId();
                    recurringItem.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
                    queue.insert(prio, recurringItem);
                }

                if (!item.recurring)
                {
                    changedCacheItem = updateCache(false, item.command, item.param, item.receiver);
                }
                haveCommandToEmit = true;
                ++m_dispatchedCommands;
                immediateDispatchStreak = prio == kPriorityImmediate ? immediateDispatchStreak + 1 : 0;
            }

            // Cache updates wake this thread so UI state can be delivered
            // promptly, but they must not move the next command
            // deadline. During a busy memory sync or radio status burst,
            // resetting the deadline on every non-command wake can starve
            // recurring polls indefinitely.
            deadline.setRemainingTime(queueInterval);
        }
        if (!pendingItems.isEmpty() || haveCommandToEmit || changedCacheItem.has_value())
        {
            locker.unlock();
            if (changedCacheItem.has_value())
            {
                emit cacheUpdated(*changedCacheItem);
            }
            if (!pendingItems.isEmpty())
            {
                QVector<CacheItem> batch;
                batch.reserve(pendingItems.size());
                while (!pendingItems.isEmpty())
                {
                    batch.append(pendingItems.dequeue());
                }
                emit sendValues(batch);
            }
            if (haveCommandToEmit)
            {
                emit haveCommand(item.command, item.param, item.receiver);
            }
            locker.lock();
        }
    }
}

Funcs CachingQueue::checkCommandAvailable(Funcs cmd) const
{
    if (radioCaps != nullptr && cmd != funcNone && cmd != funcSelectVFO && !radioCaps->commands.contains(cmd))
    {
        return funcNone;
    }
    return cmd;
}

void CachingQueue::add(QueuePriority prio, Funcs func, bool recurring, uchar receiver)
{
    QueueItem q(func, recurring, receiver);
    add(prio, q, false);
}

void CachingQueue::addUnique(QueuePriority prio, Funcs func, bool recurring, uchar receiver)
{
    QueueItem q(func, recurring, receiver);
    add(prio, q, true);
}

void CachingQueue::add(QueuePriority prio, QueueItem item, bool unique)
{
    std::unique_lock locker(mutex);

    if (queueInterval == -1)
    {
        return;
    }

    item.command = checkCommandAvailable(item.command);
    if (item.command == funcNone)
    {
        return;
    }

    // Do not add a duplicate recurring command of the same priority.
    if (!item.recurring || isRecurring(item.command, item.receiver) != prio)
    {
        if (item.recurring && prio == kPriorityImmediate)
        {
            qWarning(logRadio()).noquote()
                << "CachingQueue::add() Warning, cannot add recurring command with immediate priority!"
                << funcString[item.command];
        }
        else
        {
            if (unique)
            {
                int count = queue.remove(prio, item);
                if (count > 0)
                {
                    qDebug(logRadio()).noquote() << "CachingQueue::add() deleted" << count << "entries from queue for"
                                                 << funcString[item.command] << "on receiver" << item.receiver;
                }
            }

            // Don't immediately request funcTransceiverId; wait for the queue to run.
            if (item.recurring && item.command != funcTransceiverId)
            {
                QueueItem it = item;
                it.recurring = false;
                it.param.clear();
                queue.insert(queue.cend(), kPriorityImmediate, it);
            }
            queue.insert(prio, item);
            enforceQueueLimit();
            m_queueHighWaterMark = std::max(m_queueHighWaterMark, queue.size());
            m_queueWakeRequested = true;
            locker.unlock();
            waiting.notify_one();
        }
    }
}

void CachingQueue::enforceQueueLimit()
{
    while (queue.size() > kMaximumQueueDepth)
    {
        auto victim = queue.end();
        for (auto candidate = queue.begin(); candidate != queue.end(); ++candidate)
        {
            if (candidate->recurring)
            {
                continue;
            }
            if (victim == queue.end() || candidate.key() > victim.key() ||
                (candidate.key() == victim.key() && candidate->id < victim->id))
            {
                victim = candidate;
            }
        }
        if (victim == queue.end())
        {
            victim = std::min_element(queue.begin(), queue.end(),
                                      [](const QueueItem& lhs, const QueueItem& rhs) { return lhs.id < rhs.id; });
        }

        const bool shouldLog = m_droppedForCapacity == 0 || (m_droppedForCapacity & (m_droppedForCapacity - 1)) == 0;
        if (shouldLog)
        {
            qCritical(logRadio()).noquote()
                << "CachingQueue capacity exceeded; dropped=" << (m_droppedForCapacity + 1)
                << "command=" << funcString[victim->command] << "receiver=" << victim->receiver
                << "priority=" << victim.key() << "depth=" << queue.size();
        }
        queue.erase(victim);
        ++m_droppedForCapacity;
    }
}

quint64 CachingQueue::cacheRefreshKey(Funcs func, uchar receiver)
{
    return (quint64(static_cast<quint32>(func)) << 8U) | receiver;
}

VfoCommandType CachingQueue::getVfoCommand(vfo_t vfo, uchar rx, bool set)
{
    VfoCommandType cmd;
    cmd.receiver = rx;
    cmd.vfo = vfo;
    std::lock_guard locker(mutex);
    if (radioCaps != nullptr)
    {
        if (set)
        {
            cmd.modeFunc = ((radioCaps->commands.contains(funcMode)) ? funcMode : funcModeSet);
            cmd.freqFunc = ((radioCaps->commands.contains(funcFreq)) ? funcFreq : funcFreqSet);
        }
        else
        {
            cmd.modeFunc = ((radioCaps->commands.contains(funcMode)) ? funcMode : funcModeGet);
            cmd.freqFunc = ((radioCaps->commands.contains(funcFreq)) ? funcFreq : funcFreqGet);
        }

        if (!radioCaps->hasCommand29)
        {
            // When CI-V command 29h is unavailable, selected/unselected commands address VFO A/B.
            if (radioState.vfoMode == vfoModeType_t::vfoModeVfo && (radioState.receiver == 0 && rx == 0))
            {
                if (vfo == vfoA)
                {
                    cmd.modeFunc = ((radioCaps->commands.contains(funcSelectedMode)) ? funcSelectedMode : cmd.modeFunc);
                    cmd.freqFunc = ((radioCaps->commands.contains(funcSelectedFreq)) ? funcSelectedFreq : cmd.freqFunc);
                }
                else if (vfo == vfoB)
                {
                    cmd.modeFunc =
                        ((radioCaps->commands.contains(funcUnselectedMode)) ? funcUnselectedMode : cmd.modeFunc);
                    cmd.freqFunc =
                        ((radioCaps->commands.contains(funcUnselectedFreq)) ? funcUnselectedFreq : cmd.freqFunc);
                }
            }
            else if (rx == radioState.receiver)
            {
                cmd.receiver = 0;
            }
            else
            {
                cmd.modeFunc = funcNone;
                cmd.freqFunc = funcNone;
                cmd.receiver = 0xff;
            }
        }
    }
    return cmd;
}

QueuePriority CachingQueue::del(Funcs func, uchar receiver)
{
    QueuePriority prio = kPriorityNone;
    if (func != funcNone)
    {
        std::lock_guard locker(mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func, receiver](const QueueItem& c)
                               { return (c.command == func && c.receiver == receiver && c.recurring == true); });
        if (it != queue.end())
        {
            prio = it.key();
            int count = queue.remove(it.key(), it.value());
            if (count > 0)
            {
                qDebug(logRadio()).noquote() << "CachingQueue()::del" << count << "entries from queue for"
                                             << funcString[func] << "on receiver" << receiver;
            }
        }
    }
    return prio;
}

QueuePriority CachingQueue::isRecurring(Funcs func, uchar receiver)
{
    // Caller must already hold mutex when a consistent queue snapshot is required.
    auto rec = std::find_if(queue.begin(), queue.end(), [func, receiver](const QueueItem& c)
                            { return (c.command == func && c.receiver == receiver && c.recurring); });
    if (rec != queue.end())
    {
        return rec.key();
    }
    return kPriorityNone;
}

void CachingQueue::clear()
{
    std::lock_guard locker(mutex);
    queue.clear();
}

void CachingQueue::resetSessionState()
{
    {
        std::lock_guard locker(mutex);
        radioCaps = nullptr;
        queue.clear();
        cache.clear();
        items.clear();
        m_cacheRefreshRequests.clear();
        radioState = RadioStateType();
        m_queueHighWaterMark = 0;
        m_dispatchedCommands = 0;
        m_droppedForCapacity = 0;
    }

    waiting.notify_all();
}

CachingQueueDiagnostics CachingQueue::diagnostics()
{
    std::lock_guard locker(mutex);
    CachingQueueDiagnostics result;
    result.depth = queue.size();
    result.highWaterMark = m_queueHighWaterMark;
    result.dispatched = m_dispatchedCommands;
    result.droppedForCapacity = m_droppedForCapacity;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 oldestEnqueuedAtMs = nowMs;
    for (auto item = queue.cbegin(); item != queue.cend(); ++item)
    {
        ++result.depthByPriority[item.key()];
        oldestEnqueuedAtMs = std::min(oldestEnqueuedAtMs, item->enqueuedAtMs);
    }
    if (!queue.isEmpty())
    {
        result.oldestItemAgeMs = std::max<qint64>(0, nowMs - oldestEnqueuedAtMs);
    }
    return result;
}

void CachingQueue::setRadioCaps(radioCapabilities* caps)
{
    std::lock_guard locker(mutex);
    radioCaps = caps;
}

void CachingQueue::receiveValue(Funcs func, QVariant value, uchar receiver)
{
    std::optional<CacheItem> changedCacheItem;
    {
        // Parsed CI-V replies are authoritative state. Wait for the queue lock
        // instead of dropping an update during heavy polling or memory sync.
        std::lock_guard locker(mutex);
        CacheItem c = CacheItem(func, value, receiver);
        items.enqueue(c);
        changedCacheItem = updateCache(true, func, value, receiver);
    }
    if (changedCacheItem.has_value())
    {
        emit cacheUpdated(*changedCacheItem);
    }
    waiting.notify_one();
}

std::optional<CacheItem> CachingQueue::updateCache(bool reply, QueueItem item)
{
    // Every caller must hold `mutex` for this entire operation. Besides the
    // cache map, this function mutates radioState and the outstanding-refresh
    // key set; observing only a subset of those changes would let subsequent
    // command routing use a state snapshot that never existed atomically.

    if (reply)
    {
        m_cacheRefreshRequests.remove(cacheRefreshKey(item.command, item.receiver));
        // Track radio state changes that affect later command routing.
        if (item.command == funcSatelliteMode && item.param.value<bool>())
        {
            radioState.vfoMode = vfoModeType_t::vfoModeSat;
        }
        if (item.command == funcMemoryMode && (!item.param.isValid() || item.param.value<bool>()))
        {
            radioState.vfoMode = vfoModeType_t::vfoModeMem;
        }
        if (item.command == funcVFOModeSelect || (item.command == funcVFOMode && item.param.value<bool>()))
        {
            radioState.vfoMode = vfoModeType_t::vfoModeVfo;
        }
        if (item.command == funcVFOBandMS)
        {
            radioState.receiver = item.param.toBool() ? 1 : 0;
        }
    }
    else
    {
        // Some VFO selection commands do not produce a useful confirmation.
        if (item.command == funcSelectVFO)
        {
            radioState.vfo = item.param.value<vfo_t>();
            if (radioState.vfo == vfoMain)
            {
                radioState.receiver = 0;
            }
            else if (radioState.vfo == vfoSub && radioCaps != nullptr && radioCaps->numReceiver > 1)
            {
                radioState.receiver = 1;
            }
        }
        else if (item.command == funcVFOASelect)
        {
            radioState.vfo = vfo_t::vfoA;
        }
        else if (item.command == funcVFOBSelect && radioCaps != nullptr && radioCaps->numVFO > 1)
        {
            radioState.vfo = vfo_t::vfoB;
        }
        else if (item.command == funcVFOMainSelect)
        {
            radioState.vfo = vfo_t::vfoMain;
            radioState.receiver = 0;
        }
        else if (item.command == funcVFOSubSelect && radioCaps != nullptr && radioCaps->numReceiver > 1)
        {
            radioState.vfo = vfo_t::vfoSub;
            radioState.receiver = 1;
        }
        else if (item.command == funcVFOBandMS && item.param.isValid())
        {
            radioState.receiver = item.param.toBool() ? 1 : 0;
        }
    }

    auto cv = cache.find(item.command);
    while (cv != cache.end() && cv->command == item.command)
    {
        if (cv->receiver == item.receiver)
        {
            if (reply)
            {
                cv->reply = QDateTime::currentDateTimeUtc();
            }
            else
            {
                cv->req = QDateTime::currentDateTimeUtc();
            }
            if (cacheValuesDiffer(item.param, cv.value().value))
            {
                cv->value.clear();
                cv->value.setValue(item.param);

                return cv.value();
            }
            return std::nullopt;
        }
        ++cv;
    }

    CacheItem c;
    c.command = item.command;
    c.receiver = item.receiver;

    if (reply)
    {
        c.reply = QDateTime::currentDateTimeUtc();
    }
    else
    {
        c.req = QDateTime::currentDateTimeUtc();
    }
    if (item.param.isValid())
    {
        c.value.setValue(item.param);
    }

    cache.insert(item.command, c);
    return std::nullopt;
}

std::optional<CacheItem> CachingQueue::updateCache(bool reply, Funcs func, QVariant value, uchar receiver)
{
    QueueItem q(func, value, false, receiver);
    return updateCache(reply, q);
}

void CachingQueue::recordLocalRoutingState(Funcs func, QVariant value, uchar receiver)
{
    std::optional<CacheItem> changedCacheItem;
    {
        std::lock_guard locker(mutex);
        changedCacheItem = updateCache(false, QueueItem(func, value, false, receiver));
    }
    if (changedCacheItem.has_value())
    {
        emit cacheUpdated(*changedCacheItem);
    }
}

CacheItem CachingQueue::getCache(Funcs func, uchar receiver)
{
    CacheItem ret;
    bool requestRefresh = false;
    qint64 lastReplyAgeMs = -1;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (func != funcNone)
    {
        std::lock_guard locker(mutex);
        ret = findCacheLocked(func, receiver);

        const bool stale = func != funcPowerControl && func != funcSelectVFO &&
                           (!ret.value.isValid() || ret.command == funcSWRMeter || ret.command == funcALCMeter ||
                            ret.reply.addSecs(QRandomGenerator::global()->bounded(5, 20)) <= now);
        if (stale)
        {
            const quint64 key = cacheRefreshKey(func, receiver);
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const auto pending = m_cacheRefreshRequests.constFind(key);
            if (pending == m_cacheRefreshRequests.cend() || nowMs - pending.value() >= 5000)
            {
                m_cacheRefreshRequests.insert(key, nowMs);
                requestRefresh = true;
                if (ret.reply.isValid())
                {
                    lastReplyAgeMs = ret.reply.msecsTo(now);
                }
            }
        }
    }
    // Re-request stale values after a small randomized window so periodic
    // refresh traffic does not synchronize into bursts. Keep this below
    // kPriorityHighest; raising it makes the S-meter visibly lag while command
    // intensive workflows are active.
    if (requestRefresh)
    {
        if (lastReplyAgeMs >= 0)
        {
            qDebug(logRadio()).noquote().nospace()
                << "CacheRefresh::Stale func=" << funcString[func] << " lastReplyAgeMs=" << lastReplyAgeMs
                << " receiver=" << int(receiver);
        }
        else
        {
            qDebug(logRadio()).noquote().nospace()
                << "CacheRefresh::Missing func=" << funcString[func] << " receiver=" << int(receiver);
        }
        addUnique(kPriorityImmediate, func, false, receiver);
    }
    return ret;
}

CacheItem CachingQueue::peekCache(Funcs func, uchar receiver)
{
    CacheItem result;
    if (func == funcNone)
    {
        return result;
    }

    std::lock_guard locker(mutex);
    return findCacheLocked(func, receiver);
}

CacheItem CachingQueue::findCacheLocked(Funcs func, uchar receiver) const
{
    auto it = cache.find(func);
    while (it != cache.end() && it->command == func)
    {
        if (it->receiver == receiver)
        {
            return *it;
        }
        ++it;
    }
    return {};
}

bool CachingQueue::cacheValuesDiffer(const QVariant& a, const QVariant& b)
{
    bool changed = false;

    if (a.isValid() && b.isValid())
    {
        const int ValueType = a.userType();
        const auto valueHolds = [ValueType](int type) { return ValueType == type; };
        if (ValueType != b.userType())
        {
            return true;
        }

        // Qt handles built-in values, enums, and custom metatypes that expose
        // an equality comparator. Only protocol structs with deliberately
        // partial or always-notify policies need cases below.
        if (a.metaType().isEqualityComparable())
        {
            return a != b;
        }
        if (valueHolds(qMetaTypeId<ModeInfo>()))
        {
            if (a.value<ModeInfo>().mk != b.value<ModeInfo>().mk ||
                a.value<ModeInfo>().reg != b.value<ModeInfo>().reg ||
                a.value<ModeInfo>().filter != b.value<ModeInfo>().filter ||
                a.value<ModeInfo>().data != b.value<ModeInfo>().data)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<Frequency>()))
        {
            if (a.value<Frequency>().Hz != b.value<Frequency>().Hz)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<AntennaInfo>()))
        {
            if (a.value<AntennaInfo>().antenna != b.value<AntennaInfo>().antenna ||
                a.value<AntennaInfo>().rx != b.value<AntennaInfo>().rx)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<radioInput>()))
        {
            if (a.value<radioInput>().type != b.value<radioInput>().type)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<duplexMode_t>()))
        {
            if (a.value<duplexMode_t>() != b.value<duplexMode_t>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<ToneInfo>()))
        {
            if (a.value<ToneInfo>().tone != b.value<ToneInfo>().tone)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<meter_t>()))
        {
            if (a.value<meter_t>() != b.value<meter_t>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<vfo_t>()))
        {
            if (a.value<vfo_t>() != b.value<vfo_t>())
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<LpfHpf>()))
        {
            if (a.value<LpfHpf>().lpf != b.value<LpfHpf>().lpf || a.value<LpfHpf>().hpf != b.value<LpfHpf>().hpf)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<SpectrumBounds>()))
        {
            if (a.value<SpectrumBounds>().edge != b.value<SpectrumBounds>().edge ||
                a.value<SpectrumBounds>().start != b.value<SpectrumBounds>().start ||
                a.value<SpectrumBounds>().end != b.value<SpectrumBounds>().end)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<centerSpanData>()))
        {
            if (a.value<centerSpanData>().reg != b.value<centerSpanData>().reg ||
                a.value<centerSpanData>().freq != b.value<centerSpanData>().freq)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<RptrAccessData>()))
        {
            if (a.value<RptrAccessData>().accessMode != b.value<RptrAccessData>().accessMode ||
                a.value<RptrAccessData>().turnOffTSQL != b.value<RptrAccessData>().turnOffTSQL ||
                a.value<RptrAccessData>().turnOffTone != b.value<RptrAccessData>().turnOffTone)
            {
                changed = true;
            }
        }
        else if (valueHolds(qMetaTypeId<ScopeData>()) || valueHolds(qMetaTypeId<MemoryType>()) ||
                 valueHolds(qMetaTypeId<TimeKind>()) || valueHolds(qMetaTypeId<DateKind>()) ||
                 valueHolds(qMetaTypeId<MeterKind>()) || valueHolds(qMetaTypeId<UdpConnectionSettings>()))
        {
            changed = true;
        }
        else
        {
            qInfo(logRadio()).noquote() << "Unsupported cache value:" << a.typeName();
            // Unknown payloads must be treated as changed so newly introduced
            // response types still reach models until an intentional
            // comparison policy is added above.
            changed = true;
        }
    }
    else if (a.isValid())
    {
        changed = true;
    }

    return changed;
}
