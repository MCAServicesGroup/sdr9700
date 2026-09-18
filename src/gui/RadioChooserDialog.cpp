#include "RadioChooserDialog.h"
#include "ConfirmationDialog.h"
#include "DialogFooter.h"
#include "AppSettings.h"
#include "DialogPlacement.h"
#include "RadioProfile.h"
#include "UdpBase.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

RadioChooserDialog::RadioChooserDialog(QWidget* parent)
    : sdr9700::ui::UtilityWindow(QStringLiteral("Radio Chooser"), parent)
{
    const QString titleText = QStringLiteral("Radio Chooser");
    setMinimumSize(640, 400);
    resize(720, 430);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* titleBar = new sdr9700::ui::UtilityTitleBar(titleText, this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(titleBar);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(UiTheme::Size::DialogContentMargin, 14, UiTheme::Size::DialogContentMargin, 0);
    contentLayout->setSpacing(sdr9700::ui::kDialogFooterSpacing);

    auto* title = new QLabel("Select and manage radio targets:", content);
    title->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 14px; font-weight: bold; }")
                             .arg(QLatin1String(UiTheme::Color::TextPrimary)));
    contentLayout->addWidget(title);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    auto* leftPanel = new QWidget(content);
    auto* leftVbox = new QVBoxLayout(leftPanel);
    leftVbox->setContentsMargins(0, 0, 0, 0);
    leftVbox->setSpacing(6);
    leftPanel->setFixedWidth(240);

    m_list = new QListWidget(leftPanel);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumHeight(220);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: %1; border: 1px solid %2; color: %3; outline: 0; }"
                                         "QListWidget::item { border-bottom: 1px solid %2; padding: 7px 6px; }"
                                         "QListWidget::item:selected { background: %4; color: %5; }")
                              .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::Border),
                                   QLatin1String(UiTheme::Color::TextStatusPrimary),
                                   QLatin1String(UiTheme::Color::AccentDark),
                                   QLatin1String(UiTheme::Color::TextBright)));
    leftVbox->addWidget(m_list, 1);

    auto* listBtns = new QHBoxLayout;
    m_addBtn = new QPushButton("Add", leftPanel);
    m_addBtn->setObjectName(QStringLiteral("addRadioProfileButton"));
    m_removeBtn = new QPushButton("Remove", leftPanel);
    m_removeBtn->setEnabled(false);
    listBtns->addWidget(m_addBtn);
    listBtns->addWidget(m_removeBtn);
    leftVbox->addLayout(listBtns);

    body->addWidget(leftPanel);

    auto* line = new QFrame(content);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    body->addWidget(line);

    auto* rightVbox = new QVBoxLayout;
    rightVbox->setSpacing(8);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    m_nameEdit = new QLineEdit(content);
    m_nameEdit->setObjectName(QStringLiteral("radioProfileName"));
    m_nameEdit->setPlaceholderText("e.g. Home IC-9700");
    form->addRow("Name:", m_nameEdit);

    m_hostEdit = new QLineEdit(content);
    m_hostEdit->setObjectName(QStringLiteral("radioProfileHost"));
    m_hostEdit->setPlaceholderText("192.168.1.x");
    form->addRow("Host / IP:", m_hostEdit);

    m_portSpin = new QSpinBox(content);
    m_portSpin->setRange(1, kIcomLanControlPortMax);
    m_portSpin->setValue(50001);
    form->addRow("Control port:", m_portSpin);

    m_userEdit = new QLineEdit(content);
    form->addRow("Username:", m_userEdit);

    auto* passRow = new QHBoxLayout;
    m_passEdit = new QLineEdit(content);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_showPassBtn = new QPushButton("Show", content);
    m_showPassBtn->setFixedWidth(52);
    m_showPassBtn->setCheckable(true);
    passRow->addWidget(m_passEdit);
    passRow->addWidget(m_showPassBtn);
    form->addRow("Password:", passRow);

    rightVbox->addLayout(form);

    auto* saveRow = new QHBoxLayout;
    saveRow->addStretch(1);
    m_saveBtn = new QPushButton("Save Target", content);
    m_saveBtn->setObjectName(QStringLiteral("saveRadioProfileButton"));
    m_saveBtn->setEnabled(false);
    m_saveBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px;"
                       " color: %3; padding: 4px 14px; }"
                       "QPushButton:hover { background: %4; border-color: %5; }"
                       "QPushButton:disabled { background: %6; border-color: %7; color: %8; }")
            .arg(QLatin1String(UiTheme::Color::Accent), QLatin1String(UiTheme::Color::AccentBright),
                 QLatin1String(UiTheme::Color::PanelDark), QLatin1String(UiTheme::Color::AccentHover),
                 QLatin1String(UiTheme::Color::AccentBright), QLatin1String(UiTheme::Color::Button),
                 QLatin1String(UiTheme::Color::Border), QLatin1String(UiTheme::Color::TextMuted)));
    saveRow->addWidget(m_saveBtn);
    rightVbox->addLayout(saveRow);

    rightVbox->addStretch(1);
    body->addLayout(rightVbox, 1);
    contentLayout->addLayout(body, 1);

    m_autoConnectCheck = new QCheckBox("Auto-connect previous radio on startup", content);
    m_autoConnectCheck->setChecked(AppSettings::instance().value("autoConnect", "True").toBool());
    contentLayout->addWidget(m_autoConnectCheck);

    const sdr9700::ui::DialogFooter footer = sdr9700::ui::createDialogFooter(content);
    footer.buttonBox->setObjectName(QStringLiteral("radioChooserButtonBox"));
    auto* cancelBtn = footer.buttonBox->addButton(QDialogButtonBox::Cancel);
    m_connectBtn = footer.buttonBox->addButton(QStringLiteral("Connect"), QDialogButtonBox::AcceptRole);
    m_connectBtn->setObjectName(QStringLiteral("connectRadioButton"));
    m_connectBtn->setDefault(true);
    m_connectBtn->setEnabled(false);
    contentLayout->addWidget(footer.widget);
    root->addWidget(content, 1);

    setFormEnabled(false);

    connect(m_list, &QListWidget::currentRowChanged, this, &RadioChooserDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { onConnect(); });
    connect(m_addBtn, &QPushButton::clicked, this, &RadioChooserDialog::onAddProfile);
    connect(m_removeBtn, &QPushButton::clicked, this, &RadioChooserDialog::onRemoveProfile);
    connect(m_saveBtn, &QPushButton::clicked, this, &RadioChooserDialog::onSaveProfile);
    connect(m_connectBtn, &QPushButton::clicked, this, &RadioChooserDialog::onConnect);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, &RadioChooserDialog::onAutoConnectToggled);
    connect(m_showPassBtn, &QPushButton::toggled, this,
            [this](bool on)
            {
                m_passEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
                m_showPassBtn->setText(on ? "Hide" : "Show");
            });

    const auto markDirty = [this]() { setFormDirty(true); };
    connect(m_nameEdit, &QLineEdit::textChanged, this, markDirty);
    connect(m_hostEdit, &QLineEdit::textChanged, this, markDirty);
    connect(m_portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    connect(m_userEdit, &QLineEdit::textChanged, this, markDirty);
    connect(m_passEdit, &QLineEdit::textChanged, this, markDirty);

    rebuildList();
}

