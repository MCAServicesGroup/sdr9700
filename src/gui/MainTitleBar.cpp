#include "MainTitleBar.h"
#include "UiTheme.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOption>
#include <QToolButton>
#include <QTimer>
#include <QWindow>
#include <memory>

namespace
{
// QToolButton that always renders the mnemonic underline (& prefix in text),
// independent of the platform style hint SH_UnderlineShortcut.
class TitleMenuButton : public QToolButton
{
  public:
    explicit TitleMenuButton(QWidget* parent = nullptr) : QToolButton(parent) {}

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        QPainter p(this);
        // Draw the native frame and background with an empty text field, then
        // draw the saved label explicitly with TextShowMnemonic. Some platform
        // styles otherwise suppress the underlined menu mnemonic even though
        // the QAction keyboard shortcut remains active.
        const QString labelText = opt.text;
        opt.text.clear();
        style()->drawComplexControl(QStyle::CC_ToolButton, &opt, &p, this);
        const QRect cr = style()->subControlRect(QStyle::CC_ToolButton, &opt, QStyle::SC_ToolButton, this);
        style()->drawItemText(&p, cr, Qt::AlignCenter | Qt::TextShowMnemonic, opt.palette, isEnabled(), labelText,
                              foregroundRole());
    }
};

QChar mnemonicChar(const QString& label)
{
    const int pos = label.indexOf(QLatin1Char('&'));
    if (pos >= 0 && pos + 1 < label.size() && label[pos + 1] != QLatin1Char('&'))
    {
        return label[pos + 1].toUpper();
    }
    return {};
}

constexpr int kTitleBarHeight = 32;
constexpr int kWindowButtonSize = 32;
constexpr int kVolumeSliderWidth = 110;
constexpr int kVolumeLabelWidth = 30;
constexpr int kTxDurationWidth = 62;
constexpr int kTitleControlSpacing = 8;
constexpr int kTitleSeparatorSpacing = 16;
constexpr int kTitleControlGroupLeftOffset = 40;
constexpr int kVolumeValueSpacing = 6;
constexpr int kHeartbeatIndicatorWidth = 10;
constexpr int kHeartbeatIndicatorSpacing = 12;
constexpr int kHeartbeatLitDurationMs = 100;
constexpr QMargins kNoMargins(0, 0, 0, 0);

QString heartbeatStyle(const char* color)
{
    return QStringLiteral("QLabel { background: %1; border-radius: 5px; }").arg(QString::fromLatin1(color));
}

QString menuButtonStyle()
{
    return QStringLiteral("QToolButton { background: transparent; color: %1; padding: 0 9px;"
                          " border: none; font-size: 13px; }"
                          "QToolButton:hover { background: %2; }"
                          "QToolButton:pressed { background: %3; }"
                          "QToolButton::menu-indicator { image: none; width: 0; }")
        .arg(UiTheme::Color::TextPrimary, UiTheme::Color::ButtonHover, UiTheme::Color::AccentDark);
}

QString controlLockButtonStyle(bool locked)
{
    return locked
               ? QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px;"
                                " padding: 0; }QPushButton:hover { background: %3; }")
                     .arg(UiTheme::Color::PanelDark, UiTheme::Color::Warning, UiTheme::Color::ButtonHover)
               : QStringLiteral("QPushButton { background: transparent; border: 1px solid %1; border-radius: 3px;"
                                " padding: 0; }"
                                "QPushButton:hover { background: %2; border-color: %3; }")
                     .arg(UiTheme::Color::BorderLight, UiTheme::Color::ButtonHover, UiTheme::Color::ButtonHoverBorder);
}

QString audioMuteButtonStyle(bool muted)
{
    if (!muted)
    {
        return controlLockButtonStyle(false);
    }
    return QStringLiteral("QPushButton { background: transparent; border: 1px solid %1; border-radius: 3px;"
                          " padding: 0; }"
                          "QPushButton:hover { background: %2; border-color: %1; }")
        .arg(UiTheme::Color::Danger, UiTheme::Color::ButtonHover);
}

QString txDurationStyle(bool active)
{
    return active ? QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px;"
                                   " color: %3; font-size: 10px; font-weight: bold; padding: 0 3px; }"
                                   "QPushButton:hover { background: %4; }")
                        .arg(UiTheme::Color::PttActive, UiTheme::Color::PttActiveBorder, UiTheme::Color::TextBright,
                             UiTheme::Color::PttHover)
                  : QStringLiteral("QPushButton { background: transparent; border: 1px solid %1; border-radius: 3px;"
                                   " color: %2; font-size: 10px; font-weight: bold; padding: 0 3px; }"
                                   "QPushButton:hover { background: %3; border-color: %4; }")
                        .arg(UiTheme::Color::BorderLight, UiTheme::Color::TextMuted, UiTheme::Color::ButtonHover,
                             UiTheme::Color::ButtonHoverBorder);
}

