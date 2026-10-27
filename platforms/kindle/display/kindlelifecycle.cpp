#include "kindlelifecycle.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <algorithm>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
constexpr const char *kEvents = "goingToScreenSaver,outOfScreenSaver,wakeupFromSuspend,readyToSuspend";
}

KindleLifecycle::KindleLifecycle(QObject *parent) : QObject(parent)
{
    restart.setSingleShot(true);
    QObject::connect(&restart, &QTimer::timeout, this, [this] { start(); });
    QObject::connect(&process, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    QObject::connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !stopping) {
            qWarning("kindlefb lifecycle: cannot start lipc-wait-event");
            restart.start(std::min(60000, 2000 << std::min(failures++, 5)));
        }
    });
    QObject::connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (stopping)
            return;
        qWarning("kindlefb lifecycle: lipc-wait-event ended (%d), restarting", code);
        restart.start(std::min(60000, 1000 << std::min(failures++, 5)));
    });
    start();
    if (qEnvironmentVariable("KOBALT_KINDLE_POWERKEY") == QLatin1String("1"))
        openPowerKey();
}

KindleLifecycle::~KindleLifecycle()
{
    stopping = true;
    keyNotifier.reset();
    if (keyFd >= 0)
        ::close(keyFd);
    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        if (!process.waitForFinished(1000)) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
}

void KindleLifecycle::start()
{
    process.start(QStringLiteral("lipc-wait-event"),
                  {QStringLiteral("-m"), QStringLiteral("com.lab126.powerd"), QString::fromLatin1(kEvents)});
}

bool KindleLifecycle::parse(const QByteArray &line, QByteArray *name, int *source)
{
    const QList<QByteArray> parts = line.simplified().split(' ');
    if (parts.isEmpty())
        return false;
    static const char *known[] = {"goingToScreenSaver", "outOfScreenSaver", "wakeupFromSuspend", "readyToSuspend"};
    if (std::none_of(std::begin(known), std::end(known), [&](const char *k) { return parts[0] == k; }))
        return false;
    bool ok = true;
    const int value = parts.size() > 1 ? parts[1].toInt(&ok) : 0;
    if (!ok)
        return false;
    *name = parts[0];
    *source = value;
    return true;
}

void KindleLifecycle::readOutput()
{
    failures = 0;
    pending += process.readAllStandardOutput();
    int newline;
    while ((newline = pending.indexOf('\n')) >= 0) {
        const QByteArray line = pending.left(newline);
        pending.remove(0, newline + 1);
        QByteArray name;
        int source = 0;
        if (!parse(line, &name, &source))
            continue;
        qInfo("kindlefb lifecycle: %s %d", name.constData(), source);
        if (name == "goingToScreenSaver")
            emit goingToSleep(source);
        // wakeupFromSuspend alone is the kernel resuming for a moment (a timer, or the charger being plugged in) and
        // powerd suspends again; only outOfScreenSaver means someone woke the device.
        else if (name == "outOfScreenSaver")
            emit wokeUp(source);
    }
    if (pending.size() > 4096)
        pending.clear();
}

// The power key is the evdev node that can report KEY_POWER and has no absolute axes.
void KindleLifecycle::openPowerKey()
{
    const QStringList names = QDir(QStringLiteral("/dev/input")).entryList({QStringLiteral("event*")}, QDir::System);
    for (const QString &name : names) {
        const QByteArray path = ("/dev/input/" + name).toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        unsigned char keys[KEY_MAX / 8 + 1] = {};
        unsigned char abs[ABS_MAX / 8 + 1] = {};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
        const bool hasPower = keys[KEY_POWER / 8] & (1 << (KEY_POWER % 8));
        const bool hasAbs = std::any_of(std::begin(abs), std::end(abs), [](unsigned char b) { return b != 0; });
        if (!hasPower || hasAbs) {
            ::close(fd);
            continue;
        }
        char label[64] = {};
        ioctl(fd, EVIOCGNAME(sizeof label - 1), label);
        qInfo("kindlefb lifecycle: power key %s -> %s", label, path.constData());
        keyFd = fd;
        keyNotifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
        QObject::connect(keyNotifier.get(), &QSocketNotifier::activated, this, [this] { readPowerKey(); });
        return;
    }
    qWarning("kindlefb lifecycle: no power key node found");
}

void KindleLifecycle::readPowerKey()
{
    input_event buffer[16];
    for (;;) {
        const ssize_t bytes = ::read(keyFd, buffer, sizeof buffer);
        if (bytes <= 0)
            return;
        for (std::size_t i = 0; i < std::size_t(bytes) / sizeof(input_event); ++i) {
            if (buffer[i].type == EV_KEY && buffer[i].code == KEY_POWER && buffer[i].value == 1) {
                qInfo("kindlefb lifecycle: power key pressed, forwarding to powerd");
                QProcess::startDetached(QStringLiteral("lipc-set-prop"),
                                        {QStringLiteral("-i"), QStringLiteral("com.lab126.powerd"),
                                         QStringLiteral("powerButton"), QStringLiteral("1")});
            }
        }
    }
}
