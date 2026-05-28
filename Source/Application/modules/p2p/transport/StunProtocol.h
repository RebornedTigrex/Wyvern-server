#pragma once

// Minimal RFC 5389 STUN implementation.
// Supports only Binding Request (client -> server) and parsing the
// XOR-MAPPED-ADDRESS attribute from a Binding Success Response.
// Sufficient for a single Google STUN call to discover the public endpoint.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace p2p::stun {

// RFC 5389 constants.
inline constexpr std::uint32_t kMagicCookie    = 0x2112A442u;
inline constexpr std::size_t   kHeaderSize     = 20; // 2+2+4+12
inline constexpr std::uint16_t kMsgTypeRequest = 0x0001;
inline constexpr std::uint16_t kMsgTypeSuccess = 0x0101;
inline constexpr std::uint16_t kAttrXorMapped  = 0x0020;

using TransactionId = std::array<std::uint8_t, 12>;

struct StunEndpoint {
    std::string   address;
    std::uint16_t port = 0;
};

// Build a STUN Binding Request with the given 12-byte transaction ID.
inline std::vector<std::uint8_t> buildBindingRequest(const TransactionId& txid) {
    std::vector<std::uint8_t> msg(kHeaderSize, 0u);
    msg[0] = 0x00; msg[1] = 0x01; // Binding Request
    msg[2] = 0x00; msg[3] = 0x00; // message body length = 0
    msg[4] = 0x21; msg[5] = 0x12; msg[6] = 0xA4; msg[7] = 0x42; // magic cookie
    std::memcpy(msg.data() + 8, txid.data(), 12);
    return msg;
}

// Returns true when the datagram looks like a STUN message
// (magic cookie at bytes 4-7).
inline bool isStunMessage(const std::uint8_t* data, std::size_t size) {
    if (size < kHeaderSize) return false;
    return data[4] == 0x21 && data[5] == 0x12 &&
           data[6] == 0xA4 && data[7] == 0x42;
}

// Parse a STUN Binding Success Response.
// Returns the public IPv4 endpoint from XOR-MAPPED-ADDRESS, or nullopt on any
// failure (wrong type, bad magic, transaction ID mismatch, no IPv4 attribute).
inline std::optional<StunEndpoint> parseBindingResponse(
    const std::uint8_t* data, std::size_t size,
    const TransactionId& expectedTxid)
{
    if (size < kHeaderSize) return std::nullopt;

    const std::uint16_t msgType =
        (static_cast<std::uint16_t>(data[0]) << 8) | data[1];
    if (msgType != kMsgTypeSuccess) return std::nullopt;
    if (!isStunMessage(data, size)) return std::nullopt;
    if (std::memcmp(data + 8, expectedTxid.data(), 12) != 0) return std::nullopt;

    const std::uint16_t bodyLen =
        (static_cast<std::uint16_t>(data[2]) << 8) | data[3];
    if (size < kHeaderSize + static_cast<std::size_t>(bodyLen)) return std::nullopt;

    const std::size_t bodyEnd = kHeaderSize + bodyLen;
    std::size_t off = kHeaderSize;

    while (off + 4 <= bodyEnd) {
        const std::uint16_t attrType =
            (static_cast<std::uint16_t>(data[off]) << 8) | data[off + 1];
        const std::uint16_t attrLen =
            (static_cast<std::uint16_t>(data[off + 2]) << 8) | data[off + 3];
        off += 4;

        if (attrType == kAttrXorMapped && attrLen >= 8) {
            // data[off+0] = reserved, data[off+1] = family
            if (data[off + 1] == 0x01) { // IPv4
                // XOR port: value XOR high 16-bits of magic cookie (0x2112)
                const std::uint16_t xorPort =
                    (static_cast<std::uint16_t>(data[off + 2]) << 8) | data[off + 3];
                const std::uint16_t port = xorPort ^ 0x2112u;

                // XOR address: each byte XOR corresponding byte of magic cookie
                const std::uint8_t a0 = data[off + 4] ^ 0x21u;
                const std::uint8_t a1 = data[off + 5] ^ 0x12u;
                const std::uint8_t a2 = data[off + 6] ^ 0xA4u;
                const std::uint8_t a3 = data[off + 7] ^ 0x42u;

                char ipBuf[16];
                std::snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u",
                    static_cast<unsigned>(a0), static_cast<unsigned>(a1),
                    static_cast<unsigned>(a2), static_cast<unsigned>(a3));

                return StunEndpoint{std::string(ipBuf), port};
            }
        }

        // Attributes are padded to 4-byte boundaries.
        const std::size_t paddedLen = (static_cast<std::size_t>(attrLen) + 3u) & ~3u;
        off += paddedLen;
    }

    return std::nullopt;
}

} // namespace p2p::stun
