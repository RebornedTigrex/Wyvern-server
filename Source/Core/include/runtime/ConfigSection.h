#pragma once

#include <boost/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace core::runtime {

// Иммутабельный снимок секции конфига.
// Хранит копию boost::json::object, поэтому переживает перерасширения родительского store.
class ConfigSection {
public:
    ConfigSection() = default;
    explicit ConfigSection(boost::json::object data) : data_(std::move(data)) {}

    bool has(std::string_view key) const {
        return data_.find(key) != data_.end();
    }

    template <typename T>
    T value(std::string_view key, T defaultValue) const {
        const auto it = data_.find(key);
        if (it == data_.end()) {
            return defaultValue;
        }
        try {
            return boost::json::value_to<T>(it->value());
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "invalid type for config key '" + std::string(key) + "': " + e.what());
        }
    }

    ConfigSection subsection(std::string_view key) const {
        const auto it = data_.find(key);
        if (it == data_.end()) {
            return ConfigSection();
        }
        if (!it->value().is_object()) {
            throw std::runtime_error(
                "config key '" + std::string(key) + "' is not an object");
        }
        return ConfigSection(it->value().as_object());
    }

    const boost::json::object& raw() const { return data_; }

private:
    boost::json::object data_;
};

} // namespace core::runtime
