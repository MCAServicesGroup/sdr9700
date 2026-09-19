#include "MemoryWriteController.h"

#include "MainWindow.h"
#include "LogCategories.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemorySyncPolicy.h"
#include "UiTheme.h"
#include "models/RadioModel.h"

#include <algorithm>
#include <QLabel>
#include <QTimer>

using namespace sdr9700::memory;

MemoryWriteController::MemoryWriteController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kRadioMemoryWriteReadbackTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_waitingForReadback)
                {
                    finish(true);
                }
            });

    m_readbackRetryTimer = new QTimer(this);
    m_readbackRetryTimer->setInterval(kRadioMemoryWriteReadbackRetryMs);
    connect(m_readbackRetryTimer, &QTimer::timeout, this, &MemoryWriteController::requestExpectedReadback);
}

void MemoryWriteController::queueWrites(const QVector<MemoryType>& memories, int startDelayMs,
                                        const QString& progressLabel, Completion completion)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }

    QTimer::singleShot(std::max(0, startDelayMs), this,
                       [this, memories, progressLabel, completion = std::move(completion)]() mutable
                       { startWrites(memories, progressLabel, std::move(completion)); });
}

void MemoryWriteController::startWrites(const QVector<MemoryType>& memories, const QString& progressLabel,
                                        Completion completion)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        if (completion)
        {
            completion(false);
        }
        return;
    }
    if (active())
    {
        m_owner->m_window->showStatusMessage(QStringLiteral("Memory write already in progress"), 5000,
                                             MainWindow::StatusMessageKind::Warning);
        if (completion)
        {
            completion(false);
        }
        return;
    }
    if (memories.isEmpty())
    {
        if (completion)
        {
            completion(true);
        }
        return;
    }

    m_memories = memories;
    m_index = 0;
    m_progressLabel = progressLabel;
    m_completion = std::move(completion);
    m_owner->setMemoryProgress(m_progressLabel, 0, m_memories.size());
    writeNext();
}

void MemoryWriteController::writeNext()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        finish(true);
        return;
    }
    if (m_index >= m_memories.size())
    {
        finish(false);
        return;
    }

    const MemoryType memory = m_memories.at(m_index);
    m_expectedKey = radioMemoryKey(memory.group, memory.channel);
    m_waitingForReadback = true;
    m_owner->m_window->m_model->writeRadioMemory(memory);
    m_timeoutTimer->start();
    m_readbackRetryTimer->start();
}

void MemoryWriteController::handleReadback(quint32 key, const MemoryType& memory)
{
    if (!sdr9700::memoryReadbackExpected(m_waitingForReadback, m_expectedKey, key))
    {
        return;
    }
    const QStringList differences = m_index < m_memories.size()
                                        ? radioMemoryReadbackDifferences(m_memories.at(m_index), memory)
                                        : QStringList{QStringLiteral("unexpected batch position")};
    if (!differences.isEmpty())
    {
        qWarning(logGui()).noquote() << "Radio memory write readback did not match requested contents for"
                                     << memoryBandLabelForGroup(memory.group) << "channel" << memory.channel
                                     << "differing fields:" << differences;
        // The IC-9700 can briefly return the old contents while it commits a
        // memory write. The retry timer requests this slot again without
        // resending the mutation.
        return;
    }

    m_timeoutTimer->stop();
    m_readbackRetryTimer->stop();
    m_waitingForReadback = false;
    ++m_index;
    m_owner->setMemoryProgress(m_progressLabel, m_index, m_memories.size());
    QTimer::singleShot(kRadioMemoryWriteIntervalMs, this, &MemoryWriteController::writeNext);
}

void MemoryWriteController::requestExpectedReadback()
{
    if (!m_waitingForReadback || m_index >= m_memories.size() || !m_owner->m_window->m_model ||
        !m_owner->m_window->m_model->isConnected())
    {
        return;
    }

    const MemoryType& expected = m_memories.at(m_index);
    m_owner->m_window->m_model->requestRadioMemory(expected.group, expected.channel);
}

bool MemoryWriteController::active() const
{
    return m_waitingForReadback || !m_memories.isEmpty();
}

void MemoryWriteController::finish(bool failed)
{
    m_timeoutTimer->stop();
    m_readbackRetryTimer->stop();
    const Completion completion = std::move(m_completion);
    const QString label = m_progressLabel;
    m_memories.clear();
    m_index = 0;
    m_expectedKey = 0;
    m_waitingForReadback = false;
    m_progressLabel.clear();
    m_completion = {};
    m_owner->clearMemoryProgress();
    if (failed)
    {
        const QString message =
            label.isEmpty() ? QStringLiteral("Memory write timed out") : QStringLiteral("%1 timed out").arg(label);
        if (m_owner->m_window->m_memoryCountLabel)
        {
            m_owner->m_window->m_memoryCountLabel->setText(message);
            m_owner->m_window->m_memoryCountLabel->setStyleSheet(
                QStringLiteral("QLabel { color: %1; }").arg(QLatin1String(UiTheme::Color::Danger)));
            QTimer::singleShot(5000, this,
                               [this]()
                               {
                                   if (!active())
                                   {
                                       m_owner->reloadMemoryTable();
                                   }
                               });
        }
        m_owner->m_window->showStatusMessage(message, 5000, MainWindow::StatusMessageKind::Warning);
    }
    if (completion)
    {
        completion(!failed);
    }
}
