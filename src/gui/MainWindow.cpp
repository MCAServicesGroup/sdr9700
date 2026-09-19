#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "SpectrumScopeDisplay.h"
#include "RadioChooserDialog.h"
#include "SettingsDialog.h"
#include "AboutDialog.h"
#include "ApplicationLogDialog.h"
#include "AutomationController.h"
#include "DataDecoderDialog.h"
#include "DialogPlacement.h"
#include "DtmfDialog.h"
#include "SpectrumScopeController.h"
#include "MainTitleBar.h"
#include "MemoryController.h"
#include "MetersDialog.h"
#include "RadioCommandController.h"
#include "StatusBarController.h"
#include "VfoController.h"
#include "VfoSelectionController.h"
#include "UiTheme.h"
#include "UtilityWindow.h"
#include "ConfigurationManager.h"
#include "backend/ConnectionRetryPolicy.h"
#include "radio/UdpStatusMessages.h"
#include "MemoryStore.h"
#include "AppBuildConfig.h"
#include "AppInfo.h"
#include "AppSettings.h"
#include "AudioDeviceSelection.h"
#include "LogCategories.h"
#include "RadioCapabilities.h"
#include "SMeterScale.h"
#include "backend/IRadioBackend.h"
#ifdef HAVE_HIDAPI
#include "IcomRC28Controller.h"
#include "core/IcomRC28Manager.h"
#endif
#include "models/RadioModel.h"
#include "models/RadioState.h"
#include "models/VfoModel.h"
#include "models/SpectrumScopeModel.h"

#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QApplication>
#include <QAudioDevice>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QColor>
#include <QFormLayout>
#include <QFile>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequence>
#include <QMediaDevices>
#include <QCloseEvent>
#include <QEvent>
#include <QFont>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QStringList>
#include <QTimer>
#include <QDateTime>
#include <QVector>
#include <QWidgetAction>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>

namespace
{
constexpr int kMemorySelectionSettleDelayMs = 3000;
}

using namespace sdr9700::ui::main_window;

MainWindow::MainWindow(RadioModel* model, QWidget* parent, bool quitApplicationOnClose)
    : QMainWindow(parent),
      m_model(model),
      m_vfo(model->vfo()),
      m_spectrumScope(model->spectrumScope()),
      m_quitApplicationOnClose(quitApplicationOnClose)
{
    m_connectedAudioOutputChannels = qBound(1, AppSettings::instance().value("audioOutputChannels", 2).toInt(), 2);
    m_spectrumScopeController = new SpectrumScopeController(this);
#ifdef HAVE_HIDAPI
    m_icomRC28Controller = new IcomRC28Controller(this);
#endif
    m_memoryController = new MemoryController(this);
    m_radioCommandController = new RadioCommandController(this);
    m_statusBarController = new StatusBarController(this);

    setWindowFlag(Qt::FramelessWindowHint);
#if defined(Q_OS_MAC)
    setWindowFlag(Qt::WindowFullscreenButtonHint, false);
#endif
    updateWindowTitle();
    setFixedSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainContent"));
    QPalette centralPalette = central->palette();
    centralPalette.setColor(QPalette::Window, QColor(QString::fromLatin1(UiTheme::Color::ContentBackground)));
    central->setPalette(centralPalette);
    central->setAutoFillBackground(true);
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    setCentralWidget(central);

    buildToolBar();
#if defined(Q_OS_MAC)
    vbox->addWidget(m_titleBar);
#endif
    buildRadioControls();
    m_spectrumScopeController->buildSpectrumScope(vbox);
    buildMemoryWindow();
    restoreWindowLayout();
    buildStatusBar();

    connect(m_model, &RadioModel::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(m_model, &RadioModel::readyChanged, this, &MainWindow::onRadioReadyChanged, Qt::QueuedConnection);
    connect(m_model, &RadioModel::scopeSyncDegradedChanged, this,
            [this](bool degraded)
            {
                const bool scopeWasSyncing = m_spectrumScopeStillSyncingAfterReady;
                m_spectrumScopeStillSyncingAfterReady = degraded;
                if (scopeWasSyncing && !degraded)
                {
                    // The ordinary ready notification was deliberately held
                    // while the scope caught up. Re-evaluate it now so the
                    // lifecycle ends with one unambiguous "Radio ready" status message.
                    m_radioUiReadyNotified = false;
                    onRadioReadyChanged(m_model && m_model->isReady());
                }
            });
    connect(m_model, &RadioModel::meterSnapshotChanged, this, &MainWindow::onMeterSnapshotChanged);
    connect(m_model, &RadioModel::pttChanged, this, &MainWindow::onPttChanged);
    connect(m_model, &RadioModel::connectionStageChanged, this, &MainWindow::onConnectionStageChanged);
    connect(m_model, &RadioModel::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_model, &RadioModel::errorOccurred, this, &MainWindow::onError);
    connect(m_model, &RadioModel::networkQualityChanged, this, &MainWindow::updateNetworkQuality);
    connect(m_model, &RadioModel::sessionHeartbeat, m_titleBar, &MainTitleBar::pulseRadioHeartbeat);
    connect(m_model->radioState(), &sdr9700::RadioState::sharedStateChanged, this,
            &MainWindow::syncControlLockFromRadioState);
    connect(m_vfo, &VfoModel::frequencyChanged, this, &MainWindow::onFrequencyChanged);
    connect(m_vfo, &VfoModel::modeChanged, this, &MainWindow::onModeChanged);
    connect(m_vfo, &VfoModel::duplexModeChanged, this, &MainWindow::onDuplexModeChanged);
    connect(m_vfo, &VfoModel::repeaterOffsetChanged, this, &MainWindow::onRepeaterOffsetChanged);
    connect(m_vfo, &VfoModel::toneAccessModeChanged, this, &MainWindow::onToneAccessModeChanged);
    connect(m_vfo, &VfoModel::toneFrequencyChanged, this, &MainWindow::onToneFrequencyChanged);
    connect(m_vfo, &VfoModel::dtcsCodeChanged, this, &MainWindow::onDtcsCodeChanged);
    if (auto* const backend = m_model->backend())
    {
        connect(backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    if (receiver != 0)
                    {
                        return;
                    }
                    switch (func)
                    {
                    case funcVFOBandMS:
                        applyActiveVfoFromRadio();
                        break;
                    case funcFreqGet:
                    case funcFreqSet:
                    case funcSelectedFreq:
                    {
                        const auto f = value.value<Frequency>();
                        if (m_applyingMemorySelection && !m_activeMemoryId.isEmpty() &&
                            f.Hz != m_activeMemoryFrequencyHz)
                        {
                            // Cross-band memory selection briefly tunes a
                            // band-default VFO frequency before command 08h
                            // selects the band-scoped channel. Keep that
                            // routing-only transition out of the display.
                            break;
                        }
                        if (f.Hz > 0)
                        {
                            m_vfoFrequencyHz = f.Hz;
                        }
                        break;
                    }
                    case funcUnselectedFreq:
                    case funcUnselectedMode:
                        // Command 25/26 unselected data is the inactive VFO inside the MAIN band,
                        // not the SUB band. Do not paint the right-hand SUB VFO from it.
                        break;
                    default:
                        break;
                    }
                });
    }

    resetRadioOwnedControlsForSync();

    onConnectionChanged(false);

#ifdef HAVE_HIDAPI
    m_icomRC28Controller->initialize();
#endif

    QTimer::singleShot(0, this, &MainWindow::tryAutoConnect);
}

bool MainWindow::startAutomationBridge()
{
    if (!m_automationController)
    {
        m_automationController = new AutomationController(this);
    }
    return m_automationController->start();
}

