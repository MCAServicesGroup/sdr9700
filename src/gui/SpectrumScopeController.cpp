#include "SpectrumScopeController.h"
#include "SpectrumTuningPolicy.h"

#include "AppSettings.h"
#include "SpectrumFrameRate.h"
#include "SpectrumScopeDisplay.h"
#include "LogCategories.h"
#include "MainWindow.h"
#include "RadioCommandController.h"
#include "MainWindowHelpers.h"
#include "VfoController.h"
#include "VfoDisplay.h"
#include "VfoSelectionController.h"
#include "VfoSelectionPanel.h"
#include "backend/IRadioBackend.h"
#include "models/SpectrumScopeModel.h"
#include "models/RadioModel.h"
#include "models/VfoModel.h"

#include <QComboBox>
#include <QLabel>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

using namespace sdr9700::ui::main_window;

namespace
{
constexpr int kSpectrumToolbarHeight = 29;
constexpr int kExchangeScopeSyncTimeoutMs = 2000;
constexpr int kShelfShadowHeightPx = 8;

class SpectrumToolbar : public QWidget
{
  public:
    using QWidget::QWidget;

    void setCenteredControl(QWidget* control)
    {
        m_centeredControl = control;
        positionControls();
    }

    void setLeadingControls(const QVector<QWidget*>& controls)
    {
        m_leadingControls = controls;
        positionControls();
    }

    void setAnchorXProvider(std::function<int()> provider)
    {
        m_anchorXProvider = provider;
        positionControls();
    }

  protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        positionControls();
    }

  private:
    void positionControls()
    {
        if (!m_centeredControl)
        {
            return;
        }
        const QRect bounds = contentsRect();
        const int defaultCenterX = bounds.x() + bounds.width() / 2;
        const int centerX = m_anchorXProvider ? m_anchorXProvider() : defaultCenterX;
        const int x = centerX - m_centeredControl->width() / 2;
        const int y = bounds.y() + (bounds.height() - m_centeredControl->height()) / 2;
        m_centeredControl->move(x, y);
        m_centeredControl->raise();

        const int controlsWidth =
            std::accumulate(m_leadingControls.cbegin(), m_leadingControls.cend(), 0,
                            [](int width, const QWidget* control) { return width + (control ? control->width() : 0); });
        const int availableWidth = x - bounds.x();
        const int gap =
            std::max(0, (availableWidth - controlsWidth) / (static_cast<int>(m_leadingControls.size()) + 1));
        int controlX = bounds.x() + gap;
        for (QWidget* control : m_leadingControls)
        {
            if (!control)
            {
                continue;
            }
            control->move(controlX, bounds.y() + (bounds.height() - control->height()) / 2);
            controlX += control->width() + gap;
        }
    }

    QWidget* m_centeredControl{nullptr};
    QVector<QWidget*> m_leadingControls;
    std::function<int()> m_anchorXProvider;
};
} // namespace

SpectrumScopeController::SpectrumScopeController(MainWindow* window) : QObject(window), m_window(window)
{
    m_exchangeScopeSyncTimer = new QTimer(this);
    m_exchangeScopeSyncTimer->setSingleShot(true);
    m_exchangeScopeSyncTimer->setInterval(kExchangeScopeSyncTimeoutMs);
    connect(m_exchangeScopeSyncTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!m_exchangeScopeSyncPending)
                {
                    return;
                }
                qCritical(logSpectrumScope()).noquote()
                    << "Exchange scope synchronization timed out; releasing exchange controls";
                m_exchangeScopeSyncPending = false;
                m_exchangeRejectedFrames = 0;
                if (m_window->m_vfoSelectionController)
                {
                    const Vfo selected = m_window->m_vfoSelectionController->selectedVfo();
                    const VfoController* selectedController =
                        selected == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController;
                    m_activeVfoStatePublished = selectedController && selectedController->hasPublishedState();
                    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
                    {
                        backend->setScopeVfo(selected);
                    }
                    updateScopeFrameGate();
                    m_window->m_vfoSelectionController->completeExchangeScopeSync();
                }
            });
}

