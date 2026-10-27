#pragma once
#include "platform/api/reader_host_services.h"
#include <memory>

namespace kobalt::kindle {
class LightDevice;
class OnScreenKeyboard;
// `light` is the one front-light device of the process; it outlives every reader window.
// `keyboard` is the window's on-screen keyboard, likewise owned by the window.
ReaderHostServices readerServices(std::shared_ptr<LightDevice> light, OnScreenKeyboard *keyboard);
}
