#pragma once
#include "contracts/IModule.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class BaseModule : public virtual core::contracts::IModule {
    enum class ModuleStatus : uint8_t { SUCCESS, DISABLED, MODULE_ERROR };

protected:
    using ModuleId      = core::contracts::ModuleId;
    using LifecycleState = core::contracts::LifecycleState;

    std::string name_;
    std::string moduleKey_;
    std::vector<std::string> dependencies_;
    std::atomic<bool> enabled_;
    std::atomic<bool> initialized_;
    std::atomic<LifecycleState> lifecycleState_;
    ModuleId id_;

public:
    BaseModule(
        const std::string& name = "Dev-Name",
        const ModuleId& id = static_cast<ModuleId>(-1),
        std::string moduleKey = {},
        std::vector<std::string> dependencies = {}
    )
        : id_(id),
        name_(name),
        moduleKey_(moduleKey.empty() ? name : std::move(moduleKey)),
        dependencies_(std::move(dependencies)),
        enabled_(true),
        initialized_(false),
        lifecycleState_(LifecycleState::Created) {
    }

    virtual ~BaseModule() = default;

    // IModule implementation
    ModuleId getId() const override { return id_; }
    std::string getName() const override { return name_; }
    std::string moduleKey() const override { return moduleKey_; }
    std::vector<std::string> dependencies() const override { return dependencies_; }
    bool isEnabled() const override { return enabled_.load(); }
    void setEnabled(bool enabled) override { enabled_.store(enabled); }
    LifecycleState state() const override { return lifecycleState_.load(); }

    bool initialize() override {
        if (!enabled_ || initialized_) return false;
        lifecycleState_.store(LifecycleState::Initializing);

        bool result = onInitialize();
        if (result) {
            initialized_.store(true);
            lifecycleState_.store(LifecycleState::Running);
        } else {
            lifecycleState_.store(LifecycleState::Failed);
        }
        return result;
    }

    bool ready() override {
        if (!enabled_ || !initialized_) return false;
        return onReady();
    }

    void shutdown() override {
        if (initialized_) {
            std::cout << "[shutdown] " << name_ << " ..." << std::endl;
            lifecycleState_.store(LifecycleState::Stopping);
            onShutdown();
            initialized_.store(false);
            lifecycleState_.store(LifecycleState::Stopped);
            std::cout << "[shutdown] " << name_ << " done" << std::endl;
        }
    }

    friend class ModuleRegistry;

protected:
    // Для реализации в наследниках
    virtual bool onInitialize() = 0;
    virtual bool onReady() { return true; }
    virtual void onShutdown() = 0;

    // Вспомогательные методы
    bool isInitialized() const { return initialized_.load(); }

    void setId(ModuleId id) { id_ = id; }
    void setModuleKey(std::string moduleKey) { moduleKey_ = std::move(moduleKey); }
    void setDependencies(std::vector<std::string> dependencies) { dependencies_ = std::move(dependencies); }
};