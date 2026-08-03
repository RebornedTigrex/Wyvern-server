#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace core::contracts {

using ModuleId = std::uint64_t;
using SubscriptionId = std::uint64_t;

enum class LifecycleState : std::uint8_t {
    Created = 0,
    Registered,
    Initializing,
    Running,
    Stopping,
    Stopped,
    Failed
};

struct OperationStatus {
    bool ok;
    std::string message;

    static OperationStatus success() {
        return { true, {} };
    }

    static OperationStatus failure(std::string error) {
        return { false, std::move(error) };
    }
};

// ---------------------------------------------------------------------------
// Module commands.
//
// Это диагностический/admin-интерфейс модуля для ручного вызова через
// IModuleRegistry::invokeCommand. Это НЕ замена IAction: Action это бизнес-вызов
// в потоке Feature/Agent, Command — это плоский callable для ручного тестирования.
using CommandArgs = std::vector<std::string>;

struct CommandResult {
    OperationStatus status;
    std::string output;

    static CommandResult success(std::string output = {}) {
        return { OperationStatus::success(), std::move(output) };
    }

    static CommandResult failure(std::string error, std::string output = {}) {
        return { OperationStatus::failure(std::move(error)), std::move(output) };
    }
};

using CommandHandler = std::function<CommandResult(const CommandArgs&)>;

// Полный дескриптор с callable, возвращается из IModule::commands() в момент
// регистрации модуля. Реестр забирает handler внутрь своего индекса.
struct CommandDescriptor {
    std::string name;
    std::string summary;
    CommandHandler handler;
};

// Read-only описание команды без callable — для ModuleSnapshot и list-представления.
struct CommandSummary {
    std::string name;
    std::string summary;
};

struct ModuleSnapshot {
    ModuleId id = 0;
    std::string name;
    std::string moduleKey;
    LifecycleState state = LifecycleState::Created;
    bool enabled = true;
    std::vector<CommandSummary> commands;
};

} // namespace core::contracts
