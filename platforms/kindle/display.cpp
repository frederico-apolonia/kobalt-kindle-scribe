#include "platform/api/kobo_refresh.h"
#include <QWidget>
#include <QWindow>
#include <cerrno>

namespace kobalt::platform {
// The kindlefb QPA plugin submits every flush's final damage as a partial update on its own. The only intent it
// cannot infer is a clean redraw: pageRefresh(clean) marks the window, and the plugin's next flush repaints the
// whole panel with a flashing refresh (the property is consumed there).
void textRefresh(QWidget *) {}
void pageRefresh(QWidget *widget, bool clean, bool) {
    if (!clean || !widget)
        return;
    if (auto *window = widget->window()->windowHandle())
        window->setProperty("kindlefb_full_refresh", true);
}
void setReaderRefreshWindow(QWidget *) {}
}
namespace kobalt::nickel4 {
// There is no Nickel to repair; the publication endpoint is read in process.
bool configureLoopback(int *error) {
    if (error)
        *error = ENOTSUP;
    return false;
}
}