void RadioChooserDialog::rebuildList()
{
    const QUuid lastId = RadioProfileStore::instance().lastProfileId();
    const QUuid selectedId = m_currentId.isNull() ? lastId : m_currentId;

    m_list->blockSignals(true);
    m_list->clear();
    for (const RadioProfile& p : RadioProfileStore::instance().profiles())
    {
        const QString label =
            QString("%1\n%2").arg(p.name.isEmpty() ? "(unnamed)" : p.name, p.host.isEmpty() ? "No host" : p.host);
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, p.id.toString());
        m_list->addItem(item);
        if (p.id == selectedId)
        {
            m_list->setCurrentItem(item);
        }
    }
    m_list->blockSignals(false);

    if (!m_list->currentItem() && m_list->count() > 0)
    {
        m_list->setCurrentRow(0);
    }

    onSelectionChanged();
}

void RadioChooserDialog::onSelectionChanged()
{
    auto* item = m_list->currentItem();
    m_isNewProfile = false;
    m_connectBtn->setEnabled(item != nullptr);
    if (!item)
    {
        m_currentId = QUuid();
        setFormEnabled(false);
        QSignalBlocker b1(m_nameEdit), b2(m_hostEdit), b3(m_portSpin), b4(m_userEdit), b5(m_passEdit);
        m_nameEdit->clear();
        m_hostEdit->clear();
        m_portSpin->setValue(50001);
        m_userEdit->clear();
        m_passEdit->clear();
        setFormDirty(false);
        return;
    }

    m_currentId = QUuid(item->data(Qt::UserRole).toString());
    RadioProfileStore& store = RadioProfileStore::instance();
    const RadioProfile* profile = store.profileForUse(m_currentId);
    if (profile)
    {
        loadProfileIntoForm(*profile);
        setFormEnabled(true);
        if (store.hasUnreadablePassword(profile->id) && !m_warnedUnreadablePasswords.contains(profile->id))
        {
            m_warnedUnreadablePasswords.insert(profile->id);
            const QString profileName = profile->name;
            QTimer::singleShot(0, this,
                               [this, profileName]()
                               {
                                   sdr9700::ui::showWarning(
                                       this, QStringLiteral("Radio Profile Password"),
                                       QStringLiteral("The saved password for \"%1\" could not be decrypted. "
                                                      "The profile was retained; enter its password again before "
                                                      "connecting.")
                                           .arg(profileName));
                               });
        }
    }
}

