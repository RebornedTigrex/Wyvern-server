#pragma once

#include "P2pEvents.h"
#include "StunProtocol.h"
#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

// P2pConnectionModule — low-level P2P transport.
//
// Responsibilities (only these):
//  - Open one UDP socket on the configured local port.
//  - Send a STUN Binding Request to discover the public (NAT) endpoint.
//  - Perform UDP hole punching (PROBE/ACK) to connect to a peer.
//  - Send and receive typed MASH packets once connected.
//  - Publish StunResolvedEvent, PeerConnectedEvent, PeerDisconnectedEvent,
//    P2pPacketReceivedEvent via EventBus.
//
// Does NOT know about crypto, DB, handshake protocol or CLI.
class P2pConnectionModule : public BaseModule {
public:
    static std::string moduleType() { return "p2p.connection"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        obj["localPort"]        = 9001;
        obj["stunServer"]       = "stun.l.google.com";
        obj["stunPort"]         = 19302;
        obj["probeIntervalMs"]  = 200;
        obj["probeTimeoutMs"]   = 10000;
        obj["maxDatagramBytes"] = 1400;
        return obj;
    }

    P2pConnectionModule(const core::runtime::ConfigSection& cfg,
                        boost::asio::io_context& ioc);

    std::string moduleKey() const override { return moduleType(); }

    std::vector<core::contracts::CommandDescriptor> commands() override;

    // --- Public API called by P2pMessengerModule ---

    // Resolve STUN server address and send a Binding Request.
    // Result is published as StunResolvedEvent on success.
    void requestStun();

    // Start hole punching towards the given peer endpoint.
    // Result is published as PeerConnectedEvent on success.
    void connectToPeer(const std::string& address, std::uint16_t port);

    // Send a typed MASH packet to the connected peer.
    // No-op if not connected.
    void sendToPeer(std::uint8_t type, std::uint8_t flags, std::uint64_t meta,
                    std::vector<std::uint8_t> payload);

    // Disconnect: stop probing, clear peer endpoint.
    void disconnect(const std::string& reason);

    bool isConnected() const;

    // Returns cached public endpoint from last STUN call, or nullopt.
    std::optional<p2p::stun::StunEndpoint> publicEndpoint() const;

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    using udp_socket   = boost::asio::ip::udp::socket;
    using udp_endpoint = boost::asio::ip::udp::endpoint;
    using udp_resolver = boost::asio::ip::udp::resolver;

    // --- Receive loop ---
    void doReceive();
    void onReceive(const boost::system::error_code& ec, std::size_t bytes);

    // --- Dispatch ---
    void handleStunResponse(const std::uint8_t* data, std::size_t size);
    void handleMashPacket(const std::uint8_t* data, std::size_t size);
    void handleProbe();
    void handleAck();

    // --- Hole punching ---
    void startProbeTimer();
    void stopProbeTimer();
    void sendProbe();
    void sendAck();
    void markConnected();

    // --- Raw send helpers ---
    void sendRaw(const udp_endpoint& target,
                 std::vector<std::uint8_t> data);
    void sendMash(const udp_endpoint& target,
                  std::uint8_t type, std::uint8_t flags,
                  std::uint64_t meta,
                  const std::vector<std::uint8_t>& payload);

    // --- Commands ---
    core::contracts::CommandResult cmdStun(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdStatus(const core::contracts::CommandArgs&);
    core::contracts::CommandResult cmdDisconnect(const core::contracts::CommandArgs&);

    // --- Config ---
    boost::asio::io_context& ioc_;
    std::uint16_t            localPort_;
    std::string              stunServer_;
    std::uint16_t            stunPort_;
    int                      probeIntervalMs_;
    int                      probeTimeoutMs_;
    std::size_t              maxDatagramBytes_;

    // --- Networking ---
    std::unique_ptr<udp_socket>               socket_;
    std::unique_ptr<boost::asio::steady_timer> probeTimer_;
    std::unique_ptr<boost::asio::steady_timer> probeTimeoutTimer_;
    std::vector<std::uint8_t>                 rxBuf_;
    udp_endpoint                              rxRemote_;

    // --- STUN state ---
    p2p::stun::TransactionId                  stunTxid_{};
    std::atomic<bool>                         stunPending_{false};
    std::optional<p2p::stun::StunEndpoint>    publicEndpoint_;
    mutable std::mutex                        endpointMutex_;

    // --- Hole punching state ---
    udp_endpoint               peerEndpoint_;
    std::atomic<bool>          connected_{false};
    std::atomic<bool>          probing_{false};

    // --- RNG for transaction IDs ---
    std::mt19937 rng_{std::random_device{}()};
};
