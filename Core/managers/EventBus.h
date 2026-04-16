#pragma once

#include "contracts/IEventBus.h"
#include <boost/signals2.hpp>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <mutex>

class EventBus : public core::contracts::IEventBus {
    EventBus() = default;

public:
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    static std::shared_ptr<EventBus> instance() {
        static std::shared_ptr<EventBus> inst(new EventBus);
        return inst;
    }

    // --- IEventBus (type-erased API) ---

    core::contracts::SubscriptionId subscribe(
        std::type_index eventType,
        RawHandler handler) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto id = m_nextId++;
        auto& rawSignal = m_rawSignals[eventType];
        m_connections[id] = rawSignal.connect(std::move(handler));
        return id;
    }

    void unsubscribe(core::contracts::SubscriptionId id) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(id);
        if (it != m_connections.end()) {
            it->second.disconnect();
            m_connections.erase(it);
        }
    }

    void publish(
        std::type_index eventType,
        RawEventPtr eventData) override
    {
        boost::signals2::signal<void(const RawEventPtr&)> signal_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_rawSignals.find(eventType);
            if (it == m_rawSignals.end()) return;
            signal_copy = it->second;
        }
        signal_copy(eventData);
    }

private:
    std::unordered_map<std::type_index,
        boost::signals2::signal<void(const RawEventPtr&)>> m_rawSignals;
    std::unordered_map<core::contracts::SubscriptionId,
        boost::signals2::connection>                        m_connections;
    core::contracts::SubscriptionId m_nextId = 1;
    std::mutex m_mutex;
};