void MainWindow::buildToolBar()
{
#if !defined(Q_OS_MAC)
    const QString menuStyle =
        QStringLiteral("QMenu { background: %1; border: 1px solid %2; color: %3; }"
                       "QMenu::item { padding: 5px 18px 5px 10px; }"
                       "QMenu::item:selected { background: %4; color: %5; }"
                       "QMenu::separator { height: 1px; background: %6; margin: 3px 8px; }")
            .arg(UiTheme::Color::MenuPanel, UiTheme::Color::BorderMedium, UiTheme::Color::TextPrimary,
                 UiTheme::Color::AccentDark, UiTheme::Color::White, UiTheme::Color::Border);
#endif

    m_titleBar = new MainTitleBar(this);
    const QString sliderFillColor = m_titleBar->palette().color(QPalette::Highlight).name(QColor::HexRgb);
    m_titleBar->setTitle(QStringLiteral("<span style='color:%1; font-size:13px; font-weight:bold;'>%2 v%3</span>")
                             .arg(sliderFillColor, QString::fromLatin1(APP_DISPLAY_NAME).toHtmlEscaped(),
                                  QString::fromLatin1(APP_VERSION).toHtmlEscaped()));

    auto* fileMenu = new QMenu(QStringLiteral("&File"), this);
#if !defined(Q_OS_MAC)
    fileMenu->setStyleSheet(menuStyle);
#endif
    m_radioConnectionAction = fileMenu->addAction(QStringLiteral("Connect to Radio"));
    m_radioConnectionAction->setObjectName(QStringLiteral("radioConnectionAction"));
    connect(m_radioConnectionAction, &QAction::triggered, this,
            [this]()
            {
                if (!m_model || !m_model->isConnected())
                {
                    showRadioChooserDialog();
                    return;
                }

                m_userDisconnected = true;
                m_allowChooserOnDisconnect = false;
                m_reconnecting = false;
                if (m_reconnectTimer)
                {
                    m_reconnectTimer->stop();
                }
                m_model->disconnectFromRadio();
            });
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("Quit");
    quitAction->setObjectName(QStringLiteral("quitAction"));
    quitAction->setMenuRole(QAction::QuitRole);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this,
            [this]() { QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection); });
    auto* toolsMenu = new QMenu(QStringLiteral("&Tools"), this);
#if !defined(Q_OS_MAC)
    toolsMenu->setStyleSheet(menuStyle);
#endif
    toolsMenu->addAction("Data Decoder", this, &MainWindow::showDataDecoderDialog);
    toolsMenu->addAction("DTMF", this, &MainWindow::showDtmfDialog);
    toolsMenu->addAction("Memory Manager", this, &MainWindow::showMemoryWindow);
    toolsMenu->addAction("Meters", this, &MainWindow::showMetersDialog);

    auto* windowMenu = new QMenu(QStringLiteral("&Window"), this);
    windowMenu->setObjectName(QStringLiteral("windowMenu"));
    // macOS omits an empty native top-level menu when QMenuBar registers it.
    // Seed the one entry that is always valid; aboutToShow rebuilds the same
    // action with live checked state and appends visible utility windows.
    windowMenu->addAction(QStringLiteral("Main Window"));
#if !defined(Q_OS_MAC)
    windowMenu->setStyleSheet(menuStyle);
#endif
    connect(windowMenu, &QMenu::aboutToShow, this,
            [this, windowMenu]()
            {
                windowMenu->clear();

                auto addWindowAction = [this, windowMenu](const QString& label, QWidget* target)
                {
                    auto* action = windowMenu->addAction(label);
                    action->setCheckable(true);
                    action->setChecked(target && target->isActiveWindow());
                    const QPointer<QWidget> guardedTarget(target);
                    connect(action, &QAction::triggered, this,
                            [this, guardedTarget]()
                            {
                                if (!guardedTarget)
                                {
                                    return;
                                }
                                if (guardedTarget == this)
                                {
                                    isMinimized() ? showNormal() : show();
                                    raise();
                                    activateWindow();
                                    if (QWindow* handle = windowHandle())
                                    {
                                        handle->requestActivate();
                                    }
                                    return;
                                }
                                bringDialogToFront(guardedTarget);
                                if (QWindow* handle = guardedTarget->windowHandle())
                                {
                                    handle->requestActivate();
                                }
                            });
                };

                addWindowAction(QStringLiteral("Main Window"), this);

                struct OpenWindow
                {
                    QString title;
                    QWidget* widget{nullptr};
                };
                QVector<OpenWindow> openWindows;
                for (QWidget* candidate : QApplication::topLevelWidgets())
                {
                    if (!candidate || candidate == this || !candidate->isVisible() || qobject_cast<QMenu*>(candidate))
                    {
                        continue;
                    }

                    bool ownedByMainWindow = false;
                    for (QWidget* owner = candidate->parentWidget(); owner; owner = owner->parentWidget())
                    {
                        if (owner == this)
                        {
                            ownedByMainWindow = true;
                            break;
                        }
                    }
                    const QString title = candidate->windowTitle().trimmed();
                    if (ownedByMainWindow && !title.isEmpty())
                    {
                        openWindows.append({title, candidate});
                    }
                }

                std::sort(openWindows.begin(), openWindows.end(), [](const OpenWindow& left, const OpenWindow& right)
                          { return left.title.localeAwareCompare(right.title) < 0; });
                if (!openWindows.isEmpty())
                {
                    windowMenu->addSeparator();
                    for (const OpenWindow& openWindow : std::as_const(openWindows))
                    {
                        addWindowAction(openWindow.title, openWindow.widget);
                    }
                }
            });

    auto* helpMenu = new QMenu(QStringLiteral("&Help"), this);
#if !defined(Q_OS_MAC)
    helpMenu->setStyleSheet(menuStyle);
#endif
    helpMenu->addAction(QStringLiteral("Application Log"), this,
                        [this]()
                        {
                            if (!m_applicationLogDialog)
                            {
                                m_applicationLogDialog = new ApplicationLogDialog(this);
                            }
                            m_applicationLogDialog->showCentered();
                        });
    helpMenu->addSeparator();
    auto* aboutAction = helpMenu->addAction("About", this,
                                            [this]()
                                            {
                                                AboutDialog dlg(this);
                                                centerPopupWindow(&dlg);
                                                dlg.exec();
                                            });

#if defined(Q_OS_MAC)
    aboutAction->setMenuRole(QAction::NoRole);
    auto* settingsMenu = new QMenu(QStringLiteral("&Settings"), this);
    auto* settingsAction =
        settingsMenu->addAction(QStringLiteral("Settings…"), this, [this]() { showSettingsDialog(); });
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    settingsAction->setMenuRole(QAction::NoRole);

    QMenuBar* nativeMenuBar = menuBar();
    nativeMenuBar->setNativeMenuBar(true);
    nativeMenuBar->addMenu(fileMenu);
    nativeMenuBar->addMenu(settingsMenu);
    nativeMenuBar->addMenu(toolsMenu);
    nativeMenuBar->addMenu(windowMenu);
    nativeMenuBar->addMenu(helpMenu);
    nativeMenuBar->setVisible(true);
#else
    Q_UNUSED(aboutAction);
    m_titleBar->addMenu(QStringLiteral("&File"), fileMenu);
    auto* settingsAction = m_titleBar->addAction(QStringLiteral("&Settings"), this, [this]() { showSettingsDialog(); });
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    m_titleBar->addMenu(QStringLiteral("&Tools"), toolsMenu);
    m_titleBar->addMenu(QStringLiteral("&Window"), windowMenu);
    m_titleBar->addMenu(QStringLiteral("&Help"), helpMenu);
#endif

    connect(m_titleBar, &MainTitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &MainTitleBar::closeRequested, this, &QWidget::close);
    connect(m_titleBar, &MainTitleBar::muteToggled, this, &MainWindow::toggleMute);
    connect(m_titleBar, &MainTitleBar::lockToggled, this, &MainWindow::toggleControlLock);
    connect(m_titleBar, &MainTitleBar::txDurationResetRequested, this,
            [this]()
            {
                if (m_txActive)
                {
                    m_txElapsed.restart();
                }
                m_titleBar->setTxDuration(QStringLiteral("00:00:00"), m_txActive);
            });
    connect(m_titleBar, &MainTitleBar::volumeChanged, this,
            [this](int value)
            {
                if (!radioUiReady())
                {
                    return;
                }
                const int bounded = qBound(0, value, 255);
                m_currentAfGain = bounded;
                AppSettings::instance().setValueDeferred(QStringLiteral("volumeLevel"), bounded);
                if (auto* backend = m_model->backend())
                {
                    backend->setAfGain(bounded);
                }
            });

#if !defined(Q_OS_MAC)
    menuBar()->setVisible(false);
    setMenuWidget(m_titleBar);
#endif
}

void MainWindow::buildMemoryWindow()
{
    m_memoryController->buildMemoryWindow();
}

void MainWindow::centerPopupWindow(QWidget* popup) const
{
    sdr9700::ui::centerWindowOn(popup, this);
}

