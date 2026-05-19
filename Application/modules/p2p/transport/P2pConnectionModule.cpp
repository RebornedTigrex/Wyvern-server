#include "P2pConnectionModule.h"

#include "managers/EventBus.h"
#include "Transport/udp/UdpPacket.h"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <iostream>
#include <sstream>

using namespace wyvern::transport::udp;

P2pConnectionModule::P2pConnectionModule(
    const core::runtime::ConfigSection& cfg,
    boost::asio::io_context& ioc)
    : BaseModule("P2P Connection"),
      ioc_(ioc),
      localPort_(static_cast<std::uint16_t>(cfg.value<int>("localPort", 9001))),
      stunServer_(cfg.value<std::string>("stunServer", "stun.l.google.com")),
      stunPort_(static_cast<std::uint16_t>(cfg.value<int>("stunPort", 19302))),
      probeIntervalMs_(cfg.value<int>("probeIntervalMs", 200)),
      probeTimeoutMs_(cfg.value<int>("probeTimeoutMs", 10000)),
      maxDatagramBytes_(static_cast<std::size_t>(cfg.value<int>("maxDatagramBytes", 1400)))
{}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool P2pConnectionModule::onInitialize() {
    try {
        socket_ = std::make_unique<udp_socket>(ioc_);
        socket_->open(boost::asio::ip::udp::v4());
        socket_->set_option(boost::asio::socket_base::reuse_address(true));

        udp_endpoint bindEp(boost::asio::ip::udp::v4(), localPort_);
        socket_->bind(bindEp);

        rxBuf_.resize(maxDatagramBytes_ + 1);

        probeTimer_        = std::make_unique<boost::asio::steady_timer>(ioc_);
        probeTimeoutTimer_ = std::make_unique<boost::asio::steady_timer>(ioc_);

        std::cout << "[P2pConnection] listening on UDP port " << localPort_ << "\n";
        doReceive();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[P2pConnection] init error: " << e.what() << "\n";
        return false;
    }
}

