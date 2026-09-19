#include "SettingsDialog.h"
#include "DialogFooter.h"
#include "ApplicationConfigurationSettingsPanel.h"
#include "AudioDevicesSettingsPanel.h"
#include "SpectrumScopeSettingsPanel.h"
#include "MemoryManagerSettingsPanel.h"
#include "UiTheme.h"
#ifdef HAVE_HIDAPI
#include "IcomRC28SettingsPanel.h"
#endif

#include <algorithm>
#include <QAbstractItemView>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSize>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
constexpr int kSettingsPageScrollbarGutter = 10;

int pageKey(SettingsDialog::Page page)
{
    return static_cast<int>(page);
}

class CurrentPageStackedWidget : public QStackedWidget
{
  public:
    using QStackedWidget::QStackedWidget;

    QSize sizeHint() const override
    {
        return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint();
    }
};

class SettingsNavigationTree : public QTreeWidget
{
  public:
    using QTreeWidget::QTreeWidget;

  protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QTreeWidgetItem* item = itemAt(event->position().toPoint());
        if (item && !item->parent())
        {
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        QTreeWidgetItem* item = itemAt(event->position().toPoint());
        if (item && !item->parent())
        {
            return;
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }

    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override
    {
        if (!index.isValid())
        {
            return;
        }

        constexpr int kArrowHalfWidth = 5;
        const int branchCenterX = rect.right() - indentation() / 2;
        const int rowCenterY = rect.center().y();
        const QPen linePen(QColor(UiTheme::Color::BorderMedium), 1.0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(linePen);

        const QModelIndex parent = index.parent();
        const int siblingCount = model()->rowCount(parent);
        if (index.row() > 0 || parent.isValid())
        {
            painter->drawLine(branchCenterX, rect.top(), branchCenterX, rowCenterY);
        }
        if (index.row() + 1 < siblingCount)
        {
            painter->drawLine(branchCenterX, rowCenterY, branchCenterX, rect.bottom());
        }
        if (parent.isValid())
        {
            painter->drawLine(branchCenterX, rowCenterY, rect.right(), rowCenterY);

            QModelIndex ancestor = parent;
            int ancestorX = branchCenterX - indentation();
            while (ancestor.isValid())
            {
                const QModelIndex ancestorParent = ancestor.parent();
                if (ancestor.row() + 1 < model()->rowCount(ancestorParent))
                {
                    painter->drawLine(ancestorX, rect.top(), ancestorX, rect.bottom());
                }
                ancestor = ancestorParent;
                ancestorX -= indentation();
            }
        }

        if (model()->hasChildren(index) && itemsExpandable())
        {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(UiTheme::Color::TextMuted));
            QPolygon arrow;
            if (isExpanded(index))
            {
                arrow << QPoint(branchCenterX - kArrowHalfWidth, rowCenterY - 3)
                      << QPoint(branchCenterX + kArrowHalfWidth, rowCenterY - 3)
                      << QPoint(branchCenterX, rowCenterY + 4);
            }
            else
            {
                arrow << QPoint(branchCenterX - 3, rowCenterY - kArrowHalfWidth)
                      << QPoint(branchCenterX - 3, rowCenterY + kArrowHalfWidth)
                      << QPoint(branchCenterX + 4, rowCenterY);
            }
            painter->drawPolygon(arrow);
        }
        painter->restore();
    }
};

class SettingsNavigationDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem itemOption(option);
        const bool selected = itemOption.state.testFlag(QStyle::State_Selected);
        if (selected)
        {
            const QString text = index.data(Qt::DisplayRole).toString();
            const int highlightWidth = itemOption.fontMetrics.horizontalAdvance(text) + 6;
            const QRect highlightRect(itemOption.rect.left(), itemOption.rect.top(),
                                      std::min(highlightWidth, itemOption.rect.width()), itemOption.rect.height());
            painter->fillRect(highlightRect, QColor(UiTheme::Color::AccentDark));
            itemOption.state.setFlag(QStyle::State_Selected, false);
            itemOption.state.setFlag(QStyle::State_HasFocus, false);
            itemOption.palette.setColor(QPalette::Text, QColor(UiTheme::Color::TextBright));
        }
        itemOption.state.setFlag(QStyle::State_MouseOver, false);
        QStyledItemDelegate::paint(painter, itemOption, index);
    }
};
} // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : SettingsDialog(Page::AudioDevices, parent) {}

