#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <boost/json.hpp>

namespace Wyvern::P2P::Relay {

/// JSON-based relay signaling protocol over WebSocket
/// Protocol is message-based with explicit state machine:
/// Client -> REGISTER -> Server -> RENDEZVOUS_REQUEST -> (PEER_CANDIDATES) -> RELAY_DATA

namespace Protocol {

    /// Message type enumeration
    enum class MessageType {
        // Client -> Server
        REGISTER,                  // Register client with relay
        RENDEZVOUS_REQUEST,        // Request rendezvous with peer
        RELAY_DATA,                // Forward opaque message data
        
        // Server -> Client
        RENDEZVOUS_RESPONSE,       // Response to rendezvous with peer candidates
        RELAY_DATA_RECEIVE,        // Received relayed data from peer
        ERROR_RESPONSE,            // Error message
        
        // Bidirectional
        PING,                      // Keep-alive
        PONG                       // Keep-alive response
    };

    /// Converts MessageType to string
    inline std::string messageTypeToString(MessageType type) {
        switch (type) {
            case MessageType::REGISTER: return "REGISTER";
            case MessageType::RENDEZVOUS_REQUEST: return "RENDEZVOUS_REQUEST";
            case MessageType::RELAY_DATA: return "RELAY_DATA";
            case MessageType::RENDEZVOUS_RESPONSE: return "RENDEZVOUS_RESPONSE";
            case MessageType::RELAY_DATA_RECEIVE: return "RELAY_DATA_RECEIVE";
            case MessageType::ERROR_RESPONSE: return "ERROR_RESPONSE";
            case MessageType::PING: return "PING";
            case MessageType::PONG: return "PONG";
            default: return "UNKNOWN";
        }
    }

    /// Converts string to MessageType
    inline MessageType stringToMessageType(const std::string& str) {
        if (str == "REGISTER") return MessageType::REGISTER;
        if (str == "RENDEZVOUS_REQUEST") return MessageType::RENDEZVOUS_REQUEST;
        if (str == "RELAY_DATA") return MessageType::RELAY_DATA;
        if (str == "RENDEZVOUS_RESPONSE") return MessageType::RENDEZVOUS_RESPONSE;
        if (str == "RELAY_DATA_RECEIVE") return MessageType::RELAY_DATA_RECEIVE;
        if (str == "ERROR_RESPONSE") return MessageType::ERROR_RESPONSE;
        if (str == "PING") return MessageType::PING;
        if (str == "PONG") return MessageType::PONG;
        return MessageType::ERROR_RESPONSE;
    }

    /// REGISTER message: client registers with relay
    /// {
    ///   "type": "REGISTER",
    ///   "overlay_id": "32-byte hex-encoded Ed25519 public key",
    ///   "session_id": "generated session identifier"
    /// }
    struct RegisterMessage {
        std::string overlay_id;     // 32-byte hex-encoded overlay ID (from Ed25519 pubkey)
        std::string session_id;     // Optional session identifier for persistence
        
        boost::json::object toJson() const {
            boost::json::object obj;
            obj["type"] = messageTypeToString(MessageType::REGISTER);
            obj["overlay_id"] = overlay_id;
            obj["session_id"] = session_id;
            return obj;
        }
        
        static RegisterMessage fromJson(const boost::json::object& obj) {
            RegisterMessage msg;
            if (obj.contains("overlay_id")) {
                msg.overlay_id = boost::json::value_to<std::string>(obj.at("overlay_id"));
            }
            if (obj.contains("session_id")) {
                msg.session_id = boost::json::value_to<std::string>(obj.at("session_id"));
            }
            return msg;
        }
    };

    /// RENDEZVOUS_REQUEST message: request to connect with another peer
    /// {
    ///   "type": "RENDEZVOUS_REQUEST",
    ///   "target_overlay_id": "target peer's 32-byte overlay ID"
    /// }
    struct RendezvousRequestMessage {
        std::string target_overlay_id;  // Target peer's overlay ID
        
        boost::json::object toJson() const {
            boost::json::object obj;
            obj["type"] = messageTypeToString(MessageType::RENDEZVOUS_REQUEST);
            obj["target_overlay_id"] = target_overlay_id;
            return obj;
        }
        