QString windowButtonStyle(const char* hoverBg)
{
    return QStringLiteral("QPushButton { background: transparent; border: none; color: %1; font-size: 14px; }"
                          "QPushButton:hover { background: %2; }")
        .arg(UiTheme::Color::TextMuted, QString::fromLatin1(hoverBg));
}

} // namespace

MainTitleBar::MainTitleBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kTitleBarHeight);
    QPalette titlePalette = palette();
    titlePalette.setColor(QPalette::Window, QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
    setPalette(titlePalette);
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("MainTitleBar { background: %1; }").arg(UiTheme::Color::WindowChrome));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(kNoMargins);
#if defined(Q_OS_MAC)
    root->setContentsMargins(11, 0, 0, 0);
#endif
    root->setSpacing(0);

#if !defined(Q_OS_MAC)
    // Menu buttons sit in a sub-layout so addMenu() can append to it
    auto* menuContainer = new QWidget(this);
    menuContainer->setObjectName(QStringLiteral("titleMenuContainer"));
    menuContainer->setStyleSheet(QStringLiteral("QWidget#titleMenuContainer { background: transparent; }"));
    m_menuLayout = new QHBoxLayout(menuContainer);
    m_menuLayout->setContentsMargins(10, 0, 0, 0);
    m_menuLayout->setSpacing(0);
    root->addWidget(menuContainer);

    root->addStretch(1);
#endif

    m_titleLabel = new QLabel(this);
#if defined(Q_OS_MAC)
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
#else
    m_titleLabel->setAlignment(Qt::AlignCenter);
