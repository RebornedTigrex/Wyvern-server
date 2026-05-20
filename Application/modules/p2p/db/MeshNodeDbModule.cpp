#include "MeshNodeDbModule.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace py = pybind11;
using namespace py::literals;

MeshNodeDbModule::MeshNodeDbModule(const core::runtime::ConfigSection& cfg)
    : BaseModule("Mesh Node DB"),
      dbPath_(cfg.value<std::string>("dbPath", "p2p_messages.db"))
{}

void MeshNodeDbModule::onInject(const std::string& depKey,
                                core::contracts::IModule* dep) {
    if (depKey == MeshCryptoModule::moduleType()) {
        crypto_ = dynamic_cast<MeshCryptoModule*>(dep);
    }
}

bool MeshNodeDbModule::onInitialize() {
    if (!Py_IsInitialized()) {
        std::cerr << "[MeshNodeDb] Python interpreter is not available\n";
        return false;
    }
    if (!crypto_) {
        std::cerr << "[MeshNodeDb] MeshCryptoModule not injected\n";
        return false;
    }

    py::gil_scoped_acquire gil;
    try {
        // Resolve db path.
        std::filesystem::path dp(dbPath_);
        if (dp.is_relative()) dp = std::filesystem::current_path() / dp;
        dbPath_ = dp.string();

        // Build the storage crypto provider:
        //   CallableStorageCryptoProvider(keystore, encrypt_storage_field, decrypt_storage_field)
        auto meshNodeDb = py::module_::import("mesh_node_db");
        auto cryptoAdapter = py::module_::import("mesh_node_db.crypto_adapter");
        auto meshCryptoStorage = py::module_::import("mesh_crypto.storage");

        auto CallableProvider = cryptoAdapter.attr("CallableStorageCryptoProvider");
        auto AdapterClass     = cryptoAdapter.attr("NodeDBCryptoAdapter");

        auto encryptFn = meshCryptoStorage.attr("encrypt_storage_field");
        auto decryptFn = meshCryptoStorage.attr("decrypt_storage_field");

        auto keystore = crypto_->keystoreObject();
        if (keystore.ptr() == nullptr) {
            std::cerr << "[MeshNodeDb] MeshCryptoModule did not initialize (keystore is null)\n";
            return false;
        }
        auto storageKeyId = crypto_->storageKeyId();

        auto provider = CallableProvider(
            "keystore"_a               = keystore,
            "encrypt_storage_field"_a  = encryptFn,
            "decrypt_storage_field"_a  = decryptFn
        );

        auto adapter = AdapterClass(
            "provider"_a              = provider,
            "active_storage_key_id"_a = storageKeyId
        );

        // Create NodeDatabase.
        auto NodeDatabase = meshNodeDb.attr("NodeDatabase");
        nodeDatabase_ = NodeDatabase(
            dbPath_,
            "crypto_adapter"_a = adapter
        );
        nodeDatabase_.attr("open")();
        nodeDatabase_.attr("initialize")();

        std::cout << "[MeshNodeDb] database ready: " << dbPath_ << "\n";
        return true;

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshNodeDb] init Python error: " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[MeshNodeDb] init error: " << e.what() << "\n";
        return false;
    }
}

void MeshNodeDbModule::onShutdown() {
    if (!Py_IsInitialized()) return;
    py::gil_scoped_acquire gil;
    try {
        // nodeDatabase_ may be a null py::object if onInitialize() failed early.
        if (nodeDatabase_.ptr() != nullptr && !nodeDatabase_.is_none()) {
            nodeDatabase_.attr("close")();
        }
    } catch (...) {}
    nodeDatabase_ = py::object();
}

bool MeshNodeDbModule::saveMessage(
    const StoredMessage& msg,
    const std::vector<std::uint8_t>& senderPublicKey)
{
    std::lock_guard<std::mutex> lock(dbMutex_);
    py::gil_scoped_acquire gil;
    try {
        // Lazy setup of chat/peer records.
        ensureChatSetup(msg.chatId,
                        crypto_->identityKeyId(),   // local peer id
                        msg.senderId,               // remote peer id (if we're receiver)
                        senderPublicKey);

        auto meshNodeDb = py::module_::import("mesh_node_db");
        auto tables     = py::module_::import("mesh_node_db.tables");
        auto MessageRecord = tables.attr("MessageRecord");

        auto rec = MessageRecord(
            "message_id"_a     = msg.messageId,
            "chat_id"_a        = msg.chatId,
            "sender_id"_a      = msg.senderId,
            "created_at"_a     = static_cast<long long>(msg.createdAt),
            "updated_at"_a     = static_cast<long long>(msg.createdAt),
            "payload"_a        = py::bytes(msg.text.c_str(), msg.text.size()),
            "attachment_hash"_a = py::none()
        );

        nodeDatabase_.attr("messages").attr("add")(rec);
        return true;

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshNodeDb] saveMessage error: " << e.what() << "\n";
        return false;
    }
}