void SpectrumScopeController::buildSpectrumScope(QVBoxLayout* vbox)
{
    auto* vfoStrip = new QWidget(m_window->centralWidget());
    vfoStrip->setObjectName(QStringLiteral("vfoDisplayStrip"));
    vfoStrip->setStyleSheet(
        QStringLiteral("QWidget#vfoDisplayStrip { background: %1; }").arg(UiTheme::Color::ContentBackground));
    auto* vfoLayout = new QHBoxLayout(vfoStrip);
    vfoLayout->setContentsMargins(kControlStripMargins.left(), 0, kControlStripMargins.right(), 0);
    vfoLayout->setSpacing(0);

    auto* vfoBlock = new QWidget(vfoStrip);
    vfoBlock->setObjectName(QStringLiteral("vfoBlock"));
    vfoBlock->setStyleSheet(
        QStringLiteral("QWidget#vfoBlock { background: black; border: 1px solid %1; }").arg(UiTheme::Color::Border));
    auto* vfoBlockLayout = new QHBoxLayout(vfoBlock);
    vfoBlockLayout->setContentsMargins(1, 1, 1, 1);
    vfoBlockLayout->setSpacing(0);

    m_window->m_mainVfoController =
        new VfoController(Vfo::Main, m_window->m_model->backend(), m_window->m_model->radioState(), vfoStrip, m_window);
    m_window->m_subVfoController =
        new VfoController(Vfo::Sub, m_window->m_model->backend(), m_window->m_model->radioState(), vfoStrip, m_window);
    m_window->m_mainVfoController->setLanModLevel(m_window->m_lanModValue);
    connect(m_window->m_mainVfoController, &VfoController::lanModLevelChanged, this,
            [this](int value)
            {
                if (!m_window->radioUiReady())
                {
                    return;
                }
                m_window->m_lanModValue = std::clamp(value, 0, 255);
                AppSettings::instance().setValueDeferred(QStringLiteral("LANModLevel"), m_window->m_lanModValue);
                m_window->m_model->setLanModLevel(m_window->m_lanModValue);
            });
    m_window->m_vfoSelectionController = new VfoSelectionController(
        m_window->m_model->backend(), m_window->m_mainVfoController, m_window->m_subVfoController, vfoStrip, m_window);
    m_window->m_vfoSelectionController->panel()->setPttButton(m_window->m_pttBtn);
    const auto showToneMenu = [this](Vfo vfo, const QPoint& position)
    {
        m_window->m_vfoSelectionController->runWhenSelected(
            vfo, [this, position]() { m_window->m_radioCommandController->showToneMenu(position); });
    };
    const auto showOffsetMenu = [this](Vfo vfo, const QPoint& position)
    {
        const VfoController* targetController =
            vfo == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController;
        const quint64 receiveFrequencyHz = targetController ? targetController->frequencyHz() : 0;
        m_window->m_vfoSelectionController->runWhenSelected(
            vfo, [this, position, receiveFrequencyHz]()
            { m_window->m_radioCommandController->showOffsetMenu(position, receiveFrequencyHz); });
    };
    connect(m_window->m_mainVfoController, &VfoController::toneMenuRequested, this, showToneMenu);
    connect(m_window->m_subVfoController, &VfoController::toneMenuRequested, this, showToneMenu);
    connect(m_window->m_mainVfoController, &VfoController::offsetMenuRequested, this, showOffsetMenu);
    connect(m_window->m_subVfoController, &VfoController::offsetMenuRequested, this, showOffsetMenu);
    connect(m_window->m_mainVfoController, &VfoController::compressorMenuRequested, this,
            [this](Vfo, const QPoint& position) { m_window->m_radioCommandController->showCompressorMenu(position); });
    connect(m_window->m_vfoSelectionController, &VfoSelectionController::selectedVfoChanged, this,
            [this](Vfo vfo)
            {
                resetScopeFrameGate();
                const VfoController* selectedController =
                    vfo == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController;
                m_activeVfoStatePublished = selectedController && selectedController->hasPublishedState();
                if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
                {
                    backend->setScopeVfo(vfo);
                }
                updateScopeFrameGate();
            });
    connect(m_window->m_vfoSelectionController, &VfoSelectionController::pttReadyChanged, this,
            [this](bool ready)
            {
                m_window->m_vfoPttReady = ready;
                if (m_window->m_pttBtn)
                {
                    // Never disable a held PTT button before its release event
                    // can unkey the radio.
                    m_window->m_pttBtn->setEnabled((ready || m_window->m_pttActive || m_window->m_pttBtn->isDown()) &&
                                                   m_window->m_model && m_window->m_model->isReady());
                }
            });
    connect(m_window->m_mainVfoController, &VfoController::frequencyChanged, this,
            [this](quint64 hz) { onActiveVfoFrequencyChanged(Vfo::Main, hz); });
    connect(m_window->m_subVfoController, &VfoController::frequencyChanged, this,
            [this](quint64 hz) { onActiveVfoFrequencyChanged(Vfo::Sub, hz); });
    const auto markVfoStatePublished = [this](Vfo vfo)
    {
        if (m_window->m_vfoSelectionController && m_window->m_vfoSelectionController->selectedVfo() == vfo)
        {
            m_activeVfoStatePublished = true;
            updateScopeFrameGate();
        }
    };
    connect(m_window->m_mainVfoController, &VfoController::statePublished, this, markVfoStatePublished);
    connect(m_window->m_subVfoController, &VfoController::statePublished, this, markVfoStatePublished);
    const auto markRecenterPending = [this](Vfo vfo, quint64 hz)
    {
        if (vfo == Vfo::Main)
        {
            m_pendingMainRecenterHz = hz;
        }
        else
        {
            m_pendingSubRecenterHz = hz;
        }
    };
    connect(m_window->m_mainVfoController, &VfoController::frequencyRecenterRequested, this, markRecenterPending);
    connect(m_window->m_subVfoController, &VfoController::frequencyRecenterRequested, this, markRecenterPending);
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        connect(backend, &IRadioBackend::radioValueUpdated, this,
                [this](Funcs func, const QVariant& value, uchar receiver)
                {
                    if (func != funcScopeMainSub || receiver != 0 || !m_window->m_vfoSelectionController)
                    {
                        return;
                    }
                    const Vfo confirmedVfo = value.toBool() ? Vfo::Sub : Vfo::Main;
                    if (confirmedVfo != m_window->m_vfoSelectionController->selectedVfo())
                    {
                        return;
                    }
                    m_scopeVfoConfirmed = true;
                    updateScopeFrameGate();
                });
        connect(backend, &IRadioBackend::mainSubExchangeCompleted, this,
                [this, backend]()
                {
                    if (!m_window->m_vfoSelectionController)
                    {
                        return;
                    }
                    // MAIN remains selected after an exchange, so the normal
                    // selectedVfoChanged path does not run. Reconfirm and
                    // recenter the scope because the selected VFO's contents
                    // have nevertheless changed.
                    resetScopeFrameGate();
                    m_exchangeScopeSyncPending = true;
                    m_exchangeRejectedFrames = 0;
                    m_exchangeScopeSyncTimer->start();
                    const Vfo selected = m_window->m_vfoSelectionController->selectedVfo();
                    const VfoController* selectedController =
                        selected == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController;
                    m_activeVfoStatePublished = selectedController && selectedController->hasPublishedState();
                    backend->setScopeVfo(selected);
                    updateScopeFrameGate();
                });
        connect(backend, &IRadioBackend::mainSubExchangeFailed, this,
                [this, backend]()
                {
                    if (!m_window->m_vfoSelectionController)
                    {
                        return;
                    }
                    resetScopeFrameGate();
                    m_exchangeScopeSyncPending = true;
                    m_exchangeRejectedFrames = 0;
                    m_exchangeScopeSyncTimer->start();
                    const Vfo selected = m_window->m_vfoSelectionController->selectedVfo();
                    const VfoController* selectedController =
                        selected == Vfo::Main ? m_window->m_mainVfoController : m_window->m_subVfoController;
                    m_activeVfoStatePublished = selectedController && selectedController->hasPublishedState();
                    backend->setScopeVfo(selected);
                    updateScopeFrameGate();
                });
        connect(backend, &IRadioBackend::readyChanged, this,
                [this](bool ready)
                {
                    if (!ready)
                    {
                        resetScopeFrameGate();
                        m_exchangeScopeSyncPending = false;
                        m_exchangeScopeSyncTimer->stop();
                        m_exchangeRejectedFrames = 0;
                        m_hasCenteredActiveVfo = false;
                        m_pendingMainRecenterHz = 0;
                        m_pendingSubRecenterHz = 0;
                        m_tuneIntentHz = 0;
                        m_tuneIntentClock.invalidate();
                    }
                });
    }
    auto createSeparator = [vfoBlock]()
    {
        auto* separator = new QWidget(vfoBlock);
        separator->setFixedWidth(1);
        separator->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
        return separator;
    };
    vfoBlockLayout->addWidget(m_window->m_mainVfoController->display(), 1);
    vfoBlockLayout->addWidget(createSeparator());
    vfoBlockLayout->addWidget(m_window->m_vfoSelectionController->panel());
    vfoBlockLayout->addWidget(createSeparator());
    vfoBlockLayout->addWidget(m_window->m_subVfoController->display(), 1);
    vfoLayout->addWidget(vfoBlock, 1);
    // Nineteen empty layout pixels plus the first border pixel produce a 20 px
    // visible edge-to-edge separation below the title bar.
    vbox->addSpacing(19);
    vbox->addWidget(vfoStrip);
    auto* vfoShelfShadowRow = new QWidget(m_window->centralWidget());
    auto* vfoShelfShadowLayout = new QHBoxLayout(vfoShelfShadowRow);
    vfoShelfShadowLayout->setContentsMargins(kControlStripMargins.left(), 0, kControlStripMargins.right(), 0);
    vfoShelfShadowLayout->setSpacing(0);
    auto* vfoShelfShadow = new QWidget(vfoShelfShadowRow);
    vfoShelfShadow->setFixedHeight(kShelfShadowHeightPx);
    vfoShelfShadow->setStyleSheet(
        QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(0, 4, 8, 220), "
                       "stop:1 rgba(0, 8, 15, 0)); border-top: 1px solid #2a404f;"));
    vfoShelfShadowLayout->addWidget(vfoShelfShadow);
    vbox->addWidget(vfoShelfShadowRow);
    // Preserve the existing 20 px separation below the VFO controls while
    // using the first eight pixels for the shelf shadow.
    vbox->addSpacing(12);

    m_window->m_spectrumScopeDisplay = new SpectrumScopeDisplay(m_window->centralWidget());
    m_window->m_spectrumScopeDisplay->setInvertMouseWheel(
        AppSettings::instance()
            .value(QString::fromLatin1(kSpectrumScopeInvertMouseWheelSettingsKey), "False")
            .toBool());
    m_window->m_spectrumScopeDisplay->setVfoMarkerColor(
        colorSetting(kSpectrumScopeCenterLineColorSettingsKey, kDefaultSpectrumScopeCenterLineColor));
    m_window->m_spectrumScopeDisplay->setBackgroundColor(
        colorSetting(kSpectrumScopeBackgroundColorSettingsKey, kDefaultSpectrumScopeBackgroundColor));
    m_window->m_spectrumScopeDisplay->setGridLineColor(
        colorSetting(kSpectrumScopeGridLineColorSettingsKey, kDefaultSpectrumScopeGridLineColor));
    m_window->m_spectrumScopeDisplay->setGridDensity(spectrumScopeGridDensitySetting());

    QVector<SpectrumScopeDisplay::SpanChoice> spanChoices;
    spanChoices.reserve(static_cast<int>(std::size(kSpectrumScopeSpanPresets)));
    for (const SpectrumScopeSpanPreset& preset : kSpectrumScopeSpanPresets)
    {
        spanChoices.append({preset.hz, QString::fromLatin1(preset.label)});
    }
    m_window->m_spectrumScopeDisplay->setSpanChoices(spanChoices);

    const quint64 initialSpectrumScopeSpanHz = AppSettings::instance()
                                                   .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                                          QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                                                   .toULongLong();
    m_window->m_spectrumScopeDisplay->setCurrentSpanHz(initialSpectrumScopeSpanHz);
    const double initialSpectrumScopeCenterMhz = activeVfoFrequencyHz() / 1e6;
    // IC-9700 center-scope span choices are half-spans ("+/-500 kHz"). The
    // display range is full width, so expand the stored/radio half-span here.
    const double initialSpectrumScopeBandwidthMhz = (initialSpectrumScopeSpanHz * 2.0) / 1e6;
    m_window->m_spectrumScopeDisplay->setFrequencyRange(
        initialSpectrumScopeCenterMhz - initialSpectrumScopeBandwidthMhz / 2.0,
        initialSpectrumScopeCenterMhz + initialSpectrumScopeBandwidthMhz / 2.0);
    m_window->m_spectrumScopeDisplay->setDataFrequencyRange(
        initialSpectrumScopeCenterMhz - initialSpectrumScopeBandwidthMhz / 2.0,
        initialSpectrumScopeCenterMhz + initialSpectrumScopeBandwidthMhz / 2.0);
    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::spanSelected, this,
            [this](quint64 hz)
            {
                AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                                 QVariant::fromValue<qulonglong>(hz));
                applySpectrumScopeSettings();
            });
    connect(m_window->m_vfo, &VfoModel::filterChanged, this,
            [this](int low, int high) { m_window->m_spectrumScopeDisplay->setFilterWidth(low, high); });
    auto* spectrumInset = new QWidget(m_window->centralWidget());
    spectrumInset->setObjectName(QStringLiteral("spectrumInset"));
    auto* spectrumInsetLayout = new QHBoxLayout(spectrumInset);
    spectrumInsetLayout->setContentsMargins(kControlStripMargins.left(), 0, kControlStripMargins.right(), 0);
    spectrumInsetLayout->setSpacing(0);
    auto* spectrumFrame = new QWidget(spectrumInset);
    spectrumFrame->setObjectName(QStringLiteral("spectrumFrame"));
    spectrumFrame->setStyleSheet(
        QStringLiteral("QWidget#spectrumFrame { border: 1px solid %1; }").arg(UiTheme::Color::Border));
    auto* spectrumFrameLayout = new QVBoxLayout(spectrumFrame);
    spectrumFrameLayout->setContentsMargins(1, 1, 1, 1);
    spectrumFrameLayout->setSpacing(0);

    auto* spectrumToolbar = new SpectrumToolbar(spectrumFrame);
    spectrumToolbar->setObjectName(QStringLiteral("spectrumToolbar"));
    spectrumToolbar->setFixedHeight(kSpectrumToolbarHeight);
    spectrumToolbar->setAccessibleName(QStringLiteral("Spectrum controls toolbar"));
    QPalette chromePalette = spectrumToolbar->palette();
    chromePalette.setColor(QPalette::Window, QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
    spectrumToolbar->setPalette(chromePalette);
    spectrumToolbar->setAutoFillBackground(true);
    spectrumToolbar->setStyleSheet(
        QStringLiteral("QWidget#spectrumToolbar { background: %1; border: 0; border-bottom: 1px solid %2; }")
            .arg(UiTheme::Color::WindowChrome, UiTheme::Color::StatusBorder));
    m_tuningStepSelector = new QComboBox(spectrumToolbar);
    m_tuningStepSelector->setObjectName(QStringLiteral("spectrumStepSelector"));
    m_tuningStepSelector->setAccessibleName(QStringLiteral("Tuning step"));
    m_tuningStepSelector->hide();
    for (const auto& preset : kStepPresets)
    {
        m_tuningStepSelector->addItem(QString::fromLatin1(preset.label), preset.hz);
    }
    updateTuningStepSelector(m_window->tuningStepHz());
    connect(m_tuningStepSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (index < 0 || !m_tuningStepSelector)
                {
                    return;
                }
                AppSettings::instance().setValue(QString::fromLatin1(kTuningStepHZSettingsKey),
                                                 m_tuningStepSelector->itemData(index).toInt());
                m_window->applyRadioTuningStep();
            });

    auto* spanSelector = m_window->m_spectrumScopeDisplay->spanSelector();
    spanSelector->setParent(spectrumToolbar);
    spanSelector->hide();

    auto* framesPerSecondSelector = new QComboBox(spectrumToolbar);
    framesPerSecondSelector->setObjectName(QStringLiteral("spectrumFramesPerSecondSelector"));
    framesPerSecondSelector->setAccessibleName(QStringLiteral("Spectrum frame rate"));
    framesPerSecondSelector->hide();
    for (const int framesPerSecond : sdr9700::kSpectrumFramesPerSecondPresets)
    {
        framesPerSecondSelector->addItem(QString::number(framesPerSecond), framesPerSecond);
    }
    const int storedFramesPerSecond = AppSettings::instance()
                                          .value(QString::fromLatin1(kSpectrumScopeFramesPerSecondSettingsKey),
                                                 sdr9700::kDefaultSpectrumFramesPerSecond)
                                          .toInt();
    framesPerSecondSelector->setCurrentIndex(
        framesPerSecondSelector->findData(sdr9700::normalizedSpectrumFramesPerSecond(storedFramesPerSecond)));
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        backend->setSpectrumFramesPerSecond(framesPerSecondSelector->currentData().toInt());
    }
    connect(framesPerSecondSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, framesPerSecondSelector](int index)
            {
                if (index < 0)
                {
                    return;
                }
                const int framesPerSecond = framesPerSecondSelector->itemData(index).toInt();
                AppSettings::instance().setValue(QString::fromLatin1(kSpectrumScopeFramesPerSecondSettingsKey),
                                                 framesPerSecond);
                if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
                {
                    backend->setSpectrumFramesPerSecond(framesPerSecond);
                }
            });

    constexpr int kInlineSelectorTextSpacing = 4;
    const auto makeInlineSelector = [spectrumToolbar](const QString& name, QComboBox* selector)
    {
        auto* control = new QWidget(spectrumToolbar);
        control->setFixedHeight(22);
        auto* layout = new QHBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* previous = new QToolButton(control);
        auto* label = new QLabel(name, control);
        auto* value = new QLabel(selector->currentText(), control);
        auto* next = new QToolButton(control);
        previous->setText(QStringLiteral("‹"));
        next->setText(QStringLiteral("›"));
        previous->setAccessibleName(QStringLiteral("Previous %1").arg(name));
        next->setAccessibleName(QStringLiteral("Next %1").arg(name));
        for (QToolButton* button : {previous, next})
        {
            button->setFixedSize(16, 22);
            button->setFocusPolicy(Qt::NoFocus);
            button->setStyleSheet(
                QStringLiteral("QToolButton { background: transparent; border: 0; color: %1; font-size: 14px; "
                               "font-weight: bold; padding: 0; } QToolButton:hover { color: %2; }")
                    .arg(UiTheme::Color::TextStatusSecondary, UiTheme::Color::TextBright));
        }
        label->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px; font-weight: bold;").arg(UiTheme::Color::TextStatusSecondary));
        label->setAlignment(Qt::AlignCenter);
        // Match the value cell to its rendered text. A fixed maximum-width
        // field leaves invisible padding before the right chevron whenever a
        // shorter STEP, SPAN, or FPS value is selected.
        value->setAlignment(Qt::AlignCenter);
        value->setStyleSheet(
            QStringLiteral("color: %1; font-size: 10px; font-weight: bold;").arg(UiTheme::Color::TextBright));

        const auto updateControl = [selector, value, previous, next, control, layout]()
        {
            value->setText(selector->currentText());
            value->setFixedWidth(value->fontMetrics().horizontalAdvance(value->text()));
            layout->activate();
            control->setFixedWidth(layout->sizeHint().width());
            previous->setEnabled(selector->currentIndex() > 0);
            next->setEnabled(selector->currentIndex() + 1 < selector->count());
        };
        QObject::connect(previous, &QToolButton::clicked, selector,
                         [selector]() { selector->setCurrentIndex(selector->currentIndex() - 1); });
        QObject::connect(next, &QToolButton::clicked, selector,
                         [selector]() { selector->setCurrentIndex(selector->currentIndex() + 1); });
        QObject::connect(selector, &QComboBox::currentTextChanged, control,
                         [updateControl](const QString&) { updateControl(); });

        layout->addWidget(previous);
        layout->addSpacing(kInlineSelectorTextSpacing);
        layout->addWidget(label);
        layout->addSpacing(kInlineSelectorTextSpacing);
        layout->addWidget(value);
        layout->addWidget(next);
        updateControl();
        return control;
    };

    auto* stepControl = makeInlineSelector(QStringLiteral("STEP"), m_tuningStepSelector);
    auto* spanControl = makeInlineSelector(QStringLiteral("SPAN"), spanSelector);
    auto* framesPerSecondControl = makeInlineSelector(QStringLiteral("FPS"), framesPerSecondSelector);

    auto* recenterButton = new QToolButton(spectrumToolbar);
    recenterButton->setObjectName(QStringLiteral("spectrumRecenterButton"));
    recenterButton->setText(QStringLiteral("RECENTER"));
    recenterButton->setFixedHeight(22);
    recenterButton->setFocusPolicy(Qt::NoFocus);
    recenterButton->setCursor(Qt::PointingHandCursor);
    recenterButton->setAccessibleName(QStringLiteral("Recenter spectrum"));
    recenterButton->setAccessibleDescription(
        QStringLiteral("Center the panadapter and waterfall on the active VFO frequency."));
    recenterButton->setStyleSheet(
        QStringLiteral("QToolButton { background: transparent; border: 0; color: %1; font-size: 10px; "
                       "font-weight: bold; padding: 0 4px; } QToolButton:hover { color: %2; }")
            .arg(UiTheme::Color::TextStatusSecondary, UiTheme::Color::TextBright));
    connect(recenterButton, &QToolButton::clicked, this, [this]() { recenterActiveVfo(true); });
    recenterButton->adjustSize();
    spectrumToolbar->setCenteredControl(recenterButton);
    spectrumToolbar->setLeadingControls({stepControl, spanControl, framesPerSecondControl});
    spectrumToolbar->setAnchorXProvider(
        [this, spectrumToolbar]()
        {
            if (!m_window->m_vfoSelectionController || !m_window->m_vfoSelectionController->panel())
            {
                return spectrumToolbar->contentsRect().center().x();
            }
            const QWidget* panel = m_window->m_vfoSelectionController->panel();
            return spectrumToolbar->mapFromGlobal(panel->mapToGlobal(panel->rect().center())).x();
        });
    spectrumFrameLayout->addWidget(spectrumToolbar);
    spectrumFrameLayout->addWidget(m_window->m_spectrumScopeDisplay);
    spectrumInsetLayout->addWidget(spectrumFrame);
    vbox->addWidget(spectrumInset, 1);
    auto* waterfallShelfShadowRow = new QWidget(m_window->centralWidget());
    waterfallShelfShadowRow->setObjectName(QStringLiteral("waterfallShelfShadowRow"));
    auto* waterfallShelfShadowLayout = new QHBoxLayout(waterfallShelfShadowRow);
    waterfallShelfShadowLayout->setContentsMargins(kControlStripMargins.left(), 0, kControlStripMargins.right(), 0);
    waterfallShelfShadowLayout->setSpacing(0);
    auto* waterfallShelfShadow = new QWidget(waterfallShelfShadowRow);
    waterfallShelfShadow->setObjectName(QStringLiteral("waterfallShelfShadow"));
    waterfallShelfShadow->setFixedHeight(kShelfShadowHeightPx);
    waterfallShelfShadow->setStyleSheet(
        QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(0, 4, 8, 220), "
                       "stop:1 rgba(0, 8, 15, 0)); border-top: 1px solid #2a404f;"));
    waterfallShelfShadowLayout->addWidget(waterfallShelfShadow);
    vbox->addWidget(waterfallShelfShadowRow);
    vbox->addSpacing(17);

    m_window->m_spectrumScopeTuneCommitTimer = new QTimer(m_window);
    m_window->m_spectrumScopeTuneCommitTimer->setSingleShot(true);
    m_window->m_spectrumScopeTuneCommitTimer->setInterval(kSpectrumScopeTuneCommitDelayMs);
    connect(m_window->m_spectrumScopeTuneCommitTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_window->m_pendingSpectrumScopeTuneHz == 0 || !m_window->m_model->isReady() ||
                    m_window->m_controlsLocked || !m_scopeFramesEnabled)
                {
                    return;
                }
                m_window->leaveMemoryModeForManualChange();
                tuneActiveVfo(m_window->m_pendingSpectrumScopeTuneHz);
            });

    m_window->m_spectrumScopeTuneReleaseTimer = new QTimer(m_window);
    m_window->m_spectrumScopeTuneReleaseTimer->setSingleShot(true);
    m_window->m_spectrumScopeTuneReleaseTimer->setInterval(kSpectrumScopeTuneReleaseDelayMs);
    connect(m_window->m_spectrumScopeTuneReleaseTimer, &QTimer::timeout, this,
            [this]()
            {
                m_window->m_pendingSpectrumScopeTuneHz = 0;
                if (m_tuneIntentHz > 0)
                {
                    qWarning(logSpectrumScope()).noquote().nospace()
                        << "Spectrum tune confirmation delayed generation=" << m_tuneIntentGeneration
                        << " targetHz=" << m_tuneIntentHz
                        << " elapsedMs=" << (m_tuneIntentClock.isValid() ? m_tuneIntentClock.elapsed() : -1);
                    m_tuneIntentHz = 0;
                    m_tuneIntentClock.invalidate();
                }
                m_window->m_spectrumScopeDisplayCenterHz = 0;
                m_window->m_spectrumScopeFixedPanStartHz = 0;
                m_window->m_spectrumScopeFixedPanEndHz = 0;
                if (m_window->m_spectrumScope)
                {
                    m_window->m_spectrumScope->clearDisplayCenterHold();
                }
            });

    m_window->m_spectrumScopeDisplay->setFilterWidth(m_window->m_vfo->filterLow(), m_window->m_vfo->filterHigh());
    updateInteractionLock();
    updateSpectrumVfoMarker();

    connect(m_window->m_spectrumScope, &SpectrumScopeModel::spectrumReady, this,
            &SpectrumScopeController::onSpectrumReady);
    connect(m_window->m_spectrumScope, &SpectrumScopeModel::rangeChanged, this,
            [this](double center, double bw)
            {
                m_window->m_spectrumScopeDisplayCenterHz = static_cast<quint64>(std::llround(center * 1e6));
                m_window->m_spectrumScopeDisplay->setFrequencyRange(center - bw / 2, center + bw / 2);
                updateSpectrumVfoMarker();
            });

    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::frequencyClicked, this,
            &SpectrumScopeController::onSpectrumClicked);
    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::wheelStepRequested, this,
            &SpectrumScopeController::onWheelStepRequested);
    connect(m_window->m_spectrumScopeDisplay, &SpectrumScopeDisplay::panCenterRequested, this, [this](double centerMhz)
            { panSpectrumScopeToCenter(static_cast<quint64>(std::llround(centerMhz * 1e6))); });
}

