#pragma once

#include "contracts/IAgent.h"
#include "modules/BaseModule.h"
#include "runtime/ActionRouter.h"
#include "runtime/MessageDispatcher.h"

#include <string>
#include <string_view>
#include <utility>

class BaseAgent : public BaseModule, public core::contracts::IAgent {
public:
    BaseAgent(const std::string& name,
              core::contracts::ModuleId id = static_cast<core::contracts::ModuleId>(-1))
        : BaseModule(name, id) {}

    core::contracts::OperationStatus registerAction(std::string type, ActionFactory factory) override {
        return actionRouter_.registerAction(std::move(type), std::move(factory));
    }

    core::contracts::OperationStatus unregisterAction(std::string_view type) override {
        return actionRouter_.unregisterAction(type);
    }

    core::contracts::OperationStatus handleMessage(const core::contracts::IMessage& msg) override {
        const auto* envelope = dynamic_cast<const core::runtime::RoutedMessageEnvelope*>(&msg);
        if (!envelope) {
            return core::contracts::OperationStatus::failure("BaseAgent expects RoutedMessageEnvelope.");
        }

        const auto& route = envelope->route();
        if (route.action.empty()) {
            return core::contracts::OperationStatus::failure("Route action must not be empty.");
        }

        return actionRouter_.dispatch(route.action, envelope->payload());
    }

protected:
    bool onInitialize() override {
        return true;
    }

    void onShutdown() override {
    }

    core::runtime::ActionRouter& actionRouter() {
        return actionRouter_;
    }

private:
    core::runtime::ActionRouter actionRouter_;
};