#endif
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
    root->addWidget(m_titleLabel);

    root->addSpacing(kHeartbeatIndicatorSpacing);
    m_heartbeatIndicator = new QLabel(this);
    m_heartbeatIndicator->setObjectName(QStringLiteral("radioHeartbeatIndicator"));
    m_heartbeatIndicator->setFixedSize(kHeartbeatIndicatorWidth, kHeartbeatIndicatorWidth);
    m_heartbeatIndicator->setAlignment(Qt::AlignCenter);
    m_heartbeatIndicator->setStyleSheet(heartbeatStyle(UiTheme::Color::TextStatusLabel));
    m_heartbeatIndicator->setToolTip(QStringLiteral("Radio communication heartbeat"));
    m_heartbeatIndicator->setAccessibleName(QStringLiteral("Radio communication heartbeat"));
    root->addWidget(m_heartbeatIndicator);

    m_heartbeatFadeTimer = new QTimer(this);
    m_heartbeatFadeTimer->setSingleShot(true);
    m_heartbeatFadeTimer->setInterval(kHeartbeatLitDurationMs);
    connect(m_heartbeatFadeTimer, &QTimer::timeout, this, &MainTitleBar::clearRadioHeartbeat);

    root->addStretch(1);

    auto* controlGroupSep = new QWidget(this);
    controlGroupSep->setFixedSize(1, 18);
    controlGroupSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(controlGroupSep);
    root->addSpacing(kTitleSeparatorSpacing);

    m_txDurationButton = new QPushButton(QStringLiteral("00:00:00"), this);
    m_txDurationButton->setFixedSize(kTxDurationWidth, 22);
    m_txDurationButton->setCheckable(false);
    m_txDurationButton->setStyleSheet(txDurationStyle(false));
    m_txDurationButton->setToolTip(QStringLiteral("Reset transmit duration"));
    root->addWidget(m_txDurationButton);
    root->addSpacing(kTitleSeparatorSpacing);

    auto* txSep = new QWidget(this);
    txSep->setFixedSize(1, 18);
    txSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(txSep);
    root->addSpacing(kTitleSeparatorSpacing);

    m_lockBtn = new QPushButton(this);
    m_lockBtn->setObjectName(QStringLiteral("titleLockButton"));
    m_lockBtn->setFixedSize(24, 22);
    m_lockBtn->setIconSize(QSize(18, 18));
    m_lockBtn->setCheckable(false);
    setLockStateUnknown();
    root->addWidget(m_lockBtn);
    root->addSpacing(kTitleSeparatorSpacing);

    auto* btnSep = new QWidget(this);
    btnSep->setFixedSize(1, 18);
    btnSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(btnSep);
    root->addSpacing(kTitleSeparatorSpacing);

    m_speakerMuteBtn = new QPushButton(this);
    m_speakerMuteBtn->setObjectName(QStringLiteral("titleSpeakerMuteButton"));
    m_speakerMuteBtn->setFixedSize(24, 22);
    m_speakerMuteBtn->setIcon(QIcon(QStringLiteral(":/images/icons/control_audio_unmuted.svg")));
    m_speakerMuteBtn->setIconSize(QSize(18, 18));
    m_speakerMuteBtn->setCheckable(false);
    m_speakerMuteBtn->setStyleSheet(audioMuteButtonStyle(false));
    m_speakerMuteBtn->setToolTip(QStringLiteral("Mute audio"));
    m_speakerMuteBtn->setAccessibleName(QStringLiteral("Audio mute"));
    m_speakerMuteBtn->setAccessibleDescription(QStringLiteral("Mute or unmute received audio"));
    root->addWidget(m_speakerMuteBtn, 0, Qt::AlignVCenter);
    root->addSpacing(kTitleControlSpacing);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 255);
    m_volumeSlider->setValue(128);
    m_volumeSlider->setFixedWidth(kVolumeSliderWidth);
    m_volumeSlider->setFixedHeight(20);
    m_volumeSlider->setToolTip(QStringLiteral("Volume"));
    m_volumeSlider->setAccessibleName(QStringLiteral("Application audio volume"));
    m_volumeSlider->setAccessibleDescription(QStringLiteral("Adjust received-audio playback volume"));
    root->addWidget(m_volumeSlider);
    root->addSpacing(kVolumeValueSpacing);

    m_volumeLabel = new QLabel(QStringLiteral("50%"), this);
    m_volumeLabel->setFixedWidth(kVolumeLabelWidth);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 10px; font-weight: bold; background: transparent; }")
            .arg(UiTheme::Color::TextMuted));
    root->addWidget(m_volumeLabel);
    root->addSpacing(kTitleSeparatorSpacing);

    auto* chromeSep = new QWidget(this);
    chromeSep->setFixedSize(1, 18);
    chromeSep->setStyleSheet(QStringLiteral("background: %1;").arg(UiTheme::Color::Border));
    root->addWidget(chromeSep);
    root->addSpacing(kTitleSeparatorSpacing);
    root->addSpacing(kTitleControlGroupLeftOffset);

    m_minimizeBtn = new QPushButton(QStringLiteral("−"), this);
    m_minimizeBtn->setFixedSize(kWindowButtonSize, kTitleBarHeight);
    m_minimizeBtn->setStyleSheet(windowButtonStyle(UiTheme::Color::ButtonHover));
    m_minimizeBtn->setToolTip(QStringLiteral("Minimize"));
    m_minimizeBtn->setAccessibleName(QStringLiteral("Minimize window"));
    root->addWidget(m_minimizeBtn);

    m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
    m_closeBtn->setFixedSize(kWindowButtonSize, kTitleBarHeight);
    m_closeBtn->setStyleSheet(windowButtonStyle(UiTheme::Color::Danger));
    m_closeBtn->setToolTip(QStringLiteral("Close"));
    m_closeBtn->setAccessibleName(QStringLiteral("Close window"));
    root->addWidget(m_closeBtn);

    connect(m_volumeSlider, &QSlider::valueChanged, this,
            [this](int v)
            {
                const int pct = v * 100 / 255;
                if (m_volumeLabel)
                {
                    m_volumeLabel->setText(QStringLiteral("%1%").arg(pct));
                }
                emit volumeChanged(v);
            });
    connect(m_speakerMuteBtn, &QPushButton::clicked, this, &MainTitleBar::muteToggled);
    connect(m_lockBtn, &QPushButton::clicked, this, &MainTitleBar::lockToggled);
    connect(m_txDurationButton, &QPushButton::clicked, this, &MainTitleBar::txDurationResetRequested);
    connect(m_minimizeBtn, &QPushButton::clicked, this, &MainTitleBar::minimizeRequested);
    connect(m_closeBtn, &QPushButton::clicked, this, &MainTitleBar::closeRequested);
    auto* closeShortcut = new QShortcut(QKeySequence::Close, this);
    closeShortcut->setContext(Qt::WindowShortcut);
    connect(closeShortcut, &QShortcut::activated, this, &MainTitleBar::closeRequested);
}

void MainTitleBar::addMenu(const QString& label, QMenu* menu)
{
    auto* btn = new TitleMenuButton(this);
    btn->setText(label);
    btn->setMenu(menu);
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setStyleSheet(menuButtonStyle());
    btn->setFixedHeight(kTitleBarHeight);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_menuLayout->addWidget(btn);

    const QChar mnemonic = mnemonicChar(label);
    if (!mnemonic.isNull())
    {
        auto* sc = new QShortcut(QKeySequence(Qt::ALT | mnemonic.unicode()), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, btn, [btn]() { btn->showMenu(); });
    }
}

