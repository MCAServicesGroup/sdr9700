#include "MemoryCsvController.h"

#include "MainWindow.h"
#include "ConfirmationDialog.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryCsvHelpers.h"
#include "MemoryRecordHelpers.h"
#include "MemorySyncController.h"
#include "models/RadioModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>

using namespace sdr9700::memory;

namespace
{
const QString kImportSyncStatusMessage = QStringLiteral("Syncing memories before import");
}

MemoryCsvController::MemoryCsvController(MemoryController* owner) : QObject(owner), m_owner(owner) {}

bool MemoryCsvController::exportRadioMemories()
{
    const QString path = QFileDialog::getSaveFileName(m_owner->popupParent(), QStringLiteral("Export Memories"),
                                                      memoryExportPath(QDir::homePath(), QDateTime::currentDateTime()),
                                                      QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return false;
    }

    QVector<MemoryRecord> editableMemories;
    for (const MemoryRecord& memory : m_owner->currentMemories())
    {
        if (!memory.readOnly)
        {
            editableMemories.append(memory);
        }
    }
    if (!writeMemoriesCsvFile(path, editableMemories))
    {
        QMessageBox::warning(m_owner->popupParent(), QStringLiteral("Export Memories Failed"),
                             QStringLiteral("Memory export failed. Could not save the selected file."));
        return false;
    }

    QMessageBox::information(m_owner->popupParent(), QStringLiteral("Export Memories Successful"),
                             QStringLiteral("Memory export successful."));
    return true;
}

