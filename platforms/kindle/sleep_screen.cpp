#include "sleep_screen.h"
#include <QPaintEvent>
#include <QPainter>
#include <QWindow>

namespace kobalt::kindle {
SleepScreen::SleepScreen(QWidget *host) : QWidget(host)
{
    setObjectName("kindleSleepScreen");
    setAttribute(Qt::WA_OpaquePaintEvent);
    hide();
}

bool SleepScreen::present(const QImage &next)
{
    if (next.isNull() || !parentWidget())
        return false;
    image = next;
    setGeometry(parentWidget()->rect());
    show();
    raise();
    submit(rect());
    return true;
}

bool SleepScreen::refresh(const QImage &next, const QRect &damage)
{
    if (!isVisible() || next.isNull())
        return false;
    image = next;
    submit(damage);
    return true;
}

void SleepScreen::clear()
{
    if (!isVisible())
        return;
    image = {};
    hide();
    if (auto *handle = window()->windowHandle())
        handle->setProperty("kindlefb_full_refresh", true);
}

void SleepScreen::submit(const QRegion &region)
{
    // Asks the display plugin for a flashing refresh and tells it this frame is the sleep picture, after which it
    // pauses its output instead of waiting out its safety delay.
    if (auto *handle = window()->windowHandle()) {
        handle->setProperty("kindlefb_full_refresh", true);
        handle->setProperty("kindlefb_sleep_ready", true);
    }
    repaint(region);
}

void SleepScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::white);
    if (!image.isNull())
        painter.drawImage(rect(), image);
}
}
