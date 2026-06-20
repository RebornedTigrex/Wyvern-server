#include "RelayServer.h"
#include "../modules/p2p/relay/RelayProtocol.h"
#include <boost/json.hpp>
#include <iostream>
#include <sstream>

namespace Wyvern::Relay {

RelayServer::RelayServer(boost::asio::io_context& ioc,
                        const std::string& host,
                        uint16_t port,
                        std::shared_ptr<Wyvern::DataStorage::IRelayStore> relay_store)
    : ioc_(ioc),
      listen_host_(host),
      listen_port_(port),
      relay_store_(relay_store) {
}

RelayServer::~RelayServer() {
    stop();
}

bool RelayServer::start() {
    try {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(listen_host_),
            listen_port_
        );

        acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(ioc_, endpoint);
        running_ = true;

        std::cout << "[RelayServer] Listening on ws://" << listen_host_ << ":" << listen_port_ << "\n";

        // Start async accept loop
        acceptConnections();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RelayServer] Failed to start: " << e.what() << "\n";
        running_ = false;
        return false;
    }
}

void RelayServer::stop() {
    running_ = false;
    if (acceptor_) {
        try {
            acceptor_->close();
        } catch (...) {
            // Ignore
        }
        acceptor_.reset();
    }
}

size_t RelayServer::getConnectedPeerCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

std::string RelayServer::getStatus() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::ostringstream oss;
    oss << "RelayServer running on ws://" << listen_host_ << ":" << listen_port_
        << ", " << clients_.size() << " connected peers";
    return oss.str();
}

void RelayServer::acceptConnections() {
    if (!running_ || !acceptor_) return;

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (!ec) {
            std::cout << "[RelayServer] New connection from " << socket->remote_endpoint() << "\n";
            handleConnection(socket);
        } else {
            std::cerr << "[RelayServer] Accept error: " << ec.message() << "\n";
        }

        // Continue accepting
        if (running_) {
            acceptConnections();
        }
    });
}

void RelayServer::handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    // Upgrade to WebSocket
    auto ws = std::make_shared<WsStream>(std::move(*socket));

    // Perform WebSocket handshake async
    ws->async_handshake("localhost", "/",
        [this, ws](const boost::system::error_code& ec) {
            if (ec) {
                std::cerr << "[RelayServer] WebSocket handshake failed: " << ec.message() << "\n";
                return;
            }

            std::cout << "[RelayServer] WebSocket handshake successful\n";

            // Start reading messages
            auto buffer = std::make_shared<boost::beast::flat_buffer>();

            std::function<void()> read_handler = [this, ws, buffer, read_handler]() {
                ws->async_read(*buffer,
                    [this, ws, buffer, read_handler](const boost::system::error_code& ec, size_t) {
                        if (ec) {
                            std::cerr << "[RelayServer] Read error: " << ec.message() << "\n";
                            return;
                        }

                        // Parse message
                        std::string json_text = boost::beast::buffers_to_string(buffer->data());
                        buffer->clear();

                        // Extract overlay_id from message to know which client it is
                        try {
                            auto json_val = boost::json::parse(json_text);
                            auto json_obj = json_val.as_object();

                            std::string overlay_id;
                            if (json_obj.contains("overlay_id")) {
                                overlay_id = boost::json::value_to<std::string>(json_obj.at("overlay_id"));
                            }

                            if (!overlay_id.empty()) {
                                processClientMessage(overlay_id, json_text);
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[RelayServer] JSON parse error: " << e.what() << "\n";
                        }

                        // Continue reading
                        read_handler();
                    });
            };

            read_handler();
        });
}

void RelayServer::processClientMessage(const std::string& overlay_id,
                                      const std::string& json_text) {
    try {
        auto json_val = boost::json::parse(json_text);
        auto json_obj = json_val.as_object();

        if (!json_obj.contains("type")) {
            std::cerr << "[RelayServer] Message missing 'type' field\n";
            return;
        }

        std::string type_str = boost::json::value_to<std::string>(json_obj.at("type"));
        P2P::Relay::Protocol::MessageType type = P2P::Relay::Protocol::stringToMessageType(type_str);

        switch (type) {
            case P2P::Relay::Protocol::MessageType::REGISTER: {
                std::string session_id;
                if (json_obj.contains("session_id")) {
                    session_id = boost::json::value_to<std::string>(json_obj.at("session_id"));
                }
                handleRegister(overlay_id, session_id);
                break;
            }
            case P2P::Relay::Protocol::MessageType::RENDEZVOUS_REQUEST: {
                std::string target_overlay_id;
                if (json_obj.contains("target_overlay_id")) {
                    target_overlay_id = boost::json::value_to<std::string>(json_obj.at("target_overlay_id"));
                }
                handleRendezvousRequest(overlay_id, target_overlay_id);
                break;
            }
            case P2P::Relay::Protocol::MessageType::RELAY_DATA: {
                std::string target_overlay_id;
                std::string payload;
                if (json_obj.contains("target_overlay_id")) {
                    target_overlay_id = boost::json::value_to<std::string>(json_obj.at("target_overlay_id"));
                }
                if (json_obj.contains("payload")) {
                    payload = boost::json::value_to<std::string>(json_obj.at("payload"));
                }
                handleRelayData(overlay_id, target_overlay_id, payload);
                break;
            }
            default:
                std::cerr << "[RelayServer] Unexpected message type from " << overlay_id << ": " << type_str << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[RelayServer] Error processing message: " << e.what() << "\n";
    }
}

void RelayServer::handleRegister(const std::string& overlay_id,
                               const std::string& client_session_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    ClientSession session;
    session.overlay_id = overlay_id;
    session.session_id = client_session_id;
    session.registered = true;

    clients_[overlay_id] = session;

    std::cout << "[RelayServer] Registered peer: " << overlay_id << "\n";

    // TODO: Store in relay_store for persistence (if needed)
}

void RelayServer::handleRendezvousRequest(const std::string& requesting_overlay_id,
                                         const std::string& target_overlay_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(target_overlay_id);
    if (it == clients_.end()) {
        // Peer not found
        P2P::Relay::Protocol::RendezvousResponseMessage resp;
        resp.status = "not_found";
        resp.peer_overlay_id = target_overlay_id;
        
        // Send error back to requester (in a real implementation, we'd track the connection)
        std::cout << "[RelayServer] Rendezvous request from " << requesting_overlay_id
                  << " for " << target_overlay_id << " - not found\n";
        return;
    }

    // Peer found - would respond with RENDEZVOUS_RESPONSE here
    // For now, stub implementation
    std::cout << "[RelayServer] Rendezvous request from " << requesting_overlay_id
              << " for " << target_overlay_id << " - found\n";
}

void RelayServer::handleRelayData(const std::string& sender_overlay_id,
                                 const std::string& target_overlay_id,
                                 const std::string& base64_payload) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(target_overlay_id);
    if (it == clients_.end()) {
        std::cerr << "[RelayServer] Cannot relay data - target " << target_overlay_id << " not found\n";
        return;
    }

    // Forward data to target (in a real implementation, we'd have the connection)
    std::cout << "[RelayServer] Relaying data from " << sender_overlay_id
              << " to " << target_overlay_id << " (" << base64_payload.length() << " bytes)\n";
}

} // namespace Wyvern::Relay