void MainWindow::bringDialogToFront(QWidget* dialog) const
{
    if (!dialog)
    {
        return;
    }

    if (dialog->isMinimized())
    {
        dialog->showNormal();
    }
    else
    {
        dialog->show();
    }

    centerPopupWindow(dialog);
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::showSettingsDialog()
{
    if (m_settingsDialog)
    {
        bringDialogToFront(m_settingsDialog);
        return;
    }

#ifdef HAVE_HIDAPI
    auto* dlg = new SettingsDialog(SettingsDialog::Page::AudioDevices, this, m_icomRC28Manager);
#else
    auto* dlg = new SettingsDialog(this);
#endif
    m_settingsDialog = dlg;
    dlg->setAudioConnectionState(m_model && m_model->isConnected(), m_connectedAudioOutputChannels);
    connect(dlg, &QObject::destroyed, this,
            [this, dlg]()
            {
                if (m_settingsDialog == dlg)
                {
                    m_settingsDialog = nullptr;
                }
            });
    connect(dlg, &SettingsDialog::reverseMouseWheelTuningChanged, m_spectrumScopeDisplay,
            &SpectrumScopeDisplay::setInvertMouseWheel);
    connect(dlg, &SettingsDialog::spectrumScopeCenterLineColorChanged, m_spectrumScopeDisplay,
            &SpectrumScopeDisplay::setVfoMarkerColor);
    connect(dlg, &SettingsDialog::spectrumScopeBackgroundColorChanged, m_spectrumScopeDisplay,
            &SpectrumScopeDisplay::setBackgroundColor);
    connect(dlg, &SettingsDialog::spectrumScopeGridLineColorChanged, m_spectrumScopeDisplay,
            &SpectrumScopeDisplay::setGridLineColor);
    connect(dlg, &SettingsDialog::spectrumScopeGridDensityChanged, m_spectrumScopeDisplay,
            &SpectrumScopeDisplay::setGridDensity);
    connect(dlg, &SettingsDialog::memoryPollIntervalSecondsChanged, m_memoryController,
            &MemoryController::setMemoryPollIntervalSeconds);
    connect(dlg, &SettingsDialog::memoryShowSpecialMemoriesChanged, m_memoryController,
            &MemoryController::setShowSpecialMemories);
    connect(dlg, &SettingsDialog::memoryShowSatelliteMemoriesChanged, m_memoryController,
            &MemoryController::setShowSatelliteMemories);
    connect(dlg, &SettingsDialog::audioSettingsChanged, this, &MainWindow::scheduleAudioSettingsApply);
#ifdef HAVE_HIDAPI
    connect(dlg, &SettingsDialog::icomRC28EncoderSettingsChanged, this,
            [this](const QString&, const QString&)
            {
                refreshIcomRC28EncoderSettings();
                if (!m_icomRC28AutoSnap && m_icomRC28SnapTimer)
                {
                    m_icomRC28SnapTimer->stop();
                }
            });
#endif
    centerPopupWindow(dlg);
    QTimer::singleShot(0, dlg, [this, dlg]() { centerPopupWindow(dlg); });
    QPointer<SettingsDialog> dlgGuard = dlg;
    connect(dlg, &QDialog::finished, this,
            [this, dlgGuard]()
            {
                if (m_settingsDialog == dlgGuard)
                {
                    m_settingsDialog = nullptr;
                }

                m_lanModValue = qBound(0, AppSettings::instance().value("LANModLevel", 128).toInt(), 255);
                if (m_mainVfoController)
                {
                    m_mainVfoController->setLanModLevel(m_lanModValue);
                }
                m_model->setLanModLevel(m_lanModValue);
                m_spectrumScopeDisplay->setInvertMouseWheel(
                    AppSettings::instance()
                        .value(QString::fromLatin1(kSpectrumScopeInvertMouseWheelSettingsKey), "False")
                        .toBool());
#ifdef HAVE_HIDAPI
                refreshIcomRC28EncoderSettings();
#endif
                if (dlgGuard)
                {
                    dlgGuard->deleteLater();
                }
            });
    dlg->setWindowModality(Qt::NonModal);
    dlg->show();
    bringDialogToFront(dlg);
}

void MainWindow::showMemoryWindow()
{
    m_memoryController->showMemoryWindow();
}

QString MainWindow::selectedMemoryId() const
{
    return m_memoryController->selectedMemoryId();
}

void MainWindow::selectMemoryById(const QString& id, bool showDialogOnFailure)
{
    m_memoryController->selectMemoryById(id, showDialogOnFailure);
}

void MainWindow::editSelectedMemory()
{
    m_memoryController->editSelectedMemory();
}

void MainWindow::copySelectedMemory()
{
    m_memoryController->copySelectedMemory();
}

void MainWindow::removeSelectedMemory()
{
    m_memoryController->removeSelectedMemory();
}

void MainWindow::moveSelectedMemoryUp()
{
    m_memoryController->moveSelectedMemoryUp();
}

void MainWindow::moveSelectedMemoryDown()
{
    m_memoryController->moveSelectedMemoryDown();
}

void MainWindow::moveSelectedMemory(int direction)
{
    m_memoryController->moveSelectedMemory(direction);
}

void MainWindow::updateWindowTitle()
{
    QString title =
        QStringLiteral("%1 v%2").arg(QString::fromLatin1(APP_DISPLAY_NAME), QString::fromLatin1(APP_VERSION));
#if SDR9700_DEBUG_BUILD
    title += QStringLiteral(" (DEBUG)");
#endif
    setWindowTitle(title);
}

void MainWindow::storeCurrentMemory()
{
    m_memoryController->storeCurrentMemory();
}

void MainWindow::showMemoryEditor(const QString& memoryId)
{
    m_memoryController->showMemoryEditor(memoryId);
}

void MainWindow::reloadMemoryTable()
{
    m_memoryController->reloadMemoryTable();
}

void MainWindow::buildRadioControls()
{
    m_lanModValue = qBound(0, AppSettings::instance().value("LANModLevel", 128).toInt(), 255);
    const int appVolume = appVolumeSettingValue();
    m_currentAfGain = appVolume;
    if (m_titleBar)
    {
        m_titleBar->setVolume(appVolume);
    }
    m_pttBtn = new TwoLineButton(centralWidget());
    m_pttBtn->setFixedSize(kSelectorButtonSize);
    m_pttBtn->setAccessibleName(QStringLiteral("PTT"));
    m_pttBtn->setAccessibleDescription(QStringLiteral("Hold to transmit."));
    setSelectorButtonLines(m_pttBtn, QStringLiteral("PTT"), QString());
    m_pttBtn->setCheckable(false);
    m_pttBtn->hide();

    m_dtmfDialog = new DtmfDialog(this);
    m_metersDialog = new MetersDialog(this);
    m_dtmfDialog->hide();
    m_metersDialog->hide();

    m_dtmfPttOffTimer = new QTimer(this);
    m_dtmfPttOffTimer->setSingleShot(true);
    connect(m_dtmfPttOffTimer, &QTimer::timeout, this,
            [this]()
            {
                m_vfo->setPtt(false);
                m_dtmfSendActive = false;
                if (m_dtmfDialog)
                {
                    m_dtmfDialog->setSendInProgress(false);
                }
            });

    connect(m_pttBtn, &QPushButton::pressed, this, &MainWindow::onPttPressed);
    connect(m_pttBtn, &QPushButton::released, this, &MainWindow::onPttReleased);
    connect(m_dtmfDialog, &DtmfDialog::sendRequested, this, &MainWindow::onDtmfSendRequested);
    m_currentAfGain = appVolumeSettingValue();
    m_savedAfGain = m_currentAfGain;
    resetRadioOwnedControlsForSync();
}

void MainWindow::updateTransmitState(bool on)
{
    m_statusBarController->updateTransmitState(on);
}

void MainWindow::updateTxDurationLabel()
{
    m_statusBarController->updateTxDurationLabel();
}

void MainWindow::updateStatusClock()
{
    m_statusBarController->updateStatusClock();
}

void MainWindow::toggleStatusClockMode()
{
    m_statusBarController->toggleStatusClockMode();
}

void MainWindow::updateSystemStats()
{
    m_statusBarController->updateSystemStats();
}

void MainWindow::buildStatusBar()
{
    m_statusBarController->buildStatusBar();
}

void MainWindow::showRadioChooserDialog()
{
    if (m_radioChooserDialog)
    {
        bringDialogToFront(m_radioChooserDialog);
        return;
    }

    auto* dlg = new RadioChooserDialog(this);
    m_radioChooserDialog = dlg;
    connect(dlg, &QObject::destroyed, this,
            [this, dlg]()
            {
                if (m_radioChooserDialog == dlg)
                {
                    m_radioChooserDialog = nullptr;
                }
            });
    connect(dlg, &RadioChooserDialog::connectRequested, this,
            [this](const QUuid& id)
            {
                const RadioProfile* p = RadioProfileStore::instance().profileForUse(id);
                if (p)
                {
                    onConnectToProfile(*p);
                }
            });
    centerPopupWindow(dlg);
    dlg->exec();
    // QPointer becomes null if nested event processing destroyed the dialog.
    // Otherwise defer this dialog's deletion until control returns to the main
    // event loop; the identity check prevents touching a replacement dialog.
    if (m_radioChooserDialog == dlg)
    {
        dlg->deleteLater();
        m_radioChooserDialog = nullptr;
    }
}

void MainWindow::onConnectToProfile(const RadioProfile& profile)
{
    m_connectedAudioOutputChannels = qBound(1, AppSettings::instance().value("audioOutputChannels", 2).toInt(), 2);
    m_pendingProfileId = profile.id;
    m_memoryController->setRadioProfileId(profile.id);
    m_radioHost = profile.host;
    m_radioPort = profile.port;
    m_radioUsername = profile.username;
    m_userDisconnected = false;
    // The backend validates the profile synchronously. Publish Connecting before
    // entering it so an invalid host/port cannot leave the previous UI state on
    // screen while an error status message is shown.
    onConnectionStageChanged(ConnectionStage::Connecting, QStringLiteral("Connecting to radio"));
    m_model->connectToRadio(profile.host, profile.port, profile.username, profile.password);
}

void MainWindow::tryAutoConnect()
{
    RadioProfileStore& store = RadioProfileStore::instance();
    store.load();
    m_allowChooserOnDisconnect = false;

    const bool autoConnect = AppSettings::instance().value("autoConnect", "True").toBool();
    const RadioProfile* autoConnectProfile = nullptr;
    if (autoConnect)
    {
        const QUuid lastId = store.lastProfileId();
        if (!lastId.isNull())
        {
            autoConnectProfile = store.profileForUse(lastId);
        }
    }

    if (autoConnectProfile && !store.hasUnreadablePassword(autoConnectProfile->id))
    {
        onConnectToProfile(*autoConnectProfile);
        return;
    }

    showRadioChooserDialog();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_shutdownStarted)
    {
        event->accept();
        QMainWindow::closeEvent(event);
        if (m_quitApplicationOnClose)
        {
            // The first shutdown phase hides the window before this accepted
            // close. Qt therefore cannot infer that the last visible primary
            // window was closed, and auxiliary windows can leave the event
            // loop alive. The main window owns the application lifetime, so
            // explicitly finish it after native close handling unwinds.
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        }
        return;
    }
    m_shutdownStarted = true;

    // Stop receive playback before hiding the window or beginning any other
    // shutdown work. This is a bounded synchronous handoff so queued audio
    // already held by the output worker cannot remain audible after the UI
    // disappears.
    if (m_model)
    {
        m_model->stopLocalAudio();
    }

    saveWindowLayout();
#ifdef HAVE_HIDAPI
    if (m_icomRC28Controller)
    {
        m_icomRC28Controller->close();
    }
#endif
    m_userDisconnected = true;

    // Leave the native close/mouse event before performing synchronous radio
    // teardown. macOS crash reports have shown both Qt view updates and
    // Objective-C autorelease cleanup re-entered when disconnecting directly
    // inside closeEvent().
    event->ignore();
    hide();

    if (m_model)
    {
        // RadioBackend::shutdownConnection() emits readyChanged(false).
        // MemorySyncController normally responds by finalizing its refresh and
        // rebuilding the memory table. During closeEvent that re-enters Qt
        // item-view and font layout while the native window is tearing down;
        // macOS crash reports show this path failing inside CoreText. Stop all
        // model-to-UI delivery before beginning the synchronous disconnect.
        QObject::disconnect(m_model, nullptr, nullptr, nullptr);
    }

    QTimer::singleShot(0, this,
                       [this]()
                       {
                           if (m_model)
                           {
                               m_model->disconnectFromRadio();
                           }
                           // Re-enter closeEvent only after the originating native event and
                           // its autorelease pool have unwound. m_shutdownStarted makes this
                           // second pass accept immediately.
                           QMainWindow::close();
                       });
}

