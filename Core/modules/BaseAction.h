#pragma once
#include "modules/BaseModule.h"
#include "contracts/IAction.h"
#include "contracts/IMessage.h"

class BaseAction : public BaseModule, public core::contracts::IAction {
public:
    BaseAction(const std::string& name,
               core::contracts::ModuleId id = static_cast<core::contracts::ModuleId>(-1))
        : BaseModule(name, id) {}

    virtual ~BaseAction() = default;

    std::string_view actionKey() const override { return m_actionType; }
    virtual core::contracts::OperationStatus execute(const core::contracts::IMessage& msg) override = 0;

    void setActionType(const std::string& type) { m_actionType = type; }

protected:
    std::string m_actionType;
};