#ifdef HAVE_HIDAPI
SettingsDialog::SettingsDialog(Page page, QWidget* parent, IcomRC28Manager* icomRC28Manager)
    : sdr9700::ui::UtilityWindow(QStringLiteral("Settings"), parent), m_icomRC28Manager(icomRC28Manager)
#else
SettingsDialog::SettingsDialog(Page page, QWidget* parent)
    : sdr9700::ui::UtilityWindow(QStringLiteral("Settings"), parent)
#endif
{
    const QString title = QStringLiteral("Settings");
    // Keep every page at the same size. The popup placement helper calls
    // adjustSize() before showing, so an initial resize alone can shrink to
    // the first page's hint and clip wider pages selected later.
    setFixedSize(780, 520);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(title, this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QWidget::close);
    root->addWidget(titleBar);
    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(UiTheme::Size::DialogContentMargin, 12, UiTheme::Size::DialogContentMargin, 0);
    contentLayout->setSpacing(sdr9700::ui::kDialogFooterSpacing);

    auto* search = new QLineEdit(content);
    search->setObjectName(QStringLiteral("settingsSearch"));
    search->setPlaceholderText(QStringLiteral("Search settings (%1)")
                                   .arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(QStringLiteral("Search settings"));
    search->setMinimumHeight(34);
    search->setStyleSheet(
        QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; color: %3; padding: 0 8px; }"
                       "QLineEdit:focus { border-color: %4; }")
            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::BorderFocus),
                 QLatin1String(UiTheme::Color::TextField), QLatin1String(UiTheme::Color::Accent)));
    QPalette searchPalette = search->palette();
    searchPalette.setColor(QPalette::PlaceholderText, QColor(UiTheme::Color::TextStatusSecondary));
    search->setPalette(searchPalette);
    contentLayout->addWidget(search);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);

    m_navigation = new SettingsNavigationTree(content);
    m_navigation->setObjectName(QStringLiteral("settingsNavigation"));
    m_navigation->setHeaderHidden(true);
    m_navigation->setRootIsDecorated(true);
    m_navigation->setItemsExpandable(false);
    m_navigation->setExpandsOnDoubleClick(false);
    m_navigation->setIndentation(20);
    m_navigation->setMinimumWidth(190);
    m_navigation->setMaximumWidth(245);
    m_navigation->setUniformRowHeights(false);
    m_navigation->setItemDelegate(new SettingsNavigationDelegate(m_navigation));
    m_navigation->setAccessibleName(QStringLiteral("Settings pages"));
    m_navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_navigation->setStyleSheet(
        QStringLiteral("QTreeWidget#settingsNavigation { background: %1; border: 1px solid %2; color: %3;"
                       " show-decoration-selected: 0; }"
                       "QTreeWidget#settingsNavigation::item { padding: 1px 4px 1px 0; border: none; }"
                       "QTreeWidget#settingsNavigation::item:hover { background: transparent; }"
                       "QTreeWidget#settingsNavigation::item:selected { background: transparent; color: %4; }"
                       "QTreeWidget#settingsNavigation::branch { background: transparent; }")
            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextPrimary), QLatin1String(UiTheme::Color::TextBright)));
    body->addWidget(m_navigation);

    auto* divider = new QWidget(content);
    divider->setFixedWidth(1);
    divider->setStyleSheet(QStringLiteral("background: %1;").arg(QLatin1String(UiTheme::Color::Border)));
    body->addWidget(divider);

    auto* pageHost = new QWidget(content);
    pageHost->setObjectName(QStringLiteral("settingsPageHost"));
    pageHost->setAutoFillBackground(true);
    QPalette pagePalette = pageHost->palette();
    pagePalette.setColor(QPalette::Window, QColor(UiTheme::Color::ContentBackground));
    pageHost->setPalette(pagePalette);
    m_pageLayout = new QVBoxLayout(pageHost);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(8);
    m_pageTitle = new QLabel(pageHost);
    m_pageTitle->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 16px; font-weight: bold; }")
                                   .arg(QLatin1String(UiTheme::Color::TextPrimary)));
    m_pageLayout->addWidget(m_pageTitle);
    m_pages = new CurrentPageStackedWidget(pageHost);
    m_pages->setObjectName(QStringLiteral("settingsPages"));
    m_pages->setAutoFillBackground(false);
    m_pages->setStyleSheet(QStringLiteral("QStackedWidget#settingsPages { background: transparent; border: none; }"));
    connect(m_pages, &QStackedWidget::currentChanged, m_pages, &QWidget::updateGeometry);
    connect(m_pages, &QStackedWidget::currentChanged, this,
            [this]()
            {
                updatePageScrollGutter();
                QTimer::singleShot(0, this, &SettingsDialog::updatePageScrollGutter);
            });
    m_pageLayout->addWidget(m_pages, 1);

    m_pageScroll = new QScrollArea(content);
    m_pageScroll->setWidgetResizable(true);
    m_pageScroll->setFrameShape(QFrame::NoFrame);
    m_pageScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pageScroll->viewport()->setAutoFillBackground(true);
    m_pageScroll->viewport()->setPalette(pagePalette);
    connect(m_pageScroll->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int) { updatePageScrollGutter(); });
    m_pageScroll->setWidget(pageHost);
    body->addWidget(m_pageScroll, 1);
    contentLayout->addLayout(body, 1);

