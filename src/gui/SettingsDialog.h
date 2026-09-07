// cppcheck-suppress-file unusedStructMember
#pragma once

#include "UtilityWindow.h"

#include <QColor>
#include <QHash>
#include <QString>

#include <functional>

class QWidget;
class AudioDevicesSettingsPanel;
class QLabel;
class QVBoxLayout;
class QShowEvent;
class QScrollArea;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
#ifdef HAVE_HIDAPI
class IcomRC28Manager;
#endif

class SettingsDialog : public sdr9700::ui::UtilityWindow
{
    Q_OBJECT

  public:
    enum class Page
    {
        AudioDevices,
        ApplicationConfiguration,
        MemoryManager,
        SpectrumScope,
#ifdef HAVE_HIDAPI
        IcomRC28,
#endif
    };

    explicit SettingsDialog(QWidget* parent = nullptr);
#ifdef HAVE_HIDAPI
    explicit SettingsDialog(Page page, QWidget* parent = nullptr, IcomRC28Manager* icomRC28Manager = nullptr);
#else
    explicit SettingsDialog(Page page, QWidget* parent = nullptr);
#endif

    void setAudioConnectionState(bool connected, int activeOutputChannels);

  signals:
    void spectrumScopeCenterLineColorChanged(const QColor& color);
    void spectrumScopeBackgroundColorChanged(const QColor& color);
    void spectrumScopeGridLineColorChanged(const QColor& color);
    void spectrumScopeGridDensityChanged(int density);
    void reverseMouseWheelTuningChanged(bool reversed);
    void memoryPollIntervalSecondsChanged(int seconds);
    void memoryShowSpecialMemoriesChanged(bool show);
    void memoryShowSatelliteMemoriesChanged(bool show);
    void audioSettingsChanged();
#ifdef HAVE_HIDAPI
    void icomRC28EncoderSettingsChanged(const QString& field, const QString& value);
#endif

  private:
    QTreeWidgetItem* addCategory(const QString& title, const QString& tooltip, const QString& keywords);
    void addPage(QTreeWidgetItem* parent, Page page, const QString& title, const QString& tooltip,
                 const QString& keywords, std::function<QWidget*()> builder);
    void buildDeferredPage(Page page);
    void filterNavigation(const QString& text);
    void selectPage(Page page);
    void updatePageScrollGutter();
    static QString itemSearchText(QTreeWidgetItem* item);

#ifdef HAVE_HIDAPI
    IcomRC28Manager* m_icomRC28Manager{nullptr};
#endif
    AudioDevicesSettingsPanel* m_audioDevicesPanel{nullptr};
    bool m_audioConnected{false};
    int m_activeOutputChannels{2};
    QTreeWidget* m_navigation{nullptr};
    QScrollArea* m_pageScroll{nullptr};
    QVBoxLayout* m_pageLayout{nullptr};
    QStackedWidget* m_pages{nullptr};
    QLabel* m_pageTitle{nullptr};
    QHash<int, std::function<QWidget*()>> m_deferredBuilders;
    QHash<int, int> m_pageIndexes;
    QHash<int, QTreeWidgetItem*> m_pageItems;
};
