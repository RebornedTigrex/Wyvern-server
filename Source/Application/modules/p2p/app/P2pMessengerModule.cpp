#include "P2pMessengerModule.h"

#include "managers/EventBus.h"

#include <boost/asio/post.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

P2pMessengerModule::P2pMessengerModule(
    const core::runtime::ConfigSection& /*cfg*/,
    boost::asio::io_context& ioc)
    : BaseModule("P2P Messenger"), ioc_(ioc)
{}

// ---------------------------------------------------------------------------
// Dependency injection
// ---------------------------------------------------------------------------

void P2pMessengerModule::onInject(const std::string& depKey,
                                   core::contracts::IModule* dep) {
    if (depKey == P2pConnectionModule::moduleType())
        connection_ = dynamic_cast<P2pConnectionModule*>(dep);
    else if (depKey == MeshCryptoModule::moduleType())
        crypto_ = dynamic_cast<MeshCryptoModule*>(dep);
    else if (depKey == MeshNodeDbModule::moduleType())
        db_ = dynamic_cast<MeshNodeDbModule*>(dep);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool P2pMessengerModule::onInitialize() {
    if (!connection_ || !crypto_ || !db_) {
        std::cerr << "[P2pMessenger] missing dependencies\n";
        return false;
    }

    localPeerId_ = peerIdFromPublicKey(crypto_->identityPublicKey());

    std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();
    if (!bus) return false;

    subStun_ = bus->subscribe<p2p::events::StunResolvedEvent>(
        [this](const p2p::events::StunResolvedEvent& ev) {
            onStunResolved(ev);
        });

    subConnected_ = bus->subscribe<p2p::events::PeerConnectedEvent>(
        [this](const p2p::events::PeerConnectedEvent& ev) {
            onPeerConnected(ev);
        });

    subDisconnected_ = bus->subscribe<p2p::events::PeerDisconnectedEvent>(
        [this](const p2p::events::PeerDisconnectedEvent& ev) {
            onPeerDisconnected(ev);
        });

    subPacket_ = bus->subscribe<p2p::events::P2pPacketReceivedEvent>(
        [this](const p2p::events::P2pPacketReceivedEvent& ev) {
            onPacketReceived(ev);
        });

    std::cout << "[P2pMessenger] ready. Local peer id: " << localPeerId_ << "\n"
              << "  Use 'p2p.messenger stun' to discover your public endpoint,\n"
              << "  then 'p2p.messenger connect <ip> <port>' to connect to a peer.\n";
    return true;
}

void P2pMessengerModule::onShutdown() {
    std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();
    if (bus) {
        if (subStun_)         { bus->unsubscribe(subStun_);         subStun_ = 0; }
        if (subConnected_)    { bus->unsubscribe(subConnected_);    subConnected_ = 0; }
        if (subDisconnected_) { bus->unsubscribe(subDisconnected_); subDisconnected_ = 0; }
        if (subPacket_)       { bus->unsubscribe(subPacket_);       subPacket_ = 0; }
    }
}

// ---------------------------------------------------------------------------
// EventBus handlers
// ---------------------------------------------------------------------------

void P2pMessengerModule::onStunResolved(
    const p2p::events::StunResolvedEvent& ev) {
    std::cout << "[P2pMessenger] Your public endpoint: "
              << ev.publicAddress << ":" << ev.publicPort << "\n"
              << "  Share this with your peer and then both run:\n"
              << "  p2p.messenger connect <peer_ip> <peer_port>\n";
}

void P2pMessengerModule::onPeerConnected(
    const p2p::events::PeerConnectedEvent& ev) {
    std::cout << "[P2pMessenger] Hole punch succeeded with "
              << ev.peerAddress << ":" << ev.peerPort << "\n"
              << "  Exchanging identities...\n";
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = State::AwaitingIdentity;
    }
    sendIdentityAnnounce();
}