void MainWindow::restoreWindowLayout()
{
    const QSize fixedSize(UiTheme::Size::MainWindowMinWidth, UiTheme::Size::MainWindowMinHeight);
    const bool hasSavedPos = AppSettings::instance().contains(QStringLiteral("mainWindowPositionX")) &&
                             AppSettings::instance().contains(QStringLiteral("mainWindowPositionY"));
    if (hasSavedPos)
    {
        const QPoint savedTopLeft(AppSettings::instance().value(QStringLiteral("mainWindowPositionX")).toInt(),
                                  AppSettings::instance().value(QStringLiteral("mainWindowPositionY")).toInt());
        const QRect savedRect(savedTopLeft, fixedSize);
        const QPoint pos = availableScreenContains(savedRect)
                               ? savedTopLeft
                               : centeredRectInAvailableGeometry(fixedSize, availableGeometryFor(savedRect)).topLeft();
        move(pos);
    }
}

void MainWindow::saveWindowLayout() const
{
    AppSettings::instance().setValue("mainWindowPositionX", normalGeometry().x());
    AppSettings::instance().setValue("mainWindowPositionY", normalGeometry().y());
}

void MainWindow::updateSpectrumVfoMarker()
{
    m_spectrumScopeController->updateSpectrumVfoMarker();
}

void MainWindow::setRadioControlsEnabled(bool enabled)
{
    const bool controlsEnabled = enabled && !m_controlsLocked;
    if (m_vfoSelectionController)
    {
        m_vfoSelectionController->setRadioReady(enabled);
        m_vfoSelectionController->setControlsEnabled(controlsEnabled);
    }
    if (m_mainVfoController)
    {
        m_mainVfoController->setUserInteractionEnabled(enabled);
        m_mainVfoController->setTuningInteractionEnabled(controlsEnabled);
    }
    if (m_subVfoController)
    {
        m_subVfoController->setUserInteractionEnabled(enabled);
        m_subVfoController->setTuningInteractionEnabled(controlsEnabled);
    }
    if (m_titleBar)
    {
        m_titleBar->setVolumeEnabled(enabled);
        m_titleBar->setLockEnabled(enabled && m_controlLockKnown);
    }
    if (m_pttBtn)
    {
        m_pttBtn->setEnabled(enabled && m_vfoPttReady);
    }
    if (m_spectrumScopeDisplay)
    {
        const bool scopeReady = m_spectrumScopeController && m_spectrumScopeController->interactionReady();
        m_spectrumScopeDisplay->setInteractionLocked(!scopeReady);
        if (m_vfoSelectionController)
        {
            m_vfoSelectionController->setReceiverContextReady(scopeReady);
        }
    }
}

bool MainWindow::radioUiReady() const
{
    return m_model && m_model->isConnected() && m_model->isReady();
}

void MainWindow::resetRadioOwnedControlsForSync()
{
    m_vfoFrequencyHz = 0;
    m_meterSnapshot = {};
    m_lanModValue = qBound(0, AppSettings::instance().value("LANModLevel", 128).toInt(), 255);
    m_duplexMode = dmSimplex;
    m_toneAccessMode = ratrNN;
    m_toneFrequency = 670;
    m_dtcsCode = 23;

    if (m_mainVfoController)
    {
        m_mainVfoController->setLanModLevel(m_lanModValue);
        m_mainVfoController->clearFrequency();
    }
    if (m_subVfoController)
    {
        m_subVfoController->clearFrequency();
    }

    updateTransmitState(false);
    if (m_metersDialog)
    {
        m_metersDialog->resetMeters();
    }
}

void MainWindow::applyActiveVfoFromRadio()
{
    updateSpectrumVfoMarker();
}

void MainWindow::toggleControlLock()
{
    if (!m_controlLockKnown || !m_model || !m_model->isReady())
    {
        return;
    }
    if (auto* backend = m_model->backend())
    {
        backend->setDialLockEnabled(!m_controlsLocked);
    }
}

void MainWindow::syncControlLockFromRadioState()
{
    const auto* state = m_model ? m_model->radioState() : nullptr;
    const std::optional<bool> reportedLock = state ? state->shared().dialLockEnabled : std::nullopt;
    m_controlLockKnown = reportedLock.has_value();
    m_controlsLocked = reportedLock.value_or(false);
    updateControlLockIndicator();
    setRadioControlsEnabled(radioUiReady());
    updateIcomRC28Leds();
}

void MainWindow::toggleMute()
{
    m_radioCommandController->toggleMute();
}

void MainWindow::cycleMode()
{
    m_radioCommandController->cycleMode();
}

