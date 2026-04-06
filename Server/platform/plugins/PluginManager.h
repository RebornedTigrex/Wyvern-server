#pragma once

#include "IPlugin.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wyvern::platform::plugins {

class PluginManager {
public:
    bool add(std::unique_ptr<IPlugin> plugin);
    bool registerAll();
    bool initAll();
    bool startAll();
    void stopAll();

private:
    enum class VisitState {
        NotVisited,
        Visiting,
        Visited
    };

    bool buildExecutionOrder();
    bool visitPlugin(const std::string& pluginId, std::unordered_map<std::string, VisitState>& states);

    std::vector<std::unique_ptr<IPlugin>> plugins_;
    std::unordered_map<std::string, IPlugin*> pluginById_;
    std::vector<std::string> executionOrder_;
};

} // namespace wyvern::platform::plugins
