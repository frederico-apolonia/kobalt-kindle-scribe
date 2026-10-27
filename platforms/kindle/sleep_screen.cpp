#include "sleep_screen.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/views/sleep_screen.h"
#include "support/strings.h"
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

QImage SleepScreen::libraryPicture(QSize size, const BatteryStatus &battery)
{
    QImage picture(size, QImage::Format_RGB32);
    if (picture.isNull())
        return {};
    picture.fill(Qt::white);
    {
        // The six bars of the mark, in the proportions of assets/kobalt.svg, over the name in the brand face.
        QPainter painter(&picture);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink(0x16, 0x16, 0x16);
        const qreal markWidth = size.width() * .36, scale = markWidth / 58.5;
        const qreal barHeight = 6.5 * scale, pitch = 11.3 * scale;
        const qreal markHeight = 5 * pitch + barHeight;
        const QFont name = ui::brandFont(qRound(markWidth * .5));
        const QFontMetrics metrics(name);
        const qreal gap = markWidth * .32;
        const qreal top = (size.height() - (markHeight + gap + metrics.height())) * .42;
        const qreal left = (size.width() - markWidth) / 2;
        for (int bar = 0; bar < 6; ++bar)
            painter.fillRect(QRectF(left, top + bar * pitch, bar == 5 ? 42.3 * scale : markWidth, barHeight), ink);
        painter.setPen(ink);
        painter.setFont(name);
        painter.drawText(QRectF(0, top + markHeight + gap, size.width(), metrics.height()), Qt::AlignHCenter | Qt::AlignTop,
                         QStringLiteral("Kobalt"));
    }
    const auto status = battery.fullyCharged ? reader::SleepStatus::Charged
                        : battery.charging   ? reader::SleepStatus::Charging
                                             : reader::SleepStatus::Sleeping;
    return reader::sleepScreenImage(std::move(picture), strings::get("library.title"), {}, false, battery.percent, status,
                                    false);
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
