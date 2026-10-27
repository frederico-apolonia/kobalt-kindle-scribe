#include "kindleinput.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QSocketNotifier>
#include <QtCore/QStringList>
#include <QtGui/QPointingDevice>
#include <qpa/qwindowsysteminterface.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

using namespace kindleinput;

namespace {
constexpr int kBitsPerLong = int(sizeof(unsigned long) * 8);

template <std::size_t N>
bool testBit(int bit, const unsigned long (&bits)[N])
{
    return bit >= 0 && std::size_t(bit / kBitsPerLong) < N && ((bits[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1UL);
}

int axisMaximum(int fd, int code)
{
    input_absinfo info{};
    return ioctl(fd, EVIOCGABS(code), &info) < 0 ? 0 : info.maximum;
}

// uinput copies of real devices (the Scribe's "stylus-custom" pen) live under /sys/devices/virtual.
bool isVirtualNode(const QString &path)
{
    const QString name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    return QFile::symLinkTarget(QStringLiteral("/sys/class/input/") + name).contains(QLatin1String("/devices/virtual/"));
}

ulong milliseconds(const input_event &e)
{
    return ulong(e.time.tv_sec) * 1000UL + ulong(e.time.tv_usec / 1000);
}

double seconds(const input_event &e)
{
    return double(e.time.tv_sec) + double(e.time.tv_usec) / 1e6;
}
}

struct KindleInput::Node {
    Role role;
    QString path;
    int fd = -1;
    std::unique_ptr<QSocketNotifier> notifier;
    Node(Role r, QString p, int descriptor) : role(r), path(std::move(p)), fd(descriptor) {}
    ~Node()
    {
        notifier.reset();
        if (fd >= 0)
            ::close(fd);
    }
};

// Raw captures played back at their recorded pace: each stream is a file of struct input_event records.
struct KindleInput::Replay {
    struct Stream {
        Role role;
        std::vector<input_event> events;
        std::size_t next = 0;
    };
    std::vector<Stream> streams;
    QElapsedTimer clock;
    QTimer timer;
    double origin = 0;
    std::size_t total = 0;
};

KindleInput::KindleInput(QSize panelSize, std::function<void(Qt::ScreenOrientation)> callback)
    : orientationChanged(std::move(callback)), panel(panelSize)
{
    settle.setSingleShot(true);
    settle.setInterval(700);
    QObject::connect(&settle, &QTimer::timeout, &settle, [this] {
        const Qt::ScreenOrientation next = pending == Orientation::PortraitDown ? Qt::InvertedPortraitOrientation
                                                                               : Qt::PortraitOrientation;
        if (next == current)
            return;
        current = next;
        qInfo("kindlefb input: orientation settled on %s", next == Qt::PortraitOrientation ? "portrait" : "inverted portrait");
        if (orientationChanged)
            orientationChanged(next);
    });
    const QString capture = qEnvironmentVariable("KOBALT_KINDLE_REPLAY");
    if (capture.isEmpty())
        discover();
    else
        replay(capture);
}

KindleInput::~KindleInput() = default;

Qt::ScreenOrientation KindleInput::initialOrientation()
{
    const QByteArray code = qgetenv("KOBALT_KINDLE_ACCEL").trimmed().toUpper();
    return code == "D" || code == "R" ? Qt::InvertedPortraitOrientation : Qt::PortraitOrientation;
}

void KindleInput::addTouchDevice(int maxX, int maxY)
{
    touchMaxX = std::max(1, maxX);
    touchMaxY = std::max(1, maxY);
    touchDevice = new QPointingDevice(QStringLiteral("pt_mt"), 1, QInputDevice::DeviceType::TouchScreen,
                                      QPointingDevice::PointerType::Finger,
                                      QInputDevice::Capability::Position | QInputDevice::Capability::Area
                                          | QInputDevice::Capability::Pressure
                                          | QInputDevice::Capability::NormalizedPosition,
                                      2, 0, QString(), QPointingDeviceUniqueId(), this);
    QWindowSystemInterface::registerInputDevice(touchDevice);
}

void KindleInput::addPenDevice(int maxX, int maxY, int maxPressure)
{
    penMaxX = std::max(1, maxX);
    penMaxY = std::max(1, maxY);
    penMaxPressure = std::max(1, maxPressure);
    penDevice = new QPointingDevice(QStringLiteral("WacomDigitizer"), 2, QInputDevice::DeviceType::Stylus,
                                    QPointingDevice::PointerType::Pen,
                                    QInputDevice::Capability::Position | QInputDevice::Capability::Pressure
                                        | QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt
                                        | QInputDevice::Capability::Hover,
                                    1, 0, QString(), QPointingDeviceUniqueId(), this);
    QWindowSystemInterface::registerInputDevice(penDevice);
}

void KindleInput::discover()
{
    const QByteArray wantTouch = qgetenv("KOBALT_KINDLE_TOUCH");
    const QByteArray wantPen = qgetenv("KOBALT_KINDLE_PEN");
    const QByteArray wantOrientation = qgetenv("KOBALT_KINDLE_ORIENTATION");

    QStringList paths;
    if (DIR *dir = opendir("/dev/input")) {
        while (const dirent *entry = readdir(dir)) {
            if (std::strncmp(entry->d_name, "event", 5) == 0)
                paths << QStringLiteral("/dev/input/") + QString::fromLatin1(entry->d_name);
        }
        closedir(dir);
    } else {
        qWarning("kindlefb input: cannot open /dev/input: %s", std::strerror(errno));
        return;
    }
    paths.sort();

    bool haveTouch = false, havePen = false, haveOrientation = false;
    for (const QString &path : paths) {
        const QByteArray native = path.toLocal8Bit();
        const int fd = ::open(native.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        char name[128] = {};
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        unsigned long absBits[ABS_MAX / kBitsPerLong + 1] = {};
        unsigned long keyBits[KEY_MAX / kBitsPerLong + 1] = {};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absBits), absBits);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keyBits), keyBits);

