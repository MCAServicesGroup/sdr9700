#include "MemoryViewController.h"

#include "AppSettings.h"
#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemoryViewHelpers.h"
#include "MemorySyncController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace sdr9700::memory;

namespace
{
constexpr int kMemorySectionSpacing = 8;
} // namespace

MemoryViewController::MemoryViewController(MemoryController* owner) : QObject(owner), m_owner(owner)
{
    m_showSpecialMemories =
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryShowSpecialMemoriesSettingsKey), QStringLiteral("False"))
            .toBool();
    m_showSatelliteMemories =
        AppSettings::instance()
            .value(QString::fromLatin1(kMemoryShowSatelliteMemoriesSettingsKey), QStringLiteral("False"))
            .toBool();
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(250);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, &MemoryViewController::rebuild);
}

void MemoryViewController::stopScheduledRefresh()
{
    m_refreshTimer->stop();
}

void MemoryViewController::setShowSpecialMemories(bool show)
{
    if (m_showSpecialMemories == show)
    {
        return;
    }
    m_showSpecialMemories = show;
    if (QComboBox* filter = m_owner->m_window->m_memoryBandFilter)
    {
        const int existingIndex = filter->findData(QStringLiteral("special"));
        if (show && existingIndex < 0)
        {
            const int satelliteIndex = filter->findData(QStringLiteral("satellite"));
            filter->insertItem(satelliteIndex < 0 ? filter->count() : satelliteIndex, QStringLiteral("Special"),
                               QStringLiteral("special"));
        }
        else if (!show && existingIndex >= 0)
        {
            filter->removeItem(existingIndex);
        }
    }
    rebuild();
}

void MemoryViewController::setShowSatelliteMemories(bool show)
{
    if (m_showSatelliteMemories == show)
    {
        return;
    }
    m_showSatelliteMemories = show;
    if (QComboBox* filter = m_owner->m_window->m_memoryBandFilter)
    {
        const int existingIndex = filter->findData(QStringLiteral("satellite"));
        if (show && existingIndex < 0)
        {
            filter->addItem(QStringLiteral("Satellite"), QStringLiteral("satellite"));
        }
        else if (!show && existingIndex >= 0)
        {
            filter->removeItem(existingIndex);
        }
    }
    rebuild();
}

bool MemoryViewController::operationInProgress() const
{
    return !m_progressLabel.isEmpty();
}

