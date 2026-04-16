#pragma once

#include "contracts/Primitives.h"
#include <string>
#include <vector>

namespace core::contracts {

class IModule {
public:
    virtual ~IModule() = default;

    virtual ModuleId getId() const = 0;
    virtual std::string getName() const = 0;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual LifecycleState state() const = 0;
    virtual std::string moduleKey() const { return getName(); }
    virtual std::vector<std::string> dependencies() const { return {}; }

    // Вызывается registry автоматически для каждой зависимости из dependencies().
    // dep гарантированно не nullptr — registry проверил граф до вызова.
    virtual void onInject(const std::string& /*depKey*/, IModule* /*dep*/) {}
};

} // namespace core::contracts