        static RendezvousRequestMessage fromJson(const boost::json::object& obj) {
            RendezvousRequestMessage msg;
            if (obj.contains("target_overlay_id")) {
                msg.target_overlay_id = boost::json::value_to<std::string>(obj.at("target_overlay_id"));
            }
            return msg;
        }
    };

    /// RENDEZVOUS_RESPONSE message: server responds with peer info and candidates
    /// {
    ///   "type": "RENDEZVOUS_RESPONSE",
    ///   "status": "success" or "not_found",
    ///   "peer_overlay_id": "peer's overlay ID",
    ///   "peer_candidates": [ "ip:port", ... ]
    /// }
    struct RendezvousResponseMessage {
        std::string status;                    // "success" or "not_found"
        std::string peer_overlay_id;           // Peer's overlay ID
        std::vector<std::string> peer_candidates;  // List of peer endpoints (ip:port)
        
        boost::json::object toJson() const {
            boost::json::object obj;
            obj["type"] = messageTypeToString(MessageType::RENDEZVOUS_RESPONSE);
            obj["status"] = status;
            obj["peer_overlay_id"] = peer_overlay_id;
            
            boost::json::array candidates;
            for (const auto& candidate : peer_candidates) {
                candidates.push_back(boost::json::value(candidate));
            }
            obj["peer_candidates"] = candidates;
            return obj;
        }
        
        static RendezvousResponseMessage fromJson(const boost::json::object& obj) {
            RendezvousResponseMessage msg;
            if (obj.contains("status")) {
                msg.status = boost::json::value_to<std::string>(obj.at("status"));
            }
            if (obj.contains("peer_overlay_id")) {
                msg.peer_overlay_id = boost::json::value_to<std::string>(obj.at("peer_overlay_id"));
            }
            if (obj.contains("peer_candidates")) {
                auto candidates_arr = obj.at("peer_candidates").as_array();
                for (const auto& cand : candidates_arr) {
                    msg.peer_candidates.push_back(boost::json::value_to<std::string>(cand));
                }
            }
            return msg;
        }
    };

    /// RELAY_DATA message: forward opaque message data
    /// {
    ///   "type": "RELAY_DATA",
    ///   "target_overlay_id": "target peer's overlay ID",
    ///   "payload": "base64-encoded opaque DirectMessageEnvelope"
    /// }
    struct RelayDataMessage {
        std::string target_overlay_id;  // Target peer's overlay ID
        std::string payload;            // Base64-encoded opaque envelope data
        
        boost::json::object toJson() const {
            boost::json::object obj;
            obj["type"] = messageTypeToString(MessageType::RELAY_DATA);
            obj["target_overlay_id"] = target_overlay_id;
            obj["payload"] = payload;
            return obj;
        }
        
        static RelayDataMessage fromJson(const boost::json::object& obj) {
            RelayDataMessage msg;
            if (obj.contains("target_overlay_id")) {
                msg.target_overlay_id = boost::json::value_to<std::string>(obj.at("target_overlay_id"));
            }
            if (obj.contains("payload")) {
                msg.payload = boost::json::value_to<std::string>(obj.at("payload"));
            }
            return msg;
        }
    };

    /// ERROR_RESPONSE message: server reports error
    /// {
    ///   "type": "ERROR_RESPONSE",
    ///   "code": 400,
    ///   "message": "error description"
    /// }
    struct ErrorResponseMessage {
        int code;                   // Error code (400, 404, 500, etc.)
        std::string message;        // Error description
        
        boost::json::object toJson() const {
            boost::json::object obj;
            obj["type"] = messageTypeToString(MessageType::ERROR_RESPONSE);
            obj["code"] = code;
            obj["message"] = message;
            return obj;
        }
        
        static ErrorResponseMessage fromJson(const boost::json::object& obj) {
            ErrorResponseMessage msg;
            msg.code = 500;
            msg.message = "Unknown error";
            if (obj.contains("code")) {
                msg.code = boost::json::value_to<int>(obj.at("code"));
            }
            if (obj.contains("message")) {
                msg.message = boost::json::value_to<std::string>(obj.at("message"));
            }
            return msg;
        }
    };

} // namespace Protocol

} // namespace Wyvern::P2P::Relay
