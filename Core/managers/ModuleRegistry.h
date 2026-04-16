#pragma once

#include "contracts/IModuleRegistry.h"
#include "contracts/IModule.h"
#include "modules/BaseModule.h"
#include <algorithm>

#include <iostream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

/*
# ModuleManager
    Управляет жизненным циклом модулей, сохраняет их атрибуты и позволяет иметь модульную структуру.

    Глобальный реестр: используйте instance для получения ссылки на статический экземпляр.

*/
class ModuleRegistry : public core::contracts::IModuleRegistry {
private:
    using ModuleId = core::contracts::ModuleId;

    std::unordered_map<ModuleId, std::unique_ptr<core::contracts::IModule>> modules_;
    std::unordered_map<std::string, ModuleId> moduleIdsByKey_;
    std::unordered_map<ModuleId, std::string> moduleKeysById_;
    std::unordered_map<std::string, std::vector<std::string>> dependencyGraph_;
    ModuleId nextId_ = 1;
    mutable std::mutex mutex_;

    ModuleId generateId() {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextId_++;
    }


    std::string formatCycleChain(const std::vector<std::string>& stack, const std::string& repeated) const {
        auto repeatedIt = std::find(stack.begin(), stack.end(), repeated);
        std::ostringstream out;
        if (repeatedIt == stack.end()) {
            out << repeated;
            return out.str();
        }

        for (auto it = repeatedIt; it != stack.end(); ++it) {
            if (it != repeatedIt) {
                out << " -> ";
            }
            out << *it;
        }
        out << " -> " << repeated;
        return out.str();
    }

    void validateDependencyGraphOrThrow() const {
        std::unordered_map<std::string, int> visitState;
        std::vector<std::string> recursionStack;

        std::function<void(const std::string&)> dfs = [&](const std::string& key) {
            const int state = visitState[key];
            if (state == 2) {
                return;
            }
            if (state == 1) {
                throw std::runtime_error("Cyclic dependency detected: " + formatCycleChain(recursionStack, key));
            }

            visitState[key] = 1;
            recursionStack.push_back(key);

            const auto graphIt = dependencyGraph_.find(key);
            if (graphIt != dependencyGraph_.end()) {
                for (const auto& depKey : graphIt->second) {
                    if (moduleIdsByKey_.find(depKey) == moduleIdsByKey_.end()) {
                        throw std::runtime_error("Missing dependency '" + depKey + "' required by module '" + key + "'.");
                    }
                    dfs(depKey);
                }
            }

            recursionStack.pop_back();
            visitState[key] = 2;
        };

        for (const auto& [key, id] : moduleIdsByKey_) {
            (void)id;
            dfs(key);
        }
    }

    void registerDependencyMetadata(ModuleId id, const core::contracts::IModule& module) {
        const std::string key = module.moduleKey();
        if (key.empty()) {
            throw std::runtime_error("Module key must not be empty for module '" + module.getName() + "'.");
        }
        if (moduleIdsByKey_.find(key) != moduleIdsByKey_.end()) {
            throw std::runtime_error("Module key already registered: " + key);
        }

        const auto deps = module.dependencies();
        if (std::find(deps.begin(), deps.end(), key) != deps.end()) {
            throw std::runtime_error("Cyclic dependency detected: " + key + " -> " + key);
        }
        for (const auto& depKey : deps) {
            if (moduleIdsByKey_.find(depKey) == moduleIdsByKey_.end()) {
                throw std::runtime_error("Missing dependency '" + depKey + "' required by module '" + key + "'.");
            }
        }

        moduleIdsByKey_[key] = id;
        moduleKeysById_[id] = key;
        dependencyGraph_[key] = deps;
        validateDependencyGraphOrThrow();
    }

    std::vector<ModuleId> resolveDependencyOrderOrThrow() const {
        validateDependencyGraphOrThrow();

        std::unordered_map<std::string, int> visitState;
        std::vector<std::string> keyOrder;

        std::function<void(const std::string&)> dfs = [&](const std::string& key) {
            const int state = visitState[key];
            if (state == 2) {
                return;
            }
            if (state == 1) {
                throw std::runtime_error("Cyclic dependency detected while resolving order: " + key);
            }

            visitState[key] = 1;
            const auto graphIt = dependencyGraph_.find(key);
            if (graphIt != dependencyGraph_.end()) {
                for (const auto& depKey : graphIt->second) {
                    dfs(depKey);
                }
            }
            visitState[key] = 2;
            keyOrder.push_back(key);
        };

        for (const auto& [key, id] : moduleIdsByKey_) {
            (void)id;
            dfs(key);
        }

        std::vector<ModuleId> orderedIds;
        orderedIds.reserve(keyOrder.size());
        for (const auto& key : keyOrder) {
            orderedIds.push_back(moduleIdsByKey_.at(key));
        }
        return orderedIds;
    }

public:
    virtual ~ModuleRegistry() = default;

