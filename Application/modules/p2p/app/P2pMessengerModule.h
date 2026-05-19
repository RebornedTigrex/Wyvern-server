#pragma once

#include "transport/P2pConnectionModule.h"
#include "transport/P2pEvents.h"
#include "crypto/MeshCryptoModule.h"
#include "db/MeshNodeDbModule.h"
#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"
#include "contracts/IModule.h"
#include "contracts/Primitives.h"

#include <boost/json.hpp>
#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// P2pMessengerModule — orchestrates the P2P chat scenario.
//
// Responsibilities:
//  - Expose CLI commands (p2p.stun, p2p.connect, p2p.identity,
//    p2p.send, p2p.history, p2p.status).
//  - Subscribe to EventBus events and drive the state machine:
//    Idle -> Connected -> AwaitingIdentity ->
//    HandshakeInitiated | HandshakeResponding -> Secure.
//  - Coordinate P2pConnectionModule, MeshCryptoModule, MeshNodeDbModule.
//
// Does NOT parse STUN, manage the DB schema, or call Python directly.
class P2pMessengerModule : public BaseModule {
public:
    static std::string moduleType() { return "p2p.messenger"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        return obj;
    }

    P2pMessengerModule(const core::runtime::ConfigSection& cfg,
                       boost::asio::io_context& ioc);

    std::string moduleKey() const override { return moduleType(); }

    std::vector<std::string> dependencies() const override {
        return {
            P2pConnectionModule::moduleType(),
            MeshCryptoModule::moduleType(),
            MeshNodeDbModule::moduleType(),
        };
    }

    void onInject(const std::string& depKey,
                  core::contracts::IModule* dep) override;

    std::vector<core::contracts::CommandDescriptor> commands() override;

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    enum class State {
        Idle,
        Connected,
        AwaitingIdentity,
        HandshakeInitiated,
        HandshakeResponding,
        Secure,
    };

    // --- EventBus handlers ---
    void onStunResolved(const p2p::events::StunResolvedEvent& ev);
    void onPeerConnected(const p2p::events::PeerConnectedEvent& ev);
    void onPeerDisconnected(const p2p::events::PeerDisconnectedEvent& ev);
    void onPacketReceived(const p2p::events::P2pPacketReceivedEvent& ev);

    // --- Packet handlers ---
    void handleIdentityAnnounce(const std::vector<std::uint8_t>& payload);
    void handleHandshakeInit(const std::vector<std::uint8_t>& payload);
    void handleHandshakeResponse(const std::vector<std::uint8_t>& payload);
    void handleTextMessage(const std::vector<std::uint8_t>& payload);

    // --- Protocol helpers ---
    void sendIdentityAnnounce();
    void startHandshakeAsInitiator();

    // Derive a deterministic short peer ID from a 32-byte public key.
    static std::string peerIdFromPublicKey(const std::vector<std::uint8_t>& key);

    // Build chat ID from sorted peer IDs.
    static std::string chatIdFromPeerIds(const std::string& a, const std::string& b);

    // --- Commands ---
    core::contracts::CommandResult cmdStun(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdConnect(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdIdentity(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdSend(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdHistory(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdStatus(const core::contracts::CommandArgs&);

    // --- Dependencies (injected) ---
    P2pConnectionModule* connection_ = nullptr;
    MeshCryptoModule*    crypto_     = nullptr;
    MeshNodeDbModule*    db_         = nullptr;
    boost::asio::io_context& ioc_;

    // --- State ---
    mutable std::mutex stateMutex_;
    State state_ = State::Idle;

    std::vector<std::uint8_t> remotePeerPublicKey_;
    std::string remotePeerId_;
    std::string localPeerId_;
    std::string activeChatId_;

    // --- EventBus subscriptions ---
    core::contracts::SubscriptionId subStun_        = 0;
    core::contracts::SubscriptionId subConnected_   = 0;
    core::contracts::SubscriptionId subDisconnected_= 0;
    core::contracts::SubscriptionId subPacket_      = 0;
};
