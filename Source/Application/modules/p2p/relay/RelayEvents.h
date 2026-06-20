#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Wyvern::P2P::Relay::Events {

/// Fired when RelaySignalingModule successfully connects to the relay server
struct RelayConnectedEvent {
    std::string relay_server_url;  // URL of connected relay server
    std::string local_overlay_id;  // Our registered overlay ID
};

/// Fired when RelaySignalingModule successfully registers with the relay
struct RelayRegisteredEvent {
    std::string local_overlay_id;  // Our registered overlay ID
    std::string session_id;        // Session ID assigned by relay
};

/// Fired when relay responds with peer candidates during rendezvous
struct RelayRendezvousResponseEvent {
    std::string target_overlay_id;         // Who we requested
    std::string status;                    // "success" or "not_found"
    std::string peer_overlay_id;           // Peer's overlay ID
    std::vector<std::string> peer_candidates;  // Peer endpoints (ip:port)
};

/// Fired when relay delivers data from a peer
struct RelayDataReceivedEvent {
    std::string source_overlay_id;     // Who sent this
    std::vector<std::uint8_t> payload; // Opaque DirectMessageEnvelope bytes
};

/// Fired when relay encounters an error
struct RelayErrorEvent {
    int error_code;        // Error code from server
    std::string error_msg; // Error description
};

/// Fired when relay connection is lost
struct RelayDisconnectedEvent {
    std::string reason;  // Reason for disconnection
};

} // namespace Wyvern::P2P::Relay::Events