void SpectrumScopeController::updateSpectrumVfoMarker()
{
    if (!m_window->m_spectrumScopeDisplay || !m_window->m_vfo)
    {
        return;
    }

    // Keep the large VFO display radio-authoritative, but give tuning gestures
    // immediate visual feedback on the scope while the CI-V set/readback is in
    // flight. SUB takes an additional receiver-selection round trip and would
    // otherwise look stalled on nearly every click.
    const quint64 displayedHz =
        spectrumTuneDisplayFrequency(activeVfoFrequencyHz(), m_window->m_pendingSpectrumScopeTuneHz);
    m_window->m_spectrumScopeDisplay->setVfoFrequency(displayedHz / 1e6);
}

void SpectrumScopeController::updateTuningStepSelector(int tuningStepHz)
{
    m_tuningStepHz = tuningStepHz;
    if (!m_tuningStepSelector)
    {
        return;
    }
    const int index = m_tuningStepSelector->findData(m_tuningStepHz);
    if (index >= 0)
    {
        const QSignalBlocker blocker(m_tuningStepSelector);
        m_tuningStepSelector->setCurrentIndex(index);
    }
}

void SpectrumScopeController::updateSpectrumScopeBandLimits(quint64 hz)
{
    if (!m_window->m_spectrumScope)
    {
        return;
    }

    quint64 startHz = 0;
    quint64 endHz = 0;
    const availableBands band = sdr9700::radioBandForFrequency(hz);
    if (band == bandUnknown || !sdr9700::radioBandEdges(band, &startHz, &endHz))
    {
        if (!m_hasLastSpectrumScopeLimits)
        {
            return;
        }
        m_hasLastSpectrumScopeLimits = false;
        m_lastSpectrumScopeLimitStartHz = 0;
        m_lastSpectrumScopeLimitEndHz = 0;
        m_window->m_spectrumScope->clearFrequencyLimits();
        if (m_window->m_spectrumScopeDisplay)
        {
            m_window->m_spectrumScopeDisplay->clearFrequencyPanRange();
        }
        return;
    }

    if (m_window->m_spectrumScopeFixedPanStartHz == 0 && m_window->m_spectrumScopeFixedPanEndHz == 0)
    {
        // Center mode may legitimately extend beyond an amateur-band edge
        // when the active VFO is closer to that edge than the selected
        // half-span. Keep the band range for the scrollbar, but do not clamp
        // the canvas away from the VFO's true center.
        m_hasLastSpectrumScopeLimits = true;
        m_lastSpectrumScopeLimitStartHz = startHz;
        m_lastSpectrumScopeLimitEndHz = endHz;
        m_window->m_spectrumScope->clearFrequencyLimits();
        if (m_window->m_spectrumScopeDisplay)
        {
            m_window->m_spectrumScopeDisplay->setFrequencyPanRange(startHz / 1e6, endHz / 1e6);
        }
        return;
    }
    m_hasLastSpectrumScopeLimits = true;
    m_lastSpectrumScopeLimitStartHz = startHz;
    m_lastSpectrumScopeLimitEndHz = endHz;
    m_window->m_spectrumScope->setFrequencyLimits(startHz / 1e6, endHz / 1e6);
    if (m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->setFrequencyPanRange(startHz / 1e6, endHz / 1e6);
    }
}

