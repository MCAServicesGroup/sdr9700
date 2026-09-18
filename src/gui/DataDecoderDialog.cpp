#include "DataDecoderDialog.h"
#include "UiTheme.h"

#include <QDateTime>
#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <utility>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QUrl>

namespace
{
constexpr QSize kWindowSize{1180, 620};
constexpr int kMaximumRows = 1000;
constexpr int kContentSpacing = 12;

QLabel* metricValue(QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("0"), parent);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        QStringLiteral("color: %1; font-weight: bold;").arg(QLatin1String(UiTheme::Color::TextBright)));
    return label;
}

void addMetric(QHBoxLayout* layout, const QString& name, QLabel* value, QWidget* parent)
{
    auto* metric = new QWidget(parent);
    auto* metricLayout = new QVBoxLayout(metric);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setSpacing(2);
    auto* label = new QLabel(name, metric);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::TextMuted)));
    value->setParent(metric);
    metricLayout->addWidget(label);
    metricLayout->addWidget(value);
    layout->addWidget(metric, 1, Qt::AlignVCenter);
}

QString callsignFromAddress(const QString& address)
{
    static const QRegularExpression callsignPattern(QStringLiteral("^[A-Z0-9]{1,2}[0-9][A-Z]{1,4}$"));
    QString base = address;
    if (base.endsWith(QLatin1Char('*')))
    {
        base.chop(1);
    }
    const qsizetype ssidSeparator = base.lastIndexOf(QLatin1Char('-'));
    if (ssidSeparator > 0)
    {
        base.truncate(ssidSeparator);
    }
    return callsignPattern.match(base).hasMatch() ? base : QString();
}

class CallsignDelegate final : public QStyledItemDelegate
{
  public:
    explicit CallsignDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem styledOption(option);
        initStyleOption(&styledOption, index);
        const QString callsign = callsignFromAddress(index.data().toString());
        if (callsign.isEmpty())
        {
            QStyledItemDelegate::paint(painter, styledOption, index);
            return;
        }

        const QString suffix = styledOption.text.mid(callsign.size());
        styledOption.text.clear();
        const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &styledOption, painter, option.widget);
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &styledOption, option.widget);
        QFont linkFont = option.font;
        linkFont.setUnderline(true);
        const QFontMetrics linkMetrics(linkFont);
        const QFontMetrics normalMetrics(option.font);
        const int baseline = textRect.top() + (textRect.height() - normalMetrics.height()) / 2 + normalMetrics.ascent();
        const QColor normalColor =
            option.palette.color((option.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
        painter->save();
        painter->setFont(linkFont);
        painter->setPen((option.state & QStyle::State_Selected) ? normalColor
                                                                : QColor(QLatin1String(UiTheme::Color::AccentBright)));
        painter->drawText(textRect.left(), baseline, callsign);
        painter->setFont(option.font);
        painter->setPen(normalColor);
        painter->drawText(textRect.left() + linkMetrics.horizontalAdvance(callsign), baseline, suffix);
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override
    {
        if (event->type() != QEvent::MouseButtonRelease || static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton)
        {
            return false;
        }
        const QString callsign = callsignFromAddress(index.data().toString());
        if (callsign.isEmpty())
        {
            return false;
        }
        QStyleOptionViewItem styledOption(option);
        initStyleOption(&styledOption, index);
        const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        QRect linkRect = style->subElementRect(QStyle::SE_ItemViewItemText, &styledOption, option.widget);
        linkRect.setWidth(QFontMetrics(styledOption.font).horizontalAdvance(callsign));
        if (!linkRect.contains(static_cast<QMouseEvent*>(event)->position().toPoint()))
        {
            return false;
        }
        return QDesktopServices::openUrl(QUrl(QStringLiteral("https://qrz.com/db/%1").arg(callsign)));
    }
};
} // namespace

void Ax25DecoderWorker::processAudio(const QByteArray& pcm, int sampleRate, int channelCount)
{
    const QVector<Ax25Frame> frames = m_decoder.processPcm16(pcm, sampleRate, channelCount);
    if (!frames.isEmpty())
    {
        emit framesDecoded(frames);
    }
    emit statsChanged(m_decoder.stats());
}

void Ax25DecoderWorker::reset()
{
    m_decoder.reset();
    emit statsChanged(m_decoder.stats());
}