void P2pMessengerModule::onPeerDisconnected(
    const p2p::events::PeerDisconnectedEvent& ev) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    std::cout << "[P2pMessenger] Peer disconnected: " << ev.reason << "\n";
    state_ = State::Idle;
    remotePeerPublicKey_.clear();
    remotePeerId_.clear();
    activeChatId_.clear();
    crypto_->clearSession();
}

void P2pMessengerModule::onPacketReceived(
    const p2p::events::P2pPacketReceivedEvent& ev) {
    switch (ev.type) {
    case p2p::proto::kTypeIdentityAnnounce:
        handleIdentityAnnounce(ev.payload);
        break;
    case p2p::proto::kTypeHandshakeInit:
        handleHandshakeInit(ev.payload);
        break;
    case p2p::proto::kTypeHandshakeResponse:
        handleHandshakeResponse(ev.payload);
        break;
    case p2p::proto::kTypeTextMessage:
        handleTextMessage(ev.payload);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Packet handlers
// ---------------------------------------------------------------------------

void P2pMessengerModule::handleIdentityAnnounce(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 32) {
        std::cerr << "[P2pMessenger] bad IDENTITY_ANNOUNCE size: "
                  << payload.size() << "\n";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ != State::AwaitingIdentity &&
            state_ != State::Connected &&
            state_ != State::Idle) {
            // Already have identity.
            return;
        }
        remotePeerPublicKey_ = payload;
        remotePeerId_        = peerIdFromPublicKey(payload);
        activeChatId_        = chatIdFromPeerIds(localPeerId_, remotePeerId_);
    }

    std::cout << "[P2pMessenger] Got peer identity: "
              << peerIdFromPublicKey(payload) << "\n";

    // Determine initiator deterministically: the side with the
    // lexicographically smaller public key starts the handshake.
    const auto localKey = crypto_->identityPublicKey();
    if (localKey < payload) {
        // We are initiator.
        std::cout << "[P2pMessenger] We are initiator, sending handshake init...\n";
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = State::HandshakeInitiated;
        }
        startHandshakeAsInitiator();
    } else {
        // We are responder — wait for the other side's HandshakeInit.
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = State::HandshakeResponding;
        std::cout << "[P2pMessenger] We are responder, waiting for handshake init...\n";
    }
}

void P2pMessengerModule::handleHandshakeInit(
    const std::vector<std::uint8_t>& payload)
{
    State currentState;
    std::vector<std::uint8_t> peerKey;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentState = state_;
        peerKey = remotePeerPublicKey_;
    }

    if (currentState != State::HandshakeResponding) {
        std::cerr << "[P2pMessenger] received HandshakeInit in unexpected state\n";
        return;
    }
    if (peerKey.empty()) {
        std::cerr << "[P2pMessenger] peer identity not known yet\n";
        return;
    }

    auto responseBytes = crypto_->acceptHandshakeInit(payload, peerKey);
    if (!responseBytes) {
        std::cerr << "[P2pMessenger] failed to accept handshake init\n";
        return;
    }

    connection_->sendToPeer(p2p::proto::kTypeHandshakeResponse, 0, 0,
                            *responseBytes);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = State::Secure;
    }
    std::cout << "[P2pMessenger] Secure session established (responder).\n"
              << "  You can now use |p2p.messenger send <message>|\n";
}

void P2pMessengerModule::handleHandshakeResponse(
    const std::vector<std::uint8_t>& payload)
{
    State currentState;
    std::vector<std::uint8_t> peerKey;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentState = state_;
        peerKey = remotePeerPublicKey_;
    }

    if (currentState != State::HandshakeInitiated) {
        std::cerr << "[P2pMessenger] received HandshakeResponse in unexpected state\n";
        return;
    }

    bool ok = crypto_->completeHandshake(payload, peerKey);
    if (!ok) {
        std::cerr << "[P2pMessenger] failed to complete handshake\n";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = State::Secure;
    }
    std::cout << "[P2pMessenger] Secure session established (initiator).\n"
              << "  You can now use |p2p.messenger send <message>|\n";
}

