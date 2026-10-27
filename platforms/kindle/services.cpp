#include "services.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "light_device.h"
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
// Text entry waits for an on-screen keyboard; the editor only receives focus.
class Keyboard final : public KeyboardController {
public:
    explicit Keyboard(QLineEdit *editor) : editor(editor) {}
    bool show() override {
        if (!editor)
            return false;
        editor->setFocus();
        return true;
    }
    void hide() override {}
    void place() override {}

private:
    QPointer<QLineEdit> editor;
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
ReaderHostServices readerServices(std::shared_ptr<LightDevice> light) {
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
        return BatteryStatus{};
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
    result.keyboard = [](QWidget *, QLineEdit *editor) {
        return std::make_unique<Keyboard>(editor);
    };
    result.presentation = [](QWidget *) {
        return std::make_unique<Presentation>();
    };
    return result;
}
}