void MemoryCsvController::importRadioMemories()
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Connect to the radio before importing memories."));
        return;
    }
    if (m_owner->memoryRefreshInProgress())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Wait for the current radio memory sync to finish before importing."));
        return;
    }
    if (m_owner->memoryOperationInProgress())
    {
        QMessageBox::information(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                 QStringLiteral("Wait for the current memory operation to finish before importing."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(m_owner->popupParent(), QStringLiteral("Import Memories"),
                                                      QString(), QString::fromLatin1(kMemoryFileFilter));
    if (path.isEmpty())
    {
        return;
    }

    QStringList importErrors;
    QString fileError;
    const QVector<MemoryRecord> records = readMemoriesCsvFile(path, &importErrors, &fileError);
    if (!fileError.isEmpty())
    {
        QMessageBox::warning(m_owner->popupParent(), QStringLiteral("Import Memories Failed"),
                             QStringLiteral("Memory import failed. Could not open the selected file."));
        return;
    }
    if (!importErrors.isEmpty())
    {
        const QString details = importErrors.mid(0, 12).join(QLatin1Char('\n'));
        const QString suffix =
            importErrors.size() > 12 ? QStringLiteral("\n...and %1 more").arg(importErrors.size() - 12) : QString();
        QMessageBox::warning(
            m_owner->popupParent(), QStringLiteral("Import Memories Failed"),
            QStringLiteral("Memory import failed. Fix the CSV file and try again.\n\n%1%2").arg(details, suffix));
        return;
    }
    if (records.isEmpty())
    {
        QMessageBox::warning(
            m_owner->popupParent(), QStringLiteral("Import Memories Failed"),
            QStringLiteral("Memory import failed. The selected file does not contain importable CSV memories."));
        return;
    }

    if (!sdr9700::ui::confirmAction(
            m_owner->popupParent(), QStringLiteral("Import Memories"),
            QStringLiteral("Import these memories to the radio?\n\n"
                           "SDR9700 will first sync with the radio, then clear occupied user memory channels "
                           "on 2M, 70CM, and 23CM."),
            QStringLiteral("Import"), true))
    {
        return;
    }

    QVector<MemoryType> uploads;
    uploads.reserve(records.size());
    for (const MemoryRecord& record : records)
    {
        uploads.append(radioMemoryFromRecord(record, record.group, record.channel));
    }

    m_owner->m_window->showStatusMessage(kImportSyncStatusMessage, 4000);
    m_owner->m_memorySyncController->requestRadioMemoryRefreshForOperation(
        [this, uploads](bool synced)
        {
            m_owner->m_window->clearPersistentStatusMessage(kImportSyncStatusMessage);
            if (!synced)
            {
                m_owner->m_window->showStatusMessage(QStringLiteral("Import canceled; memory sync incomplete"), 8000,
                                                     MainWindow::StatusMessageKind::Error);
                return;
            }

            QVector<MemoryType> backup;
            backup.reserve(m_owner->m_radioMemoriesByKey.size());
            for (const MemoryType& memory : m_owner->m_radioMemoriesByKey)
            {
                backup.append(memory);
            }
            const QVector<MemoryType> deletes = deletedStoredRadioMemories(backup);

            m_owner->queueRadioMemoryWrites(deletes, 0, QStringLiteral("Clearing existing memories"),
                                            [this, uploads, backup](bool cleared)
                                            {
                                                if (!cleared)
                                                {
                                                    restoreRadioMemoriesAfterFailedImport(backup);
                                                    return;
                                                }
                                                m_owner->queueRadioMemoryWrites(
                                                    uploads, 0, QStringLiteral("Uploading memories"),
                                                    [this, backup, importedCount = uploads.size()](bool uploaded)
                                                    {
                                                        if (!uploaded)
                                                        {
                                                            restoreRadioMemoriesAfterFailedImport(backup);
                                                            return;
                                                        }
                                                        m_owner->m_window->showStatusMessage(
                                                            QStringLiteral("Imported %1 memories").arg(importedCount));
                                                        m_owner->requestRadioMemoryRefresh();
                                                    });
                                            });
        });
}

void MemoryCsvController::restoreRadioMemoriesAfterFailedImport(const QVector<MemoryType>& backup)
{
    if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isConnected())
    {
        m_owner->m_window->showStatusMessage(QStringLiteral("Import failed; reconnect and restore the previous export"),
                                             8000, MainWindow::StatusMessageKind::Error);
        return;
    }

    // Determine exactly which imported records reached the radio before
    // clearing anything during rollback. This preserves the pre-import
    // snapshot while avoiding delete commands for empty channels.
    m_owner->m_window->showStatusMessage(QStringLiteral("Import failed; preparing rollback"), 0,
                                         MainWindow::StatusMessageKind::Warning);
    m_owner->m_memorySyncController->requestRadioMemoryRefreshForOperation(
        [this, backup](bool synced)
        {
            if (!synced)
            {
                m_owner->m_window->showStatusMessage(QStringLiteral("Rollback could not start; memory sync failed"),
                                                     8000, MainWindow::StatusMessageKind::Error);
                return;
            }

            QVector<MemoryType> current;
            current.reserve(m_owner->m_radioMemoriesByKey.size());
            for (const MemoryType& memory : m_owner->m_radioMemoriesByKey)
            {
                current.append(memory);
            }
            const QVector<MemoryType> deletes = deletedStoredRadioMemories(current);

            m_owner->queueRadioMemoryWrites(
                deletes, 0, QStringLiteral("Clearing partial memory import"),
                [this, backup](bool cleared)
                {
                    if (!cleared)
                    {
                        m_owner->m_window->showStatusMessage(
                            QStringLiteral("Rollback failed while clearing imported memories"), 8000,
                            MainWindow::StatusMessageKind::Error);
                        m_owner->requestRadioMemoryRefresh();
                        return;
                    }
                    m_owner->m_radioMemoriesByKey.clear();
                    m_owner->queueRadioMemoryWrites(
                        backup, 0, QStringLiteral("Restoring previous memories"),
                        [this](bool restored)
                        {
                            m_owner->m_window->showStatusMessage(
                                restored ? QStringLiteral("Import failed; previous memories restored")
                                         : QStringLiteral("Rollback failed while restoring memories"),
                                8000,
                                restored ? MainWindow::StatusMessageKind::Warning
                                         : MainWindow::StatusMessageKind::Error);
                            m_owner->requestRadioMemoryRefresh();
                        });
                });
        });
}
