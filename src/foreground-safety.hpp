#pragma once
#include <QWidget>

// An owned tool window can have the hidden OBS main window as its root owner.
// Only the visible, restored main window represents OBS being foreground.
inline bool isMainWindowForeground(const QWidget *main, const void *rootOwner, const void *mainHandle)
{
    return main && main->isVisible() && !main->isMinimized() && mainHandle && rootOwner == mainHandle;
}
