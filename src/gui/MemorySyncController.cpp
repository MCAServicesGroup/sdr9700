#include "MemorySyncController.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemoryEditorPolicy.h"
#include "MemorySyncPolicy.h"
#include "MemoryViewController.h"
#include "models/RadioModel.h"

#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <algorithm>

using namespace sdr9700::ui::main_window;
using namespace sdr9700::memory;

MemorySyncController::MemorySyncController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(kRadioMemoryRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &MemorySyncController::requestNextRadioMemory);

    m_periodicRefreshTimer = new QTimer(this);
    setMemoryPollIntervalSeconds(AppSettings::instance()
                                     .value(QString::fromLatin1(kMemoryPollIntervalSecondsSettingsKey),
                                            sdr9700::kDefaultMemoryPollIntervalSeconds)
                                     .toInt());
    connect(m_periodicRefreshTimer, &QTimer::timeout, this, &MemorySyncController::startScheduledRadioMemoryRefresh);

    m_syncTimeoutTimer = new QTimer(this);
    m_syncTimeoutTimer->setSingleShot(true);
    connect(m_syncTimeoutTimer, &QTimer::timeout, this, [this]() { finishRadioMemoryRefresh(true); });

    m_replyGraceTimer = new QTimer(this);
    m_replyGraceTimer->setSingleShot(true);
    connect(m_replyGraceTimer, &QTimer::timeout, this, [this]() { finishRadioMemoryRefresh(false); });

    m_startupStabilityTimer = new QTimer(this);
    m_startupStabilityTimer->setSingleShot(true);
    m_startupStabilityTimer->setInterval(kRadioMemoryStartupStabilityMs);
    connect(m_startupStabilityTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_startupScopeFrameCount >= kRadioMemoryStartupMinimumScopeFrames)
                {
                    startInitialRadioMemoryRefresh();
                }
            });

    m_startupFallbackTimer = new QTimer(this);
    m_startupFallbackTimer->setSingleShot(true);
    m_startupFallbackTimer->setInterval(kRadioMemoryStartupFallbackMs);
    connect(m_startupFallbackTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_initialSyncPending)
                {
                    qWarning(logGui()).noquote()
                        << "Starting initial memory sync without confirmed spectrum activity after bounded wait";
                    startInitialRadioMemoryRefresh();
                }
            });
}

void MemorySyncController::forceRadioMemorySync()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Connect to the radio before syncing memories."));
        return;
    }

    if (m_refreshInProgress)
    {
        cancelRadioMemoryRefresh();
    }
    if (m_owner->memoryOperationInProgress())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Sync Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before syncing."));
        return;
    }

    requestRadioMemoryRefresh();
    m_owner->reloadMemoryTable();
    m_owner->showMemoryStatusMessage(QStringLiteral("Memory sync started"));
}

void MemorySyncController::setMemoryPollIntervalSeconds(int seconds)
{
    m_memoryPollIntervalSeconds = sdr9700::normalizeMemoryPollIntervalSeconds(seconds);
    if (m_memoryPollIntervalSeconds == 0)
    {
        m_periodicRefreshTimer->stop();
        return;
    }

    m_periodicRefreshTimer->setInterval(m_memoryPollIntervalSeconds * 1000);
    if (m_radioReady)
    {
        m_periodicRefreshTimer->start();
    }
}

bool MemorySyncController::periodicRefreshScheduled() const
{
    return m_periodicRefreshTimer && m_periodicRefreshTimer->isActive();
}

void MemorySyncController::handleRadioReadyChanged(bool ready)
{
    qInfo(logGui()).noquote() << "MemorySyncController observed radio readyChanged:" << ready;
    m_radioReady = ready;
    if (ready)
    {
        if (m_initialSyncComplete)
        {
            m_initialSyncComplete = false;
            emit m_owner->initialMemorySyncChanged(false);
        }
        m_initialSyncPending = true;
        m_startupScopeFrameCount = 0;
        m_startupStabilityTimer->stop();
        m_startupFallbackTimer->start();
        // Memory verification is background work after receiver readiness.
        // Its stability gate and progress belong to Memory Manager rather than
        // the operator-facing radio connection lifecycle.
        if (m_memoryPollIntervalSeconds > 0)
        {
            m_periodicRefreshTimer->start();
        }
        return;
    }

    m_initialSyncPending = false;
    m_startupScopeFrameCount = 0;
    m_startupStabilityTimer->stop();
    m_startupFallbackTimer->stop();
    m_refreshTimer->stop();
    m_periodicRefreshTimer->stop();
    m_owner->m_memoryViewController->stopScheduledRefresh();
    finishRadioMemoryRefresh(false);
    if (m_initialSyncComplete)
    {
        m_initialSyncComplete = false;
        emit m_owner->initialMemorySyncChanged(false);
    }
    // Preserve the last radio-confirmed snapshot for offline viewing and for a
    // fast reconnect. m_receivedMemoryKeys is still cleared below, so cached
    // rows cannot be mistaken for current-session proof that a slot is empty
    // or used to bypass the normal radio write/readback verification.
    clearReceivedMemories();
    m_owner->rebuildMemoryViews();
}

