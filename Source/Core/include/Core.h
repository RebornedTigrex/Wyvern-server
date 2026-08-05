#pragma once

#include <filesystem>
#include <memory>

#include <boost/asio/io_context.hpp>

#include <managers/ConfigStore.h>
#include <managers/EventBus.h>
#include <managers/ModuleRegistry.h>
#include <runtime/ConfigSection.h>
#include <runtime/RuntimeServices.h>
#include <contracts/IModule.h>

/**
 * @class Core
 * @brief Ядро приложения. Владеет EventBus, ModuleRegistry, ConfigStore и RuntimeServices.
 *
 * Singleton-обёртка над базовыми компонентами. После `bootstrap()` модулям доступны
 * их конфиг-секции (`moduleConfig<T>()`) и общий `io_context` (`ioContext()`).
 */
class Core {
private:
    Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    std::shared_ptr<EventBus> eventBus;
    std::shared_ptr<ModuleRegistry> moduleRegistry;
    std::unique_ptr<core::managers::ConfigStore> configStore;
    std::unique_ptr<core::runtime::RuntimeServices> runtimeServices;
    std::filesystem::path configPath;
    bool initialized = false;
    bool bootstrapped = false;

public:
    ~Core() = default;

    /**
     * @brief Получить единственный экземпляр Core (Singleton).
     */
    static std::shared_ptr<Core> instance();

    /**
     * @brief Инициализировать ядро (EventBus + ModuleRegistry).
     */
    bool initialize();

    bool isInitialized() const;

    template<typename T, typename... Args>
    T* registerModule(Args&&... args) {
        static_assert(std::is_base_of_v<BaseModule, T>,
            "Core::registerModule<T>: T must derive from BaseModule.");
        moduleRegistry.addUnregisteredModule(std::make_unique<T>(std::forward<Args>(args)...));
    }
    /**
     * @brief Полный bootstrap: парсит CLI (--config|-c), грузит конфиг,
     *        создаёт RuntimeServices (включая io_context).
     *        При первом обращении вызывает initialize().
     */
    bool bootstrap(int argc, char** argv);

    void shutdown();

    bool initializeModules();
    bool readyModules();
    void shutdownModules();

    std::string getStatus() const;
};
