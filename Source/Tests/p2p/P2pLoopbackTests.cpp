#include <boost/test/unit_test.hpp>

#include "p2p/transport/P2pConnectionModule.h"
#include "p2p/transport/P2pEvents.h"
#include "p2p/transport/StunProtocol.h"
#include "managers/EventBus.h"
#include "managers/ModuleRegistry.h"
#include "runtime/ConfigSection.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// FakeStunServer
// Receives a STUN Binding Request and replies with a crafted Success Response
// containing XOR-MAPPED-ADDRESS = kFakeIp:kFakePort.
// ---------------------------------------------------------------------------
class FakeStunServer {
public:
    static constexpr std::uint8_t  kFakeIp[4] = {1, 2, 3, 4};
    static constexpr std::uint16_t kFakePort   = 5678;

    FakeStunServer(boost::asio::io_context& ioc, std::uint16_t port)
        : socket_(ioc,
                  boost::asio::ip::udp::endpoint(
                      boost::asio::ip::udp::v4(), port))
        , rxBuf_(512)
    {}

    ~FakeStunServer() {
        boost::system::error_code ec;
        socket_.cancel(ec);
        socket_.close(ec);
    }

    void start() { receive(); }

private:
    void receive() {
        socket_.async_receive_from(
            boost::asio::buffer(rxBuf_), remote_,
            [this](const boost::system::error_code& ec, std::size_t bytes) {
                if (ec) return; // cancelled or closed
                reply(bytes);
                receive();
            });
    }

    void reply(std::size_t /*bytes*/) {
        p2p::stun::TransactionId txid{};
        if (rxBuf_.size() >= p2p::stun::kHeaderSize) {
            std::memcpy(txid.data(), rxBuf_.data() + 8, 12);
        }

        auto buf = std::make_shared<std::vector<std::uint8_t>>(
            buildResponse(txid));
        socket_.async_send_to(
            boost::asio::buffer(*buf), remote_,
            [buf](const boost::system::error_code&, std::size_t) {});
    }

    static std::vector<std::uint8_t> buildResponse(
        const p2p::stun::TransactionId& txid)
    {
        std::vector<std::uint8_t> msg(32, 0u);
        // Binding Success Response
        msg[0] = 0x01; msg[1] = 0x01;
        msg[2] = 0x00; msg[3] = 0x0C; // body length = 12
        msg[4] = 0x21; msg[5] = 0x12; msg[6] = 0xA4; msg[7] = 0x42;
        std::memcpy(msg.data() + 8, txid.data(), 12);

        // XOR-MAPPED-ADDRESS attribute
        msg[20] = 0x00; msg[21] = 0x20; // type
        msg[22] = 0x00; msg[23] = 0x08; // length
        msg[24] = 0x00; msg[25] = 0x01; // reserved, IPv4
        // XOR port
        const std::uint16_t xp = kFakePort ^ 0x2112u;
        msg[26] = static_cast<std::uint8_t>(xp >> 8);
        msg[27] = static_cast<std::uint8_t>(xp & 0xFF);
        // XOR address
        msg[28] = kFakeIp[0] ^ 0x21u;
        msg[29] = kFakeIp[1] ^ 0x12u;
        msg[30] = kFakeIp[2] ^ 0xA4u;
        msg[31] = kFakeIp[3] ^ 0x42u;

        return msg;
    }

    boost::asio::ip::udp::socket   socket_;
    std::vector<std::uint8_t>      rxBuf_;
    boost::asio::ip::udp::endpoint remote_;
};

// ---------------------------------------------------------------------------
// IocRunner
// Keeps io_context alive on a background thread.
// Stops the context and joins on destruction.
// ---------------------------------------------------------------------------
struct IocRunner {
    boost::asio::io_context ioc;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work{ioc.get_executor()};
    std::thread thread{[this] { ioc.run(); }};

    ~IocRunner() {
        work.reset();
        ioc.stop();
        thread.join();
    }
};

// ---------------------------------------------------------------------------
// BusSubscription
// RAII wrapper: subscribes on construction, unsubscribes on destruction.
// Ensures EventBus (process-wide singleton) is clean after each test.
// ---------------------------------------------------------------------------
template <typename Event>
struct BusSubscription {
    core::contracts::SubscriptionId           id  = 0;
    // Hold IEventBus* so the template subscribe/unsubscribe from the base
    // class are visible.  EventBus::subscribe(raw) hides the template
    // overloads via name hiding; going through IEventBus bypasses that.
    std::shared_ptr<core::contracts::IEventBus> bus = EventBus::instance();

    template <typename Handler>
    explicit BusSubscription(Handler&& h) {
        std::function<void(const Event&)> fn = std::forward<Handler>(h);
        id = bus->subscribe(std::move(fn));
    }

    ~BusSubscription() {
        if (id != 0 && bus) bus->unsubscribe(id);
    }

    BusSubscription(const BusSubscription&)            = delete;
    BusSubscription& operator=(const BusSubscription&) = delete;
};