#ifdef HAVE_HIDAPI
void MainWindow::dispatchIcomRC28Action(const QString& action)
{
    m_icomRC28Controller->dispatchIcomRC28Action(action);
}

void MainWindow::setIcomRC28Ptt(bool on)
{
    m_icomRC28Controller->setIcomRC28Ptt(on);
}

void MainWindow::updateIcomRC28Leds()
{
    m_icomRC28Controller->updateIcomRC28Leds();
}

void MainWindow::handleIcomRC28Tune(int steps)
{
    m_icomRC28Controller->handleIcomRC28Tune(steps);
}

void MainWindow::refreshIcomRC28EncoderSettings()
{
    m_icomRC28Controller->refreshIcomRC28EncoderSettings();
}

void MainWindow::handleIcomRC28Button(int button, int action)
{
    m_icomRC28Controller->handleIcomRC28Button(button, action);
}

#else

// cppcheck-suppress functionStatic
void MainWindow::updateIcomRC28Leds() {}

#endif

void MainWindow::updateControlLockIndicator()
{
    const bool presentConfirmedState = m_controlLockKnown && radioUiReady();
    if (m_titleBar)
    {
        if (presentConfirmedState)
        {
            m_titleBar->setLocked(m_controlsLocked);
        }
        else
        {
            m_titleBar->setLockStateUnknown();
        }
    }

    if (!m_lockIndicator)
    {
        return;
    }

    const QString lockIcon = !presentConfirmedState ? QStringLiteral(":/images/icons/control_lock_unknown.svg")
                             : m_controlsLocked     ? QStringLiteral(":/images/icons/control_lock_locked.svg")
                                                    : QStringLiteral(":/images/icons/control_lock_unlocked.svg");
    m_lockIndicator->setPixmap(QIcon(lockIcon).pixmap(QSize(21, 21)));
    m_lockIndicator->setStyleSheet(
        presentConfirmedState && m_controlsLocked
            ? QStringLiteral("QLabel { background: %1; border: 1px solid %2; border-radius: 4px; padding: 0px 1px; }")
                  .arg(UiTheme::Color::PanelDark, UiTheme::Color::Warning)
            : QStringLiteral("QLabel { background: transparent; border: none; padding: 0px 1px; }"));
    const QString tooltip = !presentConfirmedState ? QStringLiteral("Dial lock state syncing")
                            : m_controlsLocked     ? QStringLiteral("Dial Locked\nClick to unlock.")
                                                   : QStringLiteral("Dial Unlocked\nClick to lock.");
    m_lockIndicator->setToolTip(tooltip);
    if (m_lockWidget)
    {
        m_lockWidget->setToolTip(tooltip);
        m_lockWidget->setAccessibleName(!presentConfirmedState ? QStringLiteral("Dial lock state syncing")
                                        : m_controlsLocked     ? QStringLiteral("Dial locked")
                                                               : QStringLiteral("Dial unlocked"));
    }
}

void MainWindow::updateSpectrumScopeBandLimits(quint64 hz)
{
    m_spectrumScopeController->updateSpectrumScopeBandLimits(hz);
}

int MainWindow::tuningStepHz() const
{
    return m_radioCommandController->tuningStepHz();
}

void MainWindow::applyRadioTuningStep()
{
    m_radioCommandController->applyRadioTuningStep();
}

void MainWindow::applySpectrumScopeSettings()
{
    m_spectrumScopeController->applySpectrumScopeSettings();
}

void MainWindow::updateStepButton()
{
    m_spectrumScopeController->updateTuningStepSelector(tuningStepHz());
}

quint64 MainWindow::roundFrequencyToStep(quint64 hz) const
{
    return m_spectrumScopeController->roundFrequencyToStep(hz);
}

void MainWindow::panSpectrumScopeToCenter(quint64 centerHz)
{
    m_spectrumScopeController->panSpectrumScopeToCenter(centerHz);
}

quint64 MainWindow::clampSpectrumScopeCenterHz(quint64 hz, double bandwidthMhz) const
{
    return m_spectrumScopeController->clampSpectrumScopeCenterHz(hz, bandwidthMhz);
}

quint64 MainWindow::clampFrequencyHzToActiveBand(quint64 hz) const
{
    return m_spectrumScopeController->clampFrequencyHzToActiveBand(hz);
}

void MainWindow::scheduleSpectrumScopeTune(quint64 hz)
{
    m_spectrumScopeController->scheduleSpectrumScopeTune(hz);
}

void MainWindow::setActiveMemory(const QString& id, quint64 frequencyHz, int mode, int duplexMode, quint64 offsetHz,
                                 int toneMode, ushort toneValue)
{
    m_activeMemoryId = id;
    m_activeMemoryMode = sdr9700::ui::main_window::memoryModeLabel(mode).toUpper();
    m_activeMemoryFrequencyHz = frequencyHz;
    m_activeMemoryDuplexMode = static_cast<duplexMode_t>(duplexMode);
    m_activeMemoryOffsetHz = offsetHz;
    m_activeMemoryToneMode = static_cast<rptAccessTxRx_t>(toneMode);
    m_activeMemoryToneValue = toneValue;
    m_activeMemoryFrequencySettled = m_vfo && m_vfo->frequencyHz() == frequencyHz;
    m_activeMemoryModeSettled = m_vfo && m_vfo->mode().compare(m_activeMemoryMode, Qt::CaseInsensitive) == 0;
    m_activeMemoryDuplexSettled = m_duplexMode == m_activeMemoryDuplexMode;
    m_activeMemoryOffsetSettled = m_activeMemoryDuplexMode == dmSimplex || m_repeaterOffsetHz == offsetHz;
    m_activeMemoryToneModeSettled = m_toneAccessMode == m_activeMemoryToneMode;
    const bool isDtcs = isDtcsToneMode(m_activeMemoryToneMode);
    m_activeMemoryToneValueSettled = toneMode == ratrNN || (isDtcs && m_dtcsCode == toneValue) ||
                                     (!isDtcs && toneMode != ratrNN && m_toneFrequency == toneValue);
    m_activeMemoryAwaitingReceiveFrequency = false;
}

void MainWindow::clearActiveMemory()
{
    m_applyingMemorySelection = false;
    m_activeMemorySelectionReleaseScheduled = false;
    m_activeMemoryFrequencySettled = false;
    m_activeMemoryModeSettled = false;
    m_activeMemoryDuplexSettled = false;
    m_activeMemoryOffsetSettled = false;
    m_activeMemoryToneModeSettled = false;
    m_activeMemoryToneValueSettled = false;
    m_activeMemoryAwaitingReceiveFrequency = false;
    if (m_activeMemoryId.isEmpty())
    {
        return;
    }

    m_activeMemoryId.clear();
    m_activeMemoryMode.clear();
    m_activeMemoryFrequencyHz = 0;
    m_activeMemoryDuplexMode = dmSimplex;
    m_activeMemoryOffsetHz = 0;
    m_activeMemoryToneMode = ratrNN;
    m_activeMemoryToneValue = 0;
}

void MainWindow::leaveMemoryModeForManualChange()
{
    if (!m_activeMemoryId.isEmpty() && m_model && m_model->isReady())
    {
        m_model->selectVfoMode();
    }
    clearActiveMemory();
}

void MainWindow::checkIfMemorySelectionComplete()
{
    if (!m_applyingMemorySelection)
    {
        return;
    }
    if (m_activeMemoryFrequencySettled && m_activeMemoryModeSettled && m_activeMemoryDuplexSettled &&
        m_activeMemoryOffsetSettled && m_activeMemoryToneModeSettled && m_activeMemoryToneValueSettled)
    {
        if (m_activeMemorySelectionReleaseScheduled)
        {
            return;
        }

        m_activeMemorySelectionReleaseScheduled = true;
        const int generation = m_memorySelectionGeneration;
        QTimer::singleShot(kMemorySelectionSettleDelayMs, this,
                           [this, generation]()
                           {
                               if (m_memorySelectionGeneration != generation)
                               {
                                   return;
                               }
                               m_applyingMemorySelection = false;
                               m_activeMemorySelectionReleaseScheduled = false;
                           });
    }
}

void MainWindow::updateConnectionTooltip()
{
    if (!m_connStateLabel || !m_connDetailLabel)
    {
        return;
    }

    QStringList lines;
    lines << QStringLiteral("Radio Connection Status");
    lines << QStringLiteral("Radio ID: %1").arg(m_model ? m_model->radioName() : QStringLiteral("IC-9700"));
    lines << QStringLiteral("State: %1").arg(m_connStateName);
    if (!m_radioHost.isEmpty())
    {
        lines << QStringLiteral("Host: %1").arg(m_radioHost);
    }
    if (m_radioPort > 0)
    {
        lines << QStringLiteral("Port: %1").arg(m_radioPort);
    }
    if (!m_radioUsername.isEmpty())
    {
        lines << QStringLiteral("Username: %1").arg(m_radioUsername);
    }

    const QString tooltip = lines.join(QLatin1Char('\n'));
    m_connStateLabel->setToolTip(tooltip);
    m_connDetailLabel->setToolTip(tooltip);
}

