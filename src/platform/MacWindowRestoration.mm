#include "MacWindowRestoration.h"

#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace
{
void removeFullScreenMenuItems(NSMenu* menu)
{
    if (!menu)
    {
        return;
    }

    for (NSInteger index = menu.numberOfItems - 1; index >= 0; --index)
    {
        NSMenuItem* item = [menu itemAtIndex:index];
        if (item.action == @selector(toggleFullScreen:))
        {
            [menu removeItemAtIndex:index];
            continue;
        }
        removeFullScreenMenuItems(item.submenu);
    }
}

void disableRestoration(QWidget* widget)
{
    if (!widget || !widget->isWindow())
    {
        return;
    }

    NSView* nativeView = (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
    NSWindow* nativeWindow = nativeView.window;
    if (!nativeWindow)
    {
        return;
    }

    nativeWindow.restorable = NO;
    nativeWindow.restorationClass = Nil;
    nativeWindow.collectionBehavior = (nativeWindow.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenPrimary) |
                                      NSWindowCollectionBehaviorFullScreenNone;
    [nativeWindow disableSnapshotRestoration];
    widget->setProperty("sdr9700MacRestorationConfigured", true);

    // AppKit injects toggleFullScreen: into a native View menu even when Qt's
    // fullscreen button hint is disabled. sdr9700 has a fixed-size main
    // window, so remove the inapplicable system command after menu creation.
    static bool mainMenuSanitized = false;
    if (!mainMenuSanitized)
    {
        removeFullScreenMenuItems(NSApp.mainMenu);
        mainMenuSanitized = true;
    }
}

class MacWindowRestorationFilter final : public QObject
{
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Show && event->type() != QEvent::WinIdChange)
        {
            return QObject::eventFilter(watched, event);
        }
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->isWindow() && widget->windowType() != Qt::ToolTip && widget->windowType() != Qt::Popup &&
            (event->type() == QEvent::WinIdChange || !widget->property("sdr9700MacRestorationConfigured").toBool()))
        {
            const QPointer<QWidget> guardedWidget(widget);
            QTimer::singleShot(0, widget,
                               [guardedWidget]()
                               {
                                   if (guardedWidget)
                                   {
                                       disableRestoration(guardedWidget);
                                   }
                               });
        }
        return QObject::eventFilter(watched, event);
    }
};
} // namespace

void configureMacWindowRestoration(QApplication& app)
{
    // sdr9700 persists its own fixed-window positions through AppSettings.
    // AppKit's independent persistent-UI archive has repeatedly crashed while
    // encoding Qt-created NSColor state on macOS 26, so opt out rather than
    // maintaining two competing restoration systems.
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        @"ApplePersistenceIgnoreState" : @YES,
        @"NSQuitAlwaysKeepsWindows" : @NO
    }];

    app.installEventFilter(new MacWindowRestorationFilter(&app));
}
