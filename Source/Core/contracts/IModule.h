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
    virtual bool ready() { return true; }
    virtual void shutdown() = 0;

    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual LifecycleState state() const = 0;
    virtual std::string moduleKey() const { return getName(); }
    virtual std::vector<std::string> dependencies() const { return {}; }

    // Вызывается registry автоматически для каждой зависимости из dependencies().
    // dep гарантированно не nullptr — registry проверил граф до вызова.
    virtual void onInject(const std::string& /*depKey*/, IModule* /*dep*/) {}

    // Декларативный перечень команд, которые модуль хочет публиковать в ModuleRegistry
    // для ручного/диагностического вызова. Симметрично dependencies(): вызывается один раз
    // при registerModule, реестр забирает handlers в свой индекс и владеет ими до разрушения
    // модуля. Default: пусто (модуль ничего не публикует).
    //
    // Не const намеренно: handler-ы обычно захватывают this и могут менять
    // внутреннее состояние модуля (например, ID подписок EventBus).
    virtual std::vector<CommandDescriptor> commands() { return {}; }
};

} // namespace core::contracts