void P2pMessengerModule::handleTextMessage(
    const std::vector<std::uint8_t>& payload)
{
    State currentState;
    std::string chatId, senderId;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentState = state_;
        chatId       = activeChatId_;
        senderId     = remotePeerId_;
    }

    if (currentState != State::Secure) {
        std::cerr << "[P2pMessenger] received text before secure session\n";
        return;
    }

    auto decrypted = crypto_->decryptMessage(payload);
    if (!decrypted) {
        std::cerr << "[P2pMessenger] failed to decrypt message\n";
        return;
    }

    const std::string text(decrypted->begin(), decrypted->end());
    std::cout << "\n[" << senderId << "]: " << text << "\n";

    // Persist message.
    if (db_) {
        // Generate a simple message ID from time + random.
        std::ostringstream msgId;
        msgId << senderId << "-"
              << std::chrono::system_clock::now().time_since_epoch().count();

        auto nowSec = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        MeshNodeDbModule::StoredMessage m;
        m.messageId = msgId.str();
        m.chatId    = chatId;
        m.senderId  = senderId;
        m.text      = text;
        m.createdAt = nowSec;

        db_->saveMessage(m, remotePeerPublicKey_);
    }
}

// ---------------------------------------------------------------------------
// Protocol helpers
// ---------------------------------------------------------------------------

void P2pMessengerModule::sendIdentityAnnounce() {
    const auto pubKey = crypto_->identityPublicKey();
    if (pubKey.size() != 32) return;
    connection_->sendToPeer(p2p::proto::kTypeIdentityAnnounce, 0, 0, pubKey);
}

void P2pMessengerModule::startHandshakeAsInitiator() {
    std::vector<std::uint8_t> peerKey;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        peerKey = remotePeerPublicKey_;
    }

    auto initBytes = crypto_->createHandshakeInit(peerKey);
    if (!initBytes) {
        std::cerr << "[P2pMessenger] failed to create handshake init\n";
        return;
    }
    connection_->sendToPeer(p2p::proto::kTypeHandshakeInit, 0, 0, *initBytes);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string P2pMessengerModule::peerIdFromPublicKey(
    const std::vector<std::uint8_t>& key)
{
    // Use hex of first 8 bytes as a human-readable short ID.
    std::ostringstream oss;
    for (std::size_t i = 0; i < 8 && i < key.size(); ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(key[i]);
    }
    return oss.str();
}

std::string P2pMessengerModule::chatIdFromPeerIds(
    const std::string& a, const std::string& b)
{
    // Deterministic: smaller ID first so both sides produce the same chat ID.
    const auto& first  = (a < b) ? a : b;
    const auto& second = (a < b) ? b : a;
    return "direct:" + first + ":" + second;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

std::vector<core::contracts::CommandDescriptor> P2pMessengerModule::commands() {
    return {
        { "stun",
          "Discover your public IP:port via Google STUN",
          [this](const core::contracts::CommandArgs& a) { return cmdStun(a); } },
        { "connect",
          "p2p.messenger connect <ip> <port> -- start hole punching to peer",
          [this](const core::contracts::CommandArgs& a) { return cmdConnect(a); } },
        { "identity",
          "Show local identity (peer id and public key hex)",
          [this](const core::contracts::CommandArgs& a) { return cmdIdentity(a); } },
        { "send",
          "p2p.messenger send <message text> -- send encrypted text to connected peer",
          [this](const core::contracts::CommandArgs& a) { return cmdSend(a); } },
        { "history",
          "Show message history for the current chat",
          [this](const core::contracts::CommandArgs& a) { return cmdHistory(a); } },
        { "status",
          "Show current connection and session state",
          [this](const core::contracts::CommandArgs& a) { return cmdStatus(a); } },
    };
}

core::contracts::CommandResult P2pMessengerModule::cmdStun(
    const core::contracts::CommandArgs&)
{
    connection_->requestStun();
    return core::contracts::CommandResult::success("STUN request sent");
}

core::contracts::CommandResult P2pMessengerModule::cmdConnect(
    const core::contracts::CommandArgs& args)
{
    if (args.size() != 2) {
        return core::contracts::CommandResult::failure(
            "usage: p2p.messenger connect <ip> <port>");
    }
    std::uint16_t port = 0;
    try {
        const long p = std::stol(args[1]);
        if (p <= 0 || p > 65535)
            return core::contracts::CommandResult::failure("port out of range");
        port = static_cast<std::uint16_t>(p);
    } catch (...) {
        return core::contracts::CommandResult::failure("invalid port: " + args[1]);
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = State::Connected;
    }
    connection_->connectToPeer(args[0], port);
    return core::contracts::CommandResult::success(
        "hole punching to " + args[0] + ":" + args[1]);
}

core::contracts::CommandResult P2pMessengerModule::cmdIdentity(
    const core::contracts::CommandArgs&)
{
    const auto pubKey = crypto_->identityPublicKey();
    std::ostringstream oss;
    oss << "peer_id: " << localPeerId_ << "\n";
    oss << "public_key: ";
    for (auto b : pubKey) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(b);
    }
    return core::contracts::CommandResult::success(oss.str());
}