void SpectrumScopeController::applySpectrumScopeSettings()
{
    if (!m_window->m_model || !m_window->m_model->isReady())
    {
        return;
    }

    auto* backend = m_window->m_model->backend();
    if (!backend)
    {
        return;
    }

    const quint64 spanHz = AppSettings::instance()
                               .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                      QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                               .toULongLong();

    backend->setScopeMode(0);
    backend->setScopeSpanHz(spanHz);
    m_window->m_spectrumScopeFixedPanStartHz = 0;
    m_window->m_spectrumScopeFixedPanEndHz = 0;
    if (m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->setCurrentSpanHz(spanHz);
    }
}

quint64 SpectrumScopeController::roundFrequencyToStep(quint64 hz) const
{
    return sdr9700::roundFrequencyToStep(hz, static_cast<quint64>(m_window->tuningStepHz()));
}

void SpectrumScopeController::panSpectrumScopeToCenter(quint64 centerHz)
{
    if (!m_window->m_spectrumScopeDisplay || !m_window->m_spectrumScope || !m_window->m_model->isReady())
    {
        return;
    }

    centerHz = clampSpectrumScopeCenterHz(centerHz, m_window->m_spectrumScope->bandwidthMhz());
    const double bandwidthMhz = m_window->m_spectrumScope->bandwidthMhz();
    const quint64 bandwidthHz = static_cast<quint64>(std::llround(bandwidthMhz * 1e6));
    const quint64 startHz = centerHz - bandwidthHz / 2;
    const quint64 endHz = startHz + bandwidthHz;
    const double centerMhz = centerHz / 1e6;
    m_window->m_spectrumScopeDisplayCenterHz = centerHz;
    m_window->m_spectrumScopeDisplay->setFrequencyRange(centerMhz - bandwidthMhz / 2.0, centerMhz + bandwidthMhz / 2.0);
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        const auto changedEnough = [](quint64 current, quint64 previous)
        {
            return current > previous ? current - previous >= kSpectrumScopeFixedPanMinDeltaHz
                                      : previous - current >= kSpectrumScopeFixedPanMinDeltaHz;
        };
        if (m_window->m_spectrumScopeFixedPanStartHz == 0 || m_window->m_spectrumScopeFixedPanEndHz == 0 ||
            changedEnough(startHz, m_window->m_spectrumScopeFixedPanStartHz) ||
            changedEnough(endHz, m_window->m_spectrumScopeFixedPanEndHz))
        {
            backend->setScopeFixedRangeHz(startHz, endHz);
            m_window->m_spectrumScopeFixedPanStartHz = startHz;
            m_window->m_spectrumScopeFixedPanEndHz = endHz;
        }
    }
    updateSpectrumVfoMarker();
}