    static std::shared_ptr<ModuleRegistry> instance() {
        static std::shared_ptr<ModuleRegistry> inst(new ModuleRegistry);
        return inst;
    }

    template<typename T, typename... Args>
    T* registerModule(Args&&... args) {
        static_assert(std::is_base_of_v<BaseModule, T>,
            "ModuleRegistry::registerModule<T>: T must derive from BaseModule.");
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        const ModuleId id = generateId();
        module->setId(id);

        if (modules_.find(id) != modules_.end()) {
            throw std::runtime_error("Internal error: generated duplicate id " + std::to_string(id));
        }
        registerDependencyMetadata(id, *module);

        // Автопроброс зависимостей: для каждого ключа из dependencies() находим
        // уже зарегистрированный модуль и передаём указатель через onInject.
        // registerDependencyMetadata уже гарантировал, что все зависимости есть в реестре.
        for (const auto& depKey : module->dependencies()) {
            core::contracts::IModule* dep = getModuleByKey(depKey);
            module->onInject(depKey, dep);
        }

        T* ptr = module.get();
        modules_[id] = std::move(module);
        return ptr;
    }

    core::contracts::IModule* getModule(ModuleId id) {
        auto it = modules_.find(id);
        return it != modules_.end() ? it->second.get() : nullptr;
    }

    const core::contracts::IModule* getModule(ModuleId id) const {
        auto it = modules_.find(id);
        return it != modules_.end() ? it->second.get() : nullptr;
    }

    core::contracts::IModule* getModuleByKey(const std::string& moduleKey) {
        const auto keyIt = moduleIdsByKey_.find(moduleKey);
        if (keyIt == moduleIdsByKey_.end()) {
            return nullptr;
        }
        return getModule(keyIt->second);
    }

    const core::contracts::IModule* getModuleByKey(const std::string& moduleKey) const {
        const auto keyIt = moduleIdsByKey_.find(moduleKey);
        if (keyIt == moduleIdsByKey_.end()) {
            return nullptr;
        }
        return getModule(keyIt->second);
    }

    template<typename T>
    T* getModuleAs(const ModuleId& id) {
        return dynamic_cast<T*>(getModule(id));
    }

    template<typename T>
    T* getModuleAs(const std::string& moduleKey) {
        return dynamic_cast<T*>(getModuleByKey(moduleKey));
    }

    bool initializeAll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool all_ok = true;
        const auto orderedIds = resolveDependencyOrderOrThrow();
        for (const auto id : orderedIds) {
            auto it = modules_.find(id);
            if (it == modules_.end()) {
                continue;
            }
            auto& module = it->second;
            if (module->isEnabled()) {
                if (!module->initialize()) {
                    std::cerr << "Failed to initialize module: " << id << std::endl;
                    all_ok = false;
                }
            }
        }
        return all_ok;
    }

    void shutdownAll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto orderedIds = resolveDependencyOrderOrThrow();
        for (auto it = orderedIds.rbegin(); it != orderedIds.rend(); ++it) {
            auto moduleIt = modules_.find(*it);
            if (moduleIt == modules_.end()) {
                continue;
            }
            auto& module = moduleIt->second;
            if (module->isEnabled()) {
                module->shutdown();
            }
        }
    }

    std::vector<core::contracts::ModuleSnapshot> snapshots() const override {
        std::vector<core::contracts::ModuleSnapshot> result;
        result.reserve(modules_.size());
        for (const auto& [id, module] : modules_) {
            core::contracts::ModuleSnapshot snapshot;
            snapshot.id = id;
            snapshot.name = module->getName();
            snapshot.state = module->state();
            snapshot.enabled = module->isEnabled();
            result.push_back(std::move(snapshot));
        }
        return result;
    }

    std::vector<ModuleId> getModuleIds() const {
        std::vector<ModuleId> ids;
        ids.reserve(modules_.size());
        for (const auto& [id, module] : modules_) {
            ids.push_back(id);
        }
        return ids;
    }

    size_t size() const {
        return modules_.size();
    }

    bool empty() const {
        return modules_.empty();
    }
};
