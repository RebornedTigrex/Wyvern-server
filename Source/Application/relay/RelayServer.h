#pragma once

#include "../modules/dataStorage/IRelayStore.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Wyvern::Relay {

// WebSocket stream type for relay server connections (plain WS, no TLS for prototype)
using WsStream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;

/// Minimal relay server for symmetric NAT/CGNAT traversal via WebSocket
///
/// Responsibilities:
/// - Listen for incoming WebSocket connections
/// - Track registered peers by overlay ID
/// - Forward RENDEZVOUS_REQUEST with peer candidates
/// - Relay opaque RELAY_DATA messages between peers
/// - Maintain ephemeral session state in IRelayStore
///
/// Protocol:
/// - Clients send REGISTER with overlay_id -> server tracks them
/// - Clients send RENDEZVOUS_REQUEST(target_overlay_id) -> server responds with peer candidates
/// - Clients send RELAY_DATA(target_overlay_id, payload) -> server forwards to target
///
/// Security notes (prototype only):
/// - No TLS (ws:// not wss://)
/// - No client authentication beyond overlay-id registration
/// - Server acts as dumb forwarder, does not inspect/decrypt payload
class RelayServer {
public:
    RelayServer(boost::asio::io_context& ioc,
               const std::string& host,
               uint16_t port,
               std::shared_ptr<Wyvern::DataStorage::IRelayStore> relay_store);

    ~RelayServer();

    /// Start listening for WebSocket connections
    bool start();

    /// Stop the server gracefully
    void stop();

    /// Get number of currently connected clients
    size_t getConnectedPeerCount() const;

    /// Get human-readable status
    std::string getStatus() const;

private:
    // --- Connection handler ---
    void acceptConnections();
    void handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // --- Per-connection WebSocket handler ---
    struct ClientSession {
        std::string overlay_id;
        std::string session_id;
        bool registered = false;
        // We don't keep the WebSocket here; ownership is in async handler
    };

    // --- Message processing ---
    void processClientMessage(const std::string& overlay_id,
                             const std::string& json_text);

    // --- Protocol handlers ---
    void handleRegister(const std::string& overlay_id,
                       const std::string& client_session_id);

    void handleRendezvousRequest(const std::string& requesting_overlay_id,
                                const std::string& target_overlay_id);

    void handleRelayData(const std::string& sender_overlay_id,
                       const std::string& target_overlay_id,
                       const std::string& base64_payload);

    // --- State ---
    boost::asio::io_context& ioc_;
    std::string listen_host_;
    uint16_t listen_port_;
    std::shared_ptr<Wyvern::DataStorage::IRelayStore> relay_store_;

    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    mutable std::mutex clients_mutex_;
    std::unordered_map<std::string, ClientSession> clients_;  // overlay_id -> session

    bool running_ = false;
};

} // namespace Wyvern::Relay