        const bool isTouch = testBit(ABS_MT_SLOT, absBits) && testBit(ABS_MT_POSITION_X, absBits)
                          && testBit(ABS_MT_POSITION_Y, absBits) && testBit(ABS_MT_TRACKING_ID, absBits);
        // The raw Wacom stream carries pressure and tilt; the "stylus-custom" uinput copy advertises the same axes but
        // only reports pixel coordinates, so virtual nodes never qualify and a pen stroke is never delivered twice.
        const bool isPen = testBit(BTN_TOOL_PEN, keyBits) && testBit(ABS_X, absBits) && testBit(ABS_Y, absBits)
                        && testBit(ABS_PRESSURE, absBits) && !isVirtualNode(path);
        const bool isAccel = testBit(ABS_PRESSURE, absBits) && testBit(ABS_X, absBits) && testBit(ABS_Z, absBits)
                          && !testBit(BTN_TOOL_PEN, keyBits) && !testBit(BTN_TOUCH, keyBits);

        Role role{};
        QString detail;
        bool take = false;
        if (isTouch && !haveTouch && (wantTouch.isEmpty() || wantTouch == native)) {
            role = Role::Touch;
            haveTouch = take = true;
            addTouchDevice(axisMaximum(fd, ABS_MT_POSITION_X), axisMaximum(fd, ABS_MT_POSITION_Y));
            detail = QStringLiteral(", max %1x%2").arg(touchMaxX).arg(touchMaxY);
        } else if (isPen && !havePen && (wantPen.isEmpty() || wantPen == native)) {
            role = Role::Pen;
            havePen = take = true;
            addPenDevice(axisMaximum(fd, ABS_X), axisMaximum(fd, ABS_Y), axisMaximum(fd, ABS_PRESSURE));
            detail = QStringLiteral(", max %1x%2 pressure %3").arg(penMaxX).arg(penMaxY).arg(penMaxPressure);
        } else if (isAccel && !haveOrientation && (wantOrientation.isEmpty() || wantOrientation == native)) {
            role = Role::Orientation;
            haveOrientation = take = true;
        }

        if (!take) {
            ::close(fd);
            continue;
        }
        qInfo("kindlefb input: %s -> %s (%s%s)", name, native.constData(),
              role == Role::Touch ? "touch" : (role == Role::Pen ? "pen" : "orientation"), qPrintable(detail));
        auto node = std::make_unique<Node>(role, path, fd);
        Node *raw = node.get();
        node->notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
        QObject::connect(node->notifier.get(), &QSocketNotifier::activated, node->notifier.get(),
                         [this, raw] { drain(*raw); });
        nodes.push_back(std::move(node));
    }
    if (!haveTouch)
        qWarning("kindlefb input: no MT touch node found");
    if (!havePen)
        qWarning("kindlefb input: no pen node found");
    if (!haveOrientation)
        qWarning("kindlefb input: no orientation node found");
}