DataDecoderDialog::DataDecoderDialog(QWidget* parent)
    : sdr9700::ui::UtilityWindow(QStringLiteral("Data Decoder"), parent)
{
    qRegisterMetaType<Ax25Frame>();
    qRegisterMetaType<QVector<Ax25Frame>>();
    qRegisterMetaType<Ax25DecoderStats>();
    setObjectName(QStringLiteral("dataDecoderWindow"));
    setMinimumSize(760, 480);
    resize(kWindowSize);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(QStringLiteral("Data Decoder"), this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QWidget::hide);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(UiTheme::Size::DialogContentMargin, 10, UiTheme::Size::DialogContentMargin,
                               UiTheme::Size::DialogContentMargin);
    layout->setSpacing(kContentSpacing);
    root->addWidget(content, 1);

    const QString summaryStyle =
        QStringLiteral("QGroupBox { border: 1px solid %1; border-radius: 3px; color: %2; margin-top: 8px; }"
                       "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }")
            .arg(QLatin1String(UiTheme::Color::BorderMedium), QLatin1String(UiTheme::Color::TextMuted));
    auto* summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(10);

    auto* statusGroup = new QGroupBox(QStringLiteral("Decoder Status"), content);
    statusGroup->setStyleSheet(summaryStyle);
    auto* statusLayout = new QHBoxLayout(statusGroup);
    statusLayout->setContentsMargins(16, 5, 10, 17);
    statusLayout->setSpacing(10);
    statusLayout->setAlignment(Qt::AlignVCenter);
    m_statusIndicator = new QLabel(statusGroup);
    m_statusIndicator->setFixedSize(14, 14);
    m_statusIndicator->setAccessibleName(QStringLiteral("Decoder status indicator"));
    m_statusText = new QLabel(QStringLiteral("Waiting…"), statusGroup);
    m_statusText->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiTheme::Color::TextPrimary)));
    statusLayout->addWidget(m_statusIndicator, 0, Qt::AlignVCenter);
    statusLayout->addWidget(m_statusText, 0, Qt::AlignVCenter);
    statusLayout->addStretch(1);
    summaryRow->addWidget(statusGroup, 2);

    auto* performanceGroup = new QGroupBox(QStringLiteral("Decode Performance"), content);
    performanceGroup->setStyleSheet(summaryStyle);
    auto* performanceLayout = new QHBoxLayout(performanceGroup);
    performanceLayout->setContentsMargins(10, 5, 10, 17);
    performanceLayout->setSpacing(16);
    performanceLayout->setAlignment(Qt::AlignVCenter);
    m_candidatesValue = metricValue(performanceGroup);
    m_decodedValue = metricValue(performanceGroup);
    m_rejectedValue = metricValue(performanceGroup);
    m_successValue = metricValue(performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Candidates"), m_candidatesValue, performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Decoded"), m_decodedValue, performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Rejected"), m_rejectedValue, performanceGroup);
    addMetric(performanceLayout, QStringLiteral("Decode Rate"), m_successValue, performanceGroup);
    summaryRow->addWidget(performanceGroup, 2);
    layout->addLayout(summaryRow);

    m_packetTable = new QTableWidget(content);
    m_packetTable->setObjectName(QStringLiteral("ax25PacketTable"));
    m_packetTable->setColumnCount(4);
    m_packetTable->setHorizontalHeaderLabels(
        {QStringLiteral("Timestamp"), QStringLiteral("Protocol"), QStringLiteral("Source"), QStringLiteral("Payload")});
    m_packetTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packetTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packetTable->setAlternatingRowColors(true);
    m_packetTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_packetTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_packetTable->setShowGrid(true);
    m_packetTable->setGridStyle(Qt::SolidLine);
    m_packetTable->verticalHeader()->hide();
    m_packetTable->verticalHeader()->setDefaultSectionSize(28);
    m_packetTable->setStyleSheet(UiTheme::tableStyle(QStringLiteral("ax25PacketTable")));
    m_packetTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_packetTable->horizontalHeader()->setFixedHeight(32);
    m_packetTable->horizontalHeader()->setSectionsClickable(false);
    m_packetTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    for (int column = 0; column < 3; ++column)
    {
        m_packetTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    m_packetTable->setColumnWidth(0, 140);
    m_packetTable->setColumnWidth(1, 140);
    m_packetTable->setColumnWidth(2, 125);
    m_packetTable->setItemDelegateForColumn(2, new CallsignDelegate(m_packetTable));
    layout->addWidget(m_packetTable, 1);

    auto* detailsGroup = new QGroupBox(QStringLiteral("Packet Details"), content);
    detailsGroup->setStyleSheet(summaryStyle);
    auto* detailsLayout = new QVBoxLayout(detailsGroup);
    detailsLayout->setContentsMargins(10, 5, 10, 10);
    m_packetDetails = new QPlainTextEdit(detailsGroup);
    m_packetDetails->setObjectName(QStringLiteral("dataDecoderPacketDetails"));
    m_packetDetails->setReadOnly(true);
    m_packetDetails->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_packetDetails->setPlaceholderText(QStringLiteral("Select a packet to view its details."));
    m_packetDetails->setAccessibleName(QStringLiteral("Packet details"));
    m_packetDetails->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_packetDetails->setFixedHeight(76);
    m_packetDetails->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 3px; "
                       "color: %3; padding: 5px; }")
            .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextField)));
    detailsLayout->addWidget(m_packetDetails);
    layout->addWidget(detailsGroup);

    auto* actionBox = new QDialogButtonBox(content);
    actionBox->setObjectName(QStringLiteral("dataDecoderActionBox"));
    m_pauseButton = actionBox->addButton(QStringLiteral("Pause"), QDialogButtonBox::ActionRole);
    auto* clearButton = actionBox->addButton(QStringLiteral("Clear"), QDialogButtonBox::ResetRole);
    auto* exportButton = actionBox->addButton(QStringLiteral("Export…"), QDialogButtonBox::ActionRole);
    layout->addWidget(actionBox);

    m_worker = new Ax25DecoderWorker;
    m_worker->moveToThread(&m_decoderThread);
    connect(&m_decoderThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &DataDecoderDialog::audioReceived, m_worker, &Ax25DecoderWorker::processAudio, Qt::QueuedConnection);
    connect(this, &DataDecoderDialog::resetDecoder, m_worker, &Ax25DecoderWorker::reset, Qt::QueuedConnection);
    connect(m_worker, &Ax25DecoderWorker::framesDecoded, this, &DataDecoderDialog::appendFrames);
    connect(m_worker, &Ax25DecoderWorker::statsChanged, this, &DataDecoderDialog::updateStats);
    m_decoderThread.setObjectName(QStringLiteral("AX25 decoder"));
    m_decoderThread.start();

    connect(m_pauseButton, &QPushButton::clicked, this,
            [this]()
            {
                m_paused = !m_paused;
                m_pauseButton->setText(m_paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
                if (!m_paused && !m_pausedFrames.isEmpty())
                {
                    const QVector<Ax25Frame> frames = std::exchange(m_pausedFrames, {});
                    appendFrames(frames);
                }
            });
    connect(clearButton, &QPushButton::clicked, this,
            [this]()
            {
                m_packetTable->setRowCount(0);
                m_packetDetails->clear();
                emit resetDecoder();
            });
    connect(exportButton, &QPushButton::clicked, this, &DataDecoderDialog::exportPackets);
    connect(m_packetTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow)
            {
                if (currentRow < 0)
                {
                    m_packetDetails->clear();
                    return;
                }
                QStringList values;
                for (int column = 0; column < m_packetTable->columnCount(); ++column)
                {
                    const QTableWidgetItem* item = m_packetTable->item(currentRow, column);
                    values.append(item ? item->data(Qt::UserRole).toString() : QString());
                }
                const QTableWidgetItem* metadataItem = m_packetTable->item(currentRow, 0);
                const QString destination = metadataItem->data(Qt::UserRole + 1).toString();
                const QString path = metadataItem->data(Qt::UserRole + 2).toString();
                const QString route = values.at(2) + QLatin1Char('>') + destination +
                                      (path.isEmpty() ? QString() : QLatin1Char(',') + path);
                m_packetDetails->setPlainText(route + QLatin1Char(':') + values.at(3));
            });
    updateStats({});
}

