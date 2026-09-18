// cppcheck-suppress-file unusedStructMember
#pragma once

#include "RadioProfile.h"
#include "UtilityWindow.h"

#include <QUuid>
#include <QSet>

class QListWidget;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QSpinBox;

class RadioChooserDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    explicit RadioChooserDialog(QWidget* parent = nullptr);

  signals:
    void connectRequested(const QUuid& profileId);

  private slots:
    void onConnect();
    void onSelectionChanged();
    void onAddProfile();
    void onRemoveProfile();
    void onSaveProfile();
    void onAutoConnectToggled(bool on);

  private:
    void rebuildList();
    void loadProfileIntoForm(const RadioProfile& profile);
    RadioProfile profileFromForm() const;
    void setFormEnabled(bool enabled);
    void setFormDirty(bool dirty);

    QListWidget* m_list{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QCheckBox* m_autoConnectCheck{nullptr};

    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_hostEdit{nullptr};
    QSpinBox* m_portSpin{nullptr};
    QLineEdit* m_userEdit{nullptr};
    QLineEdit* m_passEdit{nullptr};
    QPushButton* m_showPassBtn{nullptr};
    QPushButton* m_saveBtn{nullptr};

    QUuid m_currentId;
    QSet<QUuid> m_warnedUnreadablePasswords;
    bool m_formDirty{false};
    bool m_isNewProfile{false};
};