// ---------------------------------------------------------------------------
// makeConfig — convenience wrapper around ConfigSection construction.
// ---------------------------------------------------------------------------
static core::runtime::ConfigSection makeConfig(
    int         localPort,
    const char* stunServer = "127.0.0.1",
    int         stunPort   = 19302)
{
    boost::json::object obj;
    obj["localPort"]        = localPort;
    obj["stunServer"]       = stunServer;
    obj["stunPort"]         = stunPort;
    obj["stunTimeoutMs"]    = 3000;
    obj["probeIntervalMs"]  = 100;
    obj["probeTimeoutMs"]   = 5000;
    obj["maxDatagramBytes"] = 1400;
    return core::runtime::ConfigSection(std::move(obj));
}

} // namespace

// ===========================================================================
// Test suite: STUN loopback
// ===========================================================================

BOOST_AUTO_TEST_SUITE(stun_loopback)

// Verifies that P2pConnectionModule successfully completes the STUN flow
// when talking to a local fake server (no internet required).
BOOST_AUTO_TEST_CASE(stun_resolves_via_fake_server) {
    // Declare in order: FakeStunServer is destroyed before IocRunner
    // (reverse-order destruction), so its socket cancel/close runs while
    // the io_context thread is still alive.
    IocRunner   runner;
    FakeStunServer fakeStun(runner.ioc, 19400);
    fakeStun.start();

    auto cfg = makeConfig(9150, "127.0.0.1", 19400);

    // Fresh registry per test — avoids singleton key collision.
    ModuleRegistry reg;
    auto* mod = reg.registerModule<P2pConnectionModule>(cfg, runner.ioc);
    BOOST_REQUIRE_MESSAGE(reg.initializeAll(),
        "P2pConnectionModule failed to initialize (port 9150 in use?)");

    // Collect StunResolvedEvent via EventBus.
    std::mutex              mx;
    std::condition_variable cv;
    std::string             gotAddr;
    std::uint16_t           gotPort = 0;

    BusSubscription<p2p::events::StunResolvedEvent> sub(
        [&](const p2p::events::StunResolvedEvent& ev) {
            {
                std::lock_guard<std::mutex> lock(mx);
                gotAddr = ev.publicAddress;
                gotPort = ev.publicPort;
            }
            cv.notify_one();
        });

    mod->requestStun();

    bool fired = false;
    {
        std::unique_lock<std::mutex> lock(mx);
        fired = cv.wait_for(lock, std::chrono::seconds(3),
                            [&] { return !gotAddr.empty(); });
    }

    reg.shutdownAll();
    // Destructors run in reverse: sub -> reg -> fakeStun -> runner

    BOOST_REQUIRE_MESSAGE(fired,
        "StunResolvedEvent not received within 3 s — "
        "check that port 9150/19400 are free and that FakeStunServer "
        "sent a valid response");
    BOOST_CHECK_EQUAL(gotAddr, "1.2.3.4");
    BOOST_CHECK_EQUAL(gotPort, FakeStunServer::kFakePort);
}

BOOST_AUTO_TEST_SUITE_END()

// ===========================================================================
// Test suite: Hole punch loopback
// ===========================================================================

BOOST_AUTO_TEST_SUITE(hole_punch_loopback)

// Two P2pConnectionModule instances on the same io_context, different ports.
// Each calls connectToPeer pointing at the other.  Both should exchange
// PROBE/ACK and fire PeerConnectedEvent within 5 s.
BOOST_AUTO_TEST_CASE(two_modules_connect_on_loopback) {
    IocRunner runner;

    auto cfgA = makeConfig(9151);
    auto cfgB = makeConfig(9152);

    // Two separate registries: same module key "p2p.connection" is allowed
    // because each registry is an independent instance.
    ModuleRegistry regA, regB;
    auto* modA = regA.registerModule<P2pConnectionModule>(cfgA, runner.ioc);
    auto* modB = regB.registerModule<P2pConnectionModule>(cfgB, runner.ioc);

    BOOST_REQUIRE_MESSAGE(regA.initializeAll(),
        "Module A failed to initialize (port 9151 in use?)");
    BOOST_REQUIRE_MESSAGE(regB.initializeAll(),
        "Module B failed to initialize (port 9152 in use?)");

    // Count PeerConnectedEvents — expect exactly 2 (one per module).
    std::mutex              mx;
    std::condition_variable cv;
    std::atomic<int>        count{0};

    BusSubscription<p2p::events::PeerConnectedEvent> sub(
        [&](const p2p::events::PeerConnectedEvent&) {
            count.fetch_add(1);
            cv.notify_all();
        });

    // Both sides initiate simultaneously — mirrors real NAT punch scenario.
    modA->connectToPeer("127.0.0.1", 9152);
    modB->connectToPeer("127.0.0.1", 9151);

    bool bothConnected = false;
    {
        std::unique_lock<std::mutex> lock(mx);
        bothConnected = cv.wait_for(lock, std::chrono::seconds(5),
                                    [&] { return count.load() >= 2; });
    }

    regA.shutdownAll();
    regB.shutdownAll();

    BOOST_REQUIRE_MESSAGE(bothConnected,
        "Both modules did not reach connected state within 5 s "
        "(connected so far: " << count.load() << "/2). "
        "Check PROBE/ACK logic and that ports 9151/9152 are free.");
}

BOOST_AUTO_TEST_SUITE_END()