void MemoryViewController::buildMemoryWindow()
{
    m_owner->m_window->m_memoryWindow =
        new sdr9700::ui::UtilityWindow(QStringLiteral("Memory Manager"), m_owner->m_window);
    m_owner->m_window->m_memoryWindow->setObjectName("memoryWindow");
    m_owner->m_window->m_memoryWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_owner->m_window->m_memoryWindow->resize(kMemoryWindowSize);
    m_owner->m_window->m_memoryWindow->setMinimumSize(900, 600);

    auto* panel = new QWidget(m_owner->m_window->m_memoryWindow);
    auto* root = new QHBoxLayout(panel);
    root->setContentsMargins(kMemoryWindowMargins.left(), kMemoryWindowMargins.top(), kMemoryWindowMargins.right(),
                             kMemoryWindowMargins.bottom());
    root->setSpacing(kMemoryWindowSpacing);

    auto* leftPane = new QWidget(panel);
    leftPane->setFixedWidth(kMemoryWindowSize.width() - kMemoryWindowMargins.left() - kMemoryWindowMargins.right());
    auto* leftRoot = new QVBoxLayout(leftPane);
    leftRoot->setContentsMargins(0, 0, 0, 0);
    leftRoot->setSpacing(kMemorySectionSpacing);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(kNoMargins);
    toolbar->setSpacing(kMemoryToolbarSpacing);
    const QString toolbarGroupStyle =
        QStringLiteral("QGroupBox { border: 1px solid %1; border-radius: 3px; background: transparent; }")
            .arg(QLatin1String(UiTheme::Color::BorderMedium));

    auto* filterGroup = new QGroupBox(panel);
    filterGroup->setStyleSheet(toolbarGroupStyle);
    auto* filterLayout = new QHBoxLayout(filterGroup);
    filterLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    filterLayout->setSpacing(kMemoryToolbarGroupSpacing);
    m_owner->m_window->m_memoryBandFilter = new QComboBox(panel);
    m_owner->m_window->m_memoryBandFilter->setObjectName(QStringLiteral("memoryManagerBandFilter"));
    m_owner->m_window->m_memoryBandFilter->addItem(QStringLiteral("All"), QString());
    for (const availableBands band : sdr9700::kRadioUiBandOrder)
    {
        const QString label = sdr9700::radioBandShortLabel(band);
        m_owner->m_window->m_memoryBandFilter->addItem(label, label);
    }
    if (m_showSpecialMemories)
    {
        m_owner->m_window->m_memoryBandFilter->addItem(QStringLiteral("Special"), QStringLiteral("special"));
    }
    if (m_showSatelliteMemories)
    {
        m_owner->m_window->m_memoryBandFilter->addItem(QStringLiteral("Satellite"), QStringLiteral("satellite"));
    }
    filterLayout->addWidget(m_owner->m_window->m_memoryBandFilter);
    toolbar->addWidget(filterGroup);
    toolbar->addStretch(1);

    auto* syncGroup = new QGroupBox(panel);
    syncGroup->setStyleSheet(toolbarGroupStyle);
    auto* syncLayout = new QHBoxLayout(syncGroup);
    syncLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    syncLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* syncButton = new QPushButton("Sync", panel);
    syncButton->setToolTip("Immediately read radio memories into SDR9700.");
    m_owner->m_window->m_memoryBandFilter->setFixedHeight(syncButton->sizeHint().height());
    syncLayout->addWidget(syncButton);
    toolbar->addWidget(syncGroup);
    toolbar->addStretch(1);

    auto* reorderGroup = new QGroupBox(panel);
    reorderGroup->setStyleSheet(toolbarGroupStyle);
    auto* reorderLayout = new QHBoxLayout(reorderGroup);
    reorderLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    reorderLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* upButton = new QPushButton("Up", panel);
    auto* downButton = new QPushButton("Down", panel);
    reorderLayout->addWidget(upButton);
    reorderLayout->addWidget(downButton);
    toolbar->addWidget(reorderGroup);
    toolbar->addStretch(1);

    auto* memoryGroup = new QGroupBox(panel);
    memoryGroup->setStyleSheet(toolbarGroupStyle);
    auto* memoryLayout = new QHBoxLayout(memoryGroup);
    memoryLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    memoryLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* addButton = new QPushButton("Add", panel);
    auto* editButton = new QPushButton("Edit", panel);
    auto* copyButton = new QPushButton("Copy", panel);
    auto* removeButton = new QPushButton("Remove", panel);
    memoryLayout->addWidget(addButton);
    memoryLayout->addWidget(editButton);
    memoryLayout->addWidget(copyButton);
    memoryLayout->addWidget(removeButton);
    toolbar->addWidget(memoryGroup);
    toolbar->addStretch(1);

    auto* transferGroup = new QGroupBox(panel);
    transferGroup->setStyleSheet(toolbarGroupStyle);
    auto* transferLayout = new QHBoxLayout(transferGroup);
    transferLayout->setContentsMargins(kMemoryToolbarGroupMargins);
    transferLayout->setSpacing(kMemoryToolbarGroupSpacing);
    auto* importButton = new QPushButton("Import", panel);
    auto* exportButton = new QPushButton("Export", panel);
    transferLayout->addWidget(importButton);
    transferLayout->addWidget(exportButton);
    toolbar->addWidget(transferGroup);
    leftRoot->addLayout(toolbar);

    m_owner->m_window->m_memoryTable = new QTableWidget(panel);
    m_owner->m_window->m_memoryTable->setObjectName(QStringLiteral("memoryManagerTable"));
    m_owner->m_window->m_memoryTable->setColumnCount(kMemoryTableColumnCount);
    m_owner->m_window->m_memoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("Band"), QStringLiteral("Channel"), QStringLiteral("Frequency"), QStringLiteral("Offset"),
         QStringLiteral("Mode"), QStringLiteral("Tone (TX/RX)"), QStringLiteral("ID")});
    m_owner->m_window->m_memoryTable->setColumnHidden(kMemoryIdColumn, true);
    m_owner->m_window->m_memoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_owner->m_window->m_memoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_owner->m_window->m_memoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_owner->m_window->m_memoryTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_owner->m_window->m_memoryTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_owner->m_window->m_memoryTable->setSortingEnabled(false);
    m_owner->m_window->m_memoryTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_owner->m_window->m_memoryTable->setShowGrid(true);
    m_owner->m_window->m_memoryTable->setGridStyle(Qt::SolidLine);
    m_owner->m_window->m_memoryTable->setAlternatingRowColors(true);
    m_owner->m_window->m_memoryTable->setStyleSheet(UiTheme::tableStyle(QStringLiteral("memoryManagerTable")));
    m_owner->m_window->m_memoryTable->verticalHeader()->setVisible(false);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setFixedHeight(32);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionsClickable(false);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setStretchLastSection(false);
    for (int column = kMemoryBandColumn; column <= kMemoryToneColumn; ++column)
    {
        m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
    }
    m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryChannelColumn,
                                                                               QHeaderView::Stretch);
    m_owner->m_window->m_memoryTable->horizontalHeader()->setSectionResizeMode(kMemoryIdColumn, QHeaderView::Fixed);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryBandColumn, kMemoryBandColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryChannelColumn, kMemoryChannelColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryFrequencyColumn, kMemoryFrequencyColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryDuplexColumn, kMemoryDuplexColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryModeColumn, kMemoryModeColumnWidth);
    m_owner->m_window->m_memoryTable->setColumnWidth(kMemoryToneColumn, kMemoryToneColumnWidth);
    m_owner->m_window->m_memoryTable->setItemDelegateForColumn(kMemoryToneColumn,
                                                               new ToneCellDelegate(m_owner->m_window->m_memoryTable));
    m_owner->m_window->m_memoryTable->setItemDelegateForColumn(
        kMemoryChannelColumn, new BracketAlignedItemDelegate(m_owner->m_window->m_memoryTable));
    leftRoot->addWidget(m_owner->m_window->m_memoryTable, 1);

    auto* footer = new QWidget(panel);
    footer->setObjectName(QStringLiteral("memoryManagerFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 2, 0, 2);
    footerLayout->setSpacing(12);
    m_owner->m_window->m_memoryCountLabel = new QLabel(footer);
    m_owner->m_window->m_memoryCountLabel->setObjectName(QStringLiteral("memoryManagerStatusLabel"));
    m_owner->m_window->m_memoryCountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_owner->m_window->m_memoryCountLabel->setContentsMargins(kMemoryFooterTextLeftPadding, 0, 0, 0);
    m_owner->m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
    footer->setMinimumHeight(m_owner->m_window->m_memoryCountLabel->sizeHint().height() + 4);
    m_owner->m_window->m_memoryProgressBar = new QProgressBar(footer);
    m_owner->m_window->m_memoryProgressBar->setObjectName(QStringLiteral("memoryManagerProgressBar"));
    m_owner->m_window->m_memoryProgressBar->setFixedWidth(220);
    m_owner->m_window->m_memoryProgressBar->setTextVisible(false);
    m_owner->m_window->m_memoryProgressBar->setVisible(false);
    m_owner->m_window->m_memoryProgressBar->setStyleSheet(
        QStringLiteral("QProgressBar { background: %1; border: 1px solid %2; border-radius: 3px; height: 8px; }"
                       "QProgressBar::chunk { background: %3; border-radius: 2px; }")
            .arg(UiTheme::Color::Field, UiTheme::Color::BorderMedium, UiTheme::Color::Accent));
    footerLayout->addWidget(m_owner->m_window->m_memoryCountLabel, 1);
    footerLayout->addWidget(m_owner->m_window->m_memoryProgressBar);
    leftRoot->addWidget(footer);

    root->addWidget(leftPane, 1);

    auto* windowLayout = new QVBoxLayout(m_owner->m_window->m_memoryWindow);
    windowLayout->setContentsMargins(kNoMargins);
    windowLayout->setSpacing(0);
    auto* titleBar =
        new sdr9700::ui::UtilityTitleBar(QStringLiteral("Memory Manager"), m_owner->m_window->m_memoryWindow);
    connect(titleBar->closeButton(), &QPushButton::clicked, m_owner->m_window->m_memoryWindow, &QWidget::hide);
    windowLayout->addWidget(titleBar);
    windowLayout->addWidget(panel, 1);

    connect(m_owner->m_window->m_memoryBandFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), m_owner,
            &MemoryController::reloadMemoryTable);
    connect(m_owner->m_window->m_memoryTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int)
            {
                const QTableWidgetItem* idItem = m_owner->m_window->m_memoryTable->item(row, kMemoryIdColumn);
                if (idItem)
                {
                    m_owner->selectMemoryById(idItem->text(), true);
                }
            });
    connect(syncButton, &QPushButton::clicked, m_owner, &MemoryController::forceRadioMemorySync);
    connect(upButton, &QPushButton::clicked, m_owner, &MemoryController::moveSelectedMemoryUp);
    connect(downButton, &QPushButton::clicked, m_owner, &MemoryController::moveSelectedMemoryDown);
    connect(addButton, &QPushButton::clicked, m_owner, &MemoryController::storeCurrentMemory);
    connect(editButton, &QPushButton::clicked, m_owner, &MemoryController::editSelectedMemory);
    connect(copyButton, &QPushButton::clicked, m_owner, &MemoryController::copySelectedMemory);
    connect(removeButton, &QPushButton::clicked, m_owner, &MemoryController::removeSelectedMemory);
    connect(importButton, &QPushButton::clicked, m_owner, &MemoryController::importRadioMemories);
    connect(exportButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_owner->exportRadioMemories())
                {
                    m_owner->m_window->showStatusMessage(QStringLiteral("Memories exported"));
                }
            });

    m_owner->reloadMemoryTable();
}


