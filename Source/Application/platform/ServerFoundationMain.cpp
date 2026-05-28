#include "Core.h"
#include "PlatformModules.h"

#include <iostream>

int main(int argc, char* argv[]) {
    auto core = Core::instance();
    if (!core->bootstrap(argc, argv)) {
        return EXIT_FAILURE;
    }

    registerWyvernPlatform(*core);

    if (!core->initializeModules()) {
        std::cerr << "[main] module initialization failed\n";
        core->shutdown();
        return EXIT_FAILURE;
    }
    core->readyModules();

    // Докатываем дефолты модулей в wyvern.config.json (если были добавлены).
    core->commitConfig();

    try {
        core->ioContext().run();
    } catch (const std::exception& e) {
        std::cerr << "[main] runtime error: " << e.what() << '\n';
        core->shutdown();
        return EXIT_FAILURE;
    }

    core->shutdown();
    return 0;
}