void MemorySyncController::handleSpectrumActivity()
{
    if (!m_radioReady || !m_initialSyncPending)
    {
        return;
    }

    ++m_startupScopeFrameCount;
    if (!m_startupStabilityTimer->isActive())
    {
        m_startupStabilityTimer->start();
    }
}

void MemorySyncController::startInitialRadioMemoryRefresh()
{
    if (!m_radioReady || !m_initialSyncPending)
    {
        return;
    }

    m_initialSyncPending = false;
    m_startupStabilityTimer->stop();
    m_startupFallbackTimer->stop();
    qInfo(logGui()).nospace() << "Initial memory sync stability gate satisfied scope_frames="
                              << m_startupScopeFrameCount;
    requestRadioMemoryRefresh();
}

void MemorySyncController::startScheduledRadioMemoryRefresh()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected() || m_refreshInProgress ||
        m_owner->memoryOperationInProgress())
    {
        return;
    }

    m_scheduledRefreshInProgress = true;
    m_owner->m_window->showStatusMessage(QStringLiteral("Scheduled memory sync started"));
    requestRadioMemoryRefresh();
}

void MemorySyncController::requestRadioMemoryRefresh()
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, &MemorySyncController::requestRadioMemoryRefresh, Qt::QueuedConnection);
        return;
    }

    if (!m_owner->m_window->m_model)
    {
        qInfo(logGui()).noquote() << "Radio memory sync skipped; radio model is not available";
        return;
    }
    if (!m_owner->m_window->m_model->isConnected())
    {
        qInfo(logGui()).noquote() << "Radio memory sync skipped; radio model is not connected";
        return;
    }
    if (m_refreshInProgress)
    {
        qInfo(logGui()).noquote() << "Radio memory sync skipped; refresh is already in progress";
        return;
    }

    m_pollKeys.clear();
    m_pollKeys.reserve(kRadioMemorySyncTotal);
    m_pollIndex = 0;
    m_currentGroup = 0;
    m_currentChannel = 0;
    m_missingRetryRound = 0;
    m_lastUnansweredSlotCount = 0;
    m_refreshInProgress = true;
    m_owner->beginMemoryDatabaseSync();
    m_receivedMemoryKeys.clear();
    m_expectedMemoryKeys.clear();
    for (quint16 group = kRadioMemoryFirstGroup; group <= kRadioMemoryLastGroup; ++group)
    {
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastChannel; ++channel)
        {
            m_expectedMemoryKeys.insert(radioMemoryKey(group, channel));
            m_pollKeys.append(radioMemoryKey(group, channel));
        }
    }
    for (quint16 channel = kRadioMemoryFirstSatelliteChannel; channel <= kRadioMemoryLastSatelliteChannel; ++channel)
    {
        m_expectedMemoryKeys.insert(radioMemoryKey(kRadioMemorySatelliteGroup, channel));
        m_pollKeys.append(radioMemoryKey(kRadioMemorySatelliteGroup, channel));
    }

    m_syncTimeoutTimer->start(radioMemorySyncTimeoutMs());
    m_replyGraceTimer->stop();
    qInfo(logGui()).noquote() << "Radio memory sync started; polling" << kRadioMemorySyncTotal << "slots";
    requestNextRadioMemory();
    m_owner->rebuildMemoryViews();
}

void MemorySyncController::requestRadioMemoryRefreshForOperation(Completion completion)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected() || m_refreshInProgress)
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }

    m_operationCompletion = std::move(completion);
    m_operationSyncAttempt = 1;
    requestRadioMemoryRefresh();
}