std::vector<MeshNodeDbModule::StoredMessage>
MeshNodeDbModule::loadMessages(const std::string& chatId, int limit)
{
    std::lock_guard<std::mutex> lock(dbMutex_);
    py::gil_scoped_acquire gil;
    std::vector<StoredMessage> result;
    try {
        auto rows = nodeDatabase_.attr("messages")
            .attr("list_by_chat")(chatId, limit);

        for (const auto& row : rows) {
            StoredMessage m;
            m.messageId = row.attr("message_id").cast<std::string>();
            m.chatId    = row.attr("chat_id").cast<std::string>();
            m.senderId  = row.attr("sender_id").cast<std::string>();
            m.createdAt = row.attr("created_at").cast<long long>();

            // payload is stored as bytes (the plaintext after decryption by DB).
            auto payload = row.attr("payload").cast<py::bytes>();
            std::string_view sv(payload);
            m.text = std::string(sv.data(), sv.size());

            result.push_back(std::move(m));
        }
    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshNodeDb] loadMessages error: " << e.what() << "\n";
    }
    return result;
}

void MeshNodeDbModule::ensureChatSetup(
    const std::string& chatId,
    const std::string& localPeerId,
    const std::string& remotePeerId,
    const std::vector<std::uint8_t>& remotePubKey)
{
    // Called under dbMutex_ and GIL.
    auto tables = py::module_::import("mesh_node_db.tables");

    auto nowSec = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Ensure local peer.
    if (nodeDatabase_.attr("peers").attr("read")(localPeerId).is_none()) {
        auto localPubKey = crypto_->identityPublicKey();
        auto PeerRecord  = tables.attr("PeerRecord");
        auto rec = PeerRecord(
            "peer_id"_a      = localPeerId,
            "display_name"_a = localPeerId.substr(0, 8), // short ID as display name
            "public_key"_a   = py::bytes(
                reinterpret_cast<const char*>(localPubKey.data()),
                localPubKey.size()),
            "created_at"_a   = nowSec,
            "updated_at"_a   = nowSec,
            "is_deleted"_a   = false,
            "deleted_at"_a   = py::none()
        );
        try { nodeDatabase_.attr("peers").attr("add")(rec); } catch (...) {}
    }

    // Ensure remote peer.
    if (remotePeerId != localPeerId &&
        nodeDatabase_.attr("peers").attr("read")(remotePeerId).is_none())
    {
        auto PeerRecord = tables.attr("PeerRecord");
        auto rec = PeerRecord(
            "peer_id"_a      = remotePeerId,
            "display_name"_a = remotePeerId.substr(0, 8),
            "public_key"_a   = py::bytes(
                reinterpret_cast<const char*>(remotePubKey.data()),
                remotePubKey.size()),
            "created_at"_a   = nowSec,
            "updated_at"_a   = nowSec,
            "is_deleted"_a   = false,
            "deleted_at"_a   = py::none()
        );
        try { nodeDatabase_.attr("peers").attr("add")(rec); } catch (...) {}
    }

    // Ensure chat.
    if (nodeDatabase_.attr("chats").attr("read")(chatId).is_none()) {
        auto ChatRecord = tables.attr("ChatRecord");
        auto rec = ChatRecord(
            "chat_id"_a    = chatId,
            "chat_type"_a  = "direct",
            "chat_name"_a  = py::none(),
            "created_at"_a = nowSec,
            "updated_at"_a = nowSec
        );
        try { nodeDatabase_.attr("chats").attr("add")(rec); } catch (...) {}

        // Add participants.
        auto CPRecord = tables.attr("ChatParticipantRecord");
        try {
            nodeDatabase_.attr("chat_participants").attr("add")(
                CPRecord("chat_id"_a = chatId,
                         "peer_id"_a = localPeerId,
                         "joined_at"_a = nowSec));
        } catch (...) {}
        if (remotePeerId != localPeerId) {
            try {
                nodeDatabase_.attr("chat_participants").attr("add")(
                    CPRecord("chat_id"_a = chatId,
                             "peer_id"_a = remotePeerId,
                             "joined_at"_a = nowSec));
            } catch (...) {}
        }
    }
}
