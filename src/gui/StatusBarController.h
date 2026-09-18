#pragma once

#include "MainWindow.h"
#include "SystemStats.h"

#include <QObject>

class StatusBarController : public QObject
{
  public:
    static constexpr int kRecommendedStatusMessageCharacters = 64;
    static constexpr int kMaximumStatusMessageCharacters = 72;

    explicit StatusBarController(MainWindow* window);

    void updateTransmitState(bool on);
    void updateTxDurationLabel();
    void updateStatusClock();
    void toggleStatusClockMode();
    void updateSystemStats();
    void buildStatusBar();
    void showStatusMessage(const QString& msg, int durationMs);
    void showStatusMessage(const QString& msg, int durationMs, MainWindow::StatusMessageKind kind);
    void clearPersistentStatusMessage(const QString& expectedMessage);
    void updateNetworkQuality(int rttMs);
    void setAutomationEnabled(bool enabled);
    void setAutomationClientCount(int count);

  private:
    void applyStatusMessage(const QString& message, MainWindow::StatusMessageKind kind);
    void updateStatusClockTooltip();

    MainWindow* m_window{nullptr};
    SystemStatsProvider m_systemStatsProvider;
    QString m_persistentMessage;
    MainWindow::StatusMessageKind m_persistentKind{MainWindow::StatusMessageKind::Info};
};