#ifdef HAVE_HIDAPI
    QTreeWidgetItem* accessoriesCategory =
        addCategory(QStringLiteral("ACCESSORIES"), QStringLiteral("Configure supported radio-control accessories."),
                    QStringLiteral("accessories hardware controller remote encoder"));
    addPage(accessoriesCategory, Page::IcomRC28, QStringLiteral("Icom RC-28 Remote Encoder"),
            QStringLiteral("Configure RC-28 tuning and F1, F2, and PTT button assignments."),
            QStringLiteral("hardware icom icomRC28 rc-28 remote encoder controller knob button f1 f2 ptt mapping"),
            [this]()
            {
                auto* panel = new IcomRC28SettingsPanel(m_icomRC28Manager);
                connect(panel, &IcomRC28SettingsPanel::encoderSettingsChanged, this,
                        &SettingsDialog::icomRC28EncoderSettingsChanged);
                return panel;
            });
#endif
    QTreeWidgetItem* applicationCategory =
        addCategory(QStringLiteral("APPLICATION"),
                    QStringLiteral("Configure SDR9700 behavior, memory synchronization, and display appearance."),
                    QStringLiteral("application configuration backup restore reset memory manager spectrum scope"));
    addPage(applicationCategory, Page::ApplicationConfiguration, QStringLiteral("Configuration"),
            QStringLiteral("Back up, restore, or reset SDR9700 configuration."),
            QStringLiteral("application configuration backup restore reset settings"),
            [] { return new ApplicationConfigurationSettingsPanel; });
    addPage(applicationCategory, Page::MemoryManager, QStringLiteral("Memory Manager"),
            QStringLiteral("Configure radio synchronization and which IC-9700 memory categories are displayed."),
            QStringLiteral("application memory manager memories radio sync poll polling interval refresh special "
                           "scan edge call satellite visibility show hide"),
            [this]()
            {
                auto* panel = new MemoryManagerSettingsPanel;
                connect(panel, &MemoryManagerSettingsPanel::pollIntervalSecondsChanged, this,
                        &SettingsDialog::memoryPollIntervalSecondsChanged);
                connect(panel, &MemoryManagerSettingsPanel::showSpecialMemoriesChanged, this,
                        &SettingsDialog::memoryShowSpecialMemoriesChanged);
                connect(panel, &MemoryManagerSettingsPanel::showSatelliteMemoriesChanged, this,
                        &SettingsDialog::memoryShowSatelliteMemoriesChanged);
                return panel;
            });
    addPage(applicationCategory, Page::SpectrumScope, QStringLiteral("Spectrum Scope"),
            QStringLiteral("Customize spectrum colors, grid density, and mouse-wheel tuning."),
            QStringLiteral("application appearance display visual spectrum scope center line color vfo marker "
                           "background gridlines grid density fewer normal more"),
            [this]()
            {
                auto* panel = new SpectrumScopeSettingsPanel;
                connect(panel, &SpectrumScopeSettingsPanel::centerLineColorChanged, this,
                        &SettingsDialog::spectrumScopeCenterLineColorChanged);
                connect(panel, &SpectrumScopeSettingsPanel::backgroundColorChanged, this,
                        &SettingsDialog::spectrumScopeBackgroundColorChanged);
                connect(panel, &SpectrumScopeSettingsPanel::gridLineColorChanged, this,
                        &SettingsDialog::spectrumScopeGridLineColorChanged);
                connect(panel, &SpectrumScopeSettingsPanel::gridDensityChanged, this,
                        &SettingsDialog::spectrumScopeGridDensityChanged);
                connect(panel, &SpectrumScopeSettingsPanel::reverseMouseWheelTuningChanged, this,
                        &SettingsDialog::reverseMouseWheelTuningChanged);
                return panel;
            });

    QTreeWidgetItem* audioCategory = addCategory(QStringLiteral("RECEIVE & TRANSMIT"),
                                                 QStringLiteral("Configure computer audio used with the radio."),
                                                 QStringLiteral("receive transmit audio input output computer radio"));
    addPage(audioCategory, Page::AudioDevices, QStringLiteral("Audio Devices"),
            QStringLiteral("Choose audio devices for radio receive and transmit."),
            QStringLiteral("audio input output device microphone speaker codec channels receive transmit playback"),
            [this]()
            {
                auto* panel = new AudioDevicesSettingsPanel;
                m_audioDevicesPanel = panel;
                panel->setAudioConnectionState(m_audioConnected, m_activeOutputChannels);
                connect(panel, &AudioDevicesSettingsPanel::audioSettingsChanged, this,
                        &SettingsDialog::audioSettingsChanged);
                return panel;
            });
    for (int i = 0; i < m_navigation->topLevelItemCount(); ++i)
    {
        m_navigation->topLevelItem(i)->sortChildren(0, Qt::AscendingOrder);
    }
    m_navigation->sortItems(0, Qt::AscendingOrder);
    m_navigation->expandAll();

    connect(m_navigation, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current)
            {
                if (!current)
                {
                    return;
                }

                if (!current->data(0, Qt::UserRole).isValid())
                {
                    if (current->childCount() > 0)
                    {
                        m_navigation->setCurrentItem(current->child(0));
                    }
                    return;
                }

                const auto selectedPage = static_cast<Page>(current->data(0, Qt::UserRole).toInt());
                buildDeferredPage(selectedPage);
                m_pages->setCurrentIndex(m_pageIndexes.value(pageKey(selectedPage)));
                m_pageTitle->setText(current->text(0));
                if (m_pageScroll)
                {
                    QTimer::singleShot(0, this,
                                       [this]()
                                       {
                                           if (m_pageScroll)
                                           {
                                               m_pageScroll->verticalScrollBar()->setValue(0);
                                               m_pageScroll->horizontalScrollBar()->setValue(0);
                                           }
                                       });
                }
            });
    connect(search, &QLineEdit::textChanged, this, &SettingsDialog::filterNavigation);

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, search,
            [search]()
            {
                search->setFocus();
                search->selectAll();
            });

    selectPage(page);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(content);
    footer.buttonBox->addButton(QDialogButtonBox::Close);
    contentLayout->addWidget(footer.widget);
    connect(footer.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);

    root->addWidget(content, 1);
}