quint64 SpectrumScopeController::clampSpectrumScopeCenterHz(quint64 hz, double bandwidthMhz) const
{
    const quint64 referenceHz = activeVfoFrequencyHz();
    return sdr9700::clampScopeCenterToBand(hz, referenceHz, bandwidthMhz);
}

quint64 SpectrumScopeController::clampFrequencyHzToActiveBand(quint64 hz) const
{
    const quint64 referenceHz = activeVfoFrequencyHz();
    return sdr9700::clampFrequencyToBand(hz, referenceHz);
}

void SpectrumScopeController::scheduleSpectrumScopeTune(quint64 hz)
{
    scheduleSpectrumScopeTune(hz, true, false, false, true);
}

void SpectrumScopeController::scheduleSpectrumScopeTune(quint64 hz, bool snapToTuningStep, bool commitImmediately,
                                                        bool clearStaleDisplay, bool recenterDisplay)
{
    // Wheel and RC-28 tuning are step-based controls, but a mouse click on the
    // Spectrum Scope is an absolute frequency selection. Snapping clicks to the
    // current VFO step makes off-step signals appear to move away, so callers
    // choose the behavior explicitly. Mouse clicks preserve the current view;
    // RECENTER is the operator's explicit request to move it.
    if (snapToTuningStep)
    {
        hz = roundFrequencyToStep(hz);
    }
    hz = clampFrequencyHzToActiveBand(hz);
    quint64 displayCenterHz =
        clampSpectrumScopeCenterHz(hz, m_window->m_spectrumScope ? m_window->m_spectrumScope->bandwidthMhz() : 0.0);
    if (!recenterDisplay && m_window->m_spectrumScope)
    {
        displayCenterHz = m_window->m_spectrumScopeDisplayCenterHz;
        if (displayCenterHz == 0)
        {
            displayCenterHz = static_cast<quint64>(
                std::llround((m_window->m_spectrumScope->startMhz() + m_window->m_spectrumScope->endMhz()) * 0.5e6));
        }
        panSpectrumScopeToCenter(displayCenterHz);
    }
    m_window->leaveMemoryModeForManualChange();
    m_window->m_pendingSpectrumScopeTuneHz = hz;
    m_tuneIntentHz = hz;
    ++m_tuneIntentGeneration;
    m_tuneIntentClock.restart();
    qInfo(logSpectrumScope()).noquote().nospace()
        << "Spectrum tune requested generation=" << m_tuneIntentGeneration << " targetHz=" << hz;
    updateSpectrumScopeBandLimits(hz);
    if (clearStaleDisplay && m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->clearDisplay();
    }
    if (recenterDisplay && (m_window->m_spectrumScopeFixedPanStartHz > 0 || m_window->m_spectrumScopeFixedPanEndHz > 0))
    {
        if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
        {
            const quint64 spanHz = AppSettings::instance()
                                       .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                              QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                                       .toULongLong();
            backend->setScopeMode(0);
            backend->setScopeSpanHz(spanHz);
        }
        m_window->m_spectrumScopeFixedPanStartHz = 0;
        m_window->m_spectrumScopeFixedPanEndHz = 0;
    }
    if (m_window->m_spectrumScope)
    {
        const quint64 expectedSourceCenterHz = recenterDisplay ? hz : displayCenterHz;
        m_window->m_spectrumScope->holdDisplayCenter(displayCenterHz / 1e6, expectedSourceCenterHz / 1e6);
        m_window->m_spectrumScope->centerOnFrequency(displayCenterHz / 1e6);
    }
    updateSpectrumVfoMarker();

    if (commitImmediately)
    {
        if (m_window->m_spectrumScopeTuneCommitTimer)
        {
            m_window->m_spectrumScopeTuneCommitTimer->stop();
        }
        if (m_window->m_pendingSpectrumScopeTuneHz != 0 && m_window->m_model->isReady() &&
            !m_window->m_controlsLocked && m_scopeFramesEnabled)
        {
            m_window->leaveMemoryModeForManualChange();
            tuneActiveVfo(m_window->m_pendingSpectrumScopeTuneHz);
        }
    }
    else if (m_window->m_spectrumScopeTuneCommitTimer)
    {
        m_window->m_spectrumScopeTuneCommitTimer->start();
    }
    if (m_window->m_spectrumScopeTuneReleaseTimer)
    {
        m_window->m_spectrumScopeTuneReleaseTimer->start();
    }
}