void KindleInput::replay(const QString &directory)
{
    // The axis ranges measured on the Scribe (evtest): a replay has no node to ask.
    addTouchDevice(1859, 2479);
    addPenDevice(15624, 20832, 4095);

    source = std::make_unique<Replay>();
    const struct {
        const char *file;
        Role role;
    } inputs[] = {{"event1.bin", Role::Orientation}, {"event2.bin", Role::Pen}, {"event3.bin", Role::Touch}};
    bool any = false;
    for (const auto &input : inputs) {
        QFile file(QDir(directory).filePath(QLatin1String(input.file)));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("kindlefb input: replay: cannot read %s", qPrintable(file.fileName()));
            continue;
        }
        const QByteArray bytes = file.readAll();
        Replay::Stream stream{input.role, std::vector<input_event>(std::size_t(bytes.size()) / sizeof(input_event)), 0};
        if (stream.events.empty())
            continue;
        std::memcpy(stream.events.data(), bytes.constData(), stream.events.size() * sizeof(input_event));
        const double begin = seconds(stream.events.front());
        source->origin = any ? std::min(source->origin, begin) : begin;
        any = true;
        source->total += stream.events.size();
        source->streams.push_back(std::move(stream));
    }
    qInfo("kindlefb input: replaying %zu events from %s", source->total, qPrintable(directory));
    source->clock.start();
    source->timer.setInterval(5);
    QObject::connect(&source->timer, &QTimer::timeout, this, [this] { advanceReplay(); });
    source->timer.start();
}

void KindleInput::advanceReplay()
{
    const double now = source->origin + double(source->clock.nsecsElapsed()) / 1e9;
    for (;;) {
        Replay::Stream *next = nullptr;
        for (Replay::Stream &stream : source->streams) {
            if (stream.next < stream.events.size()
                && (!next || seconds(stream.events[stream.next]) < seconds(next->events[next->next])))
                next = &stream;
        }
        if (!next) {
            source->timer.stop();
            qInfo("kindlefb input: replay finished");
            return;
        }
        const input_event &e = next->events[next->next];
        if (seconds(e) > now)
            return;
        ++next->next;
        dispatch(next->role, {e.type, e.code, e.value}, milliseconds(e));
    }
}

void KindleInput::dispatch(Role role, const RawEvent &event, ulong timestamp)
{
    switch (role) {
    case Role::Touch: handleTouch(event, timestamp); break;
    case Role::Pen: handlePen(event, timestamp); break;
    case Role::Orientation: handleOrientation(event); break;
    }
}

void KindleInput::drain(Node &node)
{
    input_event buffer[64];
    for (;;) {
        const ssize_t bytes = ::read(node.fd, buffer, sizeof buffer);
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            return; // EAGAIN: drained
        }
        if (bytes == 0)
            return;
        const std::size_t count = std::size_t(bytes) / sizeof(input_event);
        for (std::size_t i = 0; i < count; ++i) {
            const input_event &e = buffer[i];
            dispatch(node.role, {e.type, e.code, e.value}, milliseconds(e));
        }
    }
}

QPointF KindleInput::logical(double x, double y) const
{
    return rotated ? QPointF(panel.width() - 1 - x, panel.height() - 1 - y) : QPointF(x, y);
}