void MemorySyncController::requestNextRadioMemory()
{
    if (!m_refreshInProgress)
    {
        return;
    }
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        finishRadioMemoryRefresh(false);
        return;
    }
    if (m_pollIndex >= m_pollKeys.size())
    {
        m_refreshTimer->stop();
        if (allExpectedRadioMemoriesReceived())
        {
            qInfo(logGui()).noquote() << "Radio memory sync received all expected replies";
            finishRadioMemoryRefresh(false);
            return;
        }
        qInfo(logGui()).noquote() << "Radio memory sync poll pass complete; received" << m_receivedMemoryKeys.size()
                                  << "of" << m_expectedMemoryKeys.size()
                                  << "possible replies, waiting briefly for late replies";
        m_owner->setMemoryProgress(QStringLiteral("Finalizing radio memory sync"), m_receivedMemoryKeys.size(),
                                   m_expectedMemoryKeys.size());
        if (!m_replyGraceTimer->isActive())
        {
            m_replyGraceTimer->start(kRadioMemorySyncReplyGraceMs);
        }
        return;
    }

    const quint32 key = m_pollKeys.at(m_pollIndex++);
    m_currentGroup = static_cast<quint16>(key >> 16);
    m_currentChannel = static_cast<quint16>(key & 0xffffU);
    if (m_currentChannel == kRadioMemoryFirstChannel)
    {
        qInfo(logGui()).noquote() << "Radio memory sync polling" << memoryBandLabelForGroup(m_currentGroup);
    }
    else if (m_currentChannel <= 5 || (m_currentChannel % 25) == 0)
    {
        qInfo(logGui()).noquote() << "Radio memory sync polling" << memoryBandLabelForGroup(m_currentGroup) << "channel"
                                  << m_currentChannel;
    }
    const QString action = m_missingRetryRound > 0 ? QStringLiteral("Retrying") : QStringLiteral("Syncing");
    m_owner->setMemoryProgress(QStringLiteral("%1 %2 channel %3")
                                   .arg(action)
                                   .arg(memoryBandLabelForGroup(m_currentGroup))
                                   .arg(m_currentChannel, 3, 10, QLatin1Char('0')),
                               m_receivedMemoryKeys.size(), m_expectedMemoryKeys.size());
    if (m_currentGroup == kRadioMemorySatelliteGroup)
    {
        m_owner->m_window->m_model->requestSatelliteMemory(m_currentChannel);
    }
    else
    {
        m_owner->m_window->m_model->requestRadioMemory(m_currentGroup, m_currentChannel);
    }
    m_refreshTimer->start();
}

void MemorySyncController::handleRadioMemoryReceived(quint32 key)
{
    m_receivedMemoryKeys.insert(key);
    if (!m_refreshInProgress)
    {
        return;
    }

    m_refreshTimer->stop();
    if (m_pollIndex >= m_pollKeys.size())
    {
        m_owner->setMemoryProgress(QStringLiteral("Finalizing radio memory sync"), m_receivedMemoryKeys.size(),
                                   m_expectedMemoryKeys.size());
    }
    if (allExpectedRadioMemoriesReceived())
    {
        finishRadioMemoryRefresh(false);
        return;
    }
    requestNextRadioMemory();
}

bool MemorySyncController::startMissingMemoryRetry()
{
    if (m_missingRetryRound >= kRadioMemoryMissingRetryMaxRounds)
    {
        return false;
    }

    const QSet<quint32> missing = m_expectedMemoryKeys - m_receivedMemoryKeys;
    if (missing.isEmpty())
    {
        return false;
    }

    m_pollKeys = missing.values();
    std::sort(m_pollKeys.begin(), m_pollKeys.end());
    m_pollIndex = 0;
    ++m_missingRetryRound;
    qWarning(logGui()).nospace() << "Radio memory sync retry started: missing_slots=" << m_pollKeys.size()
                                 << " retry_round=" << m_missingRetryRound
                                 << " retry_round_limit=" << kRadioMemoryMissingRetryMaxRounds;
    m_owner->setMemoryProgress(QStringLiteral("Retrying %1 missing memory %2")
                                   .arg(m_pollKeys.size())
                                   .arg(m_pollKeys.size() == 1 ? QStringLiteral("slot") : QStringLiteral("slots")),
                               m_receivedMemoryKeys.size(), m_expectedMemoryKeys.size());
    m_syncTimeoutTimer->start(radioMemorySyncTimeoutMs());
    m_replyGraceTimer->stop();
    requestNextRadioMemory();
    return true;
}

