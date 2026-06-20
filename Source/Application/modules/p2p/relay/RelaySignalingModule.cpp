#include "RelaySignalingModule.h"
#include "RelayEvents.h"
#include "managers/EventBus.h"
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/bind/bind.hpp>
#include <iostream>
#include <thread>

namespace Wyvern::P2P::Relay {

RelaySignalingModule::RelaySignalingModule(const core::runtime::ConfigSection& cfg,
                                          boost::asio::io_context& ioc)
    : BaseModule("P2P Relay Signaling"),
      ioc_(ioc),
      relay_server_url_(cfg.value<std::string>("server_url", "")) {
    // Config: relay_server_url is optional, can be set later via setRelayServerUrl()
}

std::vector<core::contracts::CommandDescriptor> RelaySignalingModule::commands() {
    return {
        {
            "p2p.relay.status",
            "Display relay connection status",
            [this](const core::contracts::CommandArgs& args) {
                return cmdRelayStatus(args);
            }
        }
    };
}

bool RelaySignalingModule::onInitialize() {
    // Create strand for serializing async operations
    strand_ = std::make_unique<boost::asio::strand<boost::asio::io_context::executor_type>>(
        ioc_.get_executor()
    );

    // TODO: Attempt connection to relay server if URL is configured
    // For now, this is a stub; actual connection will be triggered by P2pMessengerModule
    // or a dedicated command.

    return true;
}

void RelaySignalingModule::onShutdown() {
    closeConnection();
}

bool RelaySignalingModule::isConnected() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return is_connected_;
}

std::string RelaySignalingModule::getRelayServerUrl() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return relay_server_url_;
}

void RelaySignalingModule::setRelayServerUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    relay_server_url_ = url;
}

void RelaySignalingModule::registerOverlayId(const std::string& overlay_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    local_overlay_id_ = overlay_id;

    // Build REGISTER message
    Protocol::RegisterMessage reg_msg;
    reg_msg.overlay_id = overlay_id;
    reg_msg.session_id = "";  // Will be assigned by server

    sendMessage(reg_msg.toJson());
}

void RelaySignalingModule::requestRendezvous(const std::string& target_overlay_id) {
    Protocol::RendezvousRequestMessage req_msg;
    req_msg.target_overlay_id = target_overlay_id;

    sendMessage(req_msg.toJson());
}

void RelaySignalingModule::sendRelayData(const std::string& target_overlay_id,
                                        const std::vector<std::uint8_t>& envelope) {
    Protocol::RelayDataMessage data_msg;
    data_msg.target_overlay_id = target_overlay_id;
    data_msg.payload = base64Encode(envelope);

    sendMessage(data_msg.toJson());
}

void RelaySignalingModule::connectToRelay() {
    if (!strand_) return;

    std::string url = relay_server_url_;
    if (url.empty()) {
        std::cerr << "[RelaySignalingModule] No relay server URL configured\n";
        return;
    }

    // Parse URL: ws://host:port or ws://host
    // For simplicity, assume format ws://host:port
    std::string host = url;
    std::string port = "9002";  // Default relay port

    // Simple parsing: remove ws:// prefix and split by :
    if (host.find("ws://") == 0) {
        host = host.substr(5);
    }

    size_t colon_pos = host.find(':');
    if (colon_pos != std::string::npos) {
        port = host.substr(colon_pos + 1);
        host = host.substr(0, colon_pos);
    }

    // Post async connection onto strand
    boost::asio::post(*strand_, [this, host, port]() {
        try {
            auto socket = std::make_unique<boost::asio::ip::tcp::socket>(ioc_);
            boost::asio::ip::tcp::resolver resolver(ioc_);
            auto results = resolver.resolve(host, port);

            boost::asio::connect(*socket, results);

            ws_ = std::make_unique<WsStream>(std::move(*socket));
            ws_->handshake(host, "/");
            
            handleConnectionSuccess();
            receiveMessages();
        } catch (const std::exception& e) {
            handleConnectionFailure(e.what());
        }
    });
}

void RelaySignalingModule::handleConnectionSuccess() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        is_connected_ = true;
    }

    if (auto bus = EventBus::instance()) {
        Events::RelayConnectedEvent ev;
        ev.relay_server_url = relay_server_url_;
        ev.local_overlay_id = local_overlay_id_;
        static_cast<core::contracts::IEventBus*>(bus.get())->publish(ev);
    }

    // TODO: Auto-register overlay ID on successful connection?
}

void RelaySignalingModule::handleConnectionFailure(const std::string& error) {
    std::cerr << "[RelaySignalingModule] Connection failed: " << error << "\n";

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        is_connected_ = false;
    }

    if (auto bus = EventBus::instance()) {
        Events::RelayDisconnectedEvent ev;
        ev.reason = error;
        static_cast<core::contracts::IEventBus*>(bus.get())->publish(ev);
    }
}

void RelaySignalingModule::closeConnection() {
    if (!ws_) return;

    try {
        ws_->close(boost::beast::websocket::close_code::normal);
    } catch (...) {
        // Ignore errors on close
    }

    ws_.reset();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        is_connected_ = false;
    }
}

