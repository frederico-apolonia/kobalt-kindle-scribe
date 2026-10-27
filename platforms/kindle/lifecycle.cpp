#include "lifecycle.h"

#include <QtCore/QDebug>
#include <algorithm>
#include <csignal>
#include <unistd.h>

namespace kobalt::kindle {
namespace {
constexpr const char *kEvents = "goingToScreenSaver,outOfScreenSaver,wakeupFromSuspend,readyToSuspend";
}

Lifecycle::Lifecycle(QObject *parent) : QObject(parent)
{
    restart.setSingleShot(true);
    QObject::connect(&restart, &QTimer::timeout, this, [this] { start(); });
    QObject::connect(&process, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    QObject::connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !stopping) {
            qWarning("kindle lifecycle: cannot start lipc-wait-event");
            restart.start(std::min(60000, 2000 << std::min(failures++, 5)));
        }
    });
    QObject::connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (stopping)
            return;
        qWarning("kindle lifecycle: lipc-wait-event ended (%d), restarting", code);
        restart.start(std::min(60000, 1000 << std::min(failures++, 5)));
    });
    start();
}

Lifecycle::~Lifecycle()
{
    stopping = true;
    if (process.state() != QProcess::NotRunning) {
        // lipc-wait-event leaves a second process of its own behind; the group takes both.
        kill(-pid_t(process.processId()), SIGTERM);
        process.terminate();
        if (!process.waitForFinished(1000)) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
}

void Lifecycle::start()
{
    process.setChildProcessModifier([] { setpgid(0, 0); });
    process.start(QStringLiteral("lipc-wait-event"),
                  {QStringLiteral("-m"), QStringLiteral("com.lab126.powerd"), QString::fromLatin1(kEvents)});
}

bool Lifecycle::parse(const QByteArray &line, QByteArray *name, int *source)
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

void Lifecycle::readOutput()
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
        qInfo("kindle lifecycle: %s %d", name.constData(), source);
        if (name == "goingToScreenSaver")
            emit goingToSleep(source);
        else if (name == "outOfScreenSaver" || name == "wakeupFromSuspend")
            emit wokeUp(source);
    }
    if (pending.size() > 4096)
        pending.clear();
}
}
