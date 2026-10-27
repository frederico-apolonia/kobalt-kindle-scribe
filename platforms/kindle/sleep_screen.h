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
    // `snapshotPath` keeps the last book's sleep screen between sessions.
    SleepScreen(QWidget *host, QString snapshotPath);
    // Covers the host with `image` and submits it to the panel at once. False if there is no image.
    bool present(const QImage &image);
    // Redraws part of the image already shown (the battery in its card), also while the device sleeps: the display
    // plugin lets that one frame through. Never shows a hidden screen.
    bool refresh(const QImage &image, const QRect &damage);
    // Removes the picture. The next frame is a full, flashing refresh, so the sleep image leaves no ghost.
    void clear();
    bool presented() const {
        return isVisible();
    }
    // The reader hands over the screen of the book it is leaving (an empty one forgets it). It is kept in memory and
    // in the snapshot file.
    void remember(const sleep::Snapshot &snapshot);
    // The picture for a sleep with no book open: the last book's sleep screen with the current battery, as the Kobo
    // home screen shows it. Without one, the Kobalt mark and name with the same status tile.
    QImage libraryPicture(QSize size, const BatteryStatus &battery, sleep::Mode mode, sleep::Tile tile) const;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage image;
    QString snapshotPath;
    sleep::Snapshot last;
    void submit(const QRegion &region, bool update = false);
};
}
