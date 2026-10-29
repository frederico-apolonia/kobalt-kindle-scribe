#include "services.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "keyboard.h"
#include "sleep_screen.h"
#include "support/strings.h"
#include "light_device.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTime>
#include <vector>
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
    // The panel's "Follow Kobo warmth schedule" row has no counterpart on the Kindle.
    bool hasAutomaticWarmth() const override {
        return false;
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
// The window's sleep screen shows the reader's sleep image; the panel keeps it while the device sleeps.
class Presentation final : public WindowPresentation {
public:
    explicit Presentation(SleepScreen *sleep) : sleep(sleep) {}
    ~Presentation() override {
        clearSleepScreen();
    }
    void pauseBackground(bool) override {}
    void resumeBackground() override {}
    void resumeAndRepaintBackground() override {}
    bool supportsSleepScreen() const override {
        return !sleep.isNull();
    }
    bool presentSleepScreen(QImage image, Qt::ScreenOrientation) override {
        return sleep && sleep->present(image);
    }
    bool updateSleepScreen(QImage image, const QRect &damage) override {
        return sleep && sleep->refresh(image, damage);
    }
    void clearSleepScreen() override {
        if (sleep)
            sleep->clear();
    }
    void saveSleepScreen(const sleep::Snapshot &snapshot) override {
        if (sleep)
            sleep->remember(snapshot);
    }
    void acceptHandoff(QDialog &, const std::function<void()> &accept) override {
        accept();
    }

private:
    QPointer<SleepScreen> sleep;
};
}
namespace {
// What the readers asked to hear about when the battery changes. Nothing runs while the device is suspended, so
// the window announces a change when the kernel resumes (see notifyBatteryObservers).
struct BatteryObserver {
    QPointer<QObject> context;
    std::function<void()> changed;
};
std::vector<BatteryObserver> &batteryObservers() {
    static std::vector<BatteryObserver> observers;
    return observers;
}
}
void notifyBatteryObservers() {
    auto &observers = batteryObservers();
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [](const BatteryObserver &observer) { return observer.context.isNull(); }),
                    observers.end());
    // A callback may open or close readers, which register or end observers.
    const auto current = observers;
    for (const auto &observer : current)
        if (observer.context && observer.changed)
            observer.changed();
}
BatteryStatus batteryStatus() {
    return readBattery();
}
ReaderHostServices readerServices(std::shared_ptr<LightDevice> light, const HostWidgets &widgets) {
    ReaderHostServices result;
    // The reader's back button returns to Kobalt's library; the library's More menu leaves the application.
    result.backButtonLabel = strings::get("library.title");
    // The Amazon screen saver is not available while Kobalt runs: the first choice leaves the page as it is.
    // The Scribe's page is 4.6 million pixels, a little above the default cap.
    result.maxSleepPixels = 6 * 1024 * 1024;
    result.stockSleepLabel = QObject::tr("Page as it is");
    result.stockSleepDescription = QObject::tr("The page stays on the screen while the device sleeps.");
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
    result.observeBattery = [](QObject *context, std::function<void()> changed) {
        batteryObservers().push_back({context, std::move(changed)});
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
    result.keyboard = [keyboard = widgets.keyboard](QWidget *, QLineEdit *editor) {
        return std::make_unique<Keyboard>(keyboard, editor);
    };
    result.presentation = [sleep = widgets.sleep](QWidget *) {
        return std::make_unique<Presentation>(sleep);
    };
    return result;
}
}