void KindleInput::handleTouch(const RawEvent &event, ulong timestamp)
{
    if (!touch.feed(event, touchPoints))
        return;
    // The controller flags a palm only after the contact began, in practice on its last frames. Qt gets a cancel
    // (Kobalt's widgets reset their gesture state on it) instead of a release that would complete a tap, and the
    // contacts still down are dropped with it.
    if (std::any_of(touchPoints.begin(), touchPoints.end(), [](const TouchDecoder::Point &p) { return p.palmCancel; })) {
        QWindowSystemInterface::handleTouchCancelEvent(nullptr, timestamp, touchDevice);
        touch.clear();
        return;
    }
    QList<QWindowSystemInterface::TouchPoint> list;
    list.reserve(int(touchPoints.size()));
    for (const auto &p : touchPoints) {
        QWindowSystemInterface::TouchPoint point;
        point.id = p.id;
        const QPointF where = logical(p.x * double(panel.width() - 1) / touchMaxX,
                                      p.y * double(panel.height() - 1) / touchMaxY);
        point.normalPosition = QPointF(where.x() / panel.width(), where.y() / panel.height());
        point.area = QRectF(where.x() - 4, where.y() - 4, 8, 8);
        point.pressure = std::clamp(p.pressure / 255.0, 0.0, 1.0);
        switch (p.state) {
        case TouchDecoder::State::Pressed: point.state = QEventPoint::State::Pressed; break;
        case TouchDecoder::State::Moved: point.state = QEventPoint::State::Updated; break;
        case TouchDecoder::State::Stationary: point.state = QEventPoint::State::Stationary; break;
        case TouchDecoder::State::Released: point.state = QEventPoint::State::Released; break;
        }
        list.append(point);
    }
    QWindowSystemInterface::handleTouchEvent(nullptr, timestamp, touchDevice, list);
}

void KindleInput::handlePen(const RawEvent &event, ulong timestamp)
{
    PenDecoder::Frame frame;
    if (!pen.feed(event, frame))
        return;
    const QPointF where = logical(frame.x * double(panel.width() - 1) / penMaxX,
                                  frame.y * double(panel.height() - 1) / penMaxY);
    const qreal xTilt = frame.tiltX / 100.0;
    const qreal yTilt = frame.tiltY / 100.0;
    if (frame.enter) {
        QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(nullptr, timestamp, penDevice, true, where, where,
                                                                      Qt::NoButton, xTilt, yTilt, 0, 0, 0);
    }
    if (frame.proximity || frame.release) {
        const qreal pressure = frame.touching ? std::clamp(frame.pressure / double(penMaxPressure), 0.0, 1.0) : 0.0;
        QWindowSystemInterface::handleTabletEvent(nullptr, timestamp, penDevice, where, where,
                                                  frame.touching ? Qt::LeftButton : Qt::NoButton, pressure, xTilt,
                                                  yTilt, 0.0, 0.0, 0);
    }
    // Contact is a press, movement while down a drag, lift or leaving proximity a release. Hovering sends nothing, so
    // the panel is not refreshed for a pen that only moves above it.
    if (frame.proximity || frame.release) {
        if (frame.touching && !penDown) {
            penDown = true;
            penWhere = where;
            penMouse(QEvent::MouseButtonPress, timestamp);
        } else if (frame.touching && where != penWhere) {
            penWhere = where;
            penMouse(QEvent::MouseMove, timestamp);
        } else if (!frame.touching && penDown) {
            penWhere = where;
            penMouse(QEvent::MouseButtonRelease, timestamp);
        }
    }
    if (frame.leave) {
        if (penDown)
            penMouse(QEvent::MouseButtonRelease, timestamp);
        QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(nullptr, timestamp, penDevice, false, where, where,
                                                                      Qt::NoButton, xTilt, yTilt, 0, 0, 0);
    }
}

void KindleInput::penMouse(QEvent::Type type, ulong timestamp)
{
    const bool release = type == QEvent::MouseButtonRelease;
    if (release)
        penDown = false;
    QWindowSystemInterface::handleMouseEvent(nullptr, timestamp, penWhere, penWhere,
                                             release ? Qt::NoButton : Qt::LeftButton, Qt::LeftButton, type);
}

void KindleInput::handleOrientation(const RawEvent &event)
{
    if (event.type != kEvAbs || event.code != kAbsPressure)
        return;
    const Orientation reading = orientationFromCode(event.value);
    // Only the two portrait states are supported; sideways readings are transitions (or ignored).
    if (reading == Orientation::PortraitUp || reading == Orientation::PortraitDown) {
        pending = reading;
        settle.start();
    }
}

void KindleInput::setRotated(bool value)
{
    rotated = value;
}

void KindleInput::releaseAll()
{
    // Qt cancels the contacts it knows about, so a rotation or a suspend can never complete a tap; the pen gets
    // its release and leaves proximity.
    if (touch.anyForwarded())
        QWindowSystemInterface::handleTouchCancelEvent(nullptr, 0, touchDevice);
    touch.clear();
    pen.reset();
    handlePen({kEvSyn, kSynReport, 0}, 0);
    if (penDown)
        penMouse(QEvent::MouseButtonRelease, 0);
}