DataDecoderDialog::~DataDecoderDialog()
{
    m_decoderThread.quit();
    m_decoderThread.wait();
}

void DataDecoderDialog::processAudio(const QByteArray& pcm, int sampleRate, int channelCount)
{
    if (!isVisible())
    {
        return;
    }
    emit audioReceived(pcm, sampleRate, channelCount);
}

void DataDecoderDialog::hideEvent(QHideEvent* event)
{
    emit resetDecoder();
    sdr9700::ui::UtilityWindow::hideEvent(event);
}

void DataDecoderDialog::appendFrames(const QVector<Ax25Frame>& frames)
{
    if (m_paused)
    {
        m_pausedFrames.append(frames);
        if (m_pausedFrames.size() > kMaximumRows)
        {
            m_pausedFrames.remove(0, m_pausedFrames.size() - kMaximumRows);
        }
        return;
    }
    for (const Ax25Frame& frame : frames)
    {
        while (m_packetTable->rowCount() >= kMaximumRows)
        {
            m_packetTable->removeRow(0);
        }
        const int row = m_packetTable->rowCount();
        m_packetTable->insertRow(row);
        const QStringList values{frame.receivedAt.toString(QStringLiteral("HH:mm:ss.zzz")), frame.protocol,
                                 frame.source, frame.payload};
        for (int column = 0; column < values.size(); ++column)
        {
            auto* item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, values.at(column));
            m_packetTable->setItem(row, column, item);
        }
        m_packetTable->item(row, 0)->setData(Qt::UserRole + 1, frame.destination);
        m_packetTable->item(row, 0)->setData(Qt::UserRole + 2, frame.path);
        m_packetTable->scrollToBottom();
    }
}