void SpectrumScopeController::onSpectrumReady(const QVector<float>& levels, double start, double end, bool outOfRange)
{
    if (!m_scopeFramesEnabled)
    {
        return;
    }
    const quint64 referenceHz = activeVfoFrequencyHz();
    if (m_exchangeScopeSyncPending)
    {
        const double referenceMhz = referenceHz / 1e6;
        const quint64 frameCenterHz = static_cast<quint64>(std::llround(((start + end) / 2.0) * 1e6));
        const availableBands referenceBand = sdr9700::radioBandForFrequency(referenceHz);
        const availableBands frameBand = sdr9700::radioBandForFrequency(frameCenterHz);
        const bool bandsMatch = referenceBand != bandUnknown && frameBand == referenceBand;
        const bool rangeContainsReference = referenceMhz >= start && referenceMhz <= end;
        if (referenceHz == 0 || (!bandsMatch && !rangeContainsReference))
        {
            ++m_exchangeRejectedFrames;
            return;
        }
        qInfo(logSpectrumScope()).noquote().nospace()
            << "Exchange scope synchronized rejectedFrames=" << m_exchangeRejectedFrames
            << " referenceHz=" << referenceHz << " frameStart=" << start << " frameEnd=" << end;
        m_exchangeScopeSyncPending = false;
        m_exchangeScopeSyncTimer->stop();
        m_exchangeRejectedFrames = 0;
        if (m_window->m_vfoSelectionController)
        {
            m_window->m_vfoSelectionController->completeExchangeScopeSync();
        }
    }
    if (referenceHz > 0)
    {
        updateSpectrumScopeBandLimits(referenceHz);
    }
    m_window->m_spectrumScopeDisplay->setDataFrequencyRange(start, end);
    updateSpectrumVfoMarker();
    m_window->m_spectrumScopeDisplay->updateSpectrum(levels, outOfRange);
}