void MemoryViewController::showMemoryWindow()
{
    if (!m_owner->m_window->m_memoryWindow)
    {
        return;
    }
    m_owner->reloadMemoryTable();
    static_cast<sdr9700::ui::UtilityWindow*>(m_owner->m_window->m_memoryWindow)->showCentered();
}


void MemoryViewController::scheduleRebuild()
{
    if (!m_owner->m_memorySyncController->refreshInProgress())
    {
        rebuild();
        return;
    }

    if (!m_refreshTimer->isActive())
    {
        m_refreshTimer->start();
    }
}


void MemoryViewController::updateTableInteraction()
{
    if (!m_owner->m_window->m_memoryTable)
    {
        return;
    }

    const bool locked = m_owner->m_memorySyncController->refreshInProgress() || !m_progressLabel.isEmpty();
    m_owner->m_window->m_memoryTable->setSelectionMode(locked ? QAbstractItemView::NoSelection
                                                              : QAbstractItemView::SingleSelection);
    m_owner->m_window->m_memoryTable->setFocusPolicy(locked ? Qt::NoFocus : Qt::StrongFocus);
    m_owner->m_window->m_memoryTable->setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    if (QWidget* viewport = m_owner->m_window->m_memoryTable->viewport())
    {
        viewport->setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    }
}


