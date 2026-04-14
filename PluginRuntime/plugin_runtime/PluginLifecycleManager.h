#pragma once

#include "plugin_runtime/IPlugin.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wyvern::plugin_runtime {

enum class PluginLifecycleState {
    Discovered = 0,
    Registered,
    Initialized,
    Started,
    Stopped,
    Failed
};

struct PluginSnapshot {
    std::string pluginId;
    std::string version;
    std::string kind;
    PluginLifecycleState state = PluginLifecycleState::Discovered;
};

struct PluginLifecycleError {
    std::string pluginId;
    std::string stage;
    std::string message;
};

class PluginLifecycleManager {
public:
    bool add(std::unique_ptr<IPlugin> plugin);

    bool registerAll();
    bool initAll();
    bool startAll();
    void stopAll();

    std::optional<PluginLifecycleError> lastError() const;
    std::vector<std::string> executionOrder() const;
    std::vector<PluginSnapshot> snapshots() const;

private:
    enum class VisitState {
        NotVisited = 0,
        Visiting,
        Visited
    };

    struct PluginRecord {
        std::unique_ptr<IPlugin> plugin;
        PluginLifecycleState state = PluginLifecycleState::Discovered;
    };

    bool ensureExecutionOrder();
    bool buildExecutionOrder();
    bool visitPlugin(const std::string& pluginId, std::unordered_map<std::string, VisitState>& states);
    bool runPhaseRegister();
    bool runPhaseInit();
    bool runPhaseStart();
    void setError(const std::string& pluginId, const std::string& stage, const std::string& message);
    void clearError();

    std::vector<PluginRecord> records_;
    std::unordered_map<std::string, std::size_t> indexByPluginId_;
    std::vector<std::string> executionOrder_;
    std::optional<PluginLifecycleError> lastError_;
};

} // namespace wyvern::plugin_runtime

