#pragma once

#include "Core.h"
#include "ServerConfig.h"
#include "macros.h"
#include <memory>

namespace wyvern::platform::runtime {

struct ServerRuntimeContext {
    ServerConfig config;
    net::io_context ioContext;
    // Требует, чтобы Core::instance()->initialize() был вызван до конструирования контекста.
    std::shared_ptr<ModuleRegistry> moduleRegistry = Core::instance()->getModuleRegistry();
};

} // namespace wyvern::platform::runtime