QTreeWidgetItem* SettingsDialog::addCategory(const QString& title, const QString& tooltip, const QString& keywords)
{
    auto* item = new QTreeWidgetItem(m_navigation, {title});
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setData(0, Qt::UserRole + 1, keywords);
    item->setToolTip(0, tooltip);
    QFont font = item->font(0);
    font.setBold(true);
    if (font.pointSizeF() > 1.0)
    {
        font.setPointSizeF(font.pointSizeF() - 0.5);
    }
    item->setFont(0, font);
    item->setSizeHint(0, QSize(0, 30));
    return item;
}

void SettingsDialog::addPage(QTreeWidgetItem* parent, Page page, const QString& title, const QString& tooltip,
                             const QString& keywords, std::function<QWidget*()> builder)
{
    auto* placeholder = new QWidget;
    const int stackIndex = m_pages->addWidget(placeholder);
    const int key = pageKey(page);

    auto* item = parent ? new QTreeWidgetItem(parent, {title}) : new QTreeWidgetItem(m_navigation, {title});
    item->setData(0, Qt::UserRole, key);
    item->setData(0, Qt::UserRole + 1, keywords);
    item->setSizeHint(0, QSize(0, 26));
    item->setToolTip(0, tooltip);

    m_pageIndexes.insert(key, stackIndex);
    m_pageItems.insert(key, item);
    m_deferredBuilders.insert(key, std::move(builder));
}

