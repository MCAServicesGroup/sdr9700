#pragma once

#include <QAudioDevice>
#include <QList>
#include <QWidget>

class QComboBox;
class QMediaDevices;
class QLabel;

class AudioDevicesSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit AudioDevicesSettingsPanel(QWidget* parent = nullptr);
    void setAudioConnectionState(bool connected, int activeOutputChannels);

  signals:
    void audioSettingsChanged();

  private:
    void updateCodecPendingState();
    void refreshInputDevices();
    void refreshOutputDevices();
    static void repopulateDeviceCombo(QComboBox* combo, const QList<QAudioDevice>& devices,
                                      const QByteArray& preferredID, const QAudioDevice& defaultDevice);

    QComboBox* m_inputCombo{nullptr};
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
    QMediaDevices* m_mediaDevices{nullptr};
    QWidget* m_codecNotice{nullptr};
    QLabel* m_codecPendingLabel{nullptr};
    QLabel* m_codecPendingHint{nullptr};
    bool m_audioConnected{false};
    int m_activeOutputChannels{2};
};