void MainWindow::onConnectionChanged(bool connected)
{
    if (m_settingsDialog)
    {
        m_settingsDialog->setAudioConnectionState(connected, m_connectedAudioOutputChannels);
    }

    if (m_radioConnectionAction)
    {
        m_radioConnectionAction->setText(connected ? QStringLiteral("Disconnect from Radio")
                                                   : QStringLiteral("Connect to Radio"));
    }

    setRadioControlsEnabled(radioUiReady());
    resetRadioOwnedControlsForSync();

    if (connected)
    {
        m_spectrumScopeStillSyncingAfterReady = false;
        m_reconnecting = false;
        m_lastErrorWasCredential = false;
        m_allowChooserOnDisconnect = true;
        if (m_reconnectTimer)
        {
            m_reconnectTimer->stop();
        }

        const int appVolume = appVolumeSettingValue();
        m_currentAfGain = appVolume;
        if (m_titleBar)
        {
            m_titleBar->setVolume(appVolume);
        }
        if (m_vfo)
        {
            m_vfo->setAfGain(appVolume);
        }

        if (m_connStateLabel)
        {
            m_connStateName = QStringLiteral("Syncing");
            m_connStateLabel->setText(
                QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
        }
        updateConnectionTooltip();
        if (!m_pendingProfileId.isNull())
        {
            if (!RadioProfileStore::instance().setLastProfileId(m_pendingProfileId))
            {
                showStatusMessage("Could not save selected radio profile", 8000, StatusMessageKind::Warning);
            }
        }
    }
    else
    {
        const bool hadRadioUiReady = m_radioUiReadyNotified;
        m_radioUiReadyNotified = false;
        m_spectrumScopeStillSyncingAfterReady = false;
        if (m_spectrumScopeTuneCommitTimer)
        {
            m_spectrumScopeTuneCommitTimer->stop();
        }
        if (m_spectrumScopeTuneReleaseTimer)
        {
            m_spectrumScopeTuneReleaseTimer->stop();
        }
        m_pendingSpectrumScopeTuneHz = 0;
        m_spectrumScopeDisplayCenterHz = 0;
        m_spectrumScopeFixedPanStartHz = 0;
        m_spectrumScopeFixedPanEndHz = 0;
        if (m_spectrumScope)
        {
            m_spectrumScope->clearDisplayCenterHold();
        }
        clearActiveMemory();
        m_spectrumScopeDisplay->clearDisplay();
        m_spectrumScopeDisplay->clearFrequencyPanRange();
#ifdef HAVE_HIDAPI
        m_icomRC28PttLatched = false;
#endif
        updateNetworkQuality(0);

        // Auto-reconnect is only useful after the radio reached full UI-ready
        // once. During startup sync failures, repeated reconnects can keep the
        // IC-9700 LAN server half-open and leave the operator watching an
        // endless Connecting/Syncing loop. Manual reconnect resets the attempt.
        const bool profileAvailable =
            !m_pendingProfileId.isNull() && RadioProfileStore::instance().profileById(m_pendingProfileId) != nullptr;
        const bool canReconnect = sdr9700::shouldRetryRadioConnection(hadRadioUiReady, false, m_userDisconnected,
                                                                      m_lastErrorWasCredential, profileAvailable);

        if (canReconnect)
        {
            scheduleRadioReconnect();
        }
        else
        {
            const bool wasUserDisconnected = m_userDisconnected;
            m_reconnecting = false;
            m_userDisconnected = false;
            m_lastErrorWasCredential = false;
            if (m_connStateLabel)
            {
                m_connStateName = QStringLiteral("Disconnected");
                m_connStateLabel->setText(
                    QStringLiteral("<span style='color:%1'>Disconnected</span>").arg(UiTheme::Color::TextStatusLabel));
            }
            updateConnectionTooltip();
            // Do not reopen the chooser for a failed startup sync. The radio
            // was contacted but never became operable, so throwing the chooser
            // over the failure hides the useful connection/status message. Once
            // the radio has reached full UI-ready, normal disconnect handling
            // may still offer the chooser.
            if (hadRadioUiReady && !wasUserDisconnected && m_allowChooserOnDisconnect)
            {
                QTimer::singleShot(0, this, [this]() { showRadioChooserDialog(); });
            }
        }
    }
}

void MainWindow::onRadioReadyChanged(bool ready)
{
    const bool connected = m_model->isConnected();
    const bool uiReady = connected && ready;
    const bool notifyReady = uiReady && !m_spectrumScopeStillSyncingAfterReady && !m_radioUiReadyNotified;
    m_radioUiReadyNotified = uiReady;
    setRadioControlsEnabled(uiReady);
    updateControlLockIndicator();
    if (!m_connStateLabel || !connected)
    {
        return;
    }

    if (uiReady)
    {
        applyRadioTuningStep();
        applySpectrumScopeSettings();
        if (m_spectrumScopeStillSyncingAfterReady)
        {
            m_connStateName = QStringLiteral("Syncing");
            m_connStateLabel->setText(
                QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
            m_connectionStatusMessage = QStringLiteral("Synchronizing spectrum scope");
            showStatusMessage(m_connectionStatusMessage, 0);
            updateConnectionTooltip();
            return;
        }
        m_connStateName = QStringLiteral("Connected");
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>Connected</span>").arg(UiTheme::Color::Success));
        if (notifyReady)
        {
            // Backend readiness means both receivers have confirmed frequency
            // and mode. Memory synchronization continues independently after
            // controls become available.
            clearPersistentStatusMessage(m_connectionStatusMessage);
            m_connectionStatusMessage.clear();
            showStatusMessage(QStringLiteral("Radio ready"), 5000);
        }
    }
    else
    {
        m_connStateName = QStringLiteral("Syncing");
        m_connStateLabel->setText(QStringLiteral("<span style='color:%1'>Syncing</span>").arg(UiTheme::Color::Warning));
    }
    updateConnectionTooltip();
}

void MainWindow::onFrequencyChanged(quint64 hz)
{
    if (m_applyingMemorySelection && !m_activeMemoryId.isEmpty() && hz != m_activeMemoryFrequencyHz)
    {
        // See the radioValueUpdated path above. VfoModel also publishes the
        // same transient frequency, so suppress it here until the selected
        // memory's authoritative frequency arrives.
        return;
    }

    if (!m_activeMemoryId.isEmpty())
    {
        if (hz == m_activeMemoryFrequencyHz)
        {
            if (!m_pttActive)
            {
                m_activeMemoryAwaitingReceiveFrequency = false;
            }
            m_activeMemoryFrequencySettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryFrequencySettled && !m_applyingMemorySelection)
        {
            if (preserveMemorySelectionForReportedFrequency(m_activeMemoryFrequencyHz, hz, m_pttActive,
                                                            m_activeMemoryAwaitingReceiveFrequency))
            {
                return;
            }
            quint64 transmitMemoryHz = 0;
            if (m_activeMemoryDuplexMode == dmDupMinus && m_activeMemoryFrequencyHz > m_activeMemoryOffsetHz)
            {
                transmitMemoryHz = m_activeMemoryFrequencyHz - m_activeMemoryOffsetHz;
            }
            else if (m_activeMemoryDuplexMode == dmDupPlus)
            {
                transmitMemoryHz = m_activeMemoryFrequencyHz + m_activeMemoryOffsetHz;
            }
            if (transmitMemoryHz > 0 && hz == transmitMemoryHz)
            {
                return;
            }
            leaveMemoryModeForManualChange();
        }
    }

    if (m_spectrumScopeTuneReleaseTimer && m_spectrumScopeTuneReleaseTimer->isActive())
    {
        m_pendingSpectrumScopeTuneHz = spectrumTunePendingAfterReadback(m_pendingSpectrumScopeTuneHz, hz);
        // Keep the Spectrum Scope hold active until the release timer expires.
        // The VFO frequency readback can arrive before the radio's scope stream
        // has produced a frame centered on that new frequency. Clearing the
        // hold here lets near-frequency stale frames be drawn under the new
        // scale, which is the operator-visible "signal moved away" symptom.
    }

    m_vfoFrequencyHz = hz;
    updateSpectrumScopeBandLimits(hz);
    qInfo(logGui()).noquote().nospace() << "VfoRoute::Frequency frequencyHz=" << hz << " destination=MAIN";
    if (m_mainVfoController)
    {
        m_mainVfoController->setFrequencyHz(hz);
    }
    updateSpectrumVfoMarker();
}

void MainWindow::onModeChanged(const QString& mode)
{
    bool stateChanged = false;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode.compare(m_activeMemoryMode, Qt::CaseInsensitive) == 0)
        {
            stateChanged = !m_activeMemoryModeSettled;
            m_activeMemoryModeSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryModeSettled && !m_applyingMemorySelection)
        {
            stateChanged = true;
            clearActiveMemory();
        }
    }
    if (stateChanged)
    {
        qInfo(logGui()).noquote().nospace() << "VfoRoute::Mode mode=" << mode.toUpper() << " destination=MAIN";
    }
}