void SettingsDialog::buildDeferredPage(Page page)
{
    const int key = pageKey(page);
    auto it = m_deferredBuilders.find(key);
    if (it == m_deferredBuilders.end())
    {
        return;
    }

    QWidget* placeholder = m_pages->widget(m_pageIndexes.value(key));
    auto* layout = new QVBoxLayout(placeholder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(it.value()(), 1);
    m_deferredBuilders.erase(it);
}

void SettingsDialog::filterNavigation(const QString& text)
{
    const QString needle = text.trimmed();
    QTreeWidgetItem* firstVisible = nullptr;

    for (int i = 0; i < m_navigation->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = m_navigation->topLevelItem(i);
        const bool selfVisible = needle.isEmpty() || itemSearchText(item).contains(needle, Qt::CaseInsensitive);
        bool childVisible = false;
        for (int childIndex = 0; childIndex < item->childCount(); ++childIndex)
        {
            QTreeWidgetItem* child = item->child(childIndex);
            const bool visible =
                selfVisible || needle.isEmpty() || itemSearchText(child).contains(needle, Qt::CaseInsensitive);
            child->setHidden(!visible);
            childVisible = childVisible || visible;
            if (visible && !firstVisible)
            {
                firstVisible = child;
            }
        }

        const bool visible = selfVisible || childVisible;
        item->setHidden(!visible);
        if (item->childCount() == 0 && visible && !firstVisible)
        {
            firstVisible = item;
        }
    }

    if (firstVisible && (!m_navigation->currentItem() || m_navigation->currentItem()->isHidden()))
    {
        m_navigation->setCurrentItem(firstVisible);
    }
}

void SettingsDialog::selectPage(Page page)
{
    QTreeWidgetItem* item = m_pageItems.value(pageKey(page), nullptr);
    if (!item)
    {
        return;
    }

    m_navigation->setCurrentItem(item);
    m_navigation->scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

void SettingsDialog::updatePageScrollGutter()
{
    if (!m_pageScroll || !m_pageLayout)
    {
        return;
    }

    const QScrollBar* verticalScrollBar = m_pageScroll->verticalScrollBar();
    const bool needsVerticalScroll = verticalScrollBar && verticalScrollBar->maximum() > verticalScrollBar->minimum();
    const int rightGutter = needsVerticalScroll ? kSettingsPageScrollbarGutter : 0;
    const QMargins margins = m_pageLayout->contentsMargins();
    if (margins.right() == rightGutter)
    {
        return;
    }

    m_pageLayout->setContentsMargins(margins.left(), margins.top(), rightGutter, margins.bottom());
}

QString SettingsDialog::itemSearchText(QTreeWidgetItem* item)
{
    return item ? item->text(0) + QLatin1Char(' ') + item->data(0, Qt::UserRole + 1).toString() : QString();
}

void SettingsDialog::setAudioConnectionState(bool connected, int activeOutputChannels)
{
    m_audioConnected = connected;
    m_activeOutputChannels = activeOutputChannels;
    if (m_audioDevicesPanel)
    {
        m_audioDevicesPanel->setAudioConnectionState(connected, activeOutputChannels);
    }
}
