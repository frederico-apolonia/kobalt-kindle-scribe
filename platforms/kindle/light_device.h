// Front light and warmth through powerd's LIPC properties (logical range 0-24 on the Scribe, measured:
// flMaxIntensity 24, currentAmberLevel 0-24). The values found at startup are put back on destruction, and only
// for the properties this object actually changed, so a reader that never touches the light leaves it alone.
#pragma once

#include <QtCore/QString>

namespace kobalt::kindle {
class LightDevice final {
public:
    LightDevice();
    ~LightDevice();
    LightDevice(const LightDevice &) = delete;
    LightDevice &operator=(const LightDevice &) = delete;

    bool available() const { return brightness_.initial >= 0; }
    int brightness() const { return brightness_.current; }
    int maxBrightness() const { return maxBrightness_; }
    bool setBrightness(int level);
    bool hasWarmth() const { return warmth_.initial >= 0; }
    int warmth() const { return warmth_.current; }
    int maxWarmth() const { return 24; }
    bool setWarmth(int level);
    // Keep the levels now set: the destructor restores nothing.
    void commit();
    // Put back the levels found at startup.
    void revert();

private:
    struct Property {
        const char *name;
        int initial = -1;
        int current = -1;
        bool changed = false;
    };
    static bool get(const char *name, int *value);
    static bool set(const char *name, int value);
    bool write(Property &property, int level, int maximum);

    Property brightness_{"flIntensity"};
    Property warmth_{"currentAmberLevel"};
    int maxBrightness_ = 24;
};
}
