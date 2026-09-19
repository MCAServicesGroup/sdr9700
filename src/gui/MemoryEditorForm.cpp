#include "MemoryEditorForm.h"

#include "ConfirmationDialog.h"
#include "DialogFooter.h"
#include "MemoryController.h"
#include "MemoryConstants.h"
#include "MemoryRecordHelpers.h"
#include "MemoryEditorPolicy.h"
#include "MemoryViewHelpers.h"
#include "MainWindow.h"
#include "UtilityWindow.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <algorithm>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QScreen>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <initializer_list>

using namespace sdr9700::memory;

MemoryEditorForm::MemoryEditorForm(MemoryController* owner) : QObject(owner), m_owner(owner) {}

void MemoryEditorForm::show(const QString& memoryId)
{
    const bool editing = !memoryId.isEmpty();
    if (!m_owner->m_window || !m_owner->m_window->m_memoryWindow)
    {
        return;
    }

    const QString dialogTitle = editing ? QStringLiteral("Edit Memory") : QStringLiteral("Add Memory");
    sdr9700::ui::UtilityWindow dialog(dialogTitle, m_owner->m_window->m_memoryWindow);
    dialog.setObjectName(QStringLiteral("memoryEditorDialog"));
    dialog.setModal(true);
    const QPoint hostCenter = m_owner->m_window->m_memoryWindow->frameGeometry().center();
    QScreen* screen = QGuiApplication::screenAt(hostCenter);
    if (!screen)
    {
        screen = QGuiApplication::primaryScreen();
    }
    const QSize availableSize =
        screen ? screen->availableGeometry().size() : QSize(kMemoryEditorDialogWidth, kMemoryEditorDialogHeight);
    const QSize dialogSize = memoryEditorDialogSize(availableSize);
    dialog.resize(dialogSize);
    dialog.setMinimumSize(std::min(440, dialogSize.width()), std::min(400, dialogSize.height()));
    dialog.setMaximumSize(dialogSize);
    QWidget* parent = &dialog;

    auto* windowRoot = new QVBoxLayout(&dialog);
    windowRoot->setContentsMargins(0, 0, 0, 0);
    windowRoot->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(dialogTitle, &dialog);
    titleBar->setObjectName(QStringLiteral("memoryEditorTitleBar"));
    connect(titleBar->closeButton(), &QPushButton::clicked, &dialog, &QDialog::reject);
    windowRoot->addWidget(titleBar);

    auto* content = new QWidget(&dialog);
    auto* dialogRoot = new QVBoxLayout(content);
    dialogRoot->setContentsMargins(UiTheme::Size::DialogContentMargin, 10, UiTheme::Size::DialogContentMargin, 0);
    dialogRoot->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    windowRoot->addWidget(content, 1);

    auto* scrollArea = new QScrollArea(content);
    scrollArea->setObjectName(QStringLiteral("memoryEditorScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* editor = new QWidget(scrollArea);
    editor->setObjectName(QStringLiteral("memoryEditorContent"));
    scrollArea->setWidget(editor);
    dialogRoot->addWidget(scrollArea, 1);

    auto* root = new QVBoxLayout(editor);
    root->setSpacing(sdr9700::ui::kDialogFooterSpacing);
    root->setContentsMargins(0, 0, 0, 0);

    auto configureSectionForm = [](QFormLayout* form)
    {
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(6);
        form->setContentsMargins(10, 12, 10, 10);
    };
    struct EditorSection
    {
        QGroupBox* group{nullptr};
        QFormLayout* form{nullptr};
    };
    auto makeSection = [editor, configureSectionForm](const QString& title)
    {
        auto* group = new QGroupBox(title, editor);
        group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        group->setStyleSheet(QStringLiteral("QGroupBox { color: %1; border: 1px solid %2; border-radius: 3px; "
                                            "margin-top: 8px; padding-top: 4px; font-size: 10px; font-weight: bold; }"
                                            "QGroupBox::title { subcontrol-origin: border; "
                                            "subcontrol-position: top left; left: 8px; top: -6px; padding: 0 4px; "
                                            "color: %3; background: %4; }")
                                 .arg(UiTheme::Color::TextStatusPrimary, UiTheme::Color::BorderMedium,
                                      UiTheme::Color::TextStatusSecondary, UiTheme::Color::Panel));
        auto* form = new QFormLayout(group);
        configureSectionForm(form);
        return EditorSection{group, form};
    };
    auto* bandEdit = new QLineEdit(editor);
    bandEdit->setObjectName(QStringLiteral("memoryEditorBand"));
    bandEdit->setReadOnly(true);
    bandEdit->setAlignment(Qt::AlignCenter);
    bandEdit->setFocusPolicy(Qt::NoFocus);
    bandEdit->setEnabled(false);

    auto* channelCombo = new QComboBox(editor);
    channelCombo->setObjectName(QStringLiteral("memoryEditorChannel"));
    channelCombo->setItemDelegate(
        new BracketAlignedItemDelegate(channelCombo, QStringLiteral("memoryEditorChannelDelegate")));
    channelCombo->setEnabled(false);
    auto setMemoryLocationBand = [this, bandEdit, channelCombo](quint16 group, quint16 preferredChannel = 0)
    {
        channelCombo->clear();
        if (group < kRadioMemoryFirstGroup || group > kRadioMemoryLastGroup)
        {
            bandEdit->clear();
            bandEdit->setProperty("memoryGroup", QVariant());
            bandEdit->setEnabled(false);
            channelCombo->setEnabled(false);
            return;
        }
        bandEdit->setText(memoryBandLabelForGroup(group));
        bandEdit->setProperty("memoryGroup", group);
        bandEdit->setEnabled(true);
        for (quint16 channel = kRadioMemoryFirstChannel; channel <= kRadioMemoryLastUserChannel; ++channel)
        {
            const auto memoryIt = m_owner->m_radioMemoriesByKey.constFind(radioMemoryKey(group, channel));
            QString disposition = QStringLiteral("EMPTY");
            if (memoryIt != m_owner->m_radioMemoriesByKey.cend())
            {
                disposition = radioMemoryName(memoryIt.value());
                if (disposition.isEmpty())
                {
                    disposition = QStringLiteral("OCCUPIED");
                }
            }
            channelCombo->addItem(QStringLiteral("%1 [%2]").arg(channel, 3, 10, QLatin1Char('0')).arg(disposition),
                                  channel);
        }
        channelCombo->setEnabled(true);
        const int preferredIndex = preferredChannel == 0 ? -1 : channelCombo->findData(preferredChannel);
        channelCombo->setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
    };
    auto* nameEdit = new QLineEdit(editor);
    nameEdit->setMaxLength(kRadioMemoryNameMaxChars);
    nameEdit->setPlaceholderText(QStringLiteral("Maximum %1 characters").arg(kRadioMemoryNameMaxChars));
    auto* frequencyEdit = new QLineEdit(editor);
    frequencyEdit->setObjectName(QStringLiteral("memoryEditorFrequency"));
    frequencyEdit->setPlaceholderText("145.000000");
    auto* modeCombo = new QComboBox(editor);
    modeCombo->addItem(QStringLiteral("FM"), modeFM);
    modeCombo->addItem(QStringLiteral("DV"), modeDV);
    modeCombo->addItem(QStringLiteral("USB"), modeUSB);
    modeCombo->addItem(QStringLiteral("LSB"), modeLSB);
    modeCombo->addItem(QStringLiteral("AM"), modeAM);
    modeCombo->addItem(QStringLiteral("CW"), modeCW);
    modeCombo->addItem(QStringLiteral("CW-R"), modeCW_R);
    modeCombo->addItem(QStringLiteral("RTTY"), modeRTTY);
    modeCombo->addItem(QStringLiteral("RTTY-R"), modeRTTY_R);
    modeCombo->addItem(QStringLiteral("DD"), modeDD);
    auto* filterCombo = new QComboBox(editor);
    filterCombo->addItem(QStringLiteral("FIL1"), 1);
    filterCombo->addItem(QStringLiteral("FIL2"), 2);
    filterCombo->addItem(QStringLiteral("FIL3"), 3);
    auto* dataModeCombo = new QComboBox(editor);
    dataModeCombo->addItem(QStringLiteral("OFF"), 0);
    dataModeCombo->addItem(QStringLiteral("DATA1"), 1);
    auto* scanGroupCombo = new QComboBox(editor);
    scanGroupCombo->addItem(QStringLiteral("OFF"), 0);
    scanGroupCombo->addItem(QStringLiteral("Group 1"), 1);
    scanGroupCombo->addItem(QStringLiteral("Group 2"), 2);
    scanGroupCombo->addItem(QStringLiteral("Group 3"), 3);
    auto* offsetCombo = new QComboBox(editor);
    auto* modeOffsetRow = new QWidget(editor);
    auto* modeOffsetLayout = new QHBoxLayout(modeOffsetRow);
    modeOffsetLayout->setContentsMargins(0, 0, 0, 0);
    modeOffsetLayout->setSpacing(10);
    auto* modeColumn = new QWidget(modeOffsetRow);
    auto* modeColumnLayout = new QVBoxLayout(modeColumn);
    modeColumnLayout->setContentsMargins(0, 0, 0, 0);
    modeColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* modeLabel = new QLabel(QStringLiteral("Mode"), modeColumn);
    modeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    modeColumnLayout->addWidget(modeLabel);
    modeColumnLayout->addWidget(modeCombo);
    auto* offsetColumn = new QWidget(modeOffsetRow);
    auto* offsetColumnLayout = new QVBoxLayout(offsetColumn);
    offsetColumnLayout->setContentsMargins(0, 0, 0, 0);
    offsetColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* offsetLabel = new QLabel(QStringLiteral("Offset"), offsetColumn);
    offsetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    offsetColumnLayout->addWidget(offsetLabel);
    offsetColumnLayout->addWidget(offsetCombo);
    modeOffsetLayout->addWidget(modeColumn, 1);
    modeOffsetLayout->addWidget(offsetColumn, 1);
    auto* customOffsetRow = new QWidget(editor);
    auto* customOffsetLayout = new QHBoxLayout(customOffsetRow);
    customOffsetLayout->setContentsMargins(0, 0, 0, 0);
    customOffsetLayout->setSpacing(6);
    auto* customOffsetModeCombo = new QComboBox(customOffsetRow);
    customOffsetModeCombo->addItem("+", dmDupPlus);
    customOffsetModeCombo->addItem("-", dmDupMinus);
    auto* customOffsetSpin = new QDoubleSpinBox(customOffsetRow);
    customOffsetSpin->setRange(0.0, 99.999);
    customOffsetSpin->setDecimals(3);
    customOffsetSpin->setSuffix(" MHz");
    customOffsetLayout->addWidget(customOffsetModeCombo);
    customOffsetLayout->addWidget(customOffsetSpin, 1);
    auto* customOffsetField = new QWidget(editor);
    auto* customOffsetFieldLayout = new QVBoxLayout(customOffsetField);
    customOffsetFieldLayout->setContentsMargins(0, 0, 0, 0);
    customOffsetFieldLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* customOffsetLabel = new QLabel(QStringLiteral("Custom Offset"), customOffsetField);
    customOffsetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    customOffsetFieldLayout->addWidget(customOffsetLabel);
    customOffsetFieldLayout->addWidget(customOffsetRow);
    auto* toneOptionCombo = new QComboBox(editor);
    toneOptionCombo->addItem("OFF", MemoryToneOff);
    toneOptionCombo->addItem("TONE", MemoryToneTone);
    toneOptionCombo->addItem("DTCS", MemoryToneDtcs);
    auto* toneEdit = new QLineEdit(editor);
    toneEdit->setPlaceholderText(QStringLiteral("103.5"));
    toneEdit->hide();
    auto* tsqlEdit = new QLineEdit(editor);
    tsqlEdit->setPlaceholderText(QStringLiteral("103.5"));
    tsqlEdit->hide();
    auto* dsqlCombo = new QComboBox(editor);
    dsqlCombo->addItem(QStringLiteral("OFF"), 0);
    dsqlCombo->addItem(QStringLiteral("DSQL"), 1);
    dsqlCombo->addItem(QStringLiteral("CSQL"), 2);
    auto* dtcsSpin = new QSpinBox(editor);
    dtcsSpin->setRange(0, 999);
    dtcsSpin->setValue(23);
    dtcsSpin->setDisplayIntegerBase(10);
    dtcsSpin->hide();
    auto* dtcsRxSpin = new QSpinBox(editor);
    dtcsRxSpin->setRange(0, 999);
    dtcsRxSpin->setValue(23);
    dtcsRxSpin->setDisplayIntegerBase(10);
    dtcsRxSpin->hide();
    auto* dtcsPolarityCombo = new QComboBox(editor);
    dtcsPolarityCombo->addItem(QStringLiteral("N"), 0);
    dtcsPolarityCombo->addItem(QStringLiteral("R"), 3);
    auto* dtcsRxPolarityCombo = new QComboBox(editor);
    dtcsRxPolarityCombo->addItem(QStringLiteral("N"), 0);
    dtcsRxPolarityCombo->addItem(QStringLiteral("R"), 3);
    auto* dvSqlSpin = new QSpinBox(editor);
    dvSqlSpin->setRange(0, 99);
    auto* urEdit = new QLineEdit(editor);
    urEdit->setMaxLength(8);
    auto* r1Edit = new QLineEdit(editor);
    r1Edit->setMaxLength(8);
    auto* r2Edit = new QLineEdit(editor);
    r2Edit->setMaxLength(8);
    auto* tonePresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto* ctcssPresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto* dtcsPresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto* dtcsRxPresetBtn = new QPushButton(QStringLiteral("NONE"), editor);
    auto setEditorFieldHeight = [](QWidget* widget) { widget->setMinimumHeight(kMemoryEditorFieldHeight); };
    for (QWidget* widget : std::initializer_list<QWidget*>{
             bandEdit,       channelCombo,   nameEdit,    frequencyEdit,         modeCombo,           filterCombo,
             dataModeCombo,  scanGroupCombo, offsetCombo, customOffsetModeCombo, customOffsetSpin,    toneOptionCombo,
             dsqlCombo,      dtcsSpin,       dtcsRxSpin,  dtcsPolarityCombo,     dtcsRxPolarityCombo, dvSqlSpin,
             urEdit,         r1Edit,         r2Edit,      tonePresetBtn,         ctcssPresetBtn,      dtcsPresetBtn,
             dtcsRxPresetBtn})
    {
        setEditorFieldHeight(widget);
    }
    tonePresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    ctcssPresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    dtcsPresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    dtcsRxPresetBtn->setFixedHeight(kMemoryEditorFieldHeight);
    auto* dtcsRow = new QWidget(editor);
    auto* dtcsRowLayout = new QHBoxLayout(dtcsRow);
    dtcsRowLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRowLayout->setSpacing(8);
    auto* dtcsCodeColumn = new QWidget(dtcsRow);
    auto* dtcsCodeColumnLayout = new QVBoxLayout(dtcsCodeColumn);
    dtcsCodeColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsCodeColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* dtcsCodeTextLabel = new QLabel(QStringLiteral("TX Code"), dtcsCodeColumn);
    dtcsCodeTextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsCodeColumnLayout->addWidget(dtcsCodeTextLabel);
    dtcsCodeColumnLayout->addWidget(dtcsPresetBtn);
    auto* dtcsPolarityColumn = new QWidget(dtcsRow);
    auto* dtcsPolarityColumnLayout = new QVBoxLayout(dtcsPolarityColumn);
    dtcsPolarityColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsPolarityColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* dtcsPolarityLabel = new QLabel(QStringLiteral("Polarity"), dtcsPolarityColumn);
    dtcsPolarityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsPolarityColumnLayout->addWidget(dtcsPolarityLabel);
    dtcsPolarityColumnLayout->addWidget(dtcsPolarityCombo);
    dtcsRowLayout->addWidget(dtcsCodeColumn, 1);
    dtcsRowLayout->addWidget(dtcsPolarityColumn, 0);

    auto* dtcsRxRow = new QWidget(editor);
    auto* dtcsRxRowLayout = new QHBoxLayout(dtcsRxRow);
    dtcsRxRowLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxRowLayout->setSpacing(8);
    auto* dtcsRxCodeColumn = new QWidget(dtcsRxRow);
    auto* dtcsRxCodeColumnLayout = new QVBoxLayout(dtcsRxCodeColumn);
    dtcsRxCodeColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxCodeColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* dtcsRxCodeTextLabel = new QLabel(QStringLiteral("RX Code"), dtcsRxCodeColumn);
    dtcsRxCodeTextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsRxCodeColumnLayout->addWidget(dtcsRxCodeTextLabel);
    dtcsRxCodeColumnLayout->addWidget(dtcsRxPresetBtn);
    auto* dtcsRxPolarityColumn = new QWidget(dtcsRxRow);
    auto* dtcsRxPolarityColumnLayout = new QVBoxLayout(dtcsRxPolarityColumn);
    dtcsRxPolarityColumnLayout->setContentsMargins(0, 0, 0, 0);
    dtcsRxPolarityColumnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
    auto* dtcsRxPolarityLabel = new QLabel(QStringLiteral("Polarity"), dtcsRxPolarityColumn);
    dtcsRxPolarityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dtcsRxPolarityColumnLayout->addWidget(dtcsRxPolarityLabel);
    dtcsRxPolarityColumnLayout->addWidget(dtcsRxPolarityCombo);
    dtcsRxRowLayout->addWidget(dtcsRxCodeColumn, 1);
    dtcsRxRowLayout->addWidget(dtcsRxPolarityColumn, 0);
    auto* toneValueRow = new QWidget(editor);
    auto* toneValueRowLayout = new QHBoxLayout(toneValueRow);
    toneValueRowLayout->setContentsMargins(0, 0, 0, 0);
    toneValueRowLayout->setSpacing(8);
    auto* dtcsValueRow = new QWidget(editor);
    auto* dtcsValueRowLayout = new QHBoxLayout(dtcsValueRow);
    dtcsValueRowLayout->setContentsMargins(0, 0, 0, 0);
    dtcsValueRowLayout->setSpacing(8);
    auto addToneColumn = [](QHBoxLayout* layout, QWidget* parent, const QString& labelText, QWidget* const field)
    {
        auto* column = new QWidget(parent);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
        auto* label = new QLabel(labelText, column);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        columnLayout->addWidget(label);
        columnLayout->addWidget(field);
        layout->addWidget(column, 1);
    };
    auto addPairDivider = [](QHBoxLayout* layout, QWidget* parent)
    {
        auto* divider = new QWidget(parent);
        divider->setFixedWidth(1);
        divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        divider->setStyleSheet(
            QStringLiteral("QWidget { background: %1; }").arg(QLatin1String(UiTheme::Color::BorderMedium)));
        layout->addWidget(divider);
    };
    addToneColumn(toneValueRowLayout, toneValueRow, QStringLiteral("TX Tone"), tonePresetBtn);
    addPairDivider(toneValueRowLayout, toneValueRow);
    addToneColumn(toneValueRowLayout, toneValueRow, QStringLiteral("RX Tone"), ctcssPresetBtn);
    dtcsValueRowLayout->addWidget(dtcsRow, 1);
    addPairDivider(dtcsValueRowLayout, dtcsValueRow);
    dtcsValueRowLayout->addWidget(dtcsRxRow, 1);
    auto setTonePick = [tonePresetBtn, toneEdit](ushort value, const QString& label)
    {
        tonePresetBtn->setText(value == 0 ? QStringLiteral("NONE") : label);
        toneEdit->setText(value == 0 ? QString() : label);
    };
    auto setCtcssPick = [ctcssPresetBtn, tsqlEdit](ushort value, const QString& label)
    {
        ctcssPresetBtn->setText(value == 0 ? QStringLiteral("NONE") : label);
        tsqlEdit->setText(value == 0 ? QString() : label);
    };
    auto setDtcsPick =
        [dtcsPresetBtn, dtcsSpin, dtcsRxPresetBtn, dtcsRxPolarityCombo, dtcsRxSpin](ushort v, const QString& label)
    {
        dtcsPresetBtn->setText(v == 0 ? QStringLiteral("NONE") : label);
        dtcsSpin->setValue(v);
        // IC-9700 memory tone modes currently expose DTCS TX-only and DTCS TX/RX, but not RX-only DTCS.
        // Keep the RX selector inaccessible until TX is set so the editor cannot create an unsupported mode.
        const bool txDtcsEnabled = v != 0;
        dtcsRxPresetBtn->setEnabled(txDtcsEnabled);
        dtcsRxPolarityCombo->setEnabled(txDtcsEnabled);
        if (!txDtcsEnabled)
        {
            dtcsRxPresetBtn->setText(QStringLiteral("NONE"));
            dtcsRxSpin->setValue(0);
            dtcsRxPolarityCombo->setCurrentIndex(std::max(0, dtcsRxPolarityCombo->findData(0)));
        }
    };
    auto setDtcsRxPick = [dtcsRxPresetBtn, dtcsRxSpin](ushort v, const QString& label)
    {
        dtcsRxPresetBtn->setText(v == 0 ? QStringLiteral("NONE") : label);
        dtcsRxSpin->setValue(v);
    };
    auto clearTonePick = [tonePresetBtn, ctcssPresetBtn, dtcsPresetBtn, dtcsRxPresetBtn, dtcsRxPolarityCombo, toneEdit,
                          tsqlEdit, dtcsSpin, dtcsRxSpin]()
    {
        tonePresetBtn->setText(QStringLiteral("NONE"));
        ctcssPresetBtn->setText(QStringLiteral("NONE"));
        dtcsPresetBtn->setText(QStringLiteral("NONE"));
        dtcsRxPresetBtn->setText(QStringLiteral("NONE"));
        dtcsRxPresetBtn->setEnabled(false);
        dtcsRxPolarityCombo->setEnabled(false);
        toneEdit->clear();
        tsqlEdit->clear();
        dtcsSpin->setValue(0);
        dtcsRxSpin->setValue(0);
    };
    const EditorSection memorySection = makeSection(QStringLiteral("Memory"));
    const EditorSection optionsSection = makeSection(QStringLiteral("Options"));
    const EditorSection toneSection = makeSection(QStringLiteral("Tone"));
    const EditorSection dstarSection = makeSection(QStringLiteral("D-STAR / DV"));
    auto* memoryGroup = memorySection.group;
    auto* optionsGroup = optionsSection.group;
    auto* toneGroup = toneSection.group;
    auto* dstarGroup = dstarSection.group;

    auto* optionsRow = new QWidget(optionsGroup);
    auto* optionsRowLayout = new QHBoxLayout(optionsRow);
    optionsRowLayout->setContentsMargins(0, 0, 0, 0);
    optionsRowLayout->setSpacing(8);
    auto addOptionColumn = [optionsRow, optionsRowLayout](const QString& labelText, QWidget* const field, int stretch)
    {
        auto* column = new QWidget(optionsRow);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
        auto* label = new QLabel(labelText, column);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        columnLayout->addWidget(label);
        columnLayout->addWidget(field);
        optionsRowLayout->addWidget(column, stretch);
    };
    addOptionColumn(QStringLiteral("Data Mode"), dataModeCombo, 1);
    addOptionColumn(QStringLiteral("Scan Group"), scanGroupCombo, 1);
    addOptionColumn(QStringLiteral("Filter"), filterCombo, 1);

    auto* memoryFields = new QWidget(memoryGroup);
    auto* memoryGrid = new QGridLayout(memoryFields);
    memoryGrid->setContentsMargins(0, 0, 0, 0);
    memoryGrid->setHorizontalSpacing(10);
    memoryGrid->setVerticalSpacing(6);
    memoryGrid->setColumnStretch(0, 1);
    memoryGrid->setColumnStretch(1, 3);
    memoryGrid->setColumnStretch(2, 2);
    auto addMemoryField = [memoryFields, memoryGrid](int row, int column, const QString& labelText,
                                                     QWidget* const field, int columnSpan = 1)
    {
        auto* fieldContainer = new QWidget(memoryFields);
        auto* fieldLayout = new QVBoxLayout(fieldContainer);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(kMemoryEditorLabelFieldSpacing);
        auto* label = new QLabel(labelText, fieldContainer);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(label);
        fieldLayout->addWidget(field);
        memoryGrid->addWidget(fieldContainer, row, column, 1, columnSpan);
    };
    addMemoryField(0, 0, QStringLiteral("Band"), bandEdit);
    addMemoryField(0, 1, QStringLiteral("Channel"), channelCombo);
    addMemoryField(0, 2, QStringLiteral("Frequency"), frequencyEdit);
    addMemoryField(1, 0, QStringLiteral("Name"), nameEdit, 3);
    memoryGrid->addWidget(modeOffsetRow, 2, 0, 1, 3);
    memoryGrid->addWidget(customOffsetField, 3, 0, 1, 3);
    memorySection.form->addRow(memoryFields);
    optionsSection.form->addRow(optionsRow);
    toneSection.form->addRow(toneOptionCombo);
    toneSection.form->addRow(toneValueRow);
    toneSection.form->addRow(dtcsValueRow);
    dstarSection.form->addRow("Digital SQL (DSQL):", dsqlCombo);
    dstarSection.form->addRow("DV SQL:", dvSqlSpin);
    dstarSection.form->addRow("Your Call (UR):", urEdit);
    dstarSection.form->addRow("Repeater 1 Callsign (R1):", r1Edit);
    dstarSection.form->addRow("Repeater 2 Callsign (R2):", r2Edit);

    root->addWidget(memoryGroup);
    root->addWidget(toneGroup);
    root->addWidget(optionsGroup);
    root->addWidget(dstarGroup);

    auto resizeEditorToContents = [editor]()
    {
        if (QLayout* layout = editor->layout())
        {
            layout->invalidate();
            layout->activate();
        }
        editor->updateGeometry();
    };

    auto populateOffsetOptions = [frequencyEdit, offsetCombo]()
    {
        const QVariant currentModeData = offsetCombo->currentData(Qt::UserRole);
        const int currentMode = currentModeData.isValid() ? currentModeData.toInt() : dmSimplex;
        const quint64 currentOffset = offsetCombo->currentData(Qt::UserRole + 1).toULongLong();
        offsetCombo->clear();
        offsetCombo->addItem(QStringLiteral("Simplex"), dmSimplex);
        offsetCombo->setItemData(offsetCombo->count() - 1, 0ULL, Qt::UserRole + 1);

        quint64 hz = 0;
        if (parseFrequencyText(frequencyEdit->text(), &hz))
        {
            for (const OffsetPreset& preset : offsetPresetsForHz(hz))
            {
                offsetCombo->addItem(preset.label, preset.mode);
                offsetCombo->setItemData(offsetCombo->count() - 1, QVariant::fromValue<qulonglong>(preset.hz),
                                         Qt::UserRole + 1);
            }
        }

        offsetCombo->addItem(QStringLiteral("Custom"), kMemoryOffsetCustom);
        offsetCombo->setItemData(offsetCombo->count() - 1, QVariant::fromValue<qulonglong>(currentOffset),
                                 Qt::UserRole + 1);

        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == currentMode &&
                offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong() == currentOffset)
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        offsetCombo->setCurrentIndex(0);
    };

    auto setOffsetSelection =
        [offsetCombo, customOffsetModeCombo, customOffsetSpin](duplexMode_t mode, quint64 offsetHz)
    {
        const quint64 scaledOffsetHz = offsetHz * 100ULL;
        for (int i = 0; i < offsetCombo->count(); ++i)
        {
            const quint64 presetOffsetHz = offsetCombo->itemData(i, Qt::UserRole + 1).toULongLong();
            if (offsetCombo->itemData(i, Qt::UserRole).toInt() == mode &&
                (presetOffsetHz == offsetHz || presetOffsetHz == scaledOffsetHz))
            {
                offsetCombo->setCurrentIndex(i);
                return;
            }
        }
        customOffsetModeCombo->setCurrentIndex(std::max(0, customOffsetModeCombo->findData(mode)));
        customOffsetSpin->setValue(offsetHz / 1000000.0);
        offsetCombo->setCurrentIndex(std::max(0, offsetCombo->findData(kMemoryOffsetCustom)));
    };

    auto updateCustomOffsetVisibility =
        [modeCombo, offsetColumn, offsetCombo, customOffsetField, resizeEditorToContents]()
    {
        const bool showOffset = modeSupportsMemoryOffset(modeCombo->currentData().toInt());
        const bool customSelected = offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom;
        offsetColumn->setVisible(showOffset);
        customOffsetField->setVisible(showOffset && customSelected);
        resizeEditorToContents();
    };

    populateOffsetOptions();
    updateCustomOffsetVisibility();

    auto populateToneValues = [clearTonePick]() { clearTonePick(); };

    auto updateConditionalSections = [modeCombo, toneOptionCombo, toneForm = toneSection.form, toneValueRow,
                                      dtcsValueRow, dstarGroup, resizeEditorToContents]()
    {
        const int mode = modeCombo->currentData().toInt();
        const auto toneFamily = static_cast<MemoryToneFamily>(toneOptionCombo->currentData().toInt());
        const bool showDstar = mode == modeDV || mode == modeDD;
        auto setToneRowVisible = [toneForm](QWidget* field, bool visible) { toneForm->setRowVisible(field, visible); };
        setToneRowVisible(toneValueRow, toneFamily == MemoryToneTone);
        setToneRowVisible(dtcsValueRow, toneFamily == MemoryToneDtcs);
        dstarGroup->setVisible(showDstar);
        resizeEditorToContents();
    };

    auto copyCurrentSettings = [this, editor, nameEdit, frequencyEdit, modeCombo, toneOptionCombo, setTonePick,
                                setDtcsPick, populateToneValues, populateOffsetOptions, setOffsetSelection,
                                updateCustomOffsetVisibility, updateConditionalSections]()
    {
        if (!m_owner->m_window->m_model || !m_owner->m_window->m_model->isReady() || !m_owner->m_window->m_vfo)
        {
            QMessageBox::information(editor, "Copy Current Settings",
                                     "Connect to the radio and wait for sync before copying current settings.");
            return;
        }

        const quint64 currentFrequencyHz = m_owner->m_window->m_vfo->frequencyHz();
        frequencyEdit->setText(memoryFrequencyLabel(currentFrequencyHz));
        if (nameEdit->text().trimmed().isEmpty())
        {
            nameEdit->setText(memoryFrequencyLabel(currentFrequencyHz));
        }
        modeCombo->setCurrentIndex(
            std::max(0, modeCombo->findData(modeRegisterFromLabel(m_owner->m_window->m_vfo->mode()))));
        populateOffsetOptions();
        setOffsetSelection(m_owner->m_window->m_duplexMode, m_owner->m_window->m_repeaterOffsetHz);
        updateCustomOffsetVisibility();
        const MemoryToneFamily toneFamily = memoryToneFamilyForMode(m_owner->m_window->m_toneAccessMode);
        toneOptionCombo->setCurrentIndex(std::max(0, toneOptionCombo->findData(toneFamily)));
        populateToneValues();
        updateConditionalSections();
        if (m_owner->m_window->m_toneAccessMode != ratrNN)
        {
            const bool isDtcs = isDtcsToneMode(m_owner->m_window->m_toneAccessMode);
            const ushort toneValue = isDtcs ? m_owner->m_window->m_dtcsCode : m_owner->m_window->m_toneFrequency;
            const QString toneText = isDtcs ? dtcsCodeLabel(toneValue) : toneFrequencyLabel(toneValue);
            if (isDtcs)
            {
                setDtcsPick(toneValue, toneText);
            }
            else
            {
                setTonePick(toneValue, toneText);
            }
        }
    };

    populateToneValues();
    connect(toneOptionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), editor,
            [populateToneValues, updateConditionalSections]()
            {
                populateToneValues();
                updateConditionalSections();
            });
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), editor,
            [updateCustomOffsetVisibility, updateConditionalSections]()
            {
                updateCustomOffsetVisibility();
                updateConditionalSections();
            });
    auto styleToneGridButton = [](QPushButton* button)
    {
        button->setFixedSize(54, 24);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; "
                                             "color: %3; font-size: 11px; }"
                                             "QPushButton:hover { background: %4; border-color: %5; color: %6; }")
                                  .arg(UiTheme::Color::Button, UiTheme::Color::BorderLight, UiTheme::Color::TextPrimary,
                                       UiTheme::Color::AccentDark, UiTheme::Color::Accent, UiTheme::Color::White));
    };
    connect(tonePresetBtn, &QPushButton::clicked, tonePresetBtn,
            [tonePresetBtn, setTonePick, styleToneGridButton]()
            {
                QMenu menu(tonePresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 4;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setTonePick, menuPtr = &menu]()
                        {
                            setTonePick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const TonePreset& preset : kTonePresets)
                {
                    const QString label = QString::fromLatin1(preset.label);
                    const ushort tone = preset.tone;
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setTonePick, label, tone, menuPtr = &menu]()
                            {
                                setTonePick(tone, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(tonePresetBtn->mapToGlobal(QPoint(0, tonePresetBtn->height())));
            });
    connect(ctcssPresetBtn, &QPushButton::clicked, ctcssPresetBtn,
            [ctcssPresetBtn, setCtcssPick, styleToneGridButton]()
            {
                QMenu menu(ctcssPresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 4;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setCtcssPick, menuPtr = &menu]()
                        {
                            setCtcssPick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const TonePreset& preset : kTonePresets)
                {
                    const QString label = QString::fromLatin1(preset.label);
                    const ushort tone = preset.tone;
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setCtcssPick, label, tone, menuPtr = &menu]()
                            {
                                setCtcssPick(tone, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(ctcssPresetBtn->mapToGlobal(QPoint(0, ctcssPresetBtn->height())));
            });
    connect(dtcsPresetBtn, &QPushButton::clicked, dtcsPresetBtn,
            [dtcsPresetBtn, setDtcsPick, styleToneGridButton]()
            {
                QMenu menu(dtcsPresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 6;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setDtcsPick, menuPtr = &menu]()
                        {
                            setDtcsPick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const ushort code : kDtcsCodes)
                {
                    const QString label = dtcsCodeLabel(code);
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setDtcsPick, label, code, menuPtr = &menu]()
                            {
                                setDtcsPick(code, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(dtcsPresetBtn->mapToGlobal(QPoint(0, dtcsPresetBtn->height())));
            });
    connect(dtcsRxPresetBtn, &QPushButton::clicked, dtcsRxPresetBtn,
            [dtcsRxPresetBtn, setDtcsRxPick, styleToneGridButton]()
            {
                QMenu menu(dtcsRxPresetBtn);
                styleCompactMenu(&menu);
                auto* panel = new QWidget(&menu);
                auto* grid = new QGridLayout(panel);
                grid->setContentsMargins(6, 6, 6, 6);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                static constexpr int kCols = 6;
                int idx = 0;
                auto* noneBtn = new QPushButton(QStringLiteral("NONE"), panel);
                styleToneGridButton(noneBtn);
                connect(noneBtn, &QPushButton::clicked, &menu,
                        [setDtcsRxPick, menuPtr = &menu]()
                        {
                            setDtcsRxPick(0, QStringLiteral("NONE"));
                            menuPtr->close();
                        });
                grid->addWidget(noneBtn, idx / kCols, idx % kCols);
                ++idx;
                for (const ushort code : kDtcsCodes)
                {
                    const QString label = dtcsCodeLabel(code);
                    auto* btn = new QPushButton(label, panel);
                    styleToneGridButton(btn);
                    connect(btn, &QPushButton::clicked, &menu,
                            [setDtcsRxPick, label, code, menuPtr = &menu]()
                            {
                                setDtcsRxPick(code, label);
                                menuPtr->close();
                            });
                    grid->addWidget(btn, idx / kCols, idx % kCols);
                    ++idx;
                }
                auto* action = new QWidgetAction(&menu);
                action->setDefaultWidget(panel);
                menu.addAction(action);
                menu.exec(dtcsRxPresetBtn->mapToGlobal(QPoint(0, dtcsRxPresetBtn->height())));
            });
    auto updateMemoryLocationFromFrequency = [this, frequencyEdit, bandEdit, channelCombo, setMemoryLocationBand]()
    {
        quint64 frequencyHz = 0;
        if (!parseFrequencyText(frequencyEdit->text(), &frequencyHz))
        {
            setMemoryLocationBand(0);
            return;
        }
        const availableBands band = sdr9700::radioBandForFrequency(frequencyHz);
        const sdr9700::RadioBandDef* definition = sdr9700::radioBandDefinition(band);
        if (!definition || definition->memGroup < kRadioMemoryFirstGroup ||
            definition->memGroup > kRadioMemoryLastGroup)
        {
            setMemoryLocationBand(0);
            return;
        }
        const quint16 group = static_cast<quint16>(definition->memGroup);
        quint16 preferredChannel = 0;
        if (bandEdit->property("memoryGroup").toUInt() == group && channelCombo->currentIndex() >= 0)
        {
            preferredChannel = static_cast<quint16>(channelCombo->currentData().toUInt());
        }
        else
        {
            if (!m_owner->firstOpenChannelForGroup(group, &preferredChannel))
            {
                // A newly opened editor can legitimately be populated from
                // the committed database before the current radio sweep has
                // verified every slot. The strict controller helper refuses
                // to call an unverified slot open because write workflows use
                // that distinction to avoid overwriting unknown radio state.
                // For presentation, however, the dropdown already labels
                // locally absent slots as EMPTY. Select the first such row so
                // the initial choice agrees with the information displayed to
                // the operator; write-time safeguards remain unchanged.
                for (quint16 candidate = kRadioMemoryFirstChannel; candidate <= kRadioMemoryLastUserChannel;
                     ++candidate)
                {
                    if (!m_owner->m_radioMemoriesByKey.contains(radioMemoryKey(group, candidate)))
                    {
                        preferredChannel = candidate;
                        break;
                    }
                }
            }
        }
        setMemoryLocationBand(group, preferredChannel);
    };
    connect(frequencyEdit, &QLineEdit::textChanged, editor,
            [updateMemoryLocationFromFrequency](const QString&) { updateMemoryLocationFromFrequency(); });
    connect(frequencyEdit, &QLineEdit::editingFinished, editor,
            [populateOffsetOptions, updateCustomOffsetVisibility]()
            {
                populateOffsetOptions();
                updateCustomOffsetVisibility();
            });
    connect(offsetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), editor, updateCustomOffsetVisibility);

    auto applyMemoryToForm = [this, setMemoryLocationBand, nameEdit, frequencyEdit, modeCombo, filterCombo,
                              dataModeCombo, scanGroupCombo, populateOffsetOptions, setOffsetSelection,
                              updateCustomOffsetVisibility, toneOptionCombo, toneEdit, tsqlEdit, dsqlCombo, dtcsSpin,
                              dtcsRxSpin, dtcsPolarityCombo, dtcsRxPolarityCombo, dvSqlSpin, urEdit, r1Edit, r2Edit,
                              setTonePick, setCtcssPick, setDtcsPick, setDtcsRxPick, populateToneValues,
                              updateConditionalSections](const MemoryRecord& memory)
    {
        quint16 group = kRadioMemoryFirstGroup;
        quint16 channel = kRadioMemoryFirstChannel;
        m_owner->parseRadioMemoryId(memory.id, &group, &channel);
        nameEdit->setText(memory.name);
        frequencyEdit->setText(memoryFrequencyLabel(memory.receiveHz));
        setMemoryLocationBand(group, channel);
        modeCombo->setCurrentIndex(std::max(0, modeCombo->findData(memory.mode)));
        filterCombo->setCurrentIndex(std::max(0, filterCombo->findData(memory.filter)));
        dataModeCombo->setCurrentIndex(std::max(0, dataModeCombo->findData(memory.dataMode)));
        scanGroupCombo->setCurrentIndex(std::max(0, scanGroupCombo->findData(memory.scan)));
        populateOffsetOptions();
        setOffsetSelection(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
        updateCustomOffsetVisibility();
        const auto toneModeForEditor = static_cast<rptAccessTxRx_t>(memory.toneMode);
        toneOptionCombo->setCurrentIndex(
            std::max(0, toneOptionCombo->findData(memoryToneFamilyForMode(toneModeForEditor))));
        toneEdit->setText(memory.tone);
        tsqlEdit->setText(memory.tsql);
        dsqlCombo->setCurrentIndex(std::max(0, dsqlCombo->findData(memory.dsql)));
        dtcsSpin->setValue(memory.dtcs);
        dtcsRxSpin->setValue(memory.dtcsB);
        dtcsPolarityCombo->setCurrentIndex(std::max(0, dtcsPolarityCombo->findData(memory.dtcsPolarity)));
        dtcsRxPolarityCombo->setCurrentIndex(std::max(0, dtcsRxPolarityCombo->findData(memory.dtcsPolarityB)));
        dvSqlSpin->setValue(memory.dvSql);
        urEdit->setText(memory.urCall);
        r1Edit->setText(memory.r1Call);
        r2Edit->setText(memory.r2Call);
        populateToneValues();
        updateConditionalSections();
        if (toneModeForEditor != ratrNN)
        {
            const bool isDtcs = isDtcsToneMode(toneModeForEditor);
            if (isDtcs)
            {
                setDtcsPick(memory.dtcs, dtcsCodeLabel(memory.dtcs));
                if (toneModeForEditor == ratrDD || toneModeForEditor == ratrDT)
                {
                    setDtcsRxPick(memory.dtcsB, dtcsCodeLabel(memory.dtcsB));
                }
            }
            else
            {
                if (toneModeForEditor == ratrTN || toneModeForEditor == ratrTT || toneModeForEditor == ratrTD)
                {
                    const ushort txTone = toneValueFromRadioText(memory.tone);
                    if (txTone > 0)
                    {
                        setTonePick(txTone, toneFrequencyLabel(txTone));
                    }
                }
                if (toneModeForEditor == ratrNT || toneModeForEditor == ratrTT || toneModeForEditor == ratrDT)
                {
                    const ushort rxTone = toneValueFromRadioText(memory.tsql);
                    if (rxTone > 0)
                    {
                        setCtcssPick(rxTone, toneFrequencyLabel(rxTone));
                    }
                }
            }
        }
    };

    if (editing)
    {
        bool found = false;
        const MemoryRecord memory = m_owner->memoryForId(memoryId, &found);
        if (!found)
        {
            QMessageBox::information(parent, "Edit Memory", "Select a memory to edit.");
            return;
        }
        applyMemoryToForm(memory);
    }

    updateCustomOffsetVisibility();
    updateConditionalSections();

    root->addStretch(1);
    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(content);
    auto* copyButton = footer.buttonBox->addButton(QStringLiteral("Copy Current"), QDialogButtonBox::ActionRole);
    copyButton->setMinimumWidth(copyButton->sizeHint().width() + 20);
    auto* cancelButton = footer.buttonBox->addButton(QDialogButtonBox::Cancel);
    auto* saveButton = footer.buttonBox->addButton(QDialogButtonBox::Save);
    dialogRoot->addWidget(footer.widget);
    resizeEditorToContents();
    connect(copyButton, &QPushButton::clicked, editor, copyCurrentSettings);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto setWriteInProgress = [&dialog, editor, titleBar, copyButton, cancelButton, saveButton](bool inProgress)
    {
        dialog.setProperty("closeBlocked", inProgress);
        editor->setEnabled(!inProgress);
        titleBar->closeButton()->setEnabled(!inProgress);
        copyButton->setEnabled(!inProgress);
        cancelButton->setEnabled(!inProgress);
        saveButton->setEnabled(!inProgress);
    };

    const QPointer<QDialog> dialogGuard(&dialog);

    connect(
        saveButton, &QPushButton::clicked, editor,
        [this, editor, frequencyEdit, toneOptionCombo, toneEdit, tsqlEdit, dtcsSpin, dtcsRxSpin, nameEdit, modeCombo,
         filterCombo, dataModeCombo, scanGroupCombo, offsetCombo, customOffsetModeCombo, customOffsetSpin, dsqlCombo,
         dtcsPolarityCombo, dtcsRxPolarityCombo, dvSqlSpin, urEdit, r1Edit, r2Edit, bandEdit, channelCombo, editing,
         memoryId, parent, dialogGuard, setWriteInProgress]()
        {
            quint64 receiveHz = 0;
            if (!parseFrequencyText(frequencyEdit->text(), &receiveHz))
            {
                QMessageBox::warning(editor, "Add/Edit Memory", "Enter a valid receive frequency.");
                frequencyEdit->setFocus();
                frequencyEdit->selectAll();
                return;
            }
            const availableBands inferredBand = sdr9700::radioBandForFrequency(receiveHz);
            const sdr9700::RadioBandDef* bandDefinition = sdr9700::radioBandDefinition(inferredBand);
            if (!bandDefinition || bandDefinition->memGroup < kRadioMemoryFirstGroup ||
                bandDefinition->memGroup > kRadioMemoryLastGroup)
            {
                QMessageBox::warning(editor, "Add/Edit Memory", "Enter a frequency in the 2M, 70CM, or 23CM range.");
                frequencyEdit->setFocus();
                frequencyEdit->selectAll();
                return;
            }
            const quint16 group = static_cast<quint16>(bandDefinition->memGroup);
            if (bandEdit->property("memoryGroup").toUInt() != group || channelCombo->currentIndex() < 0)
            {
                QMessageBox::warning(editor, "Add/Edit Memory",
                                     "Choose a valid frequency so Band and Channel can be determined.");
                frequencyEdit->setFocus();
                return;
            }

            auto toneMode = ratrNN;
            ushort toneValue = 0;
            const QString toneText = toneEdit->text().trimmed();
            const QString tsqlText = tsqlEdit->text().trimmed();
            const ushort txTone = toneValueFromRadioText(toneText);
            const ushort rxTone = toneValueFromRadioText(tsqlText);
            const ushort txDtcs = static_cast<ushort>(dtcsSpin->value());
            const ushort rxDtcs = static_cast<ushort>(dtcsRxSpin->value());
            const auto toneFamily = static_cast<MemoryToneFamily>(toneOptionCombo->currentData().toInt());
            if (toneFamily == MemoryToneTone)
            {
                if (rxTone > 0 && txTone > 0)
                {
                    toneMode = ratrTT;
                    toneValue = txTone;
                }
                else if (rxTone > 0)
                {
                    toneMode = ratrNT;
                    toneValue = rxTone;
                }
                else if (txTone > 0)
                {
                    toneMode = ratrTN;
                    toneValue = txTone;
                }
            }
            else if (toneFamily == MemoryToneDtcs)
            {
                if (txDtcs > 0 && rxDtcs > 0)
                {
                    toneMode = ratrDD;
                    toneValue = txDtcs;
                }
                else if (txDtcs > 0)
                {
                    toneMode = ratrDN;
                    toneValue = txDtcs;
                }
            }

            MemoryRecord memory;
            memory.id = editing ? memoryId : QString();
            memory.name = nameEdit->text().trimmed();
            if (memory.name.length() > kRadioMemoryNameMaxChars)
            {
                QMessageBox::warning(
                    editor, "Add/Edit Memory",
                    QStringLiteral("Radio memory names are limited to %1 characters.").arg(kRadioMemoryNameMaxChars));
                nameEdit->setFocus();
                nameEdit->selectAll();
                return;
            }
            if (memory.name.isEmpty())
            {
                memory.name = memoryFrequencyLabel(receiveHz);
            }
            memory.receiveHz = receiveHz;
            memory.mode = modeCombo->currentData().toInt();
            memory.filter = filterCombo->currentData().toInt();
            memory.dataMode = dataModeCombo->currentData().toInt();
            memory.scan = scanGroupCombo->currentData().toInt();
            memory.band = bandLabelForHz(memory.receiveHz);
            memory.bandKey = memoryBandKeyForHz(memory.receiveHz);
            if (!modeSupportsMemoryOffset(memory.mode))
            {
                memory.duplexMode = dmSimplex;
                memory.offsetHz = 0;
            }
            else if (offsetCombo->currentData(Qt::UserRole).toInt() == kMemoryOffsetCustom)
            {
                memory.duplexMode = customOffsetModeCombo->currentData().toInt();
                memory.offsetHz = static_cast<quint64>(customOffsetSpin->value() * 1000000.0 + 0.5);
            }
            else
            {
                memory.duplexMode = offsetCombo->currentData(Qt::UserRole).toInt();
                memory.offsetHz = offsetCombo->currentData(Qt::UserRole + 1).toULongLong();
            }
            memory.shift = offsetModeLabel(static_cast<duplexMode_t>(memory.duplexMode), memory.offsetHz);
            memory.toneMode = static_cast<int>(toneMode);
            memory.toneValue = toneValue;
            memory.toneOption = toneOptionLabel(toneMode);
            memory.toneFrequency = memoryToneFrequencyLabel(toneMode, toneValue);
            memory.tone = toneText;
            memory.tsql = tsqlText;
            memory.dsql = dsqlCombo->currentData().toInt();
            memory.dtcs = static_cast<ushort>(dtcsSpin->value());
            memory.dtcsPolarity = dtcsPolarityCombo->currentData().toInt();
            memory.dtcsB = static_cast<ushort>(dtcsRxSpin->value());
            memory.dtcsPolarityB = dtcsRxPolarityCombo->currentData().toInt();
            memory.dvSql = dvSqlSpin->value();
            memory.urCall = urEdit->text().trimmed().toUpper();
            memory.r1Call = r1Edit->text().trimmed().toUpper();
            memory.r2Call = r2Edit->text().trimmed().toUpper();

            const quint16 channel = static_cast<quint16>(channelCombo->currentData().toUInt());
            QVector<MemoryType> writes;
            if (editing)
            {
                quint16 oldGroup = 0;
                quint16 oldChannel = 0;
                if (!m_owner->parseRadioMemoryId(memoryId, &oldGroup, &oldChannel))
                {
                    QMessageBox::warning(parent, "Add/Edit Memory", "Could not identify the selected radio memory.");
                    return;
                }
                if (group != oldGroup || channel != oldChannel)
                {
                    if (m_owner->m_radioMemoriesByKey.contains(radioMemoryKey(group, channel)))
                    {
                        if (!sdr9700::ui::confirmAction(parent, QStringLiteral("Add/Edit Memory"),
                                                        QStringLiteral("Overwrite radio memory %1 channel %2?")
                                                            .arg(memoryBandLabelForGroup(group))
                                                            .arg(channel, 3, 10, QLatin1Char('0')),
                                                        QStringLiteral("Overwrite"), true))
                        {
                            return;
                        }
                    }
                    writes.append(deletedRadioMemory(oldGroup, oldChannel));
                }
            }
            else if (m_owner->m_radioMemoriesByKey.contains(radioMemoryKey(group, channel)))
            {
                if (!sdr9700::ui::confirmAction(parent, QStringLiteral("Add/Edit Memory"),
                                                QStringLiteral("Overwrite radio memory %1 channel %2?")
                                                    .arg(memoryBandLabelForGroup(group))
                                                    .arg(channel, 3, 10, QLatin1Char('0')),
                                                QStringLiteral("Overwrite"), true))
                {
                    return;
                }
            }

            writes.append(radioMemoryFromRecord(memory, group, channel));
            // Save closes the editor only after the radio echoes the written
            // slot. A moved memory is one ordered batch (delete old, write new),
            // preventing the previous fire-and-forget path from reporting
            // success while either command was still pending or had failed.
            setWriteInProgress(true);
            m_owner->queueRadioMemoryWrites(
                writes, 0, editing ? QStringLiteral("Updating memory") : QStringLiteral("Storing memory"),
                [this, editing, dialogGuard, setWriteInProgress](bool success)
                {
                    if (!success)
                    {
                        if (dialogGuard)
                        {
                            setWriteInProgress(false);
                        }
                        return;
                    }
                    m_owner->reloadMemoryTable();
                    if (dialogGuard)
                    {
                        dialogGuard->accept();
                    }
                    m_owner->m_window->showStatusMessage(editing ? QStringLiteral("Memory updated")
                                                                 : QStringLiteral("Memory stored"));
                });
        });

    // QDialog::exec() maps the native window before UtilityWindow's showEvent
    // can perform its normal centering pass. On macOS that exposes the window
    // manager's provisional position for one frame, followed by a small but
    // visible jump to the centered position. The editor has finished all
    // content and visibility setup at this point, so polish and center it
    // before mapping. Mark this fixed-size modal editor as pre-positioned so
    // UtilityWindow does not schedule its ordinary post-show correction
    // passes and expose a second movement to the operator.
    dialog.ensurePolished();
    dialog.centerOnHost();
    dialog.setProperty("prepositionedBeforeShow", true);
    dialog.exec();
}
