#pragma once

#include "RelayProtocol.h"
#include "RelayEvents.h"
#include "OverlayId.h"
#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"
#include "contracts/IModule.h"
#include "managers/EventBus.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <queue>

namespace Wyvern::P2P::Relay {

/// RelaySignalingModule: WebSocket client for connecting to relay server
///
/// Responsibilities:
/// - Maintain WebSocket connection to configured relay server
/// - Register local overlay ID with relay on connection
/// - Send RENDEZVOUS_REQUEST to find peer candidates
/// - Relay opaque DirectMessageEnvelope data via RELAY_DATA messages
/// - Publish EventBus events for relay state changes and data reception
/// - Handle keep-alive (PING/PONG)
///
/// Lifecycle:
/// - onInitialize: prepares config and EventBus subscriptions
/// - Main loop: async WS connection, REGISTER, rendezvous, relay operations
/// - onShutdown: closes connection and cleans up
class RelaySignalingModule : public BaseModule {
public:
    static std::string moduleType() { return "wyvern.signaling-relay"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        // Default relay server URL (can be overridden in config)
        // For prototype: ws://localhost:9002 or environment variable RELAY_SERVER_URL
        obj["server_url"] = "";  // Empty = disabled / will use env or CLI override
        return obj;
    }

    RelaySignalingModule(const core::runtime::ConfigSection& cfg,
                        boost::asio::io_context& ioc);

    std::string moduleKey() const override { return moduleType(); }

    std::vector<std::string> dependencies() const override {
        // No mandatory dependencies; works standalone
        return {};
    }

    void onInject(const std::string& depKey,
                  core::contracts::IModule* dep) override {
        // No injected dependencies
        (void)depKey;
        (void)dep;
    }

    std::vector<core::contracts::CommandDescriptor> commands() override;

protected:
    bool onInitialize() override;
    void onShutdown() override;

public:
    /// Register local overlay ID with relay (called after connection)
    /// Publishes RelayRegisteredEvent on success
    void registerOverlayId(const std::string& overlay_id);

    /// Request rendezvous with a peer by overlay ID
    /// Publishes RelayRendezvousResponseEvent on response
    void requestRendezvous(const std::string& target_overlay_id);

    /// Send opaque data via relay to peer
    /// @param target_overlay_id Recipient's overlay ID
    /// @param envelope Opaque DirectMessageEnvelope bytes
    void sendRelayData(const std::string& target_overlay_id,
                      const std::vector<std::uint8_t>& envelope);

    /// Check if relay connection is active
    bool isConnected() const;

    /// Get current relay server URL
    std::string getRelayServerUrl() const;

    /// Set relay server URL (can be called before/after init)
    void setRelayServerUrl(const std::string& url);

private:
    // --- WebSocket connection management ---
    void connectToRelay();
    void handleConnectionSuccess();
    void handleConnectionFailure(const std::string& error);
    void closeConnection();

    // --- Message send/receive ---
    void sendMessage(const boost::json::object& message);
    void receiveMessages();
    void handleMessage(const std::string& json_text);

    // --- Protocol handlers ---
    void handleRendezvousResponse(const Protocol::RendezvousResponseMessage& msg);
    void handleRelayData(const Protocol::RelayDataMessage& msg);
    void handleErrorResponse(const Protocol::ErrorResponseMessage& msg);

    // --- Helper: base64 encode/decode for payload ---
    static std::string base64Encode(const std::vector<std::uint8_t>& data);
    static std::vector<std::uint8_t> base64Decode(const std::string& encoded);

    // --- State ---
    mutable std::mutex state_mutex_;
    std::string relay_server_url_;
    std::string local_overlay_id_;
    bool is_connected_ = false;

    // --- Dependencies (injected or owned) ---
    boost::asio::io_context& ioc_;
    std::unique_ptr<boost::asio::strand<boost::asio::io_context::executor_type>> strand_;

    // --- WebSocket stream (async) ---
    // For prototype: plain WS (no TLS); wss: upgrade in future hardening step
    using WsStream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;
    std::unique_ptr<WsStream> ws_;

    // --- Message queue for async send ---
    std::queue<std::string> message_queue_;
    std::mutex queue_mutex_;

    // --- Commands ---
    core::contracts::CommandResult cmdRelayStatus(const core::contracts::CommandArgs&);
};

} // namespace Wyvern::P2P::Relay
