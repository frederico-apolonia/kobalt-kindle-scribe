// Qt-facing half of the Kindle Scribe input adapter: finds the touch, pen and accelerometer nodes by capability,
// decodes them with kindleinput_core.h and injects QPA touch/tablet events. The pt_mt touch panel and the raw
// Wacom stream are used; the virtual "stylus-custom" copy of the pen is deliberately left alone so a pen stroke is
// never delivered twice.
//
// Test seam: KOBALT_KINDLE_REPLAY=<directory> replaces the evdev nodes with raw captures (event1.bin accelerometer,
// event2.bin pen, event3.bin touch; 16-byte struct input_event records) played back at their recorded pace through
// the same decoders and delivery code, so the whole Qt path can be exercised without touching the device.
#pragma once

#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <functional>
#include <memory>
#include <vector>

#include "kindleinput_core.h"

QT_BEGIN_NAMESPACE
class QPointingDevice;
class QSocketNotifier;
QT_END_NAMESPACE

class KindleInput final : public QObject {
public:
    // `panel` is the framebuffer size in native (upright) orientation. `orientationChanged` is called, debounced,
    // when the accelerometer settles on a supported portrait orientation.
    KindleInput(QSize panel, std::function<void(Qt::ScreenOrientation)> orientationChanged);
    ~KindleInput() override;

    // Logical screen turned 180 degrees from the panel: rotate incoming coordinates to match what is drawn.
    void setRotated(bool rotated);
    // Cancel every touch contact and the pen state (rotation, suspend): Qt sees a touch cancel and a pen release,
    // never a stuck press.
    void releaseAll();
    // Orientation reported by `lipc-get-prop com.lab126.winmgr accelerometer`, passed in by the launcher through
    // KOBALT_KINDLE_ACCEL (U/D/L/R); KOReader's collapse of the four states to two portraits is kept.
    static Qt::ScreenOrientation initialOrientation();

private:
    enum class Role { Touch, Pen, Orientation };
    struct Node;
    struct Replay;
    void discover();
    void replay(const QString &directory);
    void advanceReplay();
    void addTouchDevice(int maxX, int maxY);
    void addPenDevice(int maxX, int maxY, int maxPressure);
    void drain(Node &node);
    void dispatch(Role role, const kindleinput::RawEvent &event, ulong timestamp);
    void handleTouch(const kindleinput::RawEvent &event, ulong timestamp);
    void handlePen(const kindleinput::RawEvent &event, ulong timestamp);
    void handleOrientation(const kindleinput::RawEvent &event);
    QPointF logical(double x, double y) const;

    std::vector<std::unique_ptr<Node>> nodes;
    std::unique_ptr<Replay> source;
    QPointingDevice *touchDevice = nullptr;
    QPointingDevice *penDevice = nullptr;
    kindleinput::TouchDecoder touch;
    kindleinput::PenDecoder pen;
    // The pen also drives the pointer: Kobalt's widgets ignore tablet events, so contact is mirrored as a left button.
    bool penDown = false;
    QPointF penWhere;
    void penMouse(QEvent::Type type, ulong timestamp);
    std::function<void(Qt::ScreenOrientation)> orientationChanged;
    QTimer settle;
    kindleinput::Orientation pending = kindleinput::Orientation::Unknown;
    Qt::ScreenOrientation current = Qt::PortraitOrientation;
    QSize panel;
    int penMaxX = 1;
    int penMaxY = 1;
    int touchMaxX = 1;
    int touchMaxY = 1;
    int penMaxPressure = 4095;
    bool rotated = false;
    std::vector<kindleinput::TouchDecoder::Point> touchPoints;
};
