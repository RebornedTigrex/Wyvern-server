#include "Core.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kUsage =
    "usage: [--config|-c <path>]\n";

// Прстейший разбор `--config <path>`, `--config=path`, `-c <path>`.
// При неизвестном флаге печатает usage в stderr и возвращает false.
bool parseCli(int argc, char** argv, std::filesystem::path& outConfig) {
    static const std::filesystem::path defaultPath = "wyvern.config.json";
    outConfig = defaultPath;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << arg << " expects a path\n" << kUsage;
                return false;
            }
            outConfig = argv[++i];
            continue;
        }

        constexpr std::string_view kConfigEq = "--config=";
        if (arg.substr(0, kConfigEq.size()) == kConfigEq) {
            outConfig = std::string(arg.substr(kConfigEq.size()));
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return false;
        }

        std::cerr << "error: unknown argument '" << arg << "'\n" << kUsage;
        return false;
    }
    return true;
}

} // namespace

Core::Core()
    : eventBus(nullptr),
      moduleRegistry(nullptr),
      configStore(nullptr),
      runtimeServices(nullptr),
      initialized(false),
      bootstrapped(false) {
}

std::shared_ptr<Core> Core::instance() {
    static std::shared_ptr<Core> inst(new Core);
    return inst;
}

bool Core::initialize() {
    if (initialized) {
        std::cerr << "[Core] already initialized\n";
        return false;
    }

    try {
        eventBus = EventBus::instance();
        moduleRegistry = ModuleRegistry::instance();

        if (!eventBus) {
            std::cerr << "[Core] failed to obtain EventBus\n";
            return false;
        }

        if (!moduleRegistry) {
            std::cerr << "[Core] failed to obtain ModuleRegistry\n";
            return false;
        }

        initialized = true;
        std::cout << "[Core] initialized\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Core] initialize exception: " << e.what() << '\n';
        return false;
    }
}

bool Core::bootstrap(int argc, char** argv) {
    if (bootstrapped) {
        std::cerr << "[Core] already bootstrapped\n";
        return false;
    }
    if (!initialized && !initialize()) {
        return false;
    }

    if (!parseCli(argc, argv, configPath)) {
        return false;
    }

    configStore = std::make_unique<core::managers::ConfigStore>();
    try {
        configStore->load(configPath);
    } catch (const std::exception& e) {
        std::cerr << "[Core] config load failed: " << e.what() << '\n';
        return false;
    }

    runtimeServices = std::make_unique<core::runtime::RuntimeServices>();
    bootstrapped = true;

    std::cout << "[Core] bootstrap complete (config: " << configPath.string()
              << (configStore->hasFile() ? " [loaded]" : " [will be created]")
              << ")\n";
    return true;
}

boost::asio::io_context& Core::ioContext() {
    if (!runtimeServices) {
        throw std::runtime_error("Core::ioContext() called before bootstrap()");
    }
    return runtimeServices->ioContext;
}

void Core::commitConfig() {
    if (!configStore || configPath.empty()) {
        return;
    }
    try {
        configStore->commit(configPath);
    } catch (const std::exception& e) {
        std::cerr << "[Core] commitConfig failed: " << e.what() << '\n';
    }
}

bool Core::isInitialized() const {
    return initialized;
}

void Core::shutdown() {
    if (!initialized) {
        std::cerr << "[Core] not initialized\n";
        return;
    }

    try {
        shutdownModules();
        runtimeServices.reset();
        configStore.reset();
        configPath.clear();
        eventBus = nullptr;
        moduleRegistry = nullptr;
        initialized = false;
        bootstrapped = false;

        std::cout << "[Core] shut down\n";
    } catch (const std::exception& e) {
        std::cerr << "[Core] shutdown exception: " << e.what() << '\n';
    }
}

std::shared_ptr<EventBus> Core::getEventBus() const {
    if (!initialized) {
        std::cerr << "[Core] Warning: EventBus requested before initialize()\n";
    }
    return eventBus;
}

std::shared_ptr<ModuleRegistry> Core::getModuleRegistry() const {
    if (!initialized) {
        std::cerr << "[Core] Warning: ModuleRegistry requested before initialize()\n";
    }
    return moduleRegistry;
}

bool Core::initializeModules() {
    if (!initialized) {
        std::cerr << "[Core] Error: core is not initialized, cannot initialize modules" << std::endl;
        return false;
    }

    if (!moduleRegistry) {
        std::cerr << "[Core] Error: ModuleRegistry is unavailable" << std::endl;
        return false;
    }

    try {
        std::cout << "[Core] Initializing modules..." << std::endl;
        bool result = moduleRegistry->initializeAll();
        if (result) {
            std::cout << "[Core] All modules initialized successfully" << std::endl;
        } else {
            std::cerr << "[Core] Some modules failed to initialize" << std::endl;
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Exception during module initialization: " << e.what() << std::endl;
        return false;
    }
}

bool Core::readyModules() {
    if (!initialized) {
        std::cerr << "[Core] Error: core is not initialized, cannot check module readiness" << std::endl;
        return false;
    }

    if (!moduleRegistry) {
        std::cerr << "[Core] Error: ModuleRegistry is unavailable" << std::endl;
        return false;
    }

    try {
        std::cout << "[Core] Checking module readiness..." << std::endl;
        bool result = moduleRegistry->readyAll();
        if (result) {
            std::cout << "[Core] All modules are ready" << std::endl;
        } else {
            std::cerr << "[Core] Some modules are not ready" << std::endl;
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Exception during module readiness check: " << e.what() << std::endl;
        return false;
    }
}

void Core::shutdownModules() {
    if (!initialized || !moduleRegistry) {
        return;
    }

    try {
        std::cout << "[Core] Shutting down modules..." << std::endl;
        moduleRegistry->shutdownAll();
        std::cout << "[Core] All modules shut down" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Exception during module shutdown: " << e.what() << std::endl;
    }
}

std::string Core::getStatus() const {
    std::string status;
    
    status += "=== Core Status ===\n";
    status += "Initialized: " + std::string(initialized ? "yes" : "no") + "\n";
    
    if (initialized && moduleRegistry) {
        status += "Registered modules: " + std::to_string(moduleRegistry->size()) + "\n";
    }
    
    status += "EventBus: " + std::string(eventBus ? "available" : "unavailable") + "\n";
    status += "ModuleRegistry: " + std::string(moduleRegistry ? "available" : "unavailable") + "\n";
    
    return status;
}
