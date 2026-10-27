#include "light_device.h"

#include <QtCore/QDebug>
#include <QtCore/QProcess>
#include <algorithm>

namespace kobalt::kindle {
namespace {
// Short synchronous call: powerd answers in milliseconds; a hung call is cut off after 3 s.
bool lipc(const QString &program, const QStringList &arguments, QString *output = nullptr)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(500);
        return false;
    }
    if (output)
        *output = QString::fromLatin1(process.readAllStandardOutput()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
}

bool LightDevice::get(const char *name, int *value)
{
    QString text;
    if (!lipc(QStringLiteral("lipc-get-prop"), {QStringLiteral("com.lab126.powerd"), QString::fromLatin1(name)}, &text))
        return false;
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (ok)
        *value = parsed;
    return ok;
}

bool LightDevice::set(const char *name, int value)
{
    return lipc(QStringLiteral("lipc-set-prop"),
                {QStringLiteral("-i"), QStringLiteral("com.lab126.powerd"), QString::fromLatin1(name), QString::number(value)});
}

LightDevice::LightDevice()
{
    int value = 0;
    if (get("flIntensity", &value))
        brightness_.initial = brightness_.current = value;
    if (get("flMaxIntensity", &value) && value > 0)
        maxBrightness_ = value;
    if (get("currentAmberLevel", &value))
        warmth_.initial = warmth_.current = value;
    qInfo("kindle light: brightness %d/%d warmth %d", brightness_.current, maxBrightness_, warmth_.current);
}

LightDevice::~LightDevice()
{
    for (Property *p : {&brightness_, &warmth_}) {
        if (p->changed && p->initial >= 0 && !set(p->name, p->initial))
            qWarning("kindle light: could not restore %s", p->name);
    }
}

bool LightDevice::write(Property &property, int level, int maximum)
{
    if (property.initial < 0)
        return false;
    level = std::clamp(level, 0, maximum);
    if (level == property.current)
        return true;
    if (!set(property.name, level))
        return false;
    property.current = level;
    property.changed = property.current != property.initial || property.changed;
    return true;
}

bool LightDevice::setBrightness(int level)
{
    return write(brightness_, level, maxBrightness_);
}

bool LightDevice::setWarmth(int level)
{
    return write(warmth_, level, maxWarmth());
}

void LightDevice::commit()
{
    brightness_.changed = warmth_.changed = false;
    brightness_.initial = brightness_.current;
    warmth_.initial = warmth_.current;
}

void LightDevice::revert()
{
    for (Property *p : {&brightness_, &warmth_}) {
        if (p->initial >= 0 && p->current != p->initial && set(p->name, p->initial))
            p->current = p->initial;
    }
}
}