void DataDecoderDialog::updateStats(const Ax25DecoderStats& stats)
{
    const quint64 failures = stats.fcsFailures + stats.malformed;
    const quint64 completed = stats.decoded + failures;
    m_candidatesValue->setText(QString::number(stats.candidates));
    m_decodedValue->setText(QString::number(stats.decoded));
    m_rejectedValue->setText(QString::number(failures));
    m_successValue->setText(completed == 0 ? QStringLiteral("--")
                                           : QStringLiteral("%1%").arg(qRound(100.0 * stats.decoded / completed)));

    if (stats.decoded < m_lastDecodedCount)
    {
        m_lastDecode.invalidate();
    }
    else if (stats.decoded > m_lastDecodedCount)
    {
        m_lastDecode.restart();
    }
    m_lastDecodedCount = stats.decoded;

    const char* color = UiTheme::Color::TextStatusLabel;
    QString status = QStringLiteral("Waiting…");
    if (m_lastDecode.isValid() && m_lastDecode.elapsed() < 2000)
    {
        color = UiTheme::Color::Success;
        status = QStringLiteral("Decoding…");
    }
    else if (stats.audioLevel > 3)
    {
        color = UiTheme::Color::Accent;
        status = QStringLiteral("Receiving…");
    }
    m_statusIndicator->setStyleSheet(QStringLiteral("background: %1; border-radius: 7px;").arg(QLatin1String(color)));
    m_statusText->setText(status);
}

QString DataDecoderDialog::exportText() const
{
    QStringList lines{QStringLiteral("Timestamp\tProtocol\tSource\tDestination\tPath\tPayload")};
    for (int row = 0; row < m_packetTable->rowCount(); ++row)
    {
        QStringList fields;
        const QTableWidgetItem* metadataItem = m_packetTable->item(row, 0);
        fields.append(metadataItem->data(Qt::UserRole).toString());
        fields.append(m_packetTable->item(row, 1)->data(Qt::UserRole).toString());
        fields.append(m_packetTable->item(row, 2)->data(Qt::UserRole).toString());
        fields.append(metadataItem->data(Qt::UserRole + 1).toString());
        fields.append(metadataItem->data(Qt::UserRole + 2).toString());
        fields.append(m_packetTable->item(row, 3)->data(Qt::UserRole).toString());
        lines.append(fields.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}

void DataDecoderDialog::exportPackets()
{
    const QString name = QStringLiteral("sdr9700-ax25-%1.txt")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Export AX.25 Packets"), QDir::home().filePath(name),
                                     QStringLiteral("Text files (*.txt)"));
    if (path.isEmpty())
    {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(this, QStringLiteral("Export AX.25 Packets"), file.errorString());
        return;
    }
    QTextStream stream(&file);
    stream << exportText();
    if (stream.status() != QTextStream::Ok || !file.flush())
    {
        QMessageBox::warning(this, QStringLiteral("Export AX.25 Packets"),
                             QStringLiteral("The packet export could not be written completely."));
    }
}