void RelaySignalingModule::sendMessage(const boost::json::object& message) {
    if (!ws_) {
        std::cerr << "[RelaySignalingModule] WebSocket not connected\n";
        return;
    }

    try {
        std::string json_str = boost::json::serialize(message);
        ws_->write(boost::asio::buffer(json_str));
    } catch (const std::exception& e) {
        std::cerr << "[RelaySignalingModule] Send failed: " << e.what() << "\n";
        handleConnectionFailure(e.what());
    }
}

void RelaySignalingModule::receiveMessages() {
    if (!ws_) return;

    boost::asio::post(*strand_, [this]() {
        try {
            boost::beast::flat_buffer buffer;
            ws_->read(buffer);

            std::string json_text = boost::beast::buffers_to_string(buffer.data());
            handleMessage(json_text);

            // Recurse to keep receiving
            receiveMessages();
        } catch (const std::exception& e) {
            std::cerr << "[RelaySignalingModule] Receive failed: " << e.what() << "\n";
            handleConnectionFailure(e.what());
        }
    });
}

void RelaySignalingModule::handleMessage(const std::string& json_text) {
    try {
        auto json_val = boost::json::parse(json_text);
        auto json_obj = json_val.as_object();

        if (!json_obj.contains("type")) {
            std::cerr << "[RelaySignalingModule] Message missing 'type' field\n";
            return;
        }

        std::string type_str = boost::json::value_to<std::string>(json_obj.at("type"));
        Protocol::MessageType type = Protocol::stringToMessageType(type_str);

        switch (type) {
            case Protocol::MessageType::RENDEZVOUS_RESPONSE: {
                Protocol::RendezvousResponseMessage msg = Protocol::RendezvousResponseMessage::fromJson(json_obj);
                handleRendezvousResponse(msg);
                break;
            }
            case Protocol::MessageType::RELAY_DATA_RECEIVE: {
                Protocol::RelayDataMessage msg = Protocol::RelayDataMessage::fromJson(json_obj);
                handleRelayData(msg);
                break;
            }
            case Protocol::MessageType::ERROR_RESPONSE: {
                Protocol::ErrorResponseMessage msg = Protocol::ErrorResponseMessage::fromJson(json_obj);
                handleErrorResponse(msg);
                break;
            }
            case Protocol::MessageType::PONG:
                // Keep-alive response, ignore
                break;
            default:
                std::cerr << "[RelaySignalingModule] Unexpected message type: " << type_str << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[RelaySignalingModule] JSON parse error: " << e.what() << "\n";
    }
}

void RelaySignalingModule::handleRendezvousResponse(const Protocol::RendezvousResponseMessage& msg) {
    if (auto bus = EventBus::instance()) {
        Events::RelayRendezvousResponseEvent ev;
        ev.status = msg.status;
        ev.peer_overlay_id = msg.peer_overlay_id;
        ev.peer_candidates = msg.peer_candidates;
        static_cast<core::contracts::IEventBus*>(bus.get())->publish(ev);
    }
}

void RelaySignalingModule::handleRelayData(const Protocol::RelayDataMessage& msg) {
    try {
        std::vector<std::uint8_t> payload = base64Decode(msg.payload);
        if (auto bus = EventBus::instance()) {
            Events::RelayDataReceivedEvent ev;
            ev.source_overlay_id = msg.target_overlay_id;  // In RELAY_DATA_RECEIVE, this is the source
            ev.payload = payload;
            static_cast<core::contracts::IEventBus*>(bus.get())->publish(ev);
        }
    } catch (const std::exception& e) {
        std::cerr << "[RelaySignalingModule] Failed to decode relay data: " << e.what() << "\n";
    }
}

void RelaySignalingModule::handleErrorResponse(const Protocol::ErrorResponseMessage& msg) {
    if (auto bus = EventBus::instance()) {
        Events::RelayErrorEvent ev;
        ev.error_code = msg.code;
        ev.error_msg = msg.message;
        static_cast<core::contracts::IEventBus*>(bus.get())->publish(ev);
    }
}

// Simple base64 encoding (not highly optimized, but sufficient for prototype)
std::string RelaySignalingModule::base64Encode(const std::vector<std::uint8_t>& data) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string ret;
    int val = 0;
    int valb = 0;

    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 6) {
            valb -= 6;
            ret.push_back(base64_chars[(val >> valb) & 0x3F]);
        }
    }

    if (valb > 0) {
        ret.push_back(base64_chars[(val << (6 - valb)) & 0x3F]);
    }

    while (ret.size() % 4) {
        ret.push_back('=');
    }

    return ret;
}

// Simple base64 decoding
std::vector<std::uint8_t> RelaySignalingModule::base64Decode(const std::string& encoded) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<std::uint8_t> ret;
    std::vector<int> T(256, -1);

    for (int i = 0; i < 64; i++) {
        T[base64_chars[i]] = i;
    }

    int val = 0, valb = 0;

    for (uint8_t c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 8) {
            valb -= 8;
            ret.push_back((val >> valb) & 0xFF);
        }
    }

    return ret;
}

core::contracts::CommandResult RelaySignalingModule::cmdRelayStatus(const core::contracts::CommandArgs& args) {
    (void)args;

    boost::json::object result;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        result["connected"] = is_connected_;
        result["server_url"] = relay_server_url_;
        result["local_overlay_id"] = local_overlay_id_;
    }

    return core::contracts::CommandResult::success(boost::json::serialize(result));
}

} // namespace Wyvern::P2P::Relay