void P2pConnectionModule::onShutdown() {
    stopProbeTimer();
    if (socket_ && socket_->is_open()) {
        boost::system::error_code ec;
        socket_->cancel(ec);
        socket_->close(ec);
    }
    socket_.reset();
    probeTimer_.reset();
    probeTimeoutTimer_.reset();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool P2pConnectionModule::isConnected() const {
    return connected_.load();
}

std::optional<p2p::stun::StunEndpoint> P2pConnectionModule::publicEndpoint() const {
    std::lock_guard<std::mutex> lock(endpointMutex_);
    return publicEndpoint_;
}

void P2pConnectionModule::requestStun() {
    if (!socket_ || !socket_->is_open()) return;
    if (stunPending_.exchange(true)) {
        std::cout << "[P2pConnection] STUN request already in progress\n";
        return;
    }

    // Fill random 12-byte transaction ID.
    std::uniform_int_distribution<unsigned> dist(0, 255);
    for (auto& b : stunTxid_) {
        b = static_cast<std::uint8_t>(dist(rng_));
    }

    auto request = p2p::stun::buildBindingRequest(stunTxid_);

    // Resolve STUN server and send.
    auto resolver = std::make_shared<udp_resolver>(ioc_);
    resolver->async_resolve(
        stunServer_,
        std::to_string(stunPort_),
        [this, request = std::move(request), resolver]
        (const boost::system::error_code& ec,
         udp_resolver::results_type results) mutable
        {
            if (ec) {
                stunPending_.store(false);
                std::cerr << "[P2pConnection] STUN resolve error: " << ec.message() << "\n";
                return;
            }
            udp_endpoint stunEp = results.begin()->endpoint();
            sendRaw(stunEp, std::move(request));
            std::cout << "[P2pConnection] STUN request sent to "
                      << stunEp.address().to_string() << ":" << stunEp.port() << "\n";
        });
}

void P2pConnectionModule::connectToPeer(const std::string& address, std::uint16_t port) {
    if (!socket_ || !socket_->is_open()) return;

    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(address, ec);
    if (ec) {
        std::cerr << "[P2pConnection] invalid peer address '" << address
                  << "': " << ec.message() << "\n";
        return;
    }

    peerEndpoint_ = udp_endpoint{addr, port};
    std::cout << "[P2pConnection] starting hole punch to "
              << address << ":" << port << "\n";
    startProbeTimer();
}

void P2pConnectionModule::sendToPeer(
    std::uint8_t type, std::uint8_t flags,
    std::uint64_t meta, std::vector<std::uint8_t> payload)
{
    if (!connected_.load()) return;
    sendMash(peerEndpoint_, type, flags, meta, payload);
}

void P2pConnectionModule::disconnect(const std::string& reason) {
    stopProbeTimer();
    connected_.store(false);

    std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();
    if (bus) {
        p2p::events::PeerDisconnectedEvent ev;
        ev.reason = reason;
        bus->publish(std::move(ev));
    }
    std::cout << "[P2pConnection] disconnected: " << reason << "\n";
}

// ---------------------------------------------------------------------------
// Receive loop
// ---------------------------------------------------------------------------

void P2pConnectionModule::doReceive() {
    if (!socket_ || !socket_->is_open()) return;
    socket_->async_receive_from(
        boost::asio::buffer(rxBuf_),
        rxRemote_,
        [this](const boost::system::error_code& ec, std::size_t bytes) {
            onReceive(ec, bytes);
        });
}

void P2pConnectionModule::onReceive(
    const boost::system::error_code& ec, std::size_t bytes)
{
    if (ec == boost::asio::error::operation_aborted) return;
    if (ec) {
        if (socket_ && socket_->is_open()) doReceive();
        return;
    }

    const auto* data = rxBuf_.data();

    if (p2p::stun::isStunMessage(data, bytes)) {
        handleStunResponse(data, bytes);
    } else {
        handleMashPacket(data, bytes);
    }

    doReceive();
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void P2pConnectionModule::handleStunResponse(
    const std::uint8_t* data, std::size_t size)
{
    if (!stunPending_.load()) return;

    auto result = p2p::stun::parseBindingResponse(data, size, stunTxid_);
    stunPending_.store(false);

    if (!result) {
        std::cerr << "[P2pConnection] failed to parse STUN response\n";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(endpointMutex_);
        publicEndpoint_ = result;
    }

    std::cout << "[P2pConnection] public endpoint: "
              << result->address << ":" << result->port << "\n";

    std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();
    if (bus) {
        p2p::events::StunResolvedEvent ev;
        ev.publicAddress = result->address;
        ev.publicPort    = result->port;
        bus->publish(std::move(ev));
    }
}

void P2pConnectionModule::handleMashPacket(
    const std::uint8_t* data, std::size_t size)
{
    PacketHeader header{};
    DropReason reason{};
    if (!parseHeader(data, size, header, reason)) return;

    switch (header.type) {
    case p2p::proto::kTypeHolePunchProbe:
        handleProbe();
        break;
    case p2p::proto::kTypeHolePunchAck:
        handleAck();
        break;
    default:
        if (header.type >= p2p::proto::kTypeIdentityAnnounce) {
            const std::size_t hdr = kHeaderSize;
            const std::size_t payloadSize = (size > hdr) ? (size - hdr) : 0u;

            p2p::events::P2pPacketReceivedEvent ev;
            ev.type  = header.type;
            ev.flags = header.flags;
            ev.meta  = header.meta;
            ev.payload.assign(data + hdr, data + hdr + payloadSize);

            std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();
            if (bus) bus->publish(std::move(ev));
        }
        break;
    }
}

void P2pConnectionModule::handleProbe() {
    // Always reply with ACK when we receive a PROBE.
    sendAck();
    // Mark connected: receiving PROBE means the peer can reach us.
    if (!connected_.load()) markConnected();
}

void P2pConnectionModule::handleAck() {
    // Receiving ACK means the peer got our PROBE.
    if (!connected_.load()) markConnected();
    stopProbeTimer();
}

// ---------------------------------------------------------------------------
// Hole punching
// ---------------------------------------------------------------------------

void P2pConnectionModule::startProbeTimer() {
    probing_.store(true);

    // Timeout timer — fires once after probeTimeoutMs_.
    probeTimeoutTimer_->expires_after(
        std::chrono::milliseconds(probeTimeoutMs_));
    probeTimeoutTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;
        if (!connected_.load()) {
            probing_.store(false);
            std::cerr << "[P2pConnection] hole punch timed out\n";
        }
    });

    sendProbe();
}

void P2pConnectionModule::stopProbeTimer() {
    probing_.store(false);
    if (probeTimer_) {
        boost::system::error_code ec;
        probeTimer_->cancel();
    }
    if (probeTimeoutTimer_) {
        boost::system::error_code ec;
        probeTimeoutTimer_->cancel();
    }
}