void SpectrumScopeController::resetScopeFrameGate()
{
    m_activeVfoStatePublished = false;
    m_scopeVfoConfirmed = false;
    m_scopeFramesEnabled = false;
    updateInteractionLock();
    if (m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->clearDisplay();
    }
}

void SpectrumScopeController::updateScopeFrameGate()
{
    if (m_scopeFramesEnabled || !m_activeVfoStatePublished || !m_scopeVfoConfirmed)
    {
        return;
    }
    m_scopeFramesEnabled = true;
    updateInteractionLock();
    recenterActiveVfo(true);
}

void SpectrumScopeController::updateInteractionLock()
{
    const bool ready = m_scopeFramesEnabled;
    if (m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->setInteractionLocked(!ready);
    }
    if (m_window->m_vfoSelectionController)
    {
        m_window->m_vfoSelectionController->setReceiverContextReady(ready);
    }
}

void SpectrumScopeController::onSpectrumClicked(double freqMhz)
{
    if (!m_window->m_model->isReady() || m_window->m_controlsLocked || !m_scopeFramesEnabled)
    {
        return;
    }

    const quint64 targetHz = static_cast<quint64>(std::llround(freqMhz * 1e6));
    // Rapid clicks are absolute intents, not incremental steps. Let the
    // existing short commit timer collapse them to the latest target so the
    // radio does not receive a burst of VFO sets and confirmatory readbacks.
    scheduleSpectrumScopeTune(targetHz, false, false, false, false);
}

