#include <boost/test/unit_test.hpp>

#include "p2p/transport/StunProtocol.h"

#include <array>
#include <cstring>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: build a valid STUN Binding Success Response for given txid,
// encoding XOR-MAPPED-ADDRESS (IPv4) with the provided ip and port.
// Used by multiple parse tests.
// ---------------------------------------------------------------------------
static std::vector<std::uint8_t> makeBindingResponse(
    const p2p::stun::TransactionId& txid,
    std::array<std::uint8_t, 4>     ip,
    std::uint16_t                   port)
{
    // Header(20) + attr-header(4) + XOR-MAPPED-ADDRESS-body(8) = 32 bytes
    std::vector<std::uint8_t> msg(32, 0u);

    // Binding Success Response = 0x0101
    msg[0] = 0x01; msg[1] = 0x01;
    // Body length = 12
    msg[2] = 0x00; msg[3] = 0x0C;
    // Magic cookie
    msg[4] = 0x21; msg[5] = 0x12; msg[6] = 0xA4; msg[7] = 0x42;
    // Transaction ID
    std::memcpy(msg.data() + 8, txid.data(), 12);

    // XOR-MAPPED-ADDRESS attribute (type=0x0020, length=8)
    msg[20] = 0x00; msg[21] = 0x20;
    msg[22] = 0x00; msg[23] = 0x08;
    // reserved=0, family=IPv4
    msg[24] = 0x00; msg[25] = 0x01;
    // XOR port: port ^ high-16-bits-of-magic (0x2112)
    const std::uint16_t xorPort = port ^ 0x2112u;
    msg[26] = static_cast<std::uint8_t>(xorPort >> 8);
    msg[27] = static_cast<std::uint8_t>(xorPort & 0xFF);
    // XOR address: each byte ^ corresponding magic-cookie byte
    msg[28] = ip[0] ^ 0x21u;
    msg[29] = ip[1] ^ 0x12u;
    msg[30] = ip[2] ^ 0xA4u;
    msg[31] = ip[3] ^ 0x42u;

    return msg;
}

// ===========================================================================

BOOST_AUTO_TEST_SUITE(stun_build_request)

BOOST_AUTO_TEST_CASE(size_equals_header_size) {
    p2p::stun::TransactionId txid{};
    auto req = p2p::stun::buildBindingRequest(txid);
    BOOST_REQUIRE_EQUAL(req.size(), p2p::stun::kHeaderSize);
}

BOOST_AUTO_TEST_CASE(message_type_is_binding_request) {
    p2p::stun::TransactionId txid{};
    auto req = p2p::stun::buildBindingRequest(txid);
    BOOST_CHECK_EQUAL(req[0], 0x00);
    BOOST_CHECK_EQUAL(req[1], 0x01);
}

BOOST_AUTO_TEST_CASE(magic_cookie_correct) {
    p2p::stun::TransactionId txid{};
    auto req = p2p::stun::buildBindingRequest(txid);
    BOOST_CHECK_EQUAL(req[4], 0x21);
    BOOST_CHECK_EQUAL(req[5], 0x12);
    BOOST_CHECK_EQUAL(req[6], 0xA4);
    BOOST_CHECK_EQUAL(req[7], 0x42);
}

BOOST_AUTO_TEST_CASE(transaction_id_embedded) {
    p2p::stun::TransactionId txid{
        0x01,0x02,0x03,0x04,0x05,0x06,
        0x07,0x08,0x09,0x0A,0x0B,0x0C
    };
    auto req = p2p::stun::buildBindingRequest(txid);
    BOOST_CHECK(std::memcmp(req.data() + 8, txid.data(), 12) == 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ===========================================================================

BOOST_AUTO_TEST_SUITE(stun_is_message)

BOOST_AUTO_TEST_CASE(valid_request_passes) {
    p2p::stun::TransactionId txid{};
    auto req = p2p::stun::buildBindingRequest(txid);
    BOOST_CHECK(p2p::stun::isStunMessage(req.data(), req.size()));
}

BOOST_AUTO_TEST_CASE(wrong_magic_fails) {
    std::vector<std::uint8_t> msg(20, 0u);
    msg[4] = 0xFF; // bad magic byte
    BOOST_CHECK(!p2p::stun::isStunMessage(msg.data(), msg.size()));
}

BOOST_AUTO_TEST_CASE(too_short_fails) {
    std::vector<std::uint8_t> msg(10, 0u);
    BOOST_CHECK(!p2p::stun::isStunMessage(msg.data(), msg.size()));
}

BOOST_AUTO_TEST_SUITE_END()

// ===========================================================================

BOOST_AUTO_TEST_SUITE(stun_parse_response)

BOOST_AUTO_TEST_CASE(valid_ipv4_decoded_correctly) {
    p2p::stun::TransactionId txid{
        0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
        0x11,0x22,0x33,0x44,0x55,0x66
    };
    auto resp   = makeBindingResponse(txid, {1, 2, 3, 4}, 5678);
    auto result = p2p::stun::parseBindingResponse(
                      resp.data(), resp.size(), txid);

    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(result->address, "1.2.3.4");
    BOOST_CHECK_EQUAL(result->port,    5678u);
}

BOOST_AUTO_TEST_CASE(txid_mismatch_returns_nullopt) {
    p2p::stun::TransactionId txid{
        0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
        0x11,0x22,0x33,0x44,0x55,0x66
    };
    p2p::stun::TransactionId wrong{};  // all zeros
    auto resp   = makeBindingResponse(txid, {1, 2, 3, 4}, 5678);
    auto result = p2p::stun::parseBindingResponse(
                      resp.data(), resp.size(), wrong);

    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(error_response_type_returns_nullopt) {
    p2p::stun::TransactionId txid{};
    auto resp = makeBindingResponse(txid, {1, 2, 3, 4}, 5678);
    // Overwrite type with Binding Error Response = 0x0111
    resp[0] = 0x01; resp[1] = 0x11;
    auto result = p2p::stun::parseBindingResponse(
                      resp.data(), resp.size(), txid);

    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(too_short_returns_nullopt) {
    p2p::stun::TransactionId txid{};
    std::vector<std::uint8_t> msg(10, 0u);
    auto result = p2p::stun::parseBindingResponse(
                      msg.data(), msg.size(), txid);

    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(no_xor_mapped_attr_returns_nullopt) {
    // Valid header with zero-length body (no attributes at all).
    p2p::stun::TransactionId txid{
        0x01,0x02,0x03,0x04,0x05,0x06,
        0x07,0x08,0x09,0x0A,0x0B,0x0C
    };
    std::vector<std::uint8_t> msg(20, 0u);
    msg[0] = 0x01; msg[1] = 0x01;           // Success Response
    msg[2] = 0x00; msg[3] = 0x00;           // body length = 0
    msg[4] = 0x21; msg[5] = 0x12; msg[6] = 0xA4; msg[7] = 0x42;
    std::memcpy(msg.data() + 8, txid.data(), 12);

    auto result = p2p::stun::parseBindingResponse(
                      msg.data(), msg.size(), txid);

    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
