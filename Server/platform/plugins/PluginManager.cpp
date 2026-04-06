#include "PluginManager.h"

#include <algorithm>

namespace wyvern::platform::plugins {

bool PluginManager::add(std::unique_ptr<IPlugin> plugin) {
    if (!plugin) {
        return false;
    }

    const std::string id(plugin->pluginId());
    if (id.empty()) {
        return false;
    }
    if (pluginById_.find(id) != pluginById_.end()) {
        return false;
    }

    pluginById_[id] = plugin.get();
    plugins_.push_back(std::move(plugin));
    executionOrder_.clear();
    return true;
}

bool PluginManager::registerAll() {
    if (!buildExecutionOrder()) {
        return false;
    }

    for (const auto& id : executionOrder_) {
        IPlugin* plugin = pluginById_[id];
        if (!plugin->registerPlugin()) {
            return false;
        }
    }

    return true;
}

bool PluginManager::initAll() {
    for (const auto& id : executionOrder_) {
        IPlugin* plugin = pluginById_[id];
        if (!plugin->init()) {
            return false;
        }
    }

    return true;
}

bool PluginManager::startAll() {
    for (const auto& id : executionOrder_) {
        IPlugin* plugin = pluginById_[id];
        if (!plugin->start()) {
            return false;
        }
    }

    return true;
}

void PluginManager::stopAll() {
    for (auto it = executionOrder_.rbegin(); it != executionOrder_.rend(); ++it) {
        IPlugin* plugin = pluginById_[*it];
        plugin->stop();
    }
}

bool PluginManager::buildExecutionOrder() {
    executionOrder_.clear();

    std::unordered_map<std::string, VisitState> states;
    states.reserve(pluginById_.size());
    for (const auto& [id, _] : pluginById_) {
        states[id] = VisitState::NotVisited;
    }

    for (const auto& [id, _] : pluginById_) {
        if (states[id] == VisitState::NotVisited) {
            if (!visitPlugin(id, states)) {
                executionOrder_.clear();
                return false;
            }
        }
    }

    std::reverse(executionOrder_.begin(), executionOrder_.end());
    return true;
}

bool PluginManager::visitPlugin(const std::string& pluginId, std::unordered_map<std::string, VisitState>& states) {
    auto stateIt = states.find(pluginId);
    if (stateIt == states.end()) {
        return false;
    }

    if (stateIt->second == VisitState::Visited) {
        return true;
    }
    if (stateIt->second == VisitState::Visiting) {
        return false;
    }

    stateIt->second = VisitState::Visiting;

    IPlugin* plugin = pluginById_[pluginId];
    for (const auto& dep : plugin->dependencies()) {
        if (pluginById_.find(dep) == pluginById_.end()) {
            return false;
        }
        if (!visitPlugin(dep, states)) {
            return false;
        }
    }

    stateIt->second = VisitState::Visited;
    executionOrder_.push_back(pluginId);
    return true;
}

} // namespace wyvern::platform::plugins
