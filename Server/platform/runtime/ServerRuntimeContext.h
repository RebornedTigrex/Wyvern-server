#pragma once

#include "ModuleRegistry.h"
#include "RequestHandler.h"
#include "FileCache.h"
#include "DoSProtectionModule.h"
#include "ServerConfig.h"
#include "macros.h"

namespace wyvern::platform::runtime {

struct ServerRuntimeContext {
    ServerConfig config;
    net::io_context ioContext;
    ModuleRegistry moduleRegistry;
    RequestHandler* requestHandler = nullptr;
    FileCache* fileCache = nullptr;
    DoSProtectionModule* dosProtection = nullptr;
};

} // namespace wyvern::platform::runtime