void MainWindow::onMeterSnapshotChanged(const MeterSnapshot& snapshot)
{
    if (!m_model->isReady())
    {
        return;
    }

    m_meterSnapshot = snapshot;
    if (m_metersDialog)
    {
        const bool hasAnyMeterValue =
            m_meterSnapshot.sMeterValid || m_meterSnapshot.powerValid || m_meterSnapshot.swrValid ||
            m_meterSnapshot.alcValid || m_meterSnapshot.compressionValid || m_meterSnapshot.voltageValid ||
            m_meterSnapshot.currentValid || m_meterSnapshot.txAudioPeak > 0 || m_meterSnapshot.txAudioRms > 0;
        if (!hasAnyMeterValue)
        {
            m_metersDialog->resetMeters();
        }
        else
        {
            if (m_meterSnapshot.sMeterValid)
            {
                m_metersDialog->setSMeter(m_meterSnapshot.sMeter);
            }
            if (m_meterSnapshot.powerValid)
            {
                m_metersDialog->setPowerMeter(m_meterSnapshot.powerWatts);
            }
            else
            {
                m_metersDialog->clearPowerMeter();
            }
            if (m_meterSnapshot.swrValid)
            {
                m_metersDialog->setSwr(m_meterSnapshot.swr);
            }
            else
            {
                m_metersDialog->clearSwr();
            }
            if (m_meterSnapshot.alcValid)
            {
                m_metersDialog->setAlc(m_meterSnapshot.alc);
            }
            else
            {
                m_metersDialog->clearAlc();
            }
            if (m_meterSnapshot.compressionValid)
            {
                m_metersDialog->setCompressionMeter(m_meterSnapshot.compressionDb);
            }
            else
            {
                m_metersDialog->clearCompressionMeter();
            }
            if (m_meterSnapshot.voltageValid)
            {
                m_metersDialog->setVoltageMeter(m_meterSnapshot.voltageVolts);
            }
            else
            {
                m_metersDialog->clearVoltageMeter();
            }
            if (m_meterSnapshot.currentValid)
            {
                m_metersDialog->setCurrentMeter(m_meterSnapshot.currentAmps);
            }
            else
            {
                m_metersDialog->clearCurrentMeter();
            }
            m_metersDialog->setTransmitAudioLevel(m_meterSnapshot.txAudioPeak, m_meterSnapshot.txAudioRms);
        }
    }
}

void MainWindow::showStatusMessage(const QString& msg, int durationMs, StatusMessageKind kind)
{
    m_statusBarController->showStatusMessage(msg, durationMs, kind);
}

void MainWindow::clearPersistentStatusMessage(const QString& expectedMessage)
{
    m_statusBarController->clearPersistentStatusMessage(expectedMessage);
}

void MainWindow::updateNetworkQuality(int rttMs)
{
    m_statusBarController->updateNetworkQuality(rttMs);
}

void MainWindow::onConnectionStageChanged(ConnectionStage stage, const QString& message)
{
    if (m_titleBar && (stage == ConnectionStage::Disconnected || stage == ConnectionStage::Disconnecting ||
                       stage == ConnectionStage::Failed))
    {
        m_titleBar->clearRadioHeartbeat();
    }
    const char* color = UiTheme::Color::TextStatusLabel;
    switch (stage)
    {
    case ConnectionStage::Connecting:
    case ConnectionStage::WaitingForRadio:
    case ConnectionStage::OpeningStreams:
        m_connStateName = QStringLiteral("Connecting");
        color = UiTheme::Color::Accent;
        break;
    case ConnectionStage::SyncingRadioState:
    case ConnectionStage::Ready:
        // Backend Ready means both receiver identities are usable. Spectrum
        // and memory synchronization continue without holding this state.
        m_connStateName = radioUiReady() ? QStringLiteral("Connected") : QStringLiteral("Syncing");
        color = radioUiReady() ? UiTheme::Color::Success : UiTheme::Color::Warning;
        break;
    case ConnectionStage::Reconnecting:
        m_connStateName = QStringLiteral("Reconnecting");
        color = UiTheme::Color::Danger;
        break;
    case ConnectionStage::Failed:
    case ConnectionStage::Disconnected:
        m_connStateName = QStringLiteral("Disconnected");
        break;
    case ConnectionStage::Disconnecting:
        m_connStateName = QStringLiteral("Disconnecting");
        break;
    case ConnectionStage::Unchanged:
        return;
    }

    if (m_connStateLabel)
    {
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>%2</span>").arg(QString::fromLatin1(color), m_connStateName));
    }
    updateConnectionTooltip();
    if (!message.isEmpty())
    {
        const StatusMessageKind kind =
            stage == ConnectionStage::Failed ? StatusMessageKind::Error
            : stage == ConnectionStage::Reconnecting || stage == ConnectionStage::WaitingForRadio
                ? StatusMessageKind::Warning
                : StatusMessageKind::Info;
        m_connectionStatusMessage = message;
        showStatusMessage(m_connectionStatusMessage, 0, kind);
    }
}

void MainWindow::onStatusMessage(const QString& msg, MessageSeverity severity)
{
    StatusMessageKind kind = StatusMessageKind::Info;
    if (severity == MessageSeverity::Warning)
    {
        kind = StatusMessageKind::Warning;
    }
    else if (severity == MessageSeverity::Error)
    {
        kind = StatusMessageKind::Error;
    }
    showStatusMessage(msg, 5000, kind);
}

void MainWindow::onError(ErrorCode code, const QString& msg)
{
    Q_UNUSED(msg)
    // RadioBackend publishes the same failure through connectionStageChanged,
    // which owns the persistent operator-facing status message. Keep this typed signal
    // solely for reconnect policy so one failure does not produce two messages.
    m_lastErrorWasCredential = code == ErrorCode::AuthFailure;
    const bool connectionAttemptFailed = sdr9700::isAutomaticReconnectError(code);
    const bool profileAvailable =
        !m_pendingProfileId.isNull() && RadioProfileStore::instance().profileById(m_pendingProfileId) != nullptr;
    if (sdr9700::shouldRetryRadioConnection(false, connectionAttemptFailed, m_userDisconnected,
                                            m_lastErrorWasCredential, profileAvailable) &&
        (!m_model || !m_model->isConnected()))
    {
        scheduleRadioReconnect();
    }
}

bool MainWindow::scheduleRadioReconnect()
{
    if (m_pendingProfileId.isNull() || !RadioProfileStore::instance().profileById(m_pendingProfileId))
    {
        m_reconnecting = false;
        return false;
    }

    m_reconnecting = true;
    m_connectionStatusMessage = sdr9700::reconnectingMessage(m_connectionStatusMessage);
    showStatusMessage(m_connectionStatusMessage, 0, StatusMessageKind::Warning);
    if (m_connStateLabel)
    {
        m_connStateName = QStringLiteral("Reconnecting");
        m_connStateLabel->setText(
            QStringLiteral("<span style='color:%1'>Reconnecting</span>").arg(UiTheme::Color::Danger));
    }
    // Keep the IP in the detail label so the user knows which radio is reconnecting.
    updateConnectionTooltip();

    if (!m_reconnectTimer)
    {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this,
                [this]()
                {
                    if (!m_reconnecting)
                    {
                        return;
                    }
                    const RadioProfile* profile = RadioProfileStore::instance().profileForUse(m_pendingProfileId);
                    if (profile)
                    {
                        onConnectToProfile(*profile);
                    }
                    else
                    {
                        m_reconnecting = false;
                    }
                });
    }
    m_reconnectTimer->start(5000);
    return true;
}