void P2pConnectionModule::sendProbe() {
    if (!probing_.load() || !socket_ || !socket_->is_open()) return;

    sendMash(peerEndpoint_, p2p::proto::kTypeHolePunchProbe, 0, 0, {});

    probeTimer_->expires_after(std::chrono::milliseconds(probeIntervalMs_));
    probeTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;
        if (probing_.load() && !connected_.load()) sendProbe();
    });
}

void P2pConnectionModule::sendAck() {
    sendMash(peerEndpoint_, p2p::proto::kTypeHolePunchAck, 0, 0, {});
}

void P2pConnectionModule::markConnected() {
    stopProbeTimer();
    connected_.store(true);

    std::cout << "[P2pConnection] peer connected: "
              << peerEndpoint_.address().to_string() << ":"
              << peerEndpoint_.port() << "\n";

    std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();
    if (bus) {
        p2p::events::PeerConnectedEvent ev;
        ev.peerAddress = peerEndpoint_.address().to_string();
        ev.peerPort    = peerEndpoint_.port();
        bus->publish(std::move(ev));
    }
}

// ---------------------------------------------------------------------------
// Send helpers
// ---------------------------------------------------------------------------

void P2pConnectionModule::sendRaw(const udp_endpoint& target,
                                   std::vector<std::uint8_t> data)
{
    if (!socket_ || !socket_->is_open()) return;
    auto dataBuf = std::make_shared<std::vector<std::uint8_t>>(std::move(data));
    socket_->async_send_to(
        boost::asio::buffer(*dataBuf),
        target,
        [dataBuf](const boost::system::error_code& ec, std::size_t) {
            if (ec && ec != boost::asio::error::operation_aborted) {
                std::cerr << "[P2pConnection] send error: " << ec.message() << "\n";
            }
        });
}

void P2pConnectionModule::sendMash(
    const udp_endpoint& target,
    std::uint8_t type, std::uint8_t flags,
    std::uint64_t meta,
    const std::vector<std::uint8_t>& payload)
{
    if (!socket_ || !socket_->is_open()) return;

    const std::size_t totalSize = kHeaderSize + payload.size();
    if (totalSize > maxDatagramBytes_) {
        std::cerr << "[P2pConnection] outbound MASH packet too large: " << totalSize << "\n";
        return;
    }

    auto datagram = std::make_shared<std::vector<std::uint8_t>>(totalSize);
    writeHeader(
        datagram->data(),
        type,
        flags,
        static_cast<std::uint16_t>(payload.size()),
        meta);

    if (!payload.empty()) {
        std::memcpy(datagram->data() + kHeaderSize,
                    payload.data(), payload.size());
    }

    socket_->async_send_to(
        boost::asio::buffer(*datagram),
        target,
        [datagram](const boost::system::error_code& ec, std::size_t) {
            if (ec && ec != boost::asio::error::operation_aborted) {
                std::cerr << "[P2pConnection] MASH send error: " << ec.message() << "\n";
            }
        });
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

std::vector<core::contracts::CommandDescriptor> P2pConnectionModule::commands() {
    return {
        { "p2pconn.stun",
          "Send STUN Binding Request to discover public endpoint",
          [this](const core::contracts::CommandArgs&) { return cmdStun({}); } },
        { "p2pconn.status",
          "Show connection state and public endpoint",
          [this](const core::contracts::CommandArgs&) { return cmdStatus({}); } },
        { "p2pconn.disconnect",
          "Disconnect from current peer",
          [this](const core::contracts::CommandArgs&) { return cmdDisconnect({}); } },
    };
}

core::contracts::CommandResult P2pConnectionModule::cmdStun(
    const core::contracts::CommandArgs&)
{
    requestStun();
    return core::contracts::CommandResult::success("STUN request sent");
}

core::contracts::CommandResult P2pConnectionModule::cmdStatus(
    const core::contracts::CommandArgs&)
{
    std::ostringstream oss;
    oss << "connected=" << (connected_.load() ? "yes" : "no");
    {
        std::lock_guard<std::mutex> lock(endpointMutex_);
        if (publicEndpoint_) {
            oss << " public=" << publicEndpoint_->address
                << ":" << publicEndpoint_->port;
        } else {
            oss << " public=unknown (run p2pconn.stun)";
        }
    }
    if (connected_.load()) {
        oss << " peer=" << peerEndpoint_.address().to_string()
            << ":" << peerEndpoint_.port();
    }
    return core::contracts::CommandResult::success(oss.str());
}

core::contracts::CommandResult P2pConnectionModule::cmdDisconnect(
    const core::contracts::CommandArgs&)
{
    disconnect("manual disconnect");
    return core::contracts::CommandResult::success("disconnected");
}
