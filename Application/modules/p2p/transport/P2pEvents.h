#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace p2p::events {

// -- Network events ----------------------------------------------------------

// P2pConnectionModule publishes this after a successful STUN Binding Response.
struct StunResolvedEvent {
    std::string   publicAddress;
    std::uint16_t publicPort = 0;
};

// P2pConnectionModule publishes this when UDP hole punching succeeds
// (received PROBE or ACK from the peer endpoint).
struct PeerConnectedEvent {
    std::string   peerAddress;
    std::uint16_t peerPort = 0;
};

// P2pConnectionModule publishes this when the peer is explicitly disconnected.
struct PeerDisconnectedEvent {
    std::string reason;
};

// P2pConnectionModule publishes this for every valid inbound MASH packet
// with type >= kTypeIdentityAnnounce (0x10).
// The transport layer (probe/ack) packets are consumed internally.
struct P2pPacketReceivedEvent {
    std::uint8_t              type    = 0;
    std::uint8_t              flags   = 0;
    std::uint64_t             meta    = 0;
    std::vector<std::uint8_t> payload;
};

} // namespace p2p::events

// -- P2P wire protocol packet types ------------------------------------------
// These values go into the `type` byte of the MASH packet header.
namespace p2p::proto {

inline constexpr std::uint8_t kTypeHolePunchProbe     = 0x01; // no payload
inline constexpr std::uint8_t kTypeHolePunchAck       = 0x02; // no payload
inline constexpr std::uint8_t kTypeIdentityAnnounce   = 0x10; // 32 bytes Ed25519 pubkey
inline constexpr std::uint8_t kTypeHandshakeInit      = 0x20; // JSON: DirectHandshakeInit
inline constexpr std::uint8_t kTypeHandshakeResponse  = 0x21; // JSON: DirectHandshakeResponse
inline constexpr std::uint8_t kTypeTextMessage        = 0x30; // JSON: DirectMessageEnvelope

} // namespace p2p::proto
