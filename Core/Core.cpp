#include "Core.h"
#include <iostream>

Core::Core() 
    : eventBus(nullptr), 
      moduleRegistry(nullptr), 
      initialized(false) {
    // Ядро создано, но не инициализировано
}

std::shared_ptr<Core> Core::instance() {
    static std::shared_ptr<Core> inst(new Core);
    return inst;
}

bool Core::initialize() {
    if (initialized) {
        std::cerr << "[Core] Core is already initialized" << std::endl;
        return false;
    }

    try {
        // Получаем Singleton экземпляры компонентов
        eventBus = EventBus::instance();
        moduleRegistry = ModuleRegistry::instance();

        if (!eventBus) {
            std::cerr << "[Core] Error: failed to obtain EventBus" << std::endl;
            return false;
        }

        if (!moduleRegistry) {
            std::cerr << "[Core] Error: failed to obtain ModuleRegistry" << std::endl;
            return false;
        }

        initialized = true;
        std::cout << "[Core] Core initialized successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Exception during initialization: " << e.what() << std::endl;
        return false;
    }
}

bool Core::isInitialized() const {
    return initialized;
}

void Core::shutdown() {
    if (!initialized) {
        std::cerr << "[Core] Core is not initialized yet" << std::endl;
        return;
    }

    try {
        // Выключаем модули
        shutdownModules();
        
        // Очищаем компоненты
        eventBus = nullptr;
        moduleRegistry = nullptr;
        initialized = false;
        
        std::cout << "[Core] Core shut down successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Exception during shutdown: " << e.what() << std::endl;
    }
}

std::shared_ptr<EventBus> Core::getEventBus() const {
    if (!initialized) {
        std::cerr << "[Core] Warning: explicit EventBus request before core initialization" << std::endl;
    }
    return eventBus;
}

std::shared_ptr<ModuleRegistry> Core::getModuleRegistry() const {
    if (!initialized) {
        std::cerr << "[Core] Warning: explicit ModuleRegistry request before core initialization" << std::endl;
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
