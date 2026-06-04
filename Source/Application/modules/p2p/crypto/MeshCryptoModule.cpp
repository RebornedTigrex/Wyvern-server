#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "MeshCryptoModule.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace py = pybind11;
using namespace py::literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Read a single UUID string from a text file. Returns empty string on failure.
std::string readUuidFromFile(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string line;
    std::getline(f, line);
    return line;
}

// Write a UUID string to a text file.
void writeUuidToFile(const std::filesystem::path& path, const std::string& uuid) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) throw std::runtime_error("cannot write " + path.string());
    f << uuid;
}

// Convert py::bytes to std::vector<uint8_t>.
std::vector<std::uint8_t> pyBytesToVec(const py::bytes& b) {
    std::string_view sv(b);
    return { reinterpret_cast<const std::uint8_t*>(sv.data()),
             reinterpret_cast<const std::uint8_t*>(sv.data()) + sv.size() };
}

// Convert std::vector<uint8_t> to py::bytes.
py::bytes vecToPyBytes(const std::vector<std::uint8_t>& v) {
    return py::bytes(reinterpret_cast<const char*>(v.data()), v.size());
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MeshCryptoModule::MeshCryptoModule(const core::runtime::ConfigSection& cfg)
    : BaseModule("Mesh Crypto"),
      keystorePath_(cfg.value<std::string>("keystorePath", "p2p_keystore")),
      keystorePassword_(cfg.value<std::string>("keystorePassword", "wyvern_p2p_dev"))
{}

// ---------------------------------------------------------------------------
// Dependency injection
// ---------------------------------------------------------------------------

void MeshCryptoModule::onInject(const std::string& depKey,
                                core::contracts::IModule* /*dep*/) {
    // No pointer needed; we only verify the dependency exists.
    if (depKey != PythonRuntimeModule::moduleType()) {
        std::cerr << "[MeshCrypto] unexpected dependency: " << depKey << "\n";
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool MeshCryptoModule::onInitialize() {
    if (!Py_IsInitialized()) {
        std::cerr << "[MeshCrypto] Python interpreter is not available\n";
        return false;
    }
    py::gil_scoped_acquire gil;
    try {
        // Resolve keystore path (absolute if relative).
        std::filesystem::path ksPath(keystorePath_);
        if (ksPath.is_relative()) {
            ksPath = std::filesystem::current_path() / ksPath;
        }
        std::filesystem::create_directories(ksPath);
        keystorePath_ = ksPath.string();

        auto meshCrypto     = py::module_::import("mesh_crypto");
        auto FileKeyStore   = meshCrypto.attr("FileKeyStore");
        auto PasswordProtector = meshCrypto.attr("PasswordProtector");

        // PasswordProtector expects a Python str, not bytes.
        auto protector = PasswordProtector(
            "password"_a = py::str(keystorePassword_));

        keystore_ = FileKeyStore(keystorePath_, protector);

        if (keystore_.attr("exists")().cast<bool>()) {
            keystore_.attr("load")();
        } else {
            keystore_.attr("create_new")();
        }

        if (!loadOrCreateIdentityKey()) return false;
        if (!loadOrCreateStorageKey()) return false;

        // Initialise session holders to None.
        sessionState_     = py::none();
        pendingHandshake_ = py::none();

        std::cout << "[MeshCrypto] initialized, identity key: "
                  << identityKeyId() << "\n";
        return true;
    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshCrypto] init Python error: " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[MeshCrypto] init error: " << e.what() << "\n";
        return false;
    }
}

bool MeshCryptoModule::loadOrCreateIdentityKey() {
    // Persisted identity key ID lives in <keystorePath>/identity_key_id.txt.
    const auto idFile =
        std::filesystem::path(keystorePath_) / "identity_key_id.txt";

    auto meshCrypto = py::module_::import("mesh_crypto");
    auto KeyKind    = meshCrypto.attr("KeyKind");

    identityKeyId_ = py::none(); // ensure valid py::object before is_none() check

    const std::string existing = readUuidFromFile(idFile);
    if (!existing.empty()) {
        try {
            auto uuid_mod  = py::module_::import("uuid");
            identityKeyId_ = uuid_mod.attr("UUID")(existing);
        } catch (...) {
            identityKeyId_ = py::none();
        }
    }

    if (identityKeyId_.is_none()) {
        identityKeyId_ = keystore_.attr("generate_key")(KeyKind.attr("ED25519"));
    }

    // Persist if new.
    const std::string uuidStr =
        py::str(identityKeyId_).cast<std::string>();
    if (!std::filesystem::exists(idFile) || uuidStr != existing) {
        writeUuidToFile(idFile, uuidStr);
    }

    // Read back the public key from keystore metadata.
    // get_key() returns a tuple (raw_key_bytes, meta_dict).
    py::object keyRecord = keystore_.attr("get_key")(identityKeyId_);
    py::dict   metaDict  = keyRecord.attr("__getitem__")(py::int_(1)).cast<py::dict>();

    auto pubKeyB64 = metaDict["public_key"].cast<std::string>();

    // Decode base64 using Python.
    auto base64  = py::module_::import("base64");
    auto decoded = base64.attr("b64decode")(pubKeyB64);
    identityPublicKeyBytes_ = pyBytesToVec(decoded.cast<py::bytes>());

    if (identityPublicKeyBytes_.size() != 32) {
        std::cerr << "[MeshCrypto] invalid identity public key length\n";
        return false;
    }
    return true;
}

bool MeshCryptoModule::loadOrCreateStorageKey() {
    const auto skFile =
        std::filesystem::path(keystorePath_) / "storage_key_id.txt";

    auto meshCrypto = py::module_::import("mesh_crypto");
    auto KeyKind    = meshCrypto.attr("KeyKind");

    storageKeyId_ = py::none(); // ensure valid py::object before is_none() check

    const std::string existing = readUuidFromFile(skFile);
    if (!existing.empty()) {
        try {
            auto uuid_mod  = py::module_::import("uuid");
            storageKeyId_  = uuid_mod.attr("UUID")(existing);
        } catch (...) {}
    }

    if (storageKeyId_.is_none()) {
        storageKeyId_ = keystore_.attr("generate_key")(KeyKind.attr("SYMMETRIC"));
        keystore_.attr("set_active_key")(storageKeyId_);
    }

    const std::string uuidStr =
        py::str(storageKeyId_).cast<std::string>();
    if (!std::filesystem::exists(skFile) || uuidStr != existing) {
        writeUuidToFile(skFile, uuidStr);
    }
    return true;
}

void MeshCryptoModule::onShutdown() {
    // Guard: if the interpreter was never started (or failed to start),
    // Python objects were never initialised — nothing to release.
    if (!Py_IsInitialized()) {
        identityPublicKeyBytes_.clear();
        return;
    }
    py::gil_scoped_acquire gil;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        sessionState_     = py::object();
        pendingHandshake_ = py::object();
        cachedInitBytes_.clear();
    }
    keystore_      = py::object();
    identityKeyId_ = py::object();
    storageKeyId_  = py::object();
    identityPublicKeyBytes_.clear();
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> MeshCryptoModule::identityPublicKey() const {
    return identityPublicKeyBytes_;
}

std::string MeshCryptoModule::identityKeyId() const {
    py::gil_scoped_acquire gil;
    if (identityKeyId_.is_none()) return {};
    return py::str(identityKeyId_).cast<std::string>();
}

// ---------------------------------------------------------------------------
// Keystore accessors for MeshNodeDbModule
// ---------------------------------------------------------------------------

py::object MeshCryptoModule::keystoreObject() const {
    return keystore_;
}

py::object MeshCryptoModule::storageKeyId() const {
    return storageKeyId_;
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

std::optional<std::vector<std::uint8_t>>
MeshCryptoModule::createHandshakeInit(
    const std::vector<std::uint8_t>& peerPublicKey)
{
    py::gil_scoped_acquire gil;
    std::lock_guard<std::mutex> lock(sessionMutex_);
    try {
        auto sessions = py::module_::import("mesh_crypto.sessions");
        auto createInit = sessions.attr("create_direct_handshake_init");

        py::bytes localPubKeyBytes = vecToPyBytes(identityPublicKeyBytes_);
        py::bytes peerPubKeyBytes  = vecToPyBytes(peerPublicKey);

        auto result = createInit(
            "keystore"_a = keystore_,
            "identity_key_id"_a = identityKeyId_,
            "identity_public_key"_a = localPubKeyBytes,
            "expected_peer_identity_public_key"_a = peerPubKeyBytes
        );

        // result = (pending, init)
        auto pending = result.attr("__getitem__")(0);
        auto init    = result.attr("__getitem__")(1);

        pendingHandshake_ = pending;

        // Serialize init to bytes and cache them for completeHandshake().
        auto initBytes = init.attr("to_bytes")();
        cachedInitBytes_ = pyBytesToVec(initBytes.cast<py::bytes>());
        return cachedInitBytes_;

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshCrypto] createHandshakeInit error: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>>
MeshCryptoModule::acceptHandshakeInit(
    const std::vector<std::uint8_t>& initBytes,
    const std::vector<std::uint8_t>& expectedPeerKey)
{
    py::gil_scoped_acquire gil;
    std::lock_guard<std::mutex> lock(sessionMutex_);
    try {
        auto sessions    = py::module_::import("mesh_crypto.sessions");
        auto fromBytes   = sessions.attr("DirectHandshakeInit")
                                   .attr("from_bytes");
        auto acceptInit  = sessions.attr("accept_direct_handshake_init");

        auto init = fromBytes(py::bytes(
            reinterpret_cast<const char*>(initBytes.data()), initBytes.size()));

        py::bytes localPubKeyBytes = vecToPyBytes(identityPublicKeyBytes_);
        py::bytes peerPubKeyBytes  = vecToPyBytes(expectedPeerKey);

        auto result = acceptInit(
            "keystore"_a = keystore_,
            "identity_key_id"_a = identityKeyId_,
            "identity_public_key"_a = localPubKeyBytes,
            "expected_peer_identity_public_key"_a = peerPubKeyBytes,
            "init"_a = init
        );

        // result = (session_state, response)
        sessionState_ = result.attr("__getitem__")(0);
        auto response = result.attr("__getitem__")(1);

        auto responseBytes = response.attr("to_bytes")();
        return pyBytesToVec(responseBytes.cast<py::bytes>());

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshCrypto] acceptHandshakeInit error: " << e.what() << "\n";
        return std::nullopt;
    }
}

bool MeshCryptoModule::completeHandshake(
    const std::vector<std::uint8_t>& responseBytes,
    const std::vector<std::uint8_t>& expectedPeerKey)
{
    py::gil_scoped_acquire gil;
    std::lock_guard<std::mutex> lock(sessionMutex_);
    try {
        if (pendingHandshake_.is_none()) {
            std::cerr << "[MeshCrypto] completeHandshake: no pending handshake\n";
            return false;
        }

        auto sessions       = py::module_::import("mesh_crypto.sessions");
        auto fromBytesInit  = sessions.attr("DirectHandshakeInit").attr("from_bytes");
        auto fromBytesResp  = sessions.attr("DirectHandshakeResponse").attr("from_bytes");
        auto completeHS     = sessions.attr("complete_direct_handshake");

        // We need the original init for complete_direct_handshake; it's not
        // stored here. Re-derive from pending state is not possible. Instead,
        // we store init_bytes during createHandshakeInit.
        // NOTE: For simplicity in this prototype, we pass the stored pending
        // which already holds the init transcript hash.
        auto response = fromBytesResp(py::bytes(
            reinterpret_cast<const char*>(responseBytes.data()), responseBytes.size()));

        py::bytes peerPubKeyBytes = vecToPyBytes(expectedPeerKey);

        // Re-build the original DirectHandshakeInit from the cached bytes
        // stored during createHandshakeInit().
        auto init = fromBytesInit(py::bytes(
            reinterpret_cast<const char*>(cachedInitBytes_.data()),
            cachedInitBytes_.size()));

        sessionState_ = completeHS(
            "pending"_a = pendingHandshake_,
            "init"_a = init,
            "response"_a = response,
            "expected_peer_identity_public_key"_a = peerPubKeyBytes
        );

        pendingHandshake_ = py::none();
        std::cout << "[MeshCrypto] handshake complete, session established\n";
        return true;

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshCrypto] completeHandshake error: " << e.what() << "\n";
        return false;
    }
}

// ---------------------------------------------------------------------------
// Messaging
// ---------------------------------------------------------------------------

std::optional<std::vector<std::uint8_t>>
MeshCryptoModule::encryptMessage(const std::vector<std::uint8_t>& plaintext)
{
    py::gil_scoped_acquire gil;
    std::lock_guard<std::mutex> lock(sessionMutex_);
    try {
        if (sessionState_.is_none()) {
            std::cerr << "[MeshCrypto] encryptMessage: no active session\n";
            return std::nullopt;
        }

        auto sessions = py::module_::import("mesh_crypto.sessions");
        auto encryptFn = sessions.attr("encrypt_direct_message");

        py::bytes plaintextBytes = vecToPyBytes(plaintext);
        auto result = encryptFn(sessionState_, plaintextBytes);

        // result = (new_state, envelope)
        sessionState_ = result.attr("__getitem__")(0);
        auto envelope = result.attr("__getitem__")(1);
        auto envBytes = envelope.attr("to_bytes")();

        return pyBytesToVec(envBytes.cast<py::bytes>());

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshCrypto] encryptMessage error: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>>
MeshCryptoModule::decryptMessage(const std::vector<std::uint8_t>& envelopeBytes)
{
    py::gil_scoped_acquire gil;
    std::lock_guard<std::mutex> lock(sessionMutex_);
    try {
        if (sessionState_.is_none()) {
            std::cerr << "[MeshCrypto] decryptMessage: no active session\n";
            return std::nullopt;
        }

        auto sessions    = py::module_::import("mesh_crypto.sessions");
        auto fromBytes   = sessions.attr("DirectMessageEnvelope").attr("from_bytes");
        auto decryptFn   = sessions.attr("decrypt_direct_message");

        auto envelope = fromBytes(py::bytes(
            reinterpret_cast<const char*>(envelopeBytes.data()),
            envelopeBytes.size()));

        auto result = decryptFn(sessionState_, envelope);

        // result = (new_state, plaintext)
        // Only replace state on success (exception already prevents reaching here).
        sessionState_ = result.attr("__getitem__")(0);
        auto plaintext = result.attr("__getitem__")(1);

        return pyBytesToVec(plaintext.cast<py::bytes>());

    } catch (const py::error_already_set& e) {
        std::cerr << "[MeshCrypto] decryptMessage error: " << e.what() << "\n";
        // Per spec: on failure, old state remains valid and must NOT be replaced.
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

bool MeshCryptoModule::hasActiveSession() const {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    return !sessionState_.is_none();
}

void MeshCryptoModule::clearSession() {
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessionState_     = py::none();
    pendingHandshake_ = py::none();
    cachedInitBytes_.clear();
}