void SpectrumScopeController::onWheelStepRequested(int steps)
{
    if (steps == 0 || !m_window->m_vfo || !m_window->m_model->isReady() || m_window->m_controlsLocked ||
        !m_scopeFramesEnabled)
    {
        return;
    }

    const qint64 currentHz = static_cast<qint64>(
        m_window->m_pendingSpectrumScopeTuneHz > 0 ? m_window->m_pendingSpectrumScopeTuneHz : activeVfoFrequencyHz());
    const qint64 targetHz = currentHz + static_cast<qint64>(steps) * m_window->tuningStepHz();
    scheduleSpectrumScopeTune(
        static_cast<quint64>(std::max<qint64>(static_cast<qint64>(kMinimumTuneFrequencyHz), targetHz)));
}

quint64 SpectrumScopeController::activeVfoFrequencyHz() const
{
    if (!m_window->m_vfoSelectionController)
    {
        return m_window->m_vfoFrequencyHz > 0 ? m_window->m_vfoFrequencyHz
                                              : (m_window->m_vfo ? m_window->m_vfo->frequencyHz() : 0);
    }
    const VfoController* controller = m_window->m_vfoSelectionController->selectedVfo() == Vfo::Main
                                          ? m_window->m_mainVfoController
                                          : m_window->m_subVfoController;
    return controller ? controller->frequencyHz() : 0;
}

void SpectrumScopeController::onActiveVfoFrequencyChanged(Vfo vfo, quint64 hz)
{
    if (!m_window->m_vfoSelectionController || m_window->m_vfoSelectionController->selectedVfo() != vfo)
    {
        return;
    }

    if (m_tuneIntentHz > 0)
    {
        if (hz == m_tuneIntentHz)
        {
            qInfo(logSpectrumScope()).noquote().nospace()
                << "Spectrum tune confirmed generation=" << m_tuneIntentGeneration << " targetHz=" << hz
                << " elapsedMs=" << (m_tuneIntentClock.isValid() ? m_tuneIntentClock.elapsed() : -1);
            m_tuneIntentHz = 0;
            m_tuneIntentClock.invalidate();
        }
        else
        {
            qDebug(logSpectrumScope()).noquote().nospace()
                << "Spectrum tune ignored stale readback generation=" << m_tuneIntentGeneration << " reportedHz=" << hz
                << " targetHz=" << m_tuneIntentHz;
        }
    }

    quint64& pendingRecenterHz = vfo == Vfo::Main ? m_pendingMainRecenterHz : m_pendingSubRecenterHz;
    const bool requestedRecenter = pendingRecenterHz > 0 && pendingRecenterHz == hz;
    if (requestedRecenter)
    {
        pendingRecenterHz = 0;
    }

    if (!m_hasCenteredActiveVfo || requestedRecenter)
    {
        recenterActiveVfo(true);
        return;
    }

    updateSpectrumScopeBandLimits(hz);
    updateSpectrumVfoMarker();
}

void SpectrumScopeController::recenterActiveVfo(bool clearDisplay)
{
    const quint64 hz = activeVfoFrequencyHz();
    if (hz == 0 || !m_window->m_spectrumScope)
    {
        return;
    }

    // Panning puts the IC-9700 scope into fixed-range mode. Re-centering only
    // the local canvas leaves the radio streaming that same fixed range, so
    // the next frame immediately moves the display back. Restore center mode
    // and the configured span before accepting new frames.
    if (m_window->m_model && m_window->m_model->isReady())
    {
        if (auto* backend = m_window->m_model->backend())
        {
            const quint64 spanHz = AppSettings::instance()
                                       .value(QString::fromLatin1(kSpectrumScopeSpanHZSettingsKey),
                                              QVariant::fromValue<qulonglong>(kDefaultSpectrumScopeSpanHZ))
                                       .toULongLong();
            backend->setScopeMode(0);
            backend->setScopeSpanHz(spanHz);
            if (m_window->m_spectrumScopeDisplay)
            {
                m_window->m_spectrumScopeDisplay->setCurrentSpanHz(spanHz);
            }
        }
    }
    m_window->m_spectrumScopeFixedPanStartHz = 0;
    m_window->m_spectrumScopeFixedPanEndHz = 0;
    m_window->m_pendingSpectrumScopeTuneHz = 0;
    m_window->m_spectrumScope->clearDisplayCenterHold();
    updateSpectrumScopeBandLimits(hz);
    if (clearDisplay && m_window->m_spectrumScopeDisplay)
    {
        m_window->m_spectrumScopeDisplay->clearDisplay();
    }
    m_window->m_spectrumScope->centerOnFrequency(hz / 1e6);
    updateSpectrumVfoMarker();
    m_hasCenteredActiveVfo = true;
}

void SpectrumScopeController::tuneActiveVfo(quint64 hz)
{
    if (auto* backend = m_window->m_model ? m_window->m_model->backend() : nullptr)
    {
        const Vfo active =
            m_window->m_vfoSelectionController ? m_window->m_vfoSelectionController->selectedVfo() : Vfo::Main;
        qInfo(logSpectrumScope()).noquote().nospace()
            << "Spectrum tune dispatched generation=" << m_tuneIntentGeneration << " targetHz=" << hz
            << " elapsedMs=" << (m_tuneIntentClock.isValid() ? m_tuneIntentClock.elapsed() : -1);
        backend->setVfoFrequencyHz(active, hz);
    }
}
