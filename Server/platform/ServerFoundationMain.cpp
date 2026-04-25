#include "Core.h"
#include "ServerRuntimeContext.h"
#include "HttpTransportModule.h"
#include "PlatformRoutesModule.h"
#include "FileCache.h"
#include "RequestHandler.h"
#include "DoSProtectionModule.h"

#include <iostream>

using wyvern::platform::runtime::ServerRuntimeContext;

int main(int argc, char* argv[]) {
    auto core = Core::instance();
    if (!core->initialize()) {
        std::cerr << "[main] Core initialization failed.\n";
        return EXIT_FAILURE;
    }

    ServerRuntimeContext context;
    context.config = ServerConfig::parse(argc, argv);

    auto& registry = *context.moduleRegistry;
    registry.registerModule<FileCache>(context.config.directory.c_str(), true, 100);
    registry.registerModule<RequestHandler>();
    registry.registerModule<DoSProtectionModule>();
    registry.registerModule<PlatformRoutesModule>();
    registry.registerModule<HttpTransportModule>(
        context.ioContext,
        context.config.address,
        static_cast<unsigned short>(context.config.port));

    if (!core->initializeModules()) {
        std::cerr << "[main] Module initialization failed.\n";
        core->shutdown();
        return EXIT_FAILURE;
    }

    if (!core->readyModules()) {
        std::cerr << "[main] Some modules are not ready, continuing.\n";
    }

    try {
        context.ioContext.run();
    } catch (const std::exception& e) {
        std::cerr << "[main] Runtime error: " << e.what() << '\n';
        core->shutdown();
        return EXIT_FAILURE;
    }

    core->shutdown();
    return 0;
}
