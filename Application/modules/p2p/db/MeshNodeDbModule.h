#pragma once

#include "crypto/MeshCryptoModule.h"
#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"
#include "contracts/IModule.h"

#include <pybind11/pybind11.h>
#include <boost/json.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// MeshNodeDbModule — C++ facade over mesh_node_db.
//
// Responsibilities (only these):
//  - Open and initialise a NodeDatabase (SQLite via Python).
//  - Persist and load plain-text messages for the current direct chat.
//
// Encryption is handled by the mesh_node_db storage crypto adapter, which
// delegates to the mesh_crypto FileKeyStore owned by MeshCryptoModule.
// Depends on MeshCryptoModule (keystore must be loaded first).
class MeshNodeDbModule : public BaseModule {
public:
    static std::string moduleType() { return "p2p.mesh_node_db"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        obj["dbPath"] = "p2p_messages.db";
        return obj;
    }

    MeshNodeDbModule(const core::runtime::ConfigSection& cfg);

    std::string moduleKey() const override { return moduleType(); }

    std::vector<std::string> dependencies() const override {
        return { MeshCryptoModule::moduleType() };
    }

    void onInject(const std::string& depKey, core::contracts::IModule* dep) override;

    // Persisted message record (decrypted).
    struct StoredMessage {
        std::string messageId;
        std::string chatId;
        std::string senderId;
        std::string text;       // UTF-8 plaintext
        std::int64_t createdAt = 0; // Unix seconds
    };

    // Insert a message. Lazily creates chat/peer records on first call.
    bool saveMessage(const StoredMessage& msg,
                     const std::vector<std::uint8_t>& senderPublicKey);

    // Return up to `limit` most-recent messages for the chat, newest first.
    std::vector<StoredMessage> loadMessages(const std::string& chatId,
                                            int limit = 50);

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    // Ensure chat and both peer records exist (idempotent).
    void ensureChatSetup(const std::string& chatId,
                         const std::string& localPeerId,
                         const std::string& remotePeerId,
                         const std::vector<std::uint8_t>& remotePubKey);

    MeshCryptoModule* crypto_ = nullptr;

    std::string dbPath_;

    pybind11::object nodeDatabase_;

    mutable std::mutex dbMutex_;
};
