#pragma once

#include "contracts/Primitives.h"
#include <vector>

namespace core::contracts {

// Core v2: регистрация модулей доступна только через конкретный ModuleRegistry
// (шаблонный registerModule<T>). Контракт хранит только lifecycle и запросы.
class IModuleRegistry {
public:
    virtual ~IModuleRegistry() = default;

    virtual bool initializeAll() = 0;
    virtual bool readyAll() = 0;
    virtual void shutdownAll() = 0;

    virtual std::vector<ModuleSnapshot> snapshots() const = 0;
};

} // namespace core::contracts