void MainWindow::scheduleAudioSettingsApply()
{
    if (m_shutdownStarted)
    {
        return;
    }

    if (!m_audioSettingsApplyTimer)
    {
        m_audioSettingsApplyTimer = new QTimer(this);
        m_audioSettingsApplyTimer->setSingleShot(true);
        connect(m_audioSettingsApplyTimer, &QTimer::timeout, this, &MainWindow::applyAudioSettings);
    }

    // One operator action can update both input and output devices. Coalesce
    // nearby changes so each affected audio worker is rebuilt only once.
    m_audioSettingsApplyTimer->start(400);
}

void MainWindow::applyAudioSettings()
{
    if (!m_model || m_shutdownStarted)
    {
        return;
    }

    const AppSettings& settings = AppSettings::instance();
    const QByteArray inputID = QByteArray::fromBase64(settings.value("audioInputDeviceID").toString().toLatin1());
    const QByteArray outputID = QByteArray::fromBase64(settings.value("audioOutputDeviceID").toString().toLatin1());
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();

    m_model->setTxAudioDevice(sdr9700::selectAudioDevice(inputs, inputID, QMediaDevices::defaultAudioInput()));
    m_model->setRxAudioDevice(sdr9700::selectAudioDevice(outputs, outputID, QMediaDevices::defaultAudioOutput()));

    if (!m_model->isConnected())
    {
        m_connectedAudioOutputChannels = qBound(1, settings.value("audioOutputChannels", 2).toInt(), 2);
    }
}

void MainWindow::onAfGainChanged(int value)
{
    m_vfo->setAfGain(value);
}

void MainWindow::showDtmfDialog()
{
    if (!m_dtmfDialog)
    {
        return;
    }

    m_dtmfDialog->showCentered();
}

void MainWindow::showDataDecoderDialog()
{
    if (!m_dataDecoderDialog)
    {
        m_dataDecoderDialog = new DataDecoderDialog(this);
        connect(m_model, &RadioModel::audioDataReady, m_dataDecoderDialog, &DataDecoderDialog::processAudio);
        connect(m_model, &RadioModel::connectionChanged, m_dataDecoderDialog,
                [dialog = m_dataDecoderDialog](bool connected)
                {
                    if (!connected && dialog)
                    {
                        emit dialog->resetDecoder();
                    }
                });
    }
    m_dataDecoderDialog->showCentered();
}

void MainWindow::showMetersDialog()
{
    if (!m_metersDialog)
    {
        return;
    }

    if (m_model && m_model->isReady())
    {
        m_metersDialog->resetMeters();
        if (m_meterSnapshot.sMeterValid)
        {
            m_metersDialog->setSMeter(m_meterSnapshot.sMeter);
        }
        if (m_meterSnapshot.powerValid)
        {
            m_metersDialog->setPowerMeter(m_meterSnapshot.powerWatts);
        }
        if (m_meterSnapshot.swrValid)
        {
            m_metersDialog->setSwr(m_meterSnapshot.swr);
        }
        if (m_meterSnapshot.alcValid)
        {
            m_metersDialog->setAlc(m_meterSnapshot.alc);
        }
        if (m_meterSnapshot.compressionValid)
        {
            m_metersDialog->setCompressionMeter(m_meterSnapshot.compressionDb);
        }
        if (m_meterSnapshot.voltageValid)
        {
            m_metersDialog->setVoltageMeter(m_meterSnapshot.voltageVolts);
        }
        if (m_meterSnapshot.currentValid)
        {
            m_metersDialog->setCurrentMeter(m_meterSnapshot.currentAmps);
        }
        m_metersDialog->setTransmitAudioLevel(m_meterSnapshot.txAudioPeak, m_meterSnapshot.txAudioRms);
    }
    else
    {
        m_metersDialog->resetMeters();
    }
    m_metersDialog->showCentered();
}

void MainWindow::onDtmfSendRequested(const QString& digits)
{
    if (m_dtmfSendActive || !m_vfo || digits.isEmpty())
    {
        return;
    }

    if (!m_vfo->setPtt(true))
    {
        return;
    }
    beginMemoryPttFrequencyTransition();
    m_dtmfSendActive = true;
    if (m_dtmfDialog)
    {
        m_dtmfDialog->setSendInProgress(true);
    }

    // The DTMF PCM buffer queued to UdpAudio is consumed only after the 1000 ms
    // TX gate expires. kTrailMs must cover the remaining gate window after the
    // lead-in (600 ms) plus some silence after the last tone.
    constexpr int kLeadInMs = 400;
    constexpr int kPerDigitMs = 400;
    constexpr int kTrailMs = 900;

    QTimer::singleShot(kLeadInMs, this,
                       [this, digits]()
                       {
                           if (m_dtmfSendActive && m_vfo)
                           {
                               m_vfo->sendDtmf(digits);
                           }
                       });

    m_dtmfPttOffTimer->start(kLeadInMs + digits.length() * kPerDigitMs + kTrailMs);
}

void MainWindow::onPttPressed()
{
    if (!m_vfo || !radioUiReady())
    {
        return;
    }

    if (m_vfo->setPtt(true))
    {
        beginMemoryPttFrequencyTransition();
    }
}

void MainWindow::onPttReleased()
{
    if (!m_vfo)
    {
        return;
    }
    qDebug(logRadio()).noquote() << "PTT UI button release event";
    m_pttActive = false;
    m_vfo->setPtt(false);
    if (m_pttBtn && !m_vfoPttReady)
    {
        m_pttBtn->setEnabled(false);
    }
}

void MainWindow::beginMemoryPttFrequencyTransition()
{
    if (m_activeMemoryId.isEmpty())
    {
        return;
    }
    // Begin protection when PTT is requested, not when its radio
    // acknowledgement arrives. Split-frequency reports can precede that
    // acknowledgement on the CI-V stream.
    m_pttActive = true;
    m_activeMemoryAwaitingReceiveFrequency = true;
}

void MainWindow::onPttChanged(bool on)
{
    m_pttActive = on;
    if (!m_activeMemoryId.isEmpty())
    {
        // Split memories can publish transmit and intermediate selected
        // frequencies while the radio changes paths. Keep the memory selected
        // until an RX-frequency report confirms that key-up has settled.
        m_activeMemoryAwaitingReceiveFrequency = true;
    }
#ifdef HAVE_HIDAPI
    m_icomRC28PttLatched = on;
#endif
    if (!on && m_dtmfSendActive)
    {
        m_dtmfSendActive = false;
        if (m_dtmfPttOffTimer)
        {
            m_dtmfPttOffTimer->stop();
        }
        if (m_dtmfDialog)
        {
            m_dtmfDialog->setSendInProgress(false);
        }
    }
    updateTransmitState(on);
    m_pttBtn->setProperty("pttActive", on);
    m_pttBtn->update();
    setSelectorButtonLines(m_pttBtn, QStringLiteral("PTT"), QString());
}

void MainWindow::onDuplexModeChanged(duplexMode_t mode)
{
    m_duplexMode = mode;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode == m_activeMemoryDuplexMode)
        {
            m_activeMemoryDuplexSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryDuplexSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
}

void MainWindow::onRepeaterOffsetChanged(quint64 hz)
{
    m_repeaterOffsetHz = hz;
    if (!m_activeMemoryId.isEmpty() && m_activeMemoryDuplexMode != dmSimplex)
    {
        if (hz == m_activeMemoryOffsetHz)
        {
            m_activeMemoryOffsetSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryOffsetSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
}

void MainWindow::onToneAccessModeChanged(rptAccessTxRx_t mode)
{
    m_toneAccessMode = mode;
    if (!m_activeMemoryId.isEmpty())
    {
        if (mode == m_activeMemoryToneMode)
        {
            m_activeMemoryToneModeSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneModeSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
}

void MainWindow::onToneFrequencyChanged(ushort tone)
{
    m_toneFrequency = tone;
    if (!m_activeMemoryId.isEmpty() && m_activeMemoryToneMode != ratrNN && !isDtcsToneMode(m_activeMemoryToneMode))
    {
        if (tone == m_activeMemoryToneValue)
        {
            m_activeMemoryToneValueSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneValueSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
}

void MainWindow::onDtcsCodeChanged(ushort code)
{
    m_dtcsCode = code;
    if (!m_activeMemoryId.isEmpty() && isDtcsToneMode(m_activeMemoryToneMode))
    {
        if (code == m_activeMemoryToneValue)
        {
            m_activeMemoryToneValueSettled = true;
            checkIfMemorySelectionComplete();
        }
        else if (m_activeMemoryToneValueSettled && !m_applyingMemorySelection)
        {
            clearActiveMemory();
        }
    }
}
