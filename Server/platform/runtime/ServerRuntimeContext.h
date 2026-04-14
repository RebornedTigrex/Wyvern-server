#pragma once

#include "managers/ModuleRegistry.h"
#include "RequestHandler.h"
#include "FileCache.h"
#include "DoSProtectionModule.h"
#include "ServerConfig.h"
#include "macros.h"
#include <memory>

namespace wyvern::platform::runtime {

struct ServerRuntimeContext {
    ServerConfig config;
    net::io_context ioContext;
    std::shared_ptr<ModuleRegistry> moduleRegistry = ModuleRegistry::instance();
    RequestHandler* requestHandler = nullptr;
    FileCache* fileCache = nullptr;
    DoSProtectionModule* dosProtection = nullptr;
};

} // namespace wyvern::platform::runtime
