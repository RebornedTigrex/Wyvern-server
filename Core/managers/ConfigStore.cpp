#include "ConfigStore.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace core::managers {

namespace {

// Рекурсивно подмешивает defaults в target.
// Возвращает true, если target был модифицирован.
bool deepMergeDefaults(boost::json::object& target, const boost::json::object& defaults) {
    bool modified = false;
    for (const auto& kv : defaults) {
        const auto it = target.find(kv.key());
        if (it == target.end()) {
            target[kv.key()] = kv.value();
            modified = true;
            continue;
        }
        if (it->value().is_object() && kv.value().is_object()) {
            if (deepMergeDefaults(it->value().as_object(), kv.value().as_object())) {
                modified = true;
            }
        }
        // Существующие примитивные значения не перезаписываются.
    }
    return modified;
}

void writeIndent(std::ostream& os, int depth) {
    for (int i = 0; i < depth; ++i) {
        os << "  ";
    }
}

void prettyPrintValue(std::ostream& os, const boost::json::value& v, int depth);

void prettyPrintObject(std::ostream& os, const boost::json::object& obj, int depth) {
    if (obj.empty()) {
        os << "{}";
        return;
    }
    os << "{\n";
    bool first = true;
    for (const auto& kv : obj) {
        if (!first) {
            os << ",\n";
        }
        first = false;
        writeIndent(os, depth + 1);
        os << boost::json::serialize(boost::json::value(kv.key())) << ": ";
        prettyPrintValue(os, kv.value(), depth + 1);
    }
    os << '\n';
    writeIndent(os, depth);
    os << '}';
}

void prettyPrintArray(std::ostream& os, const boost::json::array& arr, int depth) {
    if (arr.empty()) {
        os << "[]";
        return;
    }
    os << "[\n";
    bool first = true;
    for (const auto& value : arr) {
        if (!first) {
            os << ",\n";
        }
        first = false;
        writeIndent(os, depth + 1);
        prettyPrintValue(os, value, depth + 1);
    }
    os << '\n';
    writeIndent(os, depth);
    os << ']';
}

void prettyPrintValue(std::ostream& os, const boost::json::value& v, int depth) {
    if (v.is_object()) {
        prettyPrintObject(os, v.as_object(), depth);
    } else if (v.is_array()) {
        prettyPrintArray(os, v.as_array(), depth);
    } else {
        os << boost::json::serialize(v);
    }
}

} // namespace

void ConfigStore::load(const std::filesystem::path& path) {
    root_ = {};
    dirty_ = false;
    hasFile_ = false;

    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open config file: " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    boost::json::value parsed;
    try {
        parsed = boost::json::parse(buffer.str());
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "failed to parse config '" + path.string() + "': " + e.what());
    }

    if (!parsed.is_object()) {
        throw std::runtime_error(
            "config root must be a JSON object: " + path.string());
    }

    root_ = std::move(parsed.as_object());
    hasFile_ = true;
}

core::runtime::ConfigSection ConfigStore::moduleConfig(const std::string& moduleType,
                                                       const boost::json::object& defaults) {
    auto& modulesValue = root_["modules"];
    if (!modulesValue.is_object()) {
        modulesValue = boost::json::object{};
        dirty_ = true;
    }
    auto& modulesObj = modulesValue.as_object();

    auto& moduleValue = modulesObj[moduleType];
    if (!moduleValue.is_object()) {
        moduleValue = boost::json::object{};
        dirty_ = true;
    }
    auto& moduleObj = moduleValue.as_object();

    if (deepMergeDefaults(moduleObj, defaults)) {
        dirty_ = true;
    }

    return core::runtime::ConfigSection(moduleObj);
}

void ConfigStore::commit(const std::filesystem::path& path) {
    if (!dirty_) {
        return;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        std::cerr << "[ConfigStore] failed to open config file for writing: "
                  << path.string() << '\n';
        return;
    }

    prettyPrintObject(file, root_, 0);
    file << '\n';

    dirty_ = false;
    hasFile_ = true;
}

} // namespace core::managers
