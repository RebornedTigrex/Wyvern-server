#include "InMemoryRelayStore.h"
#include <sstream>
#include <chrono>
#include <algorithm>

namespace Wyvern::DataStorage {

InMemoryRelayStore::InMemoryRelayStore()
    : nextSessionId_(1), nextMessageId_(1) {
}

std::string InMemoryRelayStore::generateSessionId() {
    std::ostringstream oss;
    oss << "sess_" << nextSessionId_++;
    return oss.str();
}

std::string InMemoryRelayStore::generateMessageId() {
    std::ostringstream oss;
    oss << "msg_" << nextMessageId_++;
    return oss.str();
}

bool InMemoryRelayStore::registerPeer(const RegisteredPeer& peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    registeredPeers_[peer.overlayId] = peer;
    return true;
}

bool InMemoryRelayStore::unregisterPeer(const std::string& overlayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return registeredPeers_.erase(overlayId) > 0;
}

std::optional<RegisteredPeer> InMemoryRelayStore::getPeer(const std::string& overlayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registeredPeers_.find(overlayId);
    if (it != registeredPeers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool InMemoryRelayStore::isPeerRegistered(const std::string& overlayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return registeredPeers_.count(overlayId) > 0;
}

std::vector<RegisteredPeer> InMemoryRelayStore::getAllPeers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RegisteredPeer> result;
    result.reserve(registeredPeers_.size());
    for (const auto& pair : registeredPeers_) {
        result.push_back(pair.second);
    }
    return result;
}

std::string InMemoryRelayStore::createSession(
    const std::string& peerAOverlayId,
    const std::string& peerBOverlayId) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string sessionId = generateSessionId();
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    RelaySession session;
    session.sessionId = sessionId;
    session.peerAOverlayId = peerAOverlayId;
    session.peerBOverlayId = peerBOverlayId;
    session.startTime = now;
    session.isActive = true;

    sessions_[sessionId] = session;
    return sessionId;
}

bool InMemoryRelayStore::closeSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second.isActive = false;
        return true;
    }
    return false;
}

std::optional<RelaySession> InMemoryRelayStore::getSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string InMemoryRelayStore::enqueuePendingMessage(const PendingMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string messageId = generateMessageId();
    PendingMessage msg = message;
    msg.messageId = messageId;

    size_t index = pendingMessages_.size();
    pendingMessages_.push_back(msg);
    messageIdToIndex_[messageId] = index;

    return messageId;
}

std::optional<PendingMessage> InMemoryRelayStore::dequeueNextPendingMessage() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (pendingMessages_.empty()) {
        return std::nullopt;
    }

    PendingMessage msg = pendingMessages_.front();
    pendingMessages_.pop_front();

    // Обновляем индекс для оставшихся сообщений
    messageIdToIndex_.erase(msg.messageId);
    for (size_t i = 0; i < pendingMessages_.size(); ++i) {
        messageIdToIndex_[pendingMessages_[i].messageId] = i;
    }

    return msg;
}

bool InMemoryRelayStore::acknowledgeMessage(const std::string& messageId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = messageIdToIndex_.find(messageId);
    if (it == messageIdToIndex_.end()) {
        return false;
    }

    size_t index = it->second;
    if (index >= pendingMessages_.size()) {
        return false;
    }

    // Удаляем сообщение из очереди
    pendingMessages_.erase(pendingMessages_.begin() + index);
    messageIdToIndex_.erase(messageId);

    // Пересчитываем индексы для сообщений после удаленного
    for (size_t i = index; i < pendingMessages_.size(); ++i) {
        messageIdToIndex_[pendingMessages_[i].messageId] = i;
    }

    return true;
}

std::vector<PendingMessage> InMemoryRelayStore::getPendingMessagesFor(
    const std::string& targetOverlayId) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<PendingMessage> result;
    for (const auto& msg : pendingMessages_) {
        if (msg.targetOverlayId == targetOverlayId) {
            result.push_back(msg);
        }
    }
    return result;
}

void InMemoryRelayStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    registeredPeers_.clear();
    sessions_.clear();
    pendingMessages_.clear();
    messageIdToIndex_.clear();
    nextSessionId_ = 1;
    nextMessageId_ = 1;
}

std::string InMemoryRelayStore::getDebugInfo() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << "InMemoryRelayStore stats: "
        << "peers=" << registeredPeers_.size()
        << ", sessions=" << sessions_.size()
        << ", pending_messages=" << pendingMessages_.size();

    return oss.str();
}

} // namespace Wyvern::DataStorage
