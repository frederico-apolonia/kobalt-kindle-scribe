#include "services.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "keyboard.h"
#include "light_device.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTime>
#include <algorithm>
#include <cmath>

namespace kobalt::kindle {
namespace {
// The Scribe's powerd exposes brightness and warmth as levels 0-24. Kobalt works in percent.
class Light final : public LightControls {
public:
    explicit Light(std::shared_ptr<LightDevice> device) : device(std::move(device)) {}
    bool initialize() override {
        return device && device->available();
    }
    bool hasWarmth() const override {
        return device && device->hasWarmth();
    }
    int warmth() const override {
        return device ? device->warmth() : 0;
    }
    int warmthMinimum() const override {
        return 0;
    }
    int warmthMaximum() const override {
        return device ? device->maxWarmth() : 0;
    }
    int warmthStep() const override {
        return 1;
    }
    bool automaticWarmth() const override {
        return false;
    }
    int setWarmth(int value) override {
        if (!device || !device->setWarmth(value))
            return -1;
        return device->warmth();
    }
    bool saveWarmth() override {
        if (!device)
            return false;
        device->commit();
        return true;
    }
    bool setAutomaticWarmth(bool) override {
        return false;
    }
    bool beginGesture() override {
        if (!initialize())
            return false;
        start = device->brightness();
        active = true;
        return true;
    }
    int setGestureLevel(int percent) override {
        if (!active || !device)
            return -1;
        const int maximum = device->maxBrightness();
        if (!device->setBrightness(int(std::lround(std::clamp(percent, 0, 100) * maximum / 100.0))))
            return -1;
        return brightness();
    }
    void finishGesture(bool save) override {
        if (!active || !device)
            return;
        active = false;
        if (save)
            device->commit();
        else
            device->setBrightness(start);
    }
    int brightness() const override {
        if (!device || device->maxBrightness() <= 0)
            return 0;
        return int(std::lround(device->brightness() * 100.0 / device->maxBrightness()));
    }
    bool gesturing() const override {
        return active;
    }

private:
    std::shared_ptr<LightDevice> device;
    int start = 0;
    bool active = false;
};
// Downloadable StarDict dictionaries are not wired up on this host yet.
class Dictionary final : public DictionaryService {
public:
    QVector<DictionaryChoice> choices() const override {
        return {};
    }
    bool select(const QString &, const QString &, QString *error) override {
        if (error)
            *error = QStringLiteral("No dictionary installed.");
        return false;
    }
    DictionaryResult lookup(const QString &, const QString &word) override {
        DictionaryResult result;
        result.word = word;
        result.error = QStringLiteral("No dictionary installed.");
        return result;
    }
};
// The gauge and the chargers are listed under /sys/class/power_supply. A battery that reports a capacity gives the
// percentage; any mains or USB supply that is online means the device is charging.
BatteryStatus readBattery() {
    const QDir root(QStringLiteral("/sys/class/power_supply"));
    BatteryStatus result;
    bool plugged = false;
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto value = [&](const char *file) {
            QFile data(root.filePath(name + QLatin1Char('/') + QLatin1String(file)));
            return data.open(QIODevice::ReadOnly) ? QString::fromLatin1(data.readAll()).trimmed() : QString();
        };
        const QString type = value("type");
        if (type == QLatin1String("Battery") && result.percent < 0) {
            bool ok = false;
            const int capacity = value("capacity").toInt(&ok);
            if (!ok)
                continue;
            const QString status = value("status");
            result.percent = std::clamp(capacity, 0, 100);
            result.charging = status == QLatin1String("Charging");
            result.fullyCharged = status == QLatin1String("Full");
        } else if (type == QLatin1String("Mains") || type == QLatin1String("USB")) {
            plugged = plugged || value("online") == QLatin1String("1");
        }
    }
    if (plugged && result.percent >= 0 && !result.fullyCharged)
        result.charging = true;
    return result;
}
// The search panel's keyboard: the window's on-screen keyboard, which the window docks above everything else. The
// panel sits in a scaled canvas, so the keyboard stays outside it, in window pixels.
class Keyboard final : public KeyboardController {
public:
    Keyboard(OnScreenKeyboard *dock, QLineEdit *editor) : dock(dock), editor(editor) {}
    ~Keyboard() override {
        release();
    }
    bool show() override {
        if (!editor || !dock)
            return false;
        // Focusing the editor docks the keyboard for it and drops any earlier Enter handler.
        editor->setFocus();
        dock->submitted = [this] {
            if (submitted)
                submitted();
        };
        dock->attach(editor);
        return true;
    }
    void hide() override {
        release();
    }
    void place() override {
        if (dock && dock->isVisible() && dock->target() == editor)
            dock->place();
    }

private:
    QPointer<OnScreenKeyboard> dock;
    QPointer<QLineEdit> editor;
    void release() {
        if (!dock || dock->target() != editor)
            return;
        dock->submitted = {};
        dock->dismiss();
    }
};
class Presentation final : public WindowPresentation {
public:
    void pauseBackground(bool) override {}
    void resumeBackground() override {}
    void resumeAndRepaintBackground() override {}
    void acceptHandoff(QDialog &, const std::function<void()> &accept) override {
        accept();
    }
};
}
ReaderHostServices readerServices(std::shared_ptr<LightDevice> light, OnScreenKeyboard *keyboard) {
    ReaderHostServices result;
    // The Scribe panel is monochrome: colorDisplay stays unset.
    result.browserAvailable = [] {
        return false;
    };
    result.openBrowser = [](const QUrl &, QWidget *) {
        return false;
    };
    result.clock = [] {
        return QTime::currentTime().toString("HH:mm");
    };
    result.battery = [] {
        return readBattery();
    };
    result.pageButtons = [] {
        return false;
    };
    result.reverseLandscapeButtons = [] {
        return false;
    };
    result.pageButtonDirection = [](QKeyEvent *) {
        return 0;
    };
    result.fontFamilies = [] {
        return QStringList{};
    };
    result.uiFont = [] {
        return ui::interfaceFont();
    };
    result.suspended = [] {
        return QGuiApplication::applicationState() != Qt::ApplicationActive;
    };
    result.lightAvailable = [light] {
        return light && light->available();
    };
    result.animationsAvailable = [] {
        return false;
    };
    result.cancelAnimation = [] {};
    result.light = [light](QWidget *) {
        return std::make_unique<Light>(light);
    };
    result.dictionary = [] {
        return std::make_unique<Dictionary>();
    };
    result.keyboard = [keyboard](QWidget *, QLineEdit *editor) {
        return std::make_unique<Keyboard>(keyboard, editor);
    };
    result.presentation = [](QWidget *) {
        return std::make_unique<Presentation>();
    };
    return result;
}
}
