#pragma once

#include <filesystem>
#include <memory>

#include <boost/asio/io_context.hpp>

#include <managers/ConfigStore.h>
#include <managers/EventBus.h>
#include <managers/ModuleRegistry.h>
#include <runtime/ConfigSection.h>
#include <runtime/RuntimeServices.h>

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

    /**
     * @brief Полный bootstrap: парсит CLI (--config|-c), грузит конфиг,
     *        создаёт RuntimeServices (включая io_context).
     *        При первом обращении вызывает initialize().
     */
    bool bootstrap(int argc, char** argv);

    /**
     * @brief io_context, на котором работают все асинхронные модули.
     *        Доступен только после успешного bootstrap().
     */
    boost::asio::io_context& ioContext();

    /**
     * @brief Возвращает секцию конфига для модуля T.
     *        Использует T::moduleType() как ключ и T::defaults() как набор
     *        значений по умолчанию для deep-merge.
     *        При отсутствии полей помечает store как dirty (commitConfig запишет).
     */
    template <typename T>
    core::runtime::ConfigSection moduleConfig() {
        return configStore->moduleConfig(T::moduleType(), T::defaults());
    }

    /**
     * @brief Записывает текущее состояние ConfigStore обратно в файл,
     *        если были добавлены дефолты. No-op, если ничего не менялось.
     */
    void commitConfig();

    void shutdown();

    std::shared_ptr<EventBus> getEventBus() const;
    std::shared_ptr<ModuleRegistry> getModuleRegistry() const;

    bool initializeModules();
    bool readyModules();
    void shutdownModules();

    std::string getStatus() const;
};
