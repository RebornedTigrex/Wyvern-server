#include "plugin_runtime/PluginLifecycleManager.h"

#include <algorithm>
#include <utility>

namespace wyvern::plugin_runtime {

bool PluginLifecycleManager::add(std::unique_ptr<IPlugin> plugin) {
    if (!plugin) {
        setError({}, "add", "Plugin pointer is null.");
        return false;
    }

    const std::string id(plugin->pluginId());
    if (id.empty()) {
        setError({}, "add", "Plugin id must not be empty.");
        return false;
    }
    if (indexByPluginId_.find(id) != indexByPluginId_.end()) {
        setError(id, "add", "Plugin id already registered.");
        return false;
    }

    PluginRecord record;
    record.plugin = std::move(plugin);

    const std::size_t index = records_.size();
    records_.push_back(std::move(record));
    indexByPluginId_[id] = index;
    executionOrder_.clear();
    clearError();
    return true;
}

bool PluginLifecycleManager::registerAll() {
    if (!ensureExecutionOrder()) {
        return false;
    }
    return runPhaseRegister();
}

bool PluginLifecycleManager::initAll() {
    if (!ensureExecutionOrder()) {
        return false;
    }
    if (!runPhaseRegister()) {
        return false;
    }
    return runPhaseInit();
}

bool PluginLifecycleManager::startAll() {
    if (!ensureExecutionOrder()) {
        return false;
    }
    if (!runPhaseRegister()) {
        return false;
    }
    if (!runPhaseInit()) {
        stopAll();
        return false;
    }
    if (!runPhaseStart()) {
        stopAll();
        return false;
    }

    clearError();
    return true;
}

void PluginLifecycleManager::stopAll() {
    if (!ensureExecutionOrder()) {
        return;
    }

    for (auto it = executionOrder_.rbegin(); it != executionOrder_.rend(); ++it) {
        const auto indexIt = indexByPluginId_.find(*it);
        if (indexIt == indexByPluginId_.end()) {
            continue;
        }

        auto& record = records_[indexIt->second];
        if (record.state == PluginLifecycleState::Started ||
            record.state == PluginLifecycleState::Initialized ||
            record.state == PluginLifecycleState::Registered ||
            record.state == PluginLifecycleState::Failed) {
            record.plugin->stop();
            record.state = PluginLifecycleState::Stopped;
        }
    }
}

std::optional<PluginLifecycleError> PluginLifecycleManager::lastError() const {
    return lastError_;
}

std::vector<std::string> PluginLifecycleManager::executionOrder() const {
    return executionOrder_;
}

std::vector<PluginSnapshot> PluginLifecycleManager::snapshots() const {
    std::vector<PluginSnapshot> result;
    result.reserve(records_.size());

    for (const auto& record : records_) {
        PluginSnapshot snapshot;
        snapshot.pluginId = std::string(record.plugin->pluginId());
        snapshot.version = std::string(record.plugin->version());
        snapshot.kind = std::string(record.plugin->kind());
        snapshot.state = record.state;
        result.push_back(std::move(snapshot));
    }

    return result;
}

bool PluginLifecycleManager::ensureExecutionOrder() {
    if (!executionOrder_.empty()) {
        return true;
    }
    return buildExecutionOrder();
}

bool PluginLifecycleManager::buildExecutionOrder() {
    executionOrder_.clear();

    std::unordered_map<std::string, VisitState> states;
    states.reserve(indexByPluginId_.size());
    for (const auto& [id, _] : indexByPluginId_) {
        states[id] = VisitState::NotVisited;
    }

    for (const auto& [id, _] : indexByPluginId_) {
        if (states[id] == VisitState::NotVisited) {
            if (!visitPlugin(id, states)) {
                executionOrder_.clear();
                return false;
            }
        }
    }

    clearError();
    return true;
}

bool PluginLifecycleManager::visitPlugin(const std::string& pluginId, std::unordered_map<std::string, VisitState>& states) {
    auto stateIt = states.find(pluginId);
    if (stateIt == states.end()) {
        setError(pluginId, "validate", "Plugin is missing in internal state map.");
        return false;
    }

    if (stateIt->second == VisitState::Visited) {
        return true;
    }
    if (stateIt->second == VisitState::Visiting) {
        setError(pluginId, "validate", "Cyclic dependency detected.");
        return false;
    }

    stateIt->second = VisitState::Visiting;

    const auto indexIt = indexByPluginId_.find(pluginId);
    if (indexIt == indexByPluginId_.end()) {
        setError(pluginId, "validate", "Plugin index lookup failed.");
        return false;
    }

    const auto& record = records_[indexIt->second];
    for (const auto& dependencyId : record.plugin->dependencies()) {
        const auto depIt = indexByPluginId_.find(dependencyId);
        if (depIt == indexByPluginId_.end()) {
            setError(pluginId, "validate", "Required dependency is missing: " + dependencyId);
            return false;
        }
        if (!visitPlugin(dependencyId, states)) {
            return false;
        }
    }

    stateIt->second = VisitState::Visited;
    executionOrder_.push_back(pluginId);
    return true;
}

bool PluginLifecycleManager::runPhaseRegister() {
    for (const auto& id : executionOrder_) {
        const auto index = indexByPluginId_.at(id);
        auto& record = records_[index];

        if (record.state == PluginLifecycleState::Registered ||
            record.state == PluginLifecycleState::Initialized ||
            record.state == PluginLifecycleState::Started) {
            continue;
        }

        if (!record.plugin->registerPlugin()) {
            record.state = PluginLifecycleState::Failed;
            setError(id, "register", "registerPlugin returned false.");
            return false;
        }

        record.state = PluginLifecycleState::Registered;
    }

    return true;
}

bool PluginLifecycleManager::runPhaseInit() {
    for (const auto& id : executionOrder_) {
        const auto index = indexByPluginId_.at(id);
        auto& record = records_[index];

        if (record.state == PluginLifecycleState::Initialized ||
            record.state == PluginLifecycleState::Started) {
            continue;
        }
        if (record.state != PluginLifecycleState::Registered) {
            setError(id, "init", "Plugin is not in registered state.");
            return false;
        }

        if (!record.plugin->init()) {
            record.state = PluginLifecycleState::Failed;
            setError(id, "init", "init returned false.");
            return false;
        }

        record.state = PluginLifecycleState::Initialized;
    }

    return true;
}

bool PluginLifecycleManager::runPhaseStart() {
    for (const auto& id : executionOrder_) {
        const auto index = indexByPluginId_.at(id);
        auto& record = records_[index];

        if (record.state == PluginLifecycleState::Started) {
            continue;
        }
        if (record.state != PluginLifecycleState::Initialized) {
            setError(id, "start", "Plugin is not in initialized state.");
            return false;
        }

        if (!record.plugin->start()) {
            record.state = PluginLifecycleState::Failed;
            setError(id, "start", "start returned false.");
            return false;
        }

        record.state = PluginLifecycleState::Started;
    }

    return true;
}

void PluginLifecycleManager::setError(const std::string& pluginId, const std::string& stage, const std::string& message) {
    lastError_ = PluginLifecycleError{ pluginId, stage, message };
}

void PluginLifecycleManager::clearError() {
    lastError_.reset();
}

} // namespace wyvern::plugin_runtime

