#pragma once
#include "platform/api/reader_host_services.h"
#include <memory>

namespace kobalt::kindle {
class LightDevice;
class OnScreenKeyboard;
class SleepScreen;
// Widgets the window owns and every reader of the process shares; they outlive each reader window.
struct HostWidgets {
    OnScreenKeyboard *keyboard = nullptr;
    SleepScreen *sleep = nullptr;
};
// `light` is the one front-light device of the process; it outlives every reader window.
ReaderHostServices readerServices(std::shared_ptr<LightDevice> light, const HostWidgets &widgets);
}
