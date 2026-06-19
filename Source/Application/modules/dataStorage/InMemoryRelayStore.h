#pragma once

#include "IRelayStore.h"
#include <unordered_map>
#include <deque>
#include <mutex>
#include <memory>

namespace Wyvern::DataStorage {

/// Реализация IRelayStore на базе std::unordered_map и std::deque, защищенная std::mutex.
/// Хранит всё в памяти, не персистим.
class InMemoryRelayStore : public IRelayStore {
public:
    InMemoryRelayStore();
    ~InMemoryRelayStore() override = default;

    // RegisteredPeer методы
    bool registerPeer(const RegisteredPeer& peer) override;
    bool unregisterPeer(const std::string& overlayId) override;
    std::optional<RegisteredPeer> getPeer(const std::string& overlayId) override;
    bool isPeerRegistered(const std::string& overlayId) override;
    std::vector<RegisteredPeer> getAllPeers() override;

    // RelaySession методы
    std::string createSession(
        const std::string& peerAOverlayId,
        const std::string& peerBOverlayId
    ) override;
    bool closeSession(const std::string& sessionId) override;
    std::optional<RelaySession> getSession(const std::string& sessionId) override;

    // PendingMessage методы
    std::string enqueuePendingMessage(const PendingMessage& message) override;
    std::optional<PendingMessage> dequeueNextPendingMessage() override;
    bool acknowledgeMessage(const std::string& messageId) override;
    std::vector<PendingMessage> getPendingMessagesFor(
        const std::string& targetOverlayId
    ) override;

    // Утилиты
    void clear() override;
    std::string getDebugInfo() override;

private:
    mutable std::mutex mutex_;

    // Хранилище зарегистрированных пиров: overlayId -> RegisteredPeer
    std::unordered_map<std::string, RegisteredPeer> registeredPeers_;

    // Хранилище активных сессий: sessionId -> RelaySession
    std::unordered_map<std::string, RelaySession> sessions_;

    // Очередь pending-сообщений (FIFO)
    std::deque<PendingMessage> pendingMessages_;

    // Индекс для быстрого поиска pending-сообщений по ID: messageId -> индекс в deque
    std::unordered_map<std::string, size_t> messageIdToIndex_;

    // Генератор уникальных ID для сессий и сообщений
    uint64_t nextSessionId_ = 1;
    uint64_t nextMessageId_ = 1;

    /// Вспомогательный метод для генерации уникального ID сессии
    std::string generateSessionId();

    /// Вспомогательный метод для генерации уникального ID сообщения
    std::string generateMessageId();
};

} // namespace Wyvern::DataStorage
