#pragma once
#include <QImage>
#include <QWidget>
#include "platform/api/reader_host_services.h"

namespace kobalt::kindle {
// The picture the panel keeps while the device sleeps: the reader's sleep image (book cover or dimmed page with
// progress and battery) drawn over the whole window. Nothing draws after the display plugin pauses at sleep, so
// presenting it paints and submits one full refresh before that.
class SleepScreen final : public QWidget {
public:
    explicit SleepScreen(QWidget *host);
    // Covers the host with `image` and submits it to the panel at once. False if there is no image.
    bool present(const QImage &image);
    // Redraws part of the image already shown (the battery in its card). Never shows a hidden screen.
    bool refresh(const QImage &image, const QRect &damage);
    // Removes the picture. The next frame is a full, flashing refresh, so the sleep image leaves no ghost.
    void clear();
    bool presented() const {
        return isVisible();
    }
    // The picture for a sleep with no book open: the Kobalt mark and name, with the sleep card (battery included).
    static QImage libraryPicture(QSize size, const BatteryStatus &battery);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage image;
    void submit(const QRegion &region);
};
}
