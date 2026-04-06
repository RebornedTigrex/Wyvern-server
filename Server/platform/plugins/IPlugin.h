#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wyvern::platform::plugins {

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual std::string_view pluginId() const = 0;
    virtual std::string_view version() const = 0;
    virtual std::string_view kind() const = 0;
    virtual std::vector<std::string> dependencies() const = 0;

    virtual bool registerPlugin() = 0;
    virtual bool init() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
};

} // namespace wyvern::platform::plugins