void MemoryViewController::setProgress(const QString& label, int value, int maximum)
{
    m_progressLabel = label;
    m_progressValue = qBound(0, value, maximum);
    m_progressMaximum = qMax(0, maximum);
    if (m_owner->m_window->m_memoryCountLabel)
    {
        m_owner->m_window->m_memoryCountLabel->setText(
            QStringLiteral("%1 (%2/%3)").arg(m_progressLabel).arg(m_progressValue).arg(m_progressMaximum));
    }
    if (m_owner->m_window->m_memoryProgressBar)
    {
        m_owner->m_window->m_memoryProgressBar->setRange(0, m_progressMaximum);
        m_owner->m_window->m_memoryProgressBar->setValue(m_progressValue);
        m_owner->m_window->m_memoryProgressBar->setVisible(m_progressMaximum > 0);
    }
    updateTableInteraction();
}


void MemoryViewController::clearProgress()
{
    m_progressLabel.clear();
    m_progressValue = 0;
    m_progressMaximum = 0;
    if (m_owner->m_window->m_memoryProgressBar)
    {
        m_owner->m_window->m_memoryProgressBar->setVisible(false);
        m_owner->m_window->m_memoryProgressBar->setValue(0);
    }
    updateTableInteraction();
}


void MemoryViewController::rebuild()
{
    const QVector<MemoryRecord> memories = m_owner->currentMemories();
    if (!m_owner->m_window->m_memoryTable)
    {
        return;
    }
    updateTableInteraction();

    const QString bandFilter = m_owner->m_window->m_memoryBandFilter
                                   ? m_owner->m_window->m_memoryBandFilter->currentData().toString()
                                   : QString();
    m_owner->m_window->m_memoryTable->setSortingEnabled(false);
    m_owner->m_window->m_memoryTable->setRowCount(0);
    int visibleCount = 0;
    int availableCount = 0;
    int availableVerifiedCount = 0;
    for (const MemoryRecord& memory : memories)
    {
        const bool satelliteMemory = memory.group == kRadioMemorySatelliteGroup;
        const bool specialMemory = !satelliteMemory && memory.channel >= kRadioMemoryFirstSpecialChannel;
        if ((specialMemory && !m_showSpecialMemories) || (satelliteMemory && !m_showSatelliteMemories))
        {
            continue;
        }
        ++availableCount;
        if (memory.verifiedThisSession)
        {
            ++availableVerifiedCount;
        }
        const bool specialFilter = bandFilter == QLatin1String("special");
        const bool satelliteFilter = bandFilter == QLatin1String("satellite");
        const bool matchesFilter = bandFilter.isEmpty() ||
                                   (specialFilter && memory.group != kRadioMemorySatelliteGroup &&
                                    memory.channel >= kRadioMemoryFirstSpecialChannel) ||
                                   (satelliteFilter && memory.group == kRadioMemorySatelliteGroup) ||
                                   (!specialFilter && !satelliteFilter && memory.band == bandFilter);
        if (!matchesFilter)
        {
            continue;
        }

        const int row = m_owner->m_window->m_memoryTable->rowCount();
        m_owner->m_window->m_memoryTable->insertRow(row);

        auto setItem = [this, row, &memory](int column, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            item->setData(kMemoryVerifiedThisSessionRole, memory.verifiedThisSession);
            item->setData(kMemoryReadOnlyRole, memory.readOnly);
            if (!memory.verifiedThisSession)
            {
                // Do not use QPalette::PlaceholderText here. On macOS that
                // role can carry reduced alpha intended for text fields and
                // becomes effectively invisible against SDR9700's explicitly
                // styled dark table. Cached rows must look secondary without
                // ever looking empty.
                item->setForeground(QColor(QLatin1String(UiTheme::Color::TextMuted)));
                item->setToolTip(QStringLiteral("Last known value from the local cache; this radio session has not "
                                                "verified the slot yet."));
            }
            if (memory.readOnly)
            {
                item->setToolTip(QStringLiteral("This IC-9700 special or satellite memory is synchronized for "
                                                "reference and is read-only in this release."));
            }
            m_owner->m_window->m_memoryTable->setItem(row, column, item);
            return item;
        };

        auto* bandItem = setItem(kMemoryBandColumn, radioMemoryBandLabel(memory.group));
        bandItem->setTextAlignment(Qt::AlignCenter);
        auto* channelItem =
            setItem(kMemoryChannelColumn,
                    QStringLiteral("%1 [%2]").arg(radioMemorySlotLabel(memory.group, memory.channel), memory.name));
        channelItem->setData(Qt::UserRole, memory.channel);
        auto* frequencyItem = setItem(kMemoryFrequencyColumn, memoryFrequencyLabel(memory.receiveHz));
        frequencyItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(memory.receiveHz));
        frequencyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setItem(kMemoryDuplexColumn, memory.shift)->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryModeColumn, memoryModeLabel(memory.mode))->setTextAlignment(Qt::AlignCenter);
        auto* toneItem = setItem(kMemoryToneColumn, memoryToneTableLabel(memory));
        toneItem->setData(kMemoryToneTypeRole, memoryToneTypeLabel(memory));
        toneItem->setData(kMemoryToneRxRole, memoryToneRxLabel(memory));
        toneItem->setData(kMemoryToneTxRole, memoryToneTxLabel(memory));
        if (memory.verifiedThisSession)
        {
            toneItem->setToolTip(toneItem->text());
        }
        toneItem->setTextAlignment(Qt::AlignCenter);
        setItem(kMemoryIdColumn, memory.id);
        ++visibleCount;
    }

    if (m_owner->m_window->m_memoryCountLabel)
    {
        m_owner->m_window->m_memoryCountLabel->setStyleSheet("QLabel { color: palette(mid); }");
        const int totalCount = availableCount;
        if (m_owner->m_memorySyncController->refreshInProgress())
        {
            if (m_progressLabel.isEmpty())
            {
                setProgress(QStringLiteral("Syncing memories"), 0, kRadioMemorySyncTotal);
            }
            else
            {
                setProgress(m_progressLabel, m_progressValue, m_progressMaximum);
            }
            return;
        }
        if (!m_progressLabel.isEmpty())
        {
            setProgress(m_progressLabel, m_progressValue, m_progressMaximum);
            return;
        }
        clearProgress();
        const int verifiedCount = availableVerifiedCount;
        const int cachedCount = totalCount - verifiedCount;
        const int unansweredSlotCount = m_owner->m_memorySyncController->lastUnansweredSlotCount();
        if (bandFilter.isEmpty())
        {
            if (cachedCount > 0 || unansweredSlotCount > 0)
            {
                m_owner->m_window->m_memoryCountLabel->setText(
                    QStringLiteral("%1 total (%2 verified, %3 cached; %4 %5 unanswered)")
                        .arg(totalCount)
                        .arg(verifiedCount)
                        .arg(cachedCount)
                        .arg(unansweredSlotCount)
                        .arg(unansweredSlotCount == 1 ? QStringLiteral("slot") : QStringLiteral("slots")));
            }
            else
            {
                m_owner->m_window->m_memoryCountLabel->setText(
                    QStringLiteral("%1 %2")
                        .arg(totalCount)
                        .arg(totalCount == 1 ? QStringLiteral("memory") : QStringLiteral("memories")));
            }
        }
        else
        {
            m_owner->m_window->m_memoryCountLabel->setText(
                QStringLiteral("%1 filtered / %2 total").arg(visibleCount).arg(totalCount));
        }
    }
}