QToolButton* MainTitleBar::addAction(const QString& label, QObject* context, std::function<void()> callback)
{
    auto* btn = new TitleMenuButton(this);
    btn->setText(label);
    btn->setStyleSheet(menuButtonStyle());
    btn->setFixedHeight(kTitleBarHeight);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_menuLayout->addWidget(btn);

    auto callbackPtr = std::make_shared<std::function<void()>>(std::move(callback));
    connect(btn, &QToolButton::clicked, context,
            [callbackPtr]()
            {
                if (*callbackPtr)
                {
                    (*callbackPtr)();
                }
            });

    const QChar mnemonic = mnemonicChar(label);
    if (!mnemonic.isNull())
    {
        auto* sc = new QShortcut(QKeySequence(Qt::ALT | mnemonic.unicode()), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, context,
                [callbackPtr]()
                {
                    if (*callbackPtr)
                    {
                        (*callbackPtr)();
                    }
                });
    }

    return btn;
}

void MainTitleBar::setTitle(const QString& title)
{
    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }
}

void MainTitleBar::setVolume(int value)
{
    if (!m_volumeSlider)
    {
        return;
    }
    const int bounded = std::clamp(value, 0, 255);
    const QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setValue(bounded);
    if (m_volumeLabel)
    {
        m_volumeLabel->setText(QStringLiteral("%1%").arg(bounded * 100 / 255));
    }
}

void MainTitleBar::setMuted(bool muted)
{
    if (m_speakerMuteBtn)
    {
        m_speakerMuteBtn->setIcon(QIcon(muted ? QStringLiteral(":/images/icons/control_audio_muted.svg")
                                              : QStringLiteral(":/images/icons/control_audio_unmuted.svg")));
        m_speakerMuteBtn->setStyleSheet(audioMuteButtonStyle(muted));
        m_speakerMuteBtn->setToolTip(muted ? QStringLiteral("Unmute audio") : QStringLiteral("Mute audio"));
    }
}

void MainTitleBar::setLocked(bool locked)
{
    if (m_lockBtn)
    {
        m_lockBtn->setIcon(QIcon(locked ? QStringLiteral(":/images/icons/control_lock_locked.svg")
                                        : QStringLiteral(":/images/icons/control_lock_unlocked.svg")));
        m_lockBtn->setStyleSheet(controlLockButtonStyle(locked));
        m_lockBtn->setToolTip(locked ? QStringLiteral("Dial locked\nClick to unlock.")
                                     : QStringLiteral("Dial unlocked\nClick to lock."));
        m_lockBtn->setAccessibleName(locked ? QStringLiteral("Dial locked") : QStringLiteral("Dial unlocked"));
    }
}

void MainTitleBar::setLockStateUnknown()
{
    if (m_lockBtn)
    {
        m_lockBtn->setIcon(QIcon(QStringLiteral(":/images/icons/control_lock_unknown.svg")));
        m_lockBtn->setStyleSheet(controlLockButtonStyle(false));
        m_lockBtn->setToolTip(QStringLiteral("Dial lock state syncing"));
        m_lockBtn->setAccessibleName(QStringLiteral("Dial lock state syncing"));
    }
}

void MainTitleBar::setTxDuration(const QString& duration, bool transmitting)
{
    if (!m_txDurationButton)
    {
        return;
    }
    m_txDurationButton->setText(duration);
    m_txDurationButton->setStyleSheet(txDurationStyle(transmitting));
}

void MainTitleBar::setTxDurationActive(bool transmitting)
{
    if (m_txDurationButton)
    {
        m_txDurationButton->setStyleSheet(txDurationStyle(transmitting));
    }
}

void MainTitleBar::pulseRadioHeartbeat()
{
    if (!m_heartbeatIndicator || !m_heartbeatFadeTimer)
    {
        return;
    }
    m_heartbeatIndicator->setStyleSheet(heartbeatStyle(UiTheme::Color::Success));
    m_heartbeatFadeTimer->start();
}

void MainTitleBar::clearRadioHeartbeat()
{
    if (m_heartbeatFadeTimer)
    {
        m_heartbeatFadeTimer->stop();
    }
    if (m_heartbeatIndicator)
    {
        m_heartbeatIndicator->setStyleSheet(heartbeatStyle(UiTheme::Color::TextStatusLabel));
    }
}

void MainTitleBar::setVolumeEnabled(bool enabled)
{
    if (m_volumeSlider)
    {
        m_volumeSlider->setEnabled(enabled);
    }
    if (m_speakerMuteBtn)
    {
        m_speakerMuteBtn->setEnabled(enabled);
    }
}

void MainTitleBar::setLockEnabled(bool enabled)
{
    if (m_lockBtn)
    {
        m_lockBtn->setEnabled(enabled);
    }
}

void MainTitleBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QString::fromLatin1(UiTheme::Color::WindowChrome)));
    painter.fillRect(0, height() - 1, width(), 1, QColor(QString::fromLatin1(UiTheme::Color::WindowChromeShadow)));
}

void MainTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (QWindow* win = window()->windowHandle())
        {
            win->startSystemMove();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
