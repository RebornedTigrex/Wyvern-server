#pragma once

#include "managers/ModuleRegistry.h"
#include "ServerConfig.h"
#include "macros.h"
#include <memory>

namespace wyvern::platform::runtime {

struct ServerRuntimeContext {
    ServerConfig config;
    net::io_context ioContext;
    std::shared_ptr<ModuleRegistry> moduleRegistry = ModuleRegistry::instance();
};

} // namespace wyvern::platform::runtime
