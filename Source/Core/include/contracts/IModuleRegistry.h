#pragma once

#include "contracts/Primitives.h"
#include <string>
#include <vector>

namespace core::contracts {

// Core v2: регистрация модулей доступна только через конкретный ModuleRegistry
// (шаблонный registerModule<T>). Контракт хранит lifecycle, запросы и единый
// вход для непрямого вызова опубликованных модулями команд.
class IModuleRegistry {
public:
    virtual ~IModuleRegistry() = default;

    virtual bool initializeAll() = 0;
    virtual bool readyAll() = 0;
    virtual void shutdownAll() = 0;

    virtual std::vector<ModuleSnapshot> snapshots() const = 0;

    // Непрямой вызов опубликованной модулем команды. Реестр находит handler
    // по паре (moduleKey, commandName), освобождает внутренний lock и вызывает его
    // в контексте вызывающего (тяжёлую логику handler-а не держим под mutex).
    virtual CommandResult invokeCommand(
        const std::string& moduleKey,
        const std::string& commandName,
        const CommandArgs& args) = 0;
};

} // namespace core::contracts
