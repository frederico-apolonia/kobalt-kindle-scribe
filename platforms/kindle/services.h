#pragma once
#include "platform/api/reader_host_services.h"
#include <memory>

namespace kobalt::kindle {
class LightDevice;
// `light` is the one front-light device of the process; it outlives every reader window.
ReaderHostServices readerServices(std::shared_ptr<LightDevice> light);
}
