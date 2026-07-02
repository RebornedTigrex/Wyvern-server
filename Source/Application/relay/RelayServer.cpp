#include "RelayServer.h"

#include "../modules/p2p/relay/RelayProtocol.h"

#include <boost/asio/socket_base.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <iostream>
#include <sstream>

namespace Wyvern::Relay {

RelayServer::RelayServer(const core::runtime::ConfigSection& cfg,
                         boost::asio::io_context& ioc,
                         std::shared_ptr<Wyvern::DataStorage::IRelayStore> relay_store)
    : BaseModule("Relay Server"),
      ioc_(ioc),
      listen_host_(cfg.value<std::string>("host", "0.0.0.0")),
      listen_port_(static_cast<std::uint16_t>(cfg.value<int>("port", 9002))),
      relay_store_(std::move(relay_store)) {}

RelayServer::~RelayServer() {
    stop();
}

std::vector<core::contracts::CommandDescriptor> RelayServer::commands() {
    return {
        {
            "status",
            "Show relay server status",
            [this](const core::contracts::CommandArgs& args) {
                return cmdStatus(args);
            }
        }
    };
}

bool RelayServer::onInitialize() {
    return start();
}

void RelayServer::onShutdown() {
    stop();
}

bool RelayServer::start() {
    if (running_.load()) {
        return true;
    }
    if (!relay_store_) {
        std::cerr << "[RelayServer] relay store dependency is not set\n";
        return false;
    }

    try {
        const boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(listen_host_),
            listen_port_);

        acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(ioc_);
        acceptor_->open(endpoint.protocol());
        acceptor_->set_option(boost::asio::socket_base::reuse_address(true));
        acceptor_->bind(endpoint);
        acceptor_->listen(boost::asio::socket_base::max_listen_connections);

        running_.store(true);
        std::cout << "[RelayServer] Listening on ws://" << listen_host_ << ":" << listen_port_ << "\n";
        acceptConnections();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RelayServer] Failed to start: " << e.what() << "\n";
        running_.store(false);
        acceptor_.reset();
        return false;
    }
}

void RelayServer::stop() {
    running_.store(false);

    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
    acceptor_.reset();

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
    }

    if (relay_store_) {
        relay_store_->clear();
    }
}

std::size_t RelayServer::getConnectedPeerCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

std::string RelayServer::getStatus() const {
    std::ostringstream oss;
    oss << "RelayServer " << (running_.load() ? "running" : "stopped")
        << " on ws://" << listen_host_ << ":" << listen_port_
        << ", connected_peers=" << getConnectedPeerCount();

    if (relay_store_) {
        oss << ", store={" << relay_store_->getDebugInfo() << "}";
    }

    return oss.str();
}

core::contracts::CommandResult RelayServer::cmdStatus(const core::contracts::CommandArgs& args) {
    (void)args;
    return core::contracts::CommandResult::success(getStatus());
}

void RelayServer::acceptConnections() {
    if (!running_.load() || !acceptor_ || !acceptor_->is_open()) {
        return;
    }

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (!ec) {
            std::cout << "[RelayServer] New connection from " << socket->remote_endpoint() << "\n";
            handleConnection(socket);
        } else if (running_.load()) {
            std::cerr << "[RelayServer] Accept error: " << ec.message() << "\n";
        }

        if (running_.load()) {
            acceptConnections();
        }
    });
}

void RelayServer::handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    auto ws = std::make_shared<WsStream>(std::move(*socket));
    ws->async_accept([this, ws](const boost::system::error_code& ec) {
        if (ec) {
            std::cerr << "[RelayServer] WebSocket accept failed: " << ec.message() << "\n";
            return;
        }

        auto buffer = std::make_shared<boost::beast::flat_buffer>();
        startSessionRead(ws, buffer);
    });
}

void RelayServer::startSessionRead(std::shared_ptr<WsStream> ws,
                                   std::shared_ptr<boost::beast::flat_buffer> buffer) {
    ws->async_read(*buffer, [this, ws, buffer](const boost::system::error_code& ec, std::size_t) {
        if (ec) {
            std::cerr << "[RelayServer] Read error: " << ec.message() << "\n";
            return;
        }

        const std::string json_text = boost::beast::buffers_to_string(buffer->data());
        buffer->consume(buffer->size());

        try {
            const auto json_val = boost::json::parse(json_text);
            const auto& json_obj = json_val.as_object();

            std::string overlay_id;
            if (json_obj.contains("overlay_id")) {
                overlay_id = boost::json::value_to<std::string>(json_obj.at("overlay_id"));
            }

            if (overlay_id.empty()) {
                std::cerr << "[RelayServer] Message missing overlay_id\n";
            } else {
                processClientMessage(overlay_id, json_text);
            }
        } catch (const std::exception& e) {
            std::cerr << "[RelayServer] JSON parse error: " << e.what() << "\n";
        }

        if (running_.load()) {
            startSessionRead(ws, buffer);
        }
    });
}

void RelayServer::processClientMessage(const std::string& overlay_id,
                                       const std::string& json_text) {
    try {
        const auto json_val = boost::json::parse(json_text);
        const auto& json_obj = json_val.as_object();

        if (!json_obj.contains("type")) {
            std::cerr << "[RelayServer] Message missing 'type' field\n";
            return;
        }

        const std::string type_str = boost::json::value_to<std::string>(json_obj.at("type"));
        const P2P::Relay::Protocol::MessageType type =
            P2P::Relay::Protocol::stringToMessageType(type_str);

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
                std::cerr << "[RelayServer] Unexpected message type from "
                          << overlay_id << ": " << type_str << "\n";
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "[RelayServer] Error processing message: " << e.what() << "\n";
    }
}

void RelayServer::handleRegister(const std::string& overlay_id,
                                 const std::string& client_session_id) {
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        ClientSession session;
        session.overlay_id = overlay_id;
        session.session_id = client_session_id;
        session.registered = true;
        clients_[overlay_id] = session;
    }

    if (relay_store_) {
        Wyvern::DataStorage::IRelayStore::RegisteredPeer peer;
        peer.overlayId = overlay_id;
        peer.sessionId = client_session_id;
        peer.endpoint = "";
        peer.registrationTime = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        relay_store_->registerPeer(peer);
    }

    std::cout << "[RelayServer] Registered peer: " << overlay_id << "\n";
}

void RelayServer::handleRendezvousRequest(const std::string& requesting_overlay_id,
                                          const std::string& target_overlay_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    const auto it = clients_.find(target_overlay_id);
    if (it == clients_.end()) {
        std::cout << "[RelayServer] Rendezvous request from " << requesting_overlay_id
                  << " for " << target_overlay_id << " - not found\n";
        return;
    }

    if (relay_store_) {
        const std::string session_id =
            relay_store_->createSession(requesting_overlay_id, target_overlay_id);
        std::cout << "[RelayServer] Session created: " << session_id << "\n";
    }

    std::cout << "[RelayServer] Rendezvous request from " << requesting_overlay_id
              << " for " << target_overlay_id << " - found\n";
}

void RelayServer::handleRelayData(const std::string& sender_overlay_id,
                                  const std::string& target_overlay_id,
                                  const std::string& base64_payload) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    const auto it = clients_.find(target_overlay_id);
    if (it == clients_.end()) {
        std::cerr << "[RelayServer] Cannot relay data - target " << target_overlay_id << " not found\n";
        return;
    }

    std::cout << "[RelayServer] Relaying data from " << sender_overlay_id
              << " to " << target_overlay_id << " (" << base64_payload.length() << " bytes)\n";
}

} // namespace Wyvern::Relay