void MemorySyncController::finishRadioMemoryRefresh(bool timedOut)
{
    m_refreshTimer->stop();
    m_syncTimeoutTimer->stop();
    m_replyGraceTimer->stop();
    m_owner->m_memoryViewController->stopScheduledRefresh();
    const bool wasInProgress = m_refreshInProgress;
    const bool completedPollPass = wasInProgress && !timedOut && m_owner->m_window->m_model &&
                                   m_owner->m_window->m_model->isConnected() && m_pollIndex >= m_pollKeys.size();
    const bool receivedAllExpected = wasInProgress && allExpectedRadioMemoriesReceived();
    const int unansweredSlotCount = wasInProgress ? (m_expectedMemoryKeys - m_receivedMemoryKeys).size() : 0;
    const bool noRadioReplies = completedPollPass && m_receivedMemoryKeys.isEmpty();
    if (completedPollPass && !receivedAllExpected && startMissingMemoryRetry())
    {
        return;
    }
    const bool scheduledRefresh = m_scheduledRefreshInProgress;
    m_scheduledRefreshInProgress = false;
    timedOut = timedOut || noRadioReplies;

    if (sdr9700::shouldRetryIncompleteMemoryOperationSync(static_cast<bool>(m_operationCompletion), completedPollPass,
                                                          receivedAllExpected, m_operationSyncAttempt,
                                                          kRadioMemoryOperationSyncMaxAttempts))
    {
        qWarning(logGui()).noquote() << "Radio memory operation sync attempt" << m_operationSyncAttempt << "received"
                                     << m_receivedMemoryKeys.size() << "of" << m_expectedMemoryKeys.size()
                                     << "expected replies; retrying the complete poll";
        ++m_operationSyncAttempt;
        m_refreshInProgress = false;
        m_currentGroup = 0;
        m_currentChannel = 0;
        m_pollKeys.clear();
        m_pollIndex = 0;
        m_expectedMemoryKeys.clear();
        m_owner->clearMemoryProgress();
        requestRadioMemoryRefresh();
        return;
    }

    if (completedPollPass && !timedOut)
    {
        m_owner->finishMemoryDatabaseSync(m_expectedMemoryKeys.size());
    }
    else
    {
        m_owner->cancelMemoryDatabaseSync();
    }

    m_refreshInProgress = false;
    m_lastUnansweredSlotCount = unansweredSlotCount;
    m_currentGroup = 0;
    m_currentChannel = 0;
    m_pollKeys.clear();
    m_pollIndex = 0;
    m_expectedMemoryKeys.clear();
    m_owner->clearMemoryProgress();

    if (timedOut && wasInProgress)
    {
        qWarning(logGui()).noquote() << "Radio memory sync failed after receiving" << m_receivedMemoryKeys.size()
                                     << "memory replies" << (noRadioReplies ? "(no CI-V memory replies)" : "(timeout)");
        m_owner->m_window->showStatusMessage(m_initialSyncComplete ? QStringLiteral("Memory sync timed out")
                                                                   : QStringLiteral("Memory sync timed out; retrying"),
                                             5000, MainWindow::StatusMessageKind::Warning);
        if (!m_initialSyncComplete && m_owner->m_window->m_model && m_owner->m_window->m_model->isConnected())
        {
            QTimer::singleShot(kRadioMemoryInitialSyncRetryDelayMs, this,
                               [this]()
                               {
                                   if (!m_initialSyncComplete)
                                   {
                                       requestRadioMemoryRefresh();
                                   }
                               });
        }
    }
    else if (completedPollPass && !m_initialSyncComplete)
    {
        m_initialSyncComplete = true;
        if (unansweredSlotCount > 0)
        {
            qWarning(logGui()).nospace() << "Initial radio memory sync exhausted targeted retries: unanswered_slots="
                                         << unansweredSlotCount;
        }
        qInfo(logGui()).noquote() << "Initial radio memory sync complete with" << m_owner->m_radioMemoriesByKey.size()
                                  << "stored memories and" << unansweredSlotCount << "unanswered slots";
        emit m_owner->initialMemorySyncChanged(true);
    }
    else if (completedPollPass)
    {
        if (unansweredSlotCount > 0)
        {
            qWarning(logGui()).nospace() << "Radio memory sync exhausted targeted retries: unanswered_slots="
                                         << unansweredSlotCount;
        }
        qInfo(logGui()).noquote() << "Radio memory sync complete with" << m_owner->m_radioMemoriesByKey.size()
                                  << "stored memories and" << unansweredSlotCount << "unanswered slots";
        if (scheduledRefresh)
        {
            m_owner->m_window->showStatusMessage(QStringLiteral("Scheduled memory sync complete"));
        }
    }
    m_owner->rebuildMemoryViews();

    Completion operationCompletion = std::move(m_operationCompletion);
    m_operationCompletion = {};
    m_operationSyncAttempt = 0;
    if (operationCompletion)
    {
        operationCompletion(completedPollPass && !timedOut && receivedAllExpected);
    }
}

void MemorySyncController::cancelRadioMemoryRefresh()
{
    finishRadioMemoryRefresh(false);
}

void MemorySyncController::clearReceivedMemories()
{
    m_receivedMemoryKeys.clear();
    m_expectedMemoryKeys.clear();
    m_lastUnansweredSlotCount = 0;
}

bool MemorySyncController::allExpectedRadioMemoriesReceived() const
{
    return sdr9700::memorySyncComplete(m_expectedMemoryKeys, m_receivedMemoryKeys);
}

bool MemorySyncController::initialMemorySyncComplete() const
{
    return m_initialSyncComplete;
}

bool MemorySyncController::refreshInProgress() const
{
    return m_refreshInProgress;
}

bool MemorySyncController::hasReceivedMemory(quint32 key) const
{
    return m_receivedMemoryKeys.contains(key);
}

int MemorySyncController::lastUnansweredSlotCount() const
{
    return m_lastUnansweredSlotCount;
}

int MemorySyncController::missingRetryRound() const
{
    return m_missingRetryRound;
}