void RadioChooserDialog::onConnect()
{
    auto* item = m_list->currentItem();
    if (!item)
    {
        return;
    }

    if (m_formDirty)
    {
        onSaveProfile();
        if (m_formDirty)
        {
            return;
        }
    }

    const QUuid id = m_currentId.isNull() ? QUuid(item->data(Qt::UserRole).toString()) : m_currentId;
    const RadioProfile* p = RadioProfileStore::instance().profileById(id);
    if (!p)
    {
        return;
    }

    if (!RadioProfileStore::instance().setLastProfileId(id))
    {
        sdr9700::ui::showWarning(this, QStringLiteral("Radio Chooser"),
                                 QStringLiteral("Could not save the last selected radio target."));
    }
    emit connectRequested(id);
    accept();
}

void RadioChooserDialog::onAddProfile()
{
    {
        QSignalBlocker listBlocker(m_list);
        m_list->setCurrentItem(nullptr);
    }
    m_currentId = QUuid::createUuid();
    m_isNewProfile = true;
    setFormEnabled(true);
    {
        QSignalBlocker b1(m_nameEdit), b2(m_hostEdit), b3(m_portSpin), b4(m_userEdit), b5(m_passEdit);
        m_nameEdit->setText(QStringLiteral("New Radio"));
        m_hostEdit->clear();
        m_portSpin->setValue(50001);
        m_userEdit->clear();
        m_passEdit->clear();
    }
    setFormDirty(false);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void RadioChooserDialog::onRemoveProfile()
{
    if (m_currentId.isNull())
    {
        return;
    }

    const RadioProfile* profile = RadioProfileStore::instance().profileById(m_currentId);
    const QString name = profile ? profile->name : "this radio";
    if (!sdr9700::ui::confirmAction(this, QStringLiteral("Remove Radio"), QStringLiteral("Remove \"%1\"?").arg(name),
                                    QStringLiteral("Remove"), true))
    {
        return;
    }

    if (!RadioProfileStore::instance().removeProfile(m_currentId))
    {
        sdr9700::ui::showWarning(this, QStringLiteral("Remove Radio"),
                                 QStringLiteral("Could not remove the selected radio target."));
        return;
    }

    m_currentId = QUuid();
    rebuildList();
}

void RadioChooserDialog::onSaveProfile()
{
    if (m_currentId.isNull())
    {
        return;
    }

    RadioProfile profile = profileFromForm();
    if (profile.host.isEmpty())
    {
        sdr9700::ui::showWarning(this, QStringLiteral("Save Radio"),
                                 QStringLiteral("Enter the radio host name or IP address."));
        m_hostEdit->setFocus();
        return;
    }
    if (profile.name.isEmpty())
    {
        profile.name = profile.host;
    }

    const bool saved = m_isNewProfile ? RadioProfileStore::instance().addProfile(profile)
                                      : RadioProfileStore::instance().updateProfile(profile);
    if (!saved)
    {
        sdr9700::ui::showWarning(this, QStringLiteral("Save Radio"),
                                 QStringLiteral("Could not save the radio target."));
        return;
    }

    m_isNewProfile = false;
    setFormDirty(false);
    rebuildList();
}

void RadioChooserDialog::onAutoConnectToggled(bool on)
{
    AppSettings::instance().setValue("autoConnect", on);
}

void RadioChooserDialog::loadProfileIntoForm(const RadioProfile& profile)
{
    QSignalBlocker b1(m_nameEdit), b2(m_hostEdit), b3(m_portSpin), b4(m_userEdit), b5(m_passEdit);
    m_nameEdit->setText(profile.name);
    m_hostEdit->setText(profile.host);
    m_portSpin->setValue(profile.port);
    m_userEdit->setText(profile.username);
    m_passEdit->setText(profile.password);
    setFormDirty(false);
}

RadioProfile RadioChooserDialog::profileFromForm() const
{
    RadioProfile profile;
    profile.id = m_currentId;
    profile.name = m_nameEdit->text().trimmed();
    profile.host = m_hostEdit->text().trimmed();
    profile.port = static_cast<quint16>(m_portSpin->value());
    profile.username = m_userEdit->text().trimmed();
    profile.password = m_passEdit->text();
    return profile;
}

void RadioChooserDialog::setFormEnabled(bool enabled)
{
    m_nameEdit->setEnabled(enabled);
    m_hostEdit->setEnabled(enabled);
    m_portSpin->setEnabled(enabled);
    m_userEdit->setEnabled(enabled);
    m_passEdit->setEnabled(enabled);
    m_showPassBtn->setEnabled(enabled);
    m_removeBtn->setEnabled(enabled && !m_isNewProfile);
    m_connectBtn->setEnabled(enabled && !m_isNewProfile);
    if (!enabled)
    {
        setFormDirty(false);
    }
}

void RadioChooserDialog::setFormDirty(bool dirty)
{
    m_formDirty = dirty && !m_currentId.isNull();
    m_saveBtn->setEnabled(m_formDirty);
}
