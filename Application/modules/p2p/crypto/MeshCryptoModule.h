#pragma once

#include "PythonRuntimeModule.h"
#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"
#include "contracts/IModule.h"

#include <pybind11/pybind11.h>
#include <boost/json.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

// MeshCryptoModule — C++ facade over mesh_crypto.
//
// Responsibilities (only these):
//  - Create or load a mesh_crypto FileKeyStore.
//  - Generate / reload an Ed25519 identity key.
//  - Provide the local Ed25519 public key for exchange with the peer.
//  - Drive the direct handshake (create init, accept init, complete).
//  - Encrypt / decrypt direct messages.
//
// Does NOT manage the database, the UI, or the network transport.
// Depends on PythonRuntimeModule (interpreter must be alive).
class MeshCryptoModule : public BaseModule {
public:
    static std::string moduleType() { return "p2p.mesh_crypto"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        obj["keystorePath"]     = "p2p_keystore";
        obj["keystorePassword"] = "wyvern_p2p_dev";
        return obj;
    }

    MeshCryptoModule(const core::runtime::ConfigSection& cfg);

    std::string moduleKey() const override { return moduleType(); }

    std::vector<std::string> dependencies() const override {
        return { PythonRuntimeModule::moduleType() };
    }

    void onInject(const std::string& depKey, core::contracts::IModule* dep) override;

    // --- Identity ---

    // 32-byte raw Ed25519 public key.
    std::vector<std::uint8_t> identityPublicKey() const;

    // String UUID of the identity key in the keystore.
    std::string identityKeyId() const;

    // --- Handshake API ---

    // Initiator: build outbound DirectHandshakeInit bytes.
    // Stores PendingDirectHandshake internally (RAM only).
    // peerPublicKey: remote 32-byte Ed25519 public key.
    std::optional<std::vector<std::uint8_t>>
        createHandshakeInit(const std::vector<std::uint8_t>& peerPublicKey);

    // Responder: accept an incoming DirectHandshakeInit (JSON bytes),
    // return DirectHandshakeResponse bytes.
    // expectedPeerKey: remote 32-byte Ed25519 public key we already know.
    std::optional<std::vector<std::uint8_t>>
        acceptHandshakeInit(const std::vector<std::uint8_t>& initBytes,
                            const std::vector<std::uint8_t>& expectedPeerKey);

    // Initiator: finalize by processing the DirectHandshakeResponse bytes.
    // Returns true on success; session becomes active.
    bool completeHandshake(const std::vector<std::uint8_t>& responseBytes,
                           const std::vector<std::uint8_t>& expectedPeerKey);

    // --- Messaging ---

    std::optional<std::vector<std::uint8_t>>
        encryptMessage(const std::vector<std::uint8_t>& plaintext);

    std::optional<std::vector<std::uint8_t>>
        decryptMessage(const std::vector<std::uint8_t>& envelopeBytes);

    // --- State queries ---
    bool hasActiveSession() const;
    void clearSession();

    // Expose keystore object for use by MeshNodeDbModule.
    // Callers MUST hold the GIL before calling this.
    pybind11::object keystoreObject() const;

    // Expose active storage key ID (UUID object) for use by MeshNodeDbModule.
    // Callers MUST hold the GIL before calling this.
    pybind11::object storageKeyId() const;

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    // Load or create identity key; persist key_id to disk.
    bool loadOrCreateIdentityKey();
    // Load or create symmetric storage key; persist key_id to disk.
    bool loadOrCreateStorageKey();

    std::string keystorePath_;
    std::string keystorePassword_;

    // Python objects (valid only after onInitialize).
    pybind11::object keystore_;
    pybind11::object identityKeyId_;   // uuid.UUID
    pybind11::object storageKeyId_;    // uuid.UUID
    std::vector<std::uint8_t> identityPublicKeyBytes_;

    // Session state (RAM-only per spec).
    pybind11::object sessionState_;     // py::none() when no session
    pybind11::object pendingHandshake_; // py::none() when not initiating

    // Cached serialised init bytes needed by completeHandshake().
    std::vector<std::uint8_t> cachedInitBytes_;

    mutable std::mutex sessionMutex_;
};
