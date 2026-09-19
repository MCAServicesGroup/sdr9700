#pragma once

#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "UiTheme.h"

#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <algorithm>

namespace sdr9700::memory
{
class BracketAlignedItemDelegate final : public QStyledItemDelegate
{
  public:
    explicit BracketAlignedItemDelegate(QObject* parent = nullptr, const QString& objectName = QString())
        : QStyledItemDelegate(parent)
    {
        setObjectName(objectName);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        const QString text = itemOption.text;
        const qsizetype delimiter = text.indexOf(QStringLiteral(" ["));
        if (delimiter <= 0)
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        // Let the active platform style paint the item background, focus,
        // selection, and disabled states. Paint only the two text portions
        // ourselves so the variable-width slot identifier occupies a fixed
        // column and every opening bracket starts at the same horizontal
        // position without forcing memory names into a monospaced font.
        itemOption.text.clear();
        const QStyle* style = itemOption.widget ? itemOption.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, itemOption.widget);
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &itemOption, itemOption.widget);
        const QFontMetrics metrics(itemOption.font);
        int widestDigit = 0;
        for (QChar digit = QLatin1Char('0'); digit <= QLatin1Char('9'); digit = QChar(digit.unicode() + 1))
        {
            widestDigit = std::max(widestDigit, metrics.horizontalAdvance(digit));
        }
        const int identifierWidth = std::max(widestDigit * 3, metrics.horizontalAdvance(text.first(delimiter)));
        const int gapWidth = metrics.horizontalAdvance(QLatin1Char(' '));
        const QPalette::ColorRole textRole =
            itemOption.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;

        painter->save();
        painter->setFont(itemOption.font);
        painter->setPen(itemOption.palette.color(itemOption.palette.currentColorGroup(), textRole));
        painter->drawText(QRect(textRect.left(), textRect.top(), identifierWidth, textRect.height()),
                          Qt::AlignRight | Qt::AlignVCenter, text.first(delimiter));
        painter->drawText(QRect(textRect.left() + identifierWidth + gapWidth, textRect.top(),
                                textRect.width() - identifierWidth - gapWidth, textRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, text.sliced(delimiter + 1));
        painter->restore();
    }
};

class ToneCellDelegate : public QStyledItemDelegate
{
  public:
    explicit ToneCellDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString type = index.data(kMemoryToneTypeRole).toString();
        if (type.isEmpty() || type == QLatin1String("OFF"))
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        itemOption.text.clear();
        const QWidget* widget = itemOption.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, widget);

        const QString rx = index.data(kMemoryToneRxRole).toString();
        const QString tx = index.data(kMemoryToneTxRole).toString();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const QColor textColor =
            selected ? option.palette.color(QPalette::HighlightedText) : option.palette.color(QPalette::Text);
        QRect rect = option.rect.adjusted(5, 0, -5, 0);
        if (rect.width() < 24 || rect.height() < 8)
        {
            return;
        }

        constexpr int kFieldGap = 10;
        const QFontMetrics metrics(option.font);
        const int typeWidth = std::max({metrics.horizontalAdvance(QStringLiteral("CTCSS")),
                                        metrics.horizontalAdvance(QStringLiteral("DTCS")),
                                        metrics.horizontalAdvance(QStringLiteral("DCS"))});
        const int txWidth = std::max(metrics.horizontalAdvance(QStringLiteral("TX: 000.0")),
                                     metrics.horizontalAdvance(QStringLiteral("TX: 000N")));
        const int typeX = rect.left() + kMemoryToneCellTextPadding;
        const int txX = typeX + typeWidth + kFieldGap;
        const int rxX = txX + txWidth + kFieldGap;
        const QRect typeRect(typeX, rect.top(), typeWidth, rect.height());
        const QRect txRect(txX, rect.top(), txWidth, rect.height());
        const QRect rxRect(rxX, rect.top(), std::max(0, rect.right() - rxX + 1), rect.height());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(textColor);
        painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, type);
        painter->drawText(txRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("TX: %1").arg(tx.isEmpty() ? QStringLiteral("OFF") : tx));
        painter->drawText(rxRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("RX: %1").arg(rx.isEmpty() ? QStringLiteral("OFF") : rx));
        painter->restore();
    }
};


inline QString dtcsMemoryValue(ushort code, int polarity)
{
    return QStringLiteral("%1%2").arg(dtcsCodeLabel(code), polarity == 3 ? QStringLiteral("R") : QStringLiteral("N"));
}

inline QString memoryToneTypeLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN)
    {
        return QStringLiteral("OFF");
    }
    return isDtcsToneMode(toneMode) ? QStringLiteral("DTCS") : QStringLiteral("TONE");
}

inline QString memoryToneRxLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN || toneMode == ratrTN || toneMode == ratrDN)
    {
        return QStringLiteral("OFF");
    }
    if (isDtcsToneMode(toneMode))
    {
        return dtcsMemoryValue(memory.dtcsB, memory.dtcsPolarityB);
    }
    return memory.tsql.isEmpty() ? memory.tone : memory.tsql;
}

inline QString memoryToneTxLabel(const MemoryRecord& memory)
{
    const auto toneMode = static_cast<rptAccessTxRx_t>(memory.toneMode);
    if (toneMode == ratrNN || toneMode == ratrNT)
    {
        return QStringLiteral("OFF");
    }
    if (isDtcsToneMode(toneMode))
    {
        return dtcsMemoryValue(memory.dtcs, memory.dtcsPolarity);
    }
    return memory.tone.isEmpty() ? memory.tsql : memory.tone;
}

inline QString memoryToneTableLabel(const MemoryRecord& memory)
{
    const QString type = memoryToneTypeLabel(memory);
    if (type == QLatin1String("OFF"))
    {
        return QStringLiteral("OFF");
    }
    return QStringLiteral("%1: %2/%3").arg(type, memoryToneTxLabel(memory), memoryToneRxLabel(memory));
}


} // namespace sdr9700::memory