core::contracts::CommandResult P2pMessengerModule::cmdSend(
    const core::contracts::CommandArgs& args)
{
    State currentState;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentState = state_;
    }

    if (currentState != State::Secure) {
        return core::contracts::CommandResult::failure(
            "no secure session (current state: " +
            std::to_string(static_cast<int>(currentState)) + ")");
    }
    if (args.empty()) {
        return core::contracts::CommandResult::failure(
            "usage: p2p.messenger send <text>");
    }

    // Join all args as the message.
    std::string text;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) text += ' ';
        text += args[i];
    }

    const std::vector<std::uint8_t> plaintext(text.begin(), text.end());
    auto envelopeBytes = crypto_->encryptMessage(plaintext);
    if (!envelopeBytes) {
        return core::contracts::CommandResult::failure("encryption failed");
    }

    connection_->sendToPeer(p2p::proto::kTypeTextMessage, 0, 0, *envelopeBytes);

    // Persist outbound message.
    if (db_) {
        std::ostringstream msgId;
        msgId << localPeerId_ << "-"
              << std::chrono::system_clock::now().time_since_epoch().count();

        auto nowSec = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        MeshNodeDbModule::StoredMessage m;
        m.messageId = msgId.str();
        m.chatId    = activeChatId_;
        m.senderId  = localPeerId_;
        m.text      = text;
        m.createdAt = nowSec;

        db_->saveMessage(m, crypto_->identityPublicKey());
    }

    return core::contracts::CommandResult::success("sent");
}

core::contracts::CommandResult P2pMessengerModule::cmdHistory(
    const core::contracts::CommandArgs&)
{
    std::string chatId;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        chatId = activeChatId_;
    }

    if (chatId.empty()) {
        return core::contracts::CommandResult::failure("no active chat");
    }

    const auto msgs = db_->loadMessages(chatId, 50);
    if (msgs.empty()) {
        return core::contracts::CommandResult::success("(no messages yet)");
    }

    std::ostringstream oss;
    // msgs come newest-first; print oldest first.
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        oss << "[" << it->senderId << "]: " << it->text << "\n";
    }
    return core::contracts::CommandResult::success(oss.str());
}

core::contracts::CommandResult P2pMessengerModule::cmdStatus(
    const core::contracts::CommandArgs&)
{
    State currentState;
    std::string remId, chatId;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentState = state_;
        remId  = remotePeerId_;
        chatId = activeChatId_;
    }

    static const char* stateNames[] = {
        "Idle", "Connected", "AwaitingIdentity",
        "HandshakeInitiated", "HandshakeResponding", "Secure"
    };
    const int si = static_cast<int>(currentState);
    const char* stateName = (si >= 0 && si < 6) ? stateNames[si] : "?";

    std::ostringstream oss;
    oss << "state=" << stateName
        << " local_peer=" << localPeerId_;
    if (!remId.empty()) oss << " remote_peer=" << remId;
    if (!chatId.empty()) oss << " chat=" << chatId;
    oss << " connected=" << (connection_->isConnected() ? "yes" : "no");
    return core::contracts::CommandResult::success(oss.str());
}
