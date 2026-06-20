#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Wyvern::P2P::Relay {

/// OverlayId helper: converts 32-byte Ed25519 public keys to hex string identifiers
/// and validates hex strings for relay addressing.
class OverlayId {
public:
    /// Convert a 32-byte public key to hex string (64 chars + null terminator)
    /// @param pubkey Vector of 32 bytes (Ed25519 public key)
    /// @return Hex string representation (lowercase)
    /// @throws std::invalid_argument if pubkey size != 32
    static std::string toHexString(const std::vector<std::uint8_t>& pubkey) {
        if (pubkey.size() != 32) {
            throw std::invalid_argument("OverlayId: public key must be exactly 32 bytes");
        }

        std::ostringstream oss;
        for (const auto byte : pubkey) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }

    /// Validate if a string is a valid hex overlay ID (64 chars of hex digits)
    /// @param hexStr String to validate
    /// @return true if valid hex of length 64
    static bool isValidHexString(const std::string& hexStr) {
        if (hexStr.length() != 64) {
            return false;
        }
        for (char c : hexStr) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        return true;
    }

    /// Convert hex string back to binary (for validation/processing)
    /// @param hexStr Hex string (must be 64 chars)
    /// @return Vector of 32 bytes
    /// @throws std::invalid_argument if hexStr is invalid format
    static std::vector<std::uint8_t> fromHexString(const std::string& hexStr) {
        if (!isValidHexString(hexStr)) {
            throw std::invalid_argument("OverlayId: hex string must be 64 valid hex characters");
        }

        std::vector<std::uint8_t> result;
        result.reserve(32);

        for (size_t i = 0; i < 64; i += 2) {
            std::string byteStr = hexStr.substr(i, 2);
            std::uint8_t byte = static_cast<std::uint8_t>(std::stoi(byteStr, nullptr, 16));
            result.push_back(byte);
        }

        return result;
    }

    /// Get the expected length of a hex overlay ID string
    static constexpr size_t HEX_LENGTH = 64;
    static constexpr size_t BINARY_LENGTH = 32;
};

} // namespace Wyvern::P2P::Relay
